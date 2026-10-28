// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <bit>
#include <array>
#include "common/alignment.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"
#include "core/hle/service/nvnflinger/ui/graphic_buffer.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
#include "core/hle/service/am/service/graphics_session.h"
#include "core/hle/service/am/service/gpu_session.h"
#include "core/hle/service/am/service/graphics_supervisor_protocol.h"
#include "core/hle/service/am/service/graphics_syncpoint_cache.h"
#include "core/hle/service/am/service/native_image_layout.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_root_service.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::AM {
struct NativeImageRegistry {
    struct Image {
        u32 id;
        u32 memory_id;
        std::array<u32, 16> preference;
        u64 size;
        bool window_pool{};
    };
    NativeImageRegistry(Kernel::KernelCore& kernel_, Kernel::KProcess* process_)
        : kernel{kernel_}, process{process_} {
        process->Open(kernel);
    }
    ~NativeImageRegistry() {
        if (module) {
            for (const auto& image : images) {
                ReleaseMemory(image.memory_id);
            }
        }
        if (memory_session) {
            module->GetContainer().CloseSession(*memory_session);
        }
        process->Close(kernel);
    }
    void ReleaseMemory(u32 id) {
        const auto info = module->GetContainer().GetNvMapFile().FreeHandle(id, true);
        if (info && info->can_unlock && info->address != 0) {
            process->GetPageTable().UnlockForDeviceAddressSpace(info->address, info->size);
        }
    }
    Kernel::KernelCore& kernel;
    Kernel::KProcess* process;
    std::mutex mutex;
    std::shared_ptr<Nvidia::Module> module;
    std::optional<Nvidia::NvCore::SessionId> memory_session;
    std::vector<Image> images;
};
using NativeImage = NativeImageRegistry::Image;


namespace {

// TIPC payloads follow a 32-bit result, so these handlers push result.raw.

class ISyncpointSupervisor final : public ServiceFramework<ISyncpointSupervisor> {
public:
    ISyncpointSupervisor(Core::System& system, std::shared_ptr<Nvidia::Module> module)
        : ServiceFramework{system, "ISyncpointSupervisor"}, m_module{std::move(module)} {
        static const FunctionInfo functions[] = {
            {1, &ISyncpointSupervisor::GetMaximumEventCount, "GetMaximumEventCount"},
            {21, &ISyncpointSupervisor::SetEventCount, "SetEventCount"},
            {103, &ISyncpointSupervisor::QueryCounter, "QueryCounter"},
        };
        RegisterHandlersTipc(functions);
    }

    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override {
        if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
            return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
        }
        const auto guard = LockService();
        LOG_DEBUG(Service_AM, "syncpoint unsupported command={}, words={:08x}", ctx.GetCommand(),
                  fmt::join(std::span<const u32>(ctx.CommandBuffer() + 2, std::min<u32>(ctx.CommandBuffer()[1], 32)), ","));
        InvokeRequestTipc(ctx);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented.raw);
        return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
    }

private:
    static constexpr u32 MaximumEventCount = 63;

    void GetMaximumEventCount(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const Result result = packet[0] == 0x11 && packet[1] == 0
            ? ResultSuccess : Kernel::ResultInvalidCombination;
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(result.raw);
        rb.Push(MaximumEventCount);
        LOG_DEBUG(Service_AM, "syncpoint maximum events={}", MaximumEventCount);
    }

    void SetEventCount(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x25 && packet[1] == 1 && packet[2] <= MaximumEventCount) {
            m_event_count = packet[2];
            result = ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        LOG_DEBUG(Service_AM, "syncpoint event count={}, result={:#x}", m_event_count, result.raw);
    }

    void QueryCounter(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        u64 value{};
        if (packet[0] == 0x77 && packet[1] == 1) {
            const u32 id = ~packet[2];
            auto current = m_module->GetContainer().GetSyncpointManager().ReadAllocatedSyncpoint(id);
            result = current ? ResultSuccess : Kernel::ResultNotFound;
            value = current.value_or(0);
            LOG_DEBUG(Service_AM, "query syncpoint id={}, value={}, result={:#x}", id, value, result.raw);
        }
        IPC::ResponseBuilder rb{ctx, 5};
        rb.Push(result.raw);
        rb.Push(value);
        rb.Push<u32>(0); // Existing NVDRV counters use 32-bit modular comparisons.
    }

    std::shared_ptr<Nvidia::Module> m_module;
    u32 m_event_count{};
};

class IVi2Session final : public ServiceFramework<IVi2Session> {
    struct ClientState {
        u64 token{};
        u32 slots{};
        std::optional<u64> display;
        std::optional<u64> layer;
        u64 available{0x7fff};
        u32 retire_notification{};
        u64 output_encoding{0x14};
        u32 available_notification{};
        std::shared_ptr<android::BufferQueueProducer> producer;
        std::array<std::optional<s32>, 15> queued{};
        std::optional<size_t> player_display;
    };

public:
    explicit IVi2Session(Core::System& system, u64 aruid, std::shared_ptr<NativeImageRegistry> images) : ServiceFramework{system, "IVi2Session"}, m_images{std::move(images)}, m_aruid{aruid}, m_context{system, "Vi2Session"} {
        static const FunctionInfo functions[] = {
            {1, &IVi2Session::GetSessionLimit, "GetNotificationLimit"},
            {11, &IVi2Session::GetNotificationEvent, "GetNotificationEvent"},
            {13, &IVi2Session::ReleaseNotificationEvent, "ReleaseNotificationEvent"},
            {21, &IVi2Session::GetNotifications, "GetNotifications"},
            {3, &IVi2Session::GetSessionLimit, "GetSessionLimit"},
            {5, &IVi2Session::GetSessionLimit, "GetRequestGroupLimit"},
            {51, &IVi2Session::ConfigureRequestGroups, "ConfigureRequestGroups"},
            {101, &IVi2Session::OpenDisplay, "OpenDisplay"},
            {102, &IVi2Session::CloseDisplay, "CloseDisplay"},
            {111, &IVi2Session::OpenEndpoint, "OpenEndpoint"},
            {112, &IVi2Session::CloseEndpoint, "CloseEndpoint"},
            {201, &IVi2Session::RegisterClientState, "RegisterClientState"},
            {202, &IVi2Session::UnregisterClientState, "UnregisterClientState"},
            {212, &IVi2Session::GetClientAvailability, "GetClientAvailability"},
            {229, &IVi2Session::PresentClientBuffer, "PresentClientBuffer"},
            {228, &IVi2Session::CancelClientBuffer, "CancelClientBuffer"},
            {351, &IVi2Session::SetClientOutputEncoding, "SetClientOutputEncoding"},
            {251, &IVi2Session::BindClientDisplay, "BindClientDisplay"},
            {252, &IVi2Session::UnbindClientDisplay, "UnbindClientDisplay"},
        };
        RegisterHandlersTipc(functions);
    }

    ~IVi2Session() override {
        if (m_release_poll) system.CoreTiming().UnscheduleEvent(m_release_poll);
        if (m_display_service) {
            for (auto& state : m_client_states) {
                if (state.layer) m_display_service->GetContainer()->DestroyManagedLayer(*state.layer);
            }
        }
        if (m_default_display) { m_display_service->CloseDisplay(*m_default_display); }
        m_context.CloseEvent(m_event);
    }

    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override {
        if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
            return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
        }
        const auto guard = LockService();
        LOG_DEBUG(Service_AM, "VI2 unsupported command={}, words={:08x}", ctx.GetCommand(),
                  fmt::join(std::span<const u32>(ctx.CommandBuffer() + 2, std::min<u32>(ctx.CommandBuffer()[1], 32)), ","));
        InvokeRequestTipc(ctx);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented.raw);
        return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
    }
private:
    static std::optional<size_t> GetPlayerDisplayIndex(u64 name) {
        if ((name & 0xffffffffffffULL) != 0x726579616c70ULL || (name >> 56) != 0) {
            return std::nullopt;
        }
        const u32 player = (name >> 48) & 0xff;
        if (player < '1' || player > '3') return std::nullopt;
        return player - '1';
    }
    Result OpenDefaultDisplay(u64 name, u32 flags) {
        if (const auto player = GetPlayerDisplayIndex(name)) {
            R_UNLESS(flags < m_notifications.size() * 32, Kernel::ResultInvalidCombination);
            R_UNLESS(m_player_display_references[*player] < 1024, Kernel::ResultOutOfResource);
            // GameShare's detached player displays retain their own lifetime;
            // they do not target the host's default display compositor.
            m_player_display_notifications[*player] = flags;
            ++m_player_display_references[*player];
            R_SUCCEED();
        }
        // The native display token is an eight-byte name; command 101 returns
        // only a result. Retain the real VI display ID for later operations.
        R_UNLESS(name == 0x00746c7561666564ULL, Kernel::ResultNotFound);
        R_UNLESS(flags == 1, Kernel::ResultNotImplemented);
        R_UNLESS(m_display_references < 1024, Kernel::ResultOutOfResource);
        if (!m_display_service) {
            const auto root = system.ServiceManager().GetService<VI::IManagerRootService>("vi:m", true);
            R_UNLESS(root, Kernel::ResultInvalidState);
            R_TRY(root->GetDisplayService(&m_display_service, VI::Policy::Compositor));
        }
        if (!m_default_display) {
            u64 id{};
            R_TRY(m_display_service->OpenDefaultDisplay(&id));
            m_default_display = id;
        }
        m_display_notification = flags;
        ++m_display_references;
        R_SUCCEED();
    }
    void OpenDisplay(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        const auto result = packet[0] == 0x75 && packet[1] == 3
            ? OpenDefaultDisplay(name, packet[4]) : Kernel::ResultInvalidCombination;
        LOG_DEBUG(Service_AM, "VI2 open display name={:#x}, flags={:#x}, references={}, result={:#x}",
                  name, packet[4], m_display_references, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void CloseDisplay(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        if (packet[0] == 0x76 && packet[1] == 2 && GetPlayerDisplayIndex(name)) {
            const auto player = *GetPlayerDisplayIndex(name);
            result = Kernel::ResultNotFound;
            if (m_player_display_references[player]) {
                if (--m_player_display_references[player] == 0) {
                    const auto notification = m_player_display_notifications[player];
                    m_notifications[notification / 32] |= 1U << (notification % 32);
                    if (m_event) m_event->Signal(kernel);
                }
                result = ResultSuccess;
            }
        } else if (packet[0] == 0x76 && packet[1] == 2 && name == 0x00746c7561666564ULL) {
            result = Kernel::ResultNotFound;
            if (m_default_display && m_display_references) {
                result = ResultSuccess;
                if (m_display_references == 1) {
                    result = m_display_service->CloseDisplay(*m_default_display);
                    if (result.IsSuccess()) { m_default_display.reset(); }
                }
                if (result.IsSuccess()) {
                    --m_display_references;
                    if (!m_display_references) {
                        m_notifications[m_display_notification / 32] |= 1U << (m_display_notification % 32);
                        if (m_event) m_event->Signal(kernel);
                        LOG_DEBUG(Service_AM, "VI2 display retired notification={}", m_display_notification);
                    }
                }
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void RegisterClientState(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        const u32 first = packet[4];
        const u32 second = packet[5];
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0xd9 && packet[1] == 4 && token != 0 &&
            first < 32 && second < 32 && first != second) {
            const u32 slots = (1U << first) | (1U << second);
            const auto existing = std::ranges::find(m_client_states, token, &ClientState::token);
            const auto free = std::ranges::find(m_client_states, u64{0}, &ClientState::token);
            result = Kernel::ResultInvalidState;
            if (existing == m_client_states.end() && !(m_client_slots & slots)) {
                result = Kernel::ResultOutOfResource;
                if (free != m_client_states.end()) {
                    // The SDK sends its object address as an opaque identity, not a
                    // service-accessible buffer. Keep the associated slots session-local.
                    *free = {token, slots};
                    free->retire_notification = first;
                    free->available_notification = second;
                    m_client_slots |= slots;
                    result = ResultSuccess;
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 register client state token={:#x}, slots={}/{}, result={:#x}",
                  token, first, second, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void UnregisterClientState(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0xda && packet[1] == 2 && token != 0) {
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (state != m_client_states.end()) {
                if (m_default_endpoint == token) { m_default_endpoint.reset(); }
                if (state->layer) m_display_service->GetContainer()->DestroyManagedLayer(*state->layer);
                m_notifications[state->retire_notification / 32] |= 1U << (state->retire_notification % 32);
                if (m_event) m_event->Signal(kernel);
                LOG_DEBUG(Service_AM, "VI2 client retired token={:#x}, notification={}", token, state->retire_notification);
                m_client_slots &= ~state->slots;
                *state = {};
                result = ResultSuccess;
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void GetClientAvailability(HLERequestContext& ctx) {
        RefreshReleasedBuffers();
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        u64 id{};
        if (packet[0] == 0xe4 && packet[1] == 2 && token) {
            auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (state != m_client_states.end() && m_default_display) {
                result = ResultSuccess;
                if (state->player_display) {
                    id = 0;
                } else if (!state->layer) {
                    result = m_display_service->GetContainer()->CreateManagedLayer(&id, *m_default_display, m_aruid);
                    if (result.IsSuccess()) state->layer = id;
                } else {
                    id = *state->layer;
                }
            }
        }
        const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
        const u64 available = result.IsSuccess() ? state->available : 0;
        LOG_DEBUG(Service_AM, "VI2 client availability token={:#x}, layer={}, mask={:#x}, result={:#x}", token, id, available, result.raw);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(result.raw);
        rb.Push(available);
    }
    void CancelClientBuffer(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        const u32 slot = GraphicsSupervisorProtocol::DecodeBufferSlot(packet[4]);
        Result result = Kernel::ResultInvalidCombination;
        u64 available{};
        if (packet[0] == 0xf4 && packet[1] == 19 && slot < 15) {
            auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end()) {
                // Unlike present (229), cancel has no image or input GPU fence.
                state->available |= u64{1} << slot;
                available = state->available;
                result = ResultSuccess;
            }
        }
        LOG_DEBUG(Service_AM, "VI2 cancel buffer token={:#x}, slot={}, mask={:#x}, result={:#x}", token, slot, available, result.raw);
        IPC::ResponseBuilder rb{ctx, 8};
        rb.Push(result.raw);
        rb.Push(available);
        for (u32 i = 0; i < 4; ++i) rb.Push<u32>(0);
    }
    void RefreshReleasedBuffers() {
        for (auto& state : m_client_states) {
            if (!state.producer || !m_images->module) continue;
            bool changed{};
            for (size_t slot = 0; slot < state.queued.size(); ++slot) {
                if (!state.queued[slot]) continue;
                const auto fence = state.producer->GetReleasedBufferFence(*state.queued[slot]);
                if (!fence) continue;
                bool complete = true;
                for (u32 i = 0; i < fence->num_fences; ++i) {
                    const auto& point = fence->fences[i];
                    if (point.id != ~u32{0}) {
                        const auto counter = m_images->module->GetContainer().GetSyncpointManager().ReadAllocatedSyncpoint(point.id);
                        if (!counter || static_cast<s32>(*counter - point.value) < 0) {
                            complete = false;
                        }
                    }
                }
                if (!complete) continue;
                state.queued[slot].reset();
                state.available |= u64{1} << slot;
                LOG_DEBUG(Service_AM, "Native display buffer retired: slot={}, mask={:#x}", slot, state.available);
                changed = true;
            }
            if (changed) {
                m_notifications[state.available_notification / 32] |= 1U << (state.available_notification % 32);
                if (m_event) m_event->Signal(kernel);
            }
        }
    }
    Result QueueNativeImage(ClientState& state, const u32* packet) {
        const u32 slot = GraphicsSupervisorProtocol::DecodeBufferSlot(packet[4]);
        R_UNLESS(!state.player_display, Kernel::ResultNotImplemented);
        R_UNLESS(state.layer && state.display && slot < 15 && !state.queued[slot], Kernel::ResultInvalidState);
        const std::scoped_lock images_lock{m_images->mutex};
        const auto image = std::ranges::find(m_images->images, packet[5], &NativeImage::id);
        R_UNLESS(image != m_images->images.end() && image->window_pool && m_images->module, Kernel::ResultNotFound);
        // The observed presentation is a four-byte color image with the
        // backend's 16-GOB block height and no byte offset within its pool.
        R_UNLESS(packet[6] == 0 && packet[7] == image->preference[2] && packet[8] == 3 &&
                 image->preference[4] == 7 && state.output_encoding == 0x14,
                 Kernel::ResultNotImplemented);
        const u32 fence_id = ~packet[22];
        R_UNLESS(packet[23] == 0 && packet[25] == 0 &&
                 (fence_id == ~u32{0} || m_images->module->GetContainer().GetSyncpointManager().IsSyncpointAllocated(fence_id)),
                 Kernel::ResultInvalidCombination);
        if (!state.producer) {
            R_TRY(m_display_service->GetContainer()->GetLayerProducerHandle(&state.producer, *state.layer));
            android::QueueBufferOutput output{};
            R_UNLESS(state.producer->Connect({}, android::NativeWindowApi::Egl, false, &output) == android::Status::NoError,
                     Kernel::ResultInvalidState);
            R_UNLESS(state.producer->SetBufferCount(16) == android::Status::NoError, Kernel::ResultInvalidState);
            s32 binder{};
            R_TRY(m_display_service->GetContainer()->OpenLayer(&binder, *state.layer, m_aruid));
            R_TRY(m_display_service->GetContainer()->SetLayerVisibility(*state.layer, true));
        }
        auto native = std::make_shared<android::NvGraphicBuffer>();
        native->width = static_cast<s32>((image->preference[2] & 0xffffU) + 1);
        native->height = static_cast<s32>((image->preference[2] >> 16) + 1);
        native->stride = static_cast<s32>(image->preference[5] * 16);
        native->format = android::PixelFormat::Rgba8888;
        native->external_format = native->format;
        native->buffer_id = image->memory_id;
        auto buffer = std::make_shared<android::GraphicBuffer>(m_images->module->GetContainer().GetNvMapFile(), native);
        s32 queue_slot{};
        R_UNLESS(state.producer->AttachBuffer(&queue_slot, buffer) == android::Status::NoError, Kernel::ResultOutOfResource);
        android::QueueBufferInput input{};
        android::QueueBufferOutput output{};
        input.crop = {0, 0, native->width, native->height};
        input.swap_interval = 1;
        input.fence = android::Fence::NoFence();
        if (fence_id != ~u32{0}) {
            input.fence.num_fences = 1;
            input.fence.fences[0] = {static_cast<s32>(fence_id), packet[24]};
        }
        if (state.producer->QueueBuffer(queue_slot, input, &output) != android::Status::NoError) {
            state.producer->CancelBuffer(queue_slot, input.fence);
            return Kernel::ResultInvalidState;
        }
        state.queued[slot] = queue_slot;
        state.available &= ~(u64{1} << slot);
        if (!m_release_poll) {
            m_release_poll = Core::Timing::CreateEvent("NativeViBufferRelease", [this](s64, std::chrono::nanoseconds) -> std::optional<std::chrono::nanoseconds> {
                const auto guard = LockService();
                RefreshReleasedBuffers();
                return std::nullopt;
            });
            system.CoreTiming().ScheduleLoopingEvent(std::chrono::milliseconds{1}, std::chrono::milliseconds{1}, m_release_poll);
        }
        LOG_INFO(Service_AM, "Native image queued for display: image={}, slot={}, fence={}/{}, dimensions={}x{}",
                 image->id, slot, fence_id, packet[24], native->width, native->height);
        R_SUCCEED();
    }
    void PresentClientBuffer(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        RefreshReleasedBuffers();
        Result result = Kernel::ResultInvalidCombination;
        u64 available{};
        if (packet[0] == 0xf5 && packet[1] == 24) {
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end()) {
                result = QueueNativeImage(*state, packet);
                available = state->available;
            }
        }
        LOG_DEBUG(Service_AM, "VI2 present token={:#x}, image={}, slot={}, mask={:#x}, result={:#x}", token, packet[5], packet[4], available, result.raw);
        IPC::ResponseBuilder rb{ctx, 8};
        rb.Push(result.raw);
        rb.Push(available);
        for (u32 i = 0; i < 4; ++i) rb.Push<u32>(0);
    }
    void SetClientOutputEncoding(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        const u64 encoding = u64{packet[4]} | (u64{packet[5]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x16f && packet[1] == 4) {
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end()) {
                result = Kernel::ResultNotImplemented;
                // Only the SDK's observed default output encoding is supported.
                if (encoding == 0x14) {
                    state->output_encoding = encoding;
                    result = ResultSuccess;
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 output encoding token={:#x}, encoding={:#x}, result={:#x}", token, encoding, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void BindClientDisplay(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        const u64 name = u64{packet[4]} | (u64{packet[5]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x10b && packet[1] == 4) {
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end() && GetPlayerDisplayIndex(name)) {
                const auto player = *GetPlayerDisplayIndex(name);
                result = Kernel::ResultInvalidState;
                if (m_player_display_references[player] && !state->display) {
                    result = ResultSuccess;
                    if (state->layer) {
                        result = m_display_service->GetContainer()->DestroyManagedLayer(*state->layer);
                    }
                    if (result.IsSuccess()) {
                        state->layer.reset();
                        state->display = name;
                        state->player_display = player;
                    }
                }
            } else if (token && state != m_client_states.end() && name == 0x00746c7561666564ULL &&
                m_default_display && m_display_references) {
                result = Kernel::ResultInvalidState;
                if (!state->display) {
                    state->display = m_default_display;
                    result = ResultSuccess;
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 bind client token={:#x}, display={:#x}, result={:#x}",
                  token, name, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void UnbindClientDisplay(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x10c && packet[1] == 2) {
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end() && state->display) {
                state->display.reset();
                state->player_display.reset();
                result = ResultSuccess;
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void OpenEndpoint(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        const u64 display_name = u64{packet[4]} | (u64{packet[5]} << 32);
        const u64 token = u64{packet[6]} | (u64{packet[7]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x7f && packet[1] == 6) {
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end() && state->display &&
                m_default_display == state->display && name == 0x00746c7561666564ULL &&
                display_name == name) {
                result = Kernel::ResultInvalidState;
                if (!m_default_endpoint) {
                    m_default_endpoint = token;
                    result = ResultSuccess;
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 open endpoint name={:#x}, display={:#x}, client={:#x}, result={:#x}",
                  name, display_name, token, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void CloseEndpoint(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        const u64 display_name = u64{packet[4]} | (u64{packet[5]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x80 && packet[1] == 4 && name == 0x00746c7561666564ULL &&
            display_name == name) {
            result = Kernel::ResultNotFound;
            if (m_default_endpoint) {
                m_default_endpoint.reset();
                result = ResultSuccess;
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void ConfigureRequestGroups(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x43 && packet[1] == 1 && packet[2] < 8) {
            m_request_group_limit = packet[2];
            m_request_groups_initialized = true;
            result = ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void GetSessionLimit(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const Result result = packet[0] == 16 + ctx.GetCommand() && packet[1] == 0
                                  ? ResultSuccess : Kernel::ResultInvalidCombination;
        // The SDK converts this inclusive upper group index into 32 client slots.
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(result.raw);
        rb.Push<u32>(ctx.GetCommand() == 5 ? 7 : 0);
    }

    void GetNotifications(HLERequestContext& ctx) {
        Result result = Kernel::ResultInvalidSize;
        if (ctx.GetWriteBufferSize() >= sizeof(m_notifications)) {
            ctx.WriteBuffer(m_notifications);
            LOG_DEBUG(Service_AM, "VI2 notifications={:08x}", fmt::join(m_notifications, ","));
            m_notifications.fill(0);
            if (m_event) m_event->Clear(kernel);
            result = ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void ReleaseNotificationEvent(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const auto result = packet[0] == 0x1d && packet[1] == 1 && packet[2] == 0
            ? ResultSuccess : Kernel::ResultInvalidCombination;
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void GetNotificationEvent(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        Kernel::KReadableEvent* readable{};
        if (packet[0] == 0x1b && packet[1] == 1 && packet[2] == 0) {
            if (!m_event) {
                m_event = m_context.CreateEvent("Vi2Notification");
            }
            result = m_event ? ResultSuccess : Kernel::ResultOutOfResource;
            if (m_event) {
                if (std::ranges::any_of(m_notifications, [](u32 value) { return value != 0; })) m_event->Signal(kernel);
                readable = &m_event->GetReadableEvent();
                readable->Open(kernel);
            }
        }
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
        ctx.AddMoveObject(readable);
    }

    std::shared_ptr<NativeImageRegistry> m_images;
    std::shared_ptr<Core::Timing::EventType> m_release_poll;
    std::array<ClientState, 16> m_client_states{};
    u32 m_client_slots{};
    std::shared_ptr<VI::IApplicationDisplayService> m_display_service;
    std::optional<u64> m_default_display;
    u32 m_display_references{};
    u32 m_display_notification{};
    std::array<u32, 3> m_player_display_references{};
    std::array<u32, 3> m_player_display_notifications{};
    bool m_request_groups_initialized{};
    u32 m_request_group_limit{};
    std::optional<u64> m_default_endpoint;
    std::array<u32, 32> m_notifications{};
    const u64 m_aruid;
    KernelHelpers::ServiceContext m_context;
    Kernel::KEvent* m_event{};
};

} // namespace


IGraphicsSupervisor::IGraphicsSupervisor(Core::System& system, Kernel::KProcess* process, u64 aruid)
    : ServiceFramework{system, "IGraphicsSupervisor"}, m_context{system, "GraphicsSupervisor"},
      m_syncpoint_cache_event{m_context}, m_process{process}, m_aruid{aruid} {
    m_process->Open(kernel);
    m_image_registry = std::make_shared<NativeImageRegistry>(kernel, m_process);
    static const FunctionInfo functions[] = {
        {4, &IGraphicsSupervisor::Initialize, "Initialize"},
        {11, &IGraphicsSupervisor::BindProcess, "BindProcess"},
        {101, &IGraphicsSupervisor::CompleteBootstrap, "CompleteBootstrap"},
        {115, &IGraphicsSupervisor::ConfigureNativeMemory, "ConfigureNativeMemory"},
        {111, &IGraphicsSupervisor::CreateNativeMemory, "CreateNativeMemory"},
        {112, &IGraphicsSupervisor::DestroyNativeMemory, "DestroyNativeMemory"},
        {114, &IGraphicsSupervisor::BindNativeMemory, "BindNativeMemory"},
        {201, &IGraphicsSupervisor::InitializeSyncpointContext, "InitializeSyncpointContext"},
        {202, &IGraphicsSupervisor::GetSyncpointSession, "GetSyncpointSession"},
        {204, &IGraphicsSupervisor::BindSyncpointMemory, "BindSyncpointMemory"},
        {301, &IGraphicsSupervisor::InitializeDisplayContext, "InitializeDisplayContext"},
        {305, &IGraphicsSupervisor::OpenDisplaySession, "OpenDisplaySession"},
        {314, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "DestroyNativeWindowAliasImagePool"},
        {315, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "CreateNativeWindowAliasImagePool"},
        {401, &IGraphicsSupervisor::InitializeGpuContext, "InitializeGpuContext"},
        {402, &IGraphicsSupervisor::InitializeGpuContext, "ActivateGpuContext"},
        {423, &IGraphicsSupervisor::BindNativeGpuMemory, "BindNativeGpuMemory"},
        {429, &IGraphicsSupervisor::CreateNativeImage, "CreateNativeImage"},
        {430, &IGraphicsSupervisor::DestroyNativeImage, "DestroyNativeImage"},
        {434, &IGraphicsSupervisor::RegisterGpuAddressSpace, "RegisterGpuAddressSpace"},
        {497, &IGraphicsSupervisor::OpenGpuSession, "OpenGpuSession"},
        {1101, &IGraphicsSupervisor::InitializeFrameContext, "InitializeFrameContext"},
        {1102, &IGraphicsSupervisor::InitializeFrameContext, "ActivateFrameContext"},
        {1402, &IGraphicsSupervisor::GetNvidiaDriver, "GetNvidiaDriver"},
        {1501, &IGraphicsSupervisor::InitializeImageContext, "InitializeImageContext1501"},
        {435, &IGraphicsSupervisor::CalculateImageRequirements, "CalculateNativeImageSize"},
        {1510, &IGraphicsSupervisor::CalculateImageRequirements, "CalculateImageRequirements"},
        {1502, &IGraphicsSupervisor::InitializeImageContext, "InitializeImageContext1502"},
    };
    RegisterHandlersTipc(functions);
}

IGraphicsSupervisor::~IGraphicsSupervisor() {
    if (m_cache_poll) {
        system.CoreTiming().UnscheduleEvent(m_cache_poll);
    }
    if (m_syncpoint_rw) {
        m_syncpoint_rw->Close(kernel);
        m_syncpoint_ro->Close(kernel);
    }
    if (m_nv_module) {
        auto& nvmap = m_nv_module->GetContainer().GetNvMapFile();
        for (const auto id : m_native_memories) {
            if (const auto info = nvmap.FreeHandle(id, false); info && info->can_unlock && info->address != 0) {
                m_process->GetPageTable().UnlockForDeviceAddressSpace(info->address, info->size);
            }
        }
        if (m_native_memory_session) {
            m_nv_module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*m_native_memory_session});
        }
    }
    m_context.CloseEvent(m_syncpoint_request_event);
    m_process->Close(kernel);
}

Result IGraphicsSupervisor::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    }
    const auto guard = LockService();
    InvokeRequestTipc(ctx);
    // Unsupported TIPC must return an error instead of leaving request data as a reply.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}

void IGraphicsSupervisor::Initialize(HLERequestContext& ctx) {
    const auto request = GraphicsSupervisorProtocol::DecodeBootstrap(
        std::span<const u32>{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH});
    Result result = Kernel::ResultInvalidCombination;
    if (request) {
        auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(request->process_handle);
        const bool empty_remote_memory = std::ranges::all_of(
            request->parameters, [](u64 value) { return value == 0; });
        if (process.IsNull() || process.GetPointerUnsafe() != m_process) {
            result = Kernel::ResultInvalidHandle;
        } else if (m_phase != Phase::Created) {
            result = Kernel::ResultInvalidState;
        } else if (request->applet_resource_user_id != m_aruid) {
            result = Kernel::ResultInvalidArgument;
        } else if (request->flags != 0x8000000000000005ULL || !empty_remote_memory) {
            result = Kernel::ResultNotImplemented;
        } else {
            m_phase = Phase::Initialized;
            result = ResultSuccess;
        }
        LOG_DEBUG(Service_AM, "graphics bootstrap aruid={:#x}, flags={:#x}, result={:#x}",
                  request->applet_resource_user_id, request->flags, result.raw);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::BindProcess(HLERequestContext& ctx) {
    Result result = Kernel::ResultInvalidCombination;
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] == 0x1b && packet[1] == 0x80000000 && packet[2] == 2) {
        auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(ctx.GetCopyHandle(0));
        if (process.IsNull() || process.GetPointerUnsafe() != m_process) {
            result = Kernel::ResultInvalidHandle;
        } else if (m_phase != Phase::Initialized) {
            result = Kernel::ResultInvalidState;
        } else {
            m_phase = Phase::ProcessBound;
            result = ResultSuccess;
        }
    }
    LOG_DEBUG(Service_AM, "graphics process binding result={:#x}", result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::CompleteBootstrap(HLERequestContext& ctx) {
    Result result = Kernel::ResultInvalidCombination;
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] == 0x75 && packet[1] == 0) {
        if (m_phase != Phase::ProcessBound) {
            result = Kernel::ResultInvalidState;
        } else {
            m_phase = Phase::Ready;
            result = ResultSuccess;
        }
    }
    LOG_DEBUG(Service_AM, "graphics bootstrap completion result={:#x}", result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::InitializeImageContext(HLERequestContext& ctx) {
    Result result = Kernel::ResultInvalidCombination;
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] == 16 + ctx.GetCommand() && packet[1] == 0) {
        if (m_phase != Phase::Ready) {
            result = Kernel::ResultInvalidState;
        } else if (ctx.GetCommand() == 1501) {
            m_image_context_initialized = true;
            result = ResultSuccess;
        } else if (m_image_context_initialized) {
            m_image_formats_initialized = true;
            result = ResultSuccess;
        } else {
            result = Kernel::ResultInvalidState;
        }
    }
    LOG_DEBUG(Service_AM, "empty image context command={}, result={:#x}",
              ctx.GetCommand(), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    const u64 flags = u64{packet[2]} | (u64{packet[3]} << 32);
    if (packet[0] == 0x201 && packet[1] == 2 && flags == 4) {
        if (m_phase != Phase::Ready || !m_nv_module) {
            result = Kernel::ResultInvalidState;
        } else {
            auto handler = std::make_shared<IGpuSession>(system, m_nv_module, m_process);
            result = CreateGraphicsSession(system, ctx, handler, &client);
            if (result.IsSuccess()) {
                std::erase_if(m_gpu_sessions, [](const auto& item) { return item.expired(); });
                m_gpu_sessions.emplace_back(handler);
            }
        }
    }
    LOG_DEBUG(Service_AM, "GPU control session flags={:#x}, result={:#x}", flags, result.raw);
    // This SDK reads the move-handle slot before the result, including on failure.
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

void IGraphicsSupervisor::RegisterGpuAddressSpace(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1c2 && packet[1] == 1) {
        result = m_gpu_context_ready ? Kernel::ResultNotFound : Kernel::ResultInvalidState;
        if (m_gpu_context_ready) {
            for (const auto& item : m_gpu_sessions) {
                if (auto handler = item.lock()) {
                    result = handler->RegisterAddressSpace(packet[2]);
                    if (result.IsSuccess()) {
                        break;
                    }
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "register GPU address space id={}, result={:#x}", packet[2], result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::InitializeDisplayContext(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x13d && packet[1] == 0) {
        result = m_phase == Phase::Ready ? ResultSuccess : Kernel::ResultInvalidState;
        if (result.IsSuccess()) {
            m_display_context_initialized = true;
        }
    }
    LOG_DEBUG(Service_AM, "display context initialization result={:#x}", result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenDisplaySession(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Kernel::KClientSession* client{};
    const u64 flags = u64{packet[2]} | (u64{packet[3]} << 32);
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x141 && packet[1] == 2 && flags == 0x300020000ULL) {
        result = m_display_context_initialized ? ResultSuccess : Kernel::ResultInvalidState;
        if (result.IsSuccess()) {
            result = CreateGraphicsSession(system, ctx, std::make_shared<IVi2Session>(system, m_aruid, m_image_registry),
                                           &client);
        }
    }
    LOG_DEBUG(Service_AM, "VI2 session flags={:#x}, result={:#x}", flags, result.raw);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

void IGraphicsSupervisor::InitializeGpuContext(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 16 + ctx.GetCommand() && packet[1] == 0) {
        if (m_phase != Phase::Ready) {
            result = Kernel::ResultInvalidState;
        } else if (ctx.GetCommand() == 401) {
            m_gpu_context_initialized = true;
            result = ResultSuccess;
        } else if (m_gpu_context_initialized) {
            m_gpu_context_ready = true;
            result = ResultSuccess;
        } else {
            result = Kernel::ResultInvalidState;
        }
    }
    LOG_DEBUG(Service_AM, "GPU context command={}, result={:#x}", ctx.GetCommand(), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::InitializeFrameContext(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 16 + ctx.GetCommand() && packet[1] == 0) {
        if (m_phase != Phase::Ready) {
            result = Kernel::ResultInvalidState;
        } else if (ctx.GetCommand() == 1101) {
            m_frame_context_initialized = true;
            result = ResultSuccess;
        } else if (m_frame_context_initialized) {
            m_frame_context_ready = true;
            result = ResultSuccess;
        } else {
            result = Kernel::ResultInvalidState;
        }
    }
    LOG_DEBUG(Service_AM, "empty frame context command={}, result={:#x}", ctx.GetCommand(), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::ConfigureNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x83 && packet[1] == 2 && packet[3] == 0 &&
        (packet[2] == 0 || packet[2] == 2)) {
        result = m_phase == Phase::Ready ? ResultSuccess : Kernel::ResultInvalidState;
    }
    // Clear the optional Gpuk-memory mode: Eden currently provides the NVDRV path.
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push<u64>(0);
    LOG_DEBUG(Service_AM, "native memory uses NVDRV-compatible mode, result={:#x}", result.raw);
}

void IGraphicsSupervisor::InitializeSyncpointContext(HLERequestContext& ctx) {
    Result result = Kernel::ResultInvalidCombination;
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] == 0xd9 && packet[1] == 0) {
        if (m_phase != Phase::Ready) {
            result = Kernel::ResultInvalidState;
        } else {
            m_syncpoint_context_initialized = true;
            result = ResultSuccess;
        }
    }
    LOG_DEBUG(Service_AM, "syncpoint context initialization result={:#x}", result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::GetSyncpointSession(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidState;
    Kernel::KClientSession* client{};
    if (packet[0] == 0xda && packet[1] == 0 && m_syncpoint_context_initialized && m_nv_module) {
        result = CreateGraphicsSession(system, ctx,
            std::make_shared<ISyncpointSupervisor>(system, m_nv_module), &client);
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess.raw);
    rb.PushMoveObjects(ctx, client);
    LOG_DEBUG(Service_AM, "opened syncpoint-supervisor session");
}

void IGraphicsSupervisor::GetNvidiaDriver(HLERequestContext& ctx) {
    const auto Fail = [&](Result result) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    };
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] != 0x58a || packet[1] != 0) {
        Fail(Kernel::ResultInvalidCombination);
        return;
    }
    if (m_phase != Phase::Ready || !m_image_formats_initialized) {
        Fail(Kernel::ResultInvalidState);
        return;
    }
    auto source = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
    if (!source) {
        Fail(Kernel::ResultNotFound);
        return;
    }
    m_nv_module = source->GetModule();
    Kernel::KClientSession* client{};
    const auto result = CreateGraphicsSession(system, ctx,
        std::make_shared<Nvidia::NVDRV>(system, m_nv_module, "nvdrv:gsv", m_process, m_aruid),
        &client);
    if (result.IsError()) {
        Fail(result);
        return;
    }
    LOG_DEBUG(Service_AM, "opened process-bound NVDRV session for aruid={:#x}", m_aruid);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess.raw);
    rb.PushMoveObjects(ctx, client);
}

void IGraphicsSupervisor::BindSyncpointMemory(HLERequestContext& ctx) {
    using namespace GraphicsSyncpointCache;
    const auto Fail = [&](Result result) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 2};
        rb.Push(result.raw);
        ctx.AddMoveObject(nullptr);
        ctx.AddMoveObject(nullptr);
    };
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] != 0xdc || packet[1] != 0x80000006 || packet[2] != 4 ||
        packet[5] != 1 || packet[6] != 0x1ff || packet[7] != 0x1000 || packet[8] != 0 ||
        packet[9] != 0x2000 || packet[10] != 0) {
        Fail(Kernel::ResultInvalidCombination);
        return;
    }
    if (!m_syncpoint_context_initialized || !m_nv_module || m_syncpoint_rw) {
        Fail(Kernel::ResultInvalidState);
        return;
    }
    auto rw = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(0));
    auto ro = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(1));
    if (rw.IsNull() || ro.IsNull() || rw->GetOwner() != m_process || ro->GetOwner() != m_process ||
        rw.GetPointerUnsafe() == ro.GetPointerUnsafe()) {
        Fail(Kernel::ResultInvalidHandle);
        return;
    }
    if (rw->GetSize() != 0x1000 || ro->GetSize() != 0x2000) {
        Fail(Kernel::ResultInvalidSize);
        return;
    }
    auto* request_event = m_context.CreateEvent("SyncpointCacheRequest");
    if (request_event == nullptr) {
        Fail(Kernel::ResultOutOfResource);
        return;
    }
    m_syncpoint_request_event = request_event;
    m_syncpoint_rw = rw.GetPointerUnsafe();
    m_syncpoint_ro = ro.GetPointerUnsafe();
    m_syncpoint_rw->Open(kernel);
    m_syncpoint_ro->Open(kernel);
    auto& memory = m_process->GetMemory();
    const auto rw_base = rw->GetSourceAddress();
    const auto ro_base = ro->GetSourceAddress();
    const std::array<u8, 0x2000> zeros{};
    memory.WriteBlock(rw_base, zeros.data(), 0x1000);
    memory.WriteBlock(ro_base, zeros.data(), 0x2000);
    memory.WriteBlock(ro_base, Header.data(), sizeof(Header));
    for (u32 id = 0; id < CounterCount; ++id) {
        memory.Write64(ro_base + ValuesOffset + id * sizeof(u64), Unallocated);
    }
    m_cache_poll = Core::Timing::CreateEvent("GraphicsSyncpointCache",
        [this](s64, std::chrono::nanoseconds) -> std::optional<std::chrono::nanoseconds> {
            const auto guard = LockService();
            this->RefreshSyncpointCache();
            return std::nullopt;
        });
    system.CoreTiming().ScheduleLoopingEvent(std::chrono::milliseconds{1},
                                             std::chrono::milliseconds{1}, m_cache_poll);
    request_event->Open(kernel);
    auto* completed = m_syncpoint_cache_event.GetHandle();
    completed->Open(kernel);
    IPC::ResponseBuilder rb{ctx, 2, 0, 2};
    rb.Push(ResultSuccess.raw);
    ctx.AddMoveObject(request_event);
    ctx.AddMoveObject(completed);
    LOG_DEBUG(Service_AM, "bound syncpoint cache: {} real counters, rw={:#x}, ro={:#x}",
              CounterCount, GetInteger(rw_base), GetInteger(ro_base));
}

void IGraphicsSupervisor::RefreshSyncpointCache() {
    using namespace GraphicsSyncpointCache;
    if (!m_syncpoint_rw || !system.IsPoweredOn()) {
        return;
    }
    auto& memory = m_process->GetMemory();
    const auto rw = m_syncpoint_rw->GetSourceAddress();
    const auto ro = m_syncpoint_ro->GetSourceAddress();
    const u64 requests = memory.Read64(rw + GroupRequestOffset);
    const u64 acknowledgements = memory.Read64(ro + GroupAckOffset);
    u64 groups = (requests ^ acknowledgements) & ((1ULL << GroupCount) - 1);
    if (groups == 0) {
        return;
    }
    auto& syncpoints = m_nv_module->GetContainer().GetSyncpointManager();
    while (groups) {
        const u32 group = static_cast<u32>(std::countr_zero(groups));
        groups &= groups - 1;
        const u64 requested_slots = memory.Read64(rw + SlotRequestOffset + group * sizeof(u64));
        u64 slots = requested_slots ^ memory.Read64(ro + SlotAckOffset + group * sizeof(u64));
        while (slots) {
            const u32 bit = static_cast<u32>(std::countr_zero(slots));
            slots &= slots - 1;
            const u32 id = group * 64 + bit;
            const auto address = ro + ValuesOffset + id * sizeof(u64);
            const auto value = syncpoints.ReadAllocatedSyncpoint(id);
            const u64 updated = m_counter_values[id].Observe(value);
            memory.Write64(address, updated);
        }
        std::atomic_thread_fence(std::memory_order_release);
        memory.Write64(ro + SlotAckOffset + group * sizeof(u64), requested_slots);
    }
    std::atomic_thread_fence(std::memory_order_release);
    memory.Write64(ro + GroupAckOffset, requests);
    m_syncpoint_request_event->Clear(kernel);
    m_syncpoint_cache_event.Signal(kernel);
}

void IGraphicsSupervisor::CalculateImageRequirements(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::optional<NativeImageRequirements> requirements;
    const bool gpu_image = ctx.GetCommand() == 435;
    if (packet[0] == (gpu_image ? 0x1c3U : 0x5f6U) && packet[1] == 17 &&
        packet[3] == (gpu_image ? 1U : 2U)) {
        result = Kernel::ResultInvalidState;
        if (m_phase == Phase::Ready && m_image_formats_initialized) {
            requirements = CalculateNativeImageRequirements(std::span{packet + 2, 16}, packet[18]);
            result = requirements ? ResultSuccess : Kernel::ResultNotImplemented;
        }
    }
    LOG_DEBUG(Service_AM, "native image requirements dimensions={:#x}, block={}, pitch={}, size={:#x}, alignment={:#x}, result={:#x}",
              packet[4], packet[6], packet[7], requirements ? requirements->size : 0,
              requirements ? requirements->alignment : 0, result.raw);
    IPC::ResponseBuilder rb{ctx, gpu_image ? 4U : 6U};
    rb.Push(result.raw);
    rb.Push<u64>(requirements ? requirements->size : 0);
    if (!gpu_image) { rb.Push<u64>(requirements ? requirements->alignment : 0); }
}
void IGraphicsSupervisor::CreateNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const u64 size = u64{packet[3]} | (u64{packet[4]} << 32);
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (packet[0] == 0x7f && packet[1] == 3 && (packet[2] & 0xff) == 3) {
        result = Kernel::ResultInvalidState;
        if (m_phase == Phase::Ready && m_image_formats_initialized && m_nv_module) {
            result = Kernel::ResultInvalidSize;
            if (size && size <= (1ULL << 32) && Common::IsAligned(size, Kernel::PageSize)) {
                result = Kernel::ResultOutOfResource;
                if (m_native_memories.size() < 1024) {
                    auto& nvmap = m_nv_module->GetContainer().GetNvMapFile();
                    std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
                    if (nvmap.CreateHandle(size, handle) == Nvidia::NvResult::Success) {
                        id = handle->id;
                        m_native_memories.push_back(id);
                        result = ResultSuccess;
                    }
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "native memory create size={:#x}, id={}, result={:#x}", size, id, result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

Result IGraphicsSupervisor::BindNativeMemoryBacking(u32 id, u64 address, u64 size, u64 flags, u64 offset) {
    R_UNLESS(m_nv_module && std::ranges::find(m_native_memories, id) != m_native_memories.end(),
             Kernel::ResultNotFound);
    R_UNLESS((flags == 0 || flags == 2) && offset == 0, Kernel::ResultNotImplemented);
    auto handle = m_nv_module->GetContainer().GetNvMapFile().GetHandle(id);
    R_UNLESS(handle && !handle->allocated, Kernel::ResultInvalidState);
    R_UNLESS(size && size == handle->size && address != 0 && address + size > address,
             Kernel::ResultInvalidSize);
    R_UNLESS(Common::IsAligned(address, Kernel::PageSize), Kernel::ResultInvalidAddress);
    auto& page_table = m_process->GetPageTable();
    R_UNLESS(page_table.Contains(address, size), Kernel::ResultInvalidCurrentMemory);
    if (flags == 2) {
        for (u64 current = address; current < address + size;) {
            Kernel::KMemoryInfo info{};
            Kernel::Svc::PageInfo page{};
            R_TRY(page_table.QueryInfo(&info, &page, current));
            R_UNLESS(True(info.m_attribute & Kernel::KMemoryAttribute::GpuAccessible),
                     Kernel::ResultInvalidCurrentMemory);
            const u64 next = info.m_address + info.m_size;
            R_UNLESS(next > current, Kernel::ResultInvalidCurrentMemory);
            current = next;
        }
    }
    bool is_io{};
    R_TRY(page_table.LockForMapDeviceAddressSpace(&is_io, address, size,
                                                 Kernel::KMemoryPermission::None, true, true));
    if (is_io) {
        page_table.UnlockForDeviceAddressSpace(address, size);
        R_RETURN(Kernel::ResultNotImplemented);
    }
    if (!m_native_memory_session) {
        m_native_memory_session = m_nv_module->GetContainer().OpenSession(m_process).id;
    }
    const auto result = handle->Alloc({}, Kernel::PageSize, 0, address,
                                      Nvidia::NvCore::SessionId{*m_native_memory_session});
    if (result != Nvidia::NvResult::Success) {
        page_table.UnlockForDeviceAddressSpace(address, size);
        R_RETURN(Kernel::ResultOutOfResource);
    }
    R_SUCCEED();
}

void IGraphicsSupervisor::BindNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const auto pair = [&](size_t i) { return u64{packet[i]} | (u64{packet[i + 1]} << 32); };
    const auto result = packet[0] == 0x82 && packet[1] == 9
        ? BindNativeMemoryBacking(packet[2], pair(3), pair(5), pair(7), pair(9))
        : Kernel::ResultInvalidCombination;
    LOG_DEBUG(Service_AM, "native memory bind id={}, address={:#x}, size={:#x}, flags={:#x}, result={:#x}",
              packet[2], pair(3), pair(5), pair(7), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

Result IGraphicsSupervisor::ReleaseNativeMemory(u32 id) {
    const auto it = std::ranges::find(m_native_memories, id);
    R_UNLESS(m_nv_module && it != m_native_memories.end(), Kernel::ResultNotFound);
    auto& nvmap = m_nv_module->GetContainer().GetNvMapFile();
    auto handle = nvmap.GetHandle(id);
    R_UNLESS(handle, Kernel::ResultNotFound);
    {
        const std::scoped_lock lock{handle->mutex};
        R_UNLESS(handle->pins == 0 && handle->internal_dupes == 0 && handle->dupes == 1,
                 Kernel::ResultBusy);
    }
    const bool allocated = handle->allocated;
    handle.reset();
    const auto info = nvmap.FreeHandle(id, false);
    m_native_memories.erase(it);
    if (allocated) {
        R_UNLESS(info && info->can_unlock, Kernel::ResultInvalidState);
        R_TRY(m_process->GetPageTable().UnlockForDeviceAddressSpace(info->address, info->size));
    }
    R_SUCCEED();
}

void IGraphicsSupervisor::DestroyNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const auto result = packet[0] == 0x80 && packet[1] == 1
        ? ReleaseNativeMemory(packet[2]) : Kernel::ResultInvalidCombination;
    LOG_DEBUG(Service_AM, "native memory destroy id={}, result={:#x}", packet[2], result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::BindNativeGpuMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const auto pair = [&](size_t i) { return u64{packet[i]} | (u64{packet[i + 1]} << 32); };
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1b7 && packet[1] == 12 && pair(10) == 0 && pair(12) == 0) {
        result = Kernel::ResultNotFound;
        if (m_nv_module) {
            auto& container = m_nv_module->GetContainer();
            const auto parent = container.GetNvMapFile().GetHandle(packet[7]);
            if (parent && parent->allocated) {
                const auto* session = container.GetSession(parent->session_id);
                const u64 offset = pair(8);
                const u64 address = pair(3);
                const u64 size = pair(5);
                if (session && session->process == m_process && offset <= parent->size &&
                    size <= parent->size - offset && parent->address + offset >= parent->address &&
                    address == parent->address + offset) {
                    result = BindNativeMemoryBacking(packet[2], address, size, 2, 0);
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "native GPU memory bind id={}, parent={}, address={:#x}, size={:#x}, offset={:#x}, result={:#x}",
              packet[2], packet[7], pair(3), pair(5), pair(8), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::CreateNativeImage(HLERequestContext& ctx) {
    const std::scoped_lock images_lock{m_image_registry->mutex};
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (packet[0] == 0x1bd && packet[1] == 18 && packet[3] == 1) {
        result = Kernel::ResultInvalidState;
        if (m_phase == Phase::Ready && m_gpu_context_ready && m_image_formats_initialized && m_nv_module) {
            const auto requirements = CalculateNativeImageRequirements(std::span{packet + 2, 16}, packet[18]);
            result = Kernel::ResultNotImplemented;
            if (requirements) {
                result = Kernel::ResultNotFound;
                auto& nvmap = m_nv_module->GetContainer().GetNvMapFile();
                const auto memory = nvmap.GetHandle(packet[19]);
                if (std::ranges::find(m_native_memories, packet[19]) != m_native_memories.end() &&
                    memory && memory->allocated) {
                    result = Kernel::ResultInvalidSize;
                    if (memory->size >= requirements->size) {
                        result = Kernel::ResultOutOfResource;
                        if (m_image_registry->images.size() < 1024 && m_next_native_image_id != 0 &&
                            memory->Duplicate(true) == Nvidia::NvResult::Success) {
                            NativeImage image{m_next_native_image_id++, packet[19], {}, requirements->size};
                            std::copy_n(packet + 2, image.preference.size(), image.preference.begin());
                            id = image.id;
                            if (!m_image_registry->module) {
                                m_image_registry->module = m_nv_module;
                                m_image_registry->memory_session = m_nv_module->GetContainer().OpenSession(m_process);
                            }
                            m_image_registry->images.push_back(image);
                            result = ResultSuccess;
                        }
                    }
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "native image create memory={}, count={}, id={}, result={:#x}",
              packet[19], packet[18], id, result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGraphicsSupervisor::DestroyNativeImage(HLERequestContext& ctx) {
    const std::scoped_lock images_lock{m_image_registry->mutex};
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1be && packet[1] == 1) {
        const auto image = std::ranges::find(m_image_registry->images, packet[2], &NativeImage::id);
        result = Kernel::ResultNotFound;
        if (m_nv_module && image != m_image_registry->images.end()) {
            result = Kernel::ResultBusy;
            if (!image->window_pool) {
                m_image_registry->ReleaseMemory(image->memory_id);
                m_image_registry->images.erase(image);
                result = ResultSuccess;
            }
        }
    }
    LOG_DEBUG(Service_AM, "native image destroy id={}, result={:#x}", packet[2], result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::ConfigureNativeWindowImagePool(HLERequestContext& ctx) {
    const std::scoped_lock images_lock{m_image_registry->mutex};
    const auto* packet = ctx.CommandBuffer();
    const bool create = ctx.GetCommand() == 315;
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == (create ? 0x14bU : 0x14aU) && packet[1] == (create ? 2U : 1U)) {
        const auto image = std::ranges::find(m_image_registry->images, packet[2], &NativeImage::id);
        result = Kernel::ResultNotFound;
        if (image != m_image_registry->images.end()) {
            result = Kernel::ResultNotImplemented;
            if (!create || packet[3] == 1) {
                result = Kernel::ResultInvalidState;
                if (image->window_pool != create) {
                    image->window_pool = create;
                    result = ResultSuccess;
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "native window image pool id={}, create={}, result={:#x}",
              packet[2], create, result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

} // namespace Service::AM
