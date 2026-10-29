// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <limits>
#include "core/hle/service/am/service/native_video_layout.h"
#include "core/hle/service/am/service/native_video_decoder.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <array>
#include <cstring>
#include <string_view>
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
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
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
    struct Backing {
        u32 memory_id;
        u32 parent_id;
        u64 offset;
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
            DrainRetiredMemories();
            while (!backings.empty()) {
                ReleaseBacking(backings.back().memory_id);
            }
        }
        if (memory_session) {
            module->GetContainer().CloseSession(*memory_session);
        }
        if (backing_session) {
            module->GetContainer().CloseSession(*backing_session);
        }
        process->Close(kernel);
    }
    void ReleaseMemory(u32 id) {
        const auto info = module->GetContainer().GetNvMapFile().FreeHandle(id, true);
        if (info && info->can_unlock && info->address != 0) {
            process->GetPageTable().UnlockForDeviceAddressSpace(info->address, info->size);
        }
    }
    void ReleaseBacking(u32 id) {
        const auto it = std::ranges::find(backings, id, &Backing::memory_id);
        if (it == backings.end()) return;
        module->GetContainer().GetNvMapFile().UnpinHandle(it->parent_id);
        ReleaseMemory(it->parent_id);
        backings.erase(it);
    }
    void DrainRetiredMemories() {
        auto& nvmap = module->GetContainer().GetNvMapFile();
        std::erase_if(retired_memories, [&](u32 id) {
            auto handle = nvmap.GetHandle(id);
            if (!handle) {
                ReleaseBacking(id);
                return true;
            }
            {
                const std::scoped_lock lock{handle->mutex};
                if (handle->pins || handle->internal_dupes || handle->dupes != 1) return false;
            }
            handle.reset();
            const auto info = nvmap.FreeHandle(id, false);
            if (info && info->can_unlock && info->address) {
                process->GetPageTable().UnlockForDeviceAddressSpace(info->address, info->size);
            }
            ReleaseBacking(id);
            return true;
        });
    }
    std::vector<Backing> backings;
    std::vector<u32> retired_memories;
    Kernel::KernelCore& kernel;
    Kernel::KProcess* process;
    std::mutex mutex;
    std::shared_ptr<Nvidia::Module> module;
    std::optional<Nvidia::NvCore::SessionId> memory_session;
    std::optional<Nvidia::NvCore::SessionId> backing_session;
    std::vector<Image> images;
};
using NativeImage = NativeImageRegistry::Image;


namespace {

// TIPC payloads follow a 32-bit result, so these handlers push result.raw.

class ISyncpointSupervisor final : public ServiceFramework<ISyncpointSupervisor> {
public:
    ISyncpointSupervisor(Core::System& system, std::shared_ptr<Nvidia::Module> module)
        : ServiceFramework{system, "ISyncpointSupervisor"}, m_module{std::move(module)},
          m_context{system, "SyncpointSupervisor"} {
        static const FunctionInfo functions[] = {
            {1, &ISyncpointSupervisor::GetMaximumEventCount, "GetMaximumEventCount"},
            {21, &ISyncpointSupervisor::SetEventCount, "SetEventCount"},
            {22, &ISyncpointSupervisor::GetEvent, "GetEvent"},
            {103, &ISyncpointSupervisor::QueryCounter, "QueryCounter"},
            {201, &ISyncpointSupervisor::ArmWait, "ArmWait"},
            {202, &ISyncpointSupervisor::CancelWait, "CancelWait"},
        };
        RegisterHandlersTipc(functions);
    }

    ~ISyncpointSupervisor() override {
        if (m_wait_poll) system.CoreTiming().UnscheduleEvent(m_wait_poll);
        for (auto* event : m_events) if (event) m_context.CloseEvent(event);
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
            for (u32 slot = packet[2]; slot < MaximumEventCount; ++slot) {
                m_waits[slot].reset();
                if (m_events[slot]) m_events[slot]->Clear(kernel);
            }
            m_event_count = packet[2];
            result = ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        LOG_DEBUG(Service_AM, "syncpoint event count={}, result={:#x}", m_event_count, result.raw);
    }

    void GetEvent(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        Kernel::KReadableEvent* readable{};
        if (packet[0] == 0x26 && packet[1] == 1) {
            result = Kernel::ResultInvalidArgument;
            if (packet[2] < m_event_count) {
                auto& event = m_events[packet[2]];
                if (!event) event = m_context.CreateEvent("SyncpointNotification");
                result = event ? ResultSuccess : Kernel::ResultOutOfResource;
                if (event) {
                    readable = &event->GetReadableEvent();
                    readable->Open(kernel);
                }
            }
        }
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
        ctx.AddMoveObject(readable);
    }

    void PollWaits() {
        for (u32 slot = 0; slot < m_event_count; ++slot) {
            auto& wait = m_waits[slot];
            if (!wait) continue;
            const auto current = m_module->GetContainer().GetSyncpointManager().ReadAllocatedSyncpoint(wait->id);
            if (!current || GraphicsSupervisorProtocol::IsSyncpointWaitSatisfied(*current, wait->threshold, wait->wide)) {
                m_events[slot]->Signal(kernel);
                wait.reset();
            }
        }
    }

    void ArmWait(HLERequestContext& ctx) {
        const auto request = GraphicsSupervisorProtocol::DecodeSyncpointWait(
            std::span<const u32>{ctx.CommandBuffer(), 7});
        Result result = Kernel::ResultInvalidCombination;
        if (request) {
            result = Kernel::ResultInvalidArgument;
            if (request->slot < m_event_count) {
                const auto current = m_module->GetContainer().GetSyncpointManager().ReadAllocatedSyncpoint(request->id);
                result = current ? ResultSuccess : Kernel::ResultNotFound;
                if (current && request->wide && request->threshold > std::numeric_limits<u32>::max())
                    result = Kernel::ResultNotImplemented;
                if (result == ResultSuccess) {
                    auto& event = m_events[request->slot];
                    if (!event) event = m_context.CreateEvent("SyncpointNotification");
                    if (!event) {
                        result = Kernel::ResultOutOfResource;
                    } else {
                        event->Clear(kernel);
                        m_waits[request->slot] = *request;
                        PollWaits();
                        if (!m_wait_poll) {
                            const std::weak_ptr<ISyncpointSupervisor> weak =
                                std::static_pointer_cast<ISyncpointSupervisor>(shared_from_this());
                            m_wait_poll = Core::Timing::CreateEvent("NativeSyncpointWait",
                                [weak](s64, std::chrono::nanoseconds) -> std::optional<std::chrono::nanoseconds> {
                                    if (const auto self = weak.lock()) {
                                        const auto guard = self->LockService();
                                        self->PollWaits();
                                    }
                                    return std::nullopt;
                                });
                            system.CoreTiming().ScheduleLoopingEvent(std::chrono::milliseconds{1},
                                std::chrono::milliseconds{1}, m_wait_poll);
                        }
                    }
                }
            }
            LOG_DEBUG(Service_AM, "syncpoint wait slot={}, id={}, threshold={}, wide={}, result={:#x}",
                      request->slot, request->id, request->threshold, request->wide, result.raw);
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }

    void CancelWait(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0xda && packet[1] == 1) {
            result = Kernel::ResultInvalidArgument;
            if (packet[2] < m_event_count) {
                m_waits[packet[2]].reset();
                if (m_events[packet[2]]) m_events[packet[2]]->Signal(kernel);
                result = ResultSuccess;
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
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
    std::array<std::optional<GraphicsSupervisorProtocol::SyncpointWait>, MaximumEventCount> m_waits{};
    std::shared_ptr<Core::Timing::EventType> m_wait_poll;
    KernelHelpers::ServiceContext m_context;
    std::array<Kernel::KEvent*, MaximumEventCount> m_events{};
};

// Session returned by camera command 602 (served by the camera module on the real console). Command 3
// takes a 16-byte device name, "Default" zero-padded in MKW and Welcome Tour, and replies with the result
// and one u64. The camera module's handler (its interfaces carry no RTTI) was not located; its static
// initializer (0400000000000070 + 0x14360) builds a two-entry name table with the keys "External" and
// "Default", both mapped to the value pair {2, 2}, so the u64 reply is modelled as that value
// (0x200000002) for those names. Further commands are not implemented and are logged.
class ICameraContext final : public ServiceFramework<ICameraContext> {
public:
    explicit ICameraContext(Core::System& system) : ServiceFramework{system, "ICameraContext"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {3, D<&ICameraContext::OpenDevice>, "OpenDevice"},
            {21, D<&ICameraContext::Command21>, "Command21"},
            {4, &ICameraContext::Generic<0>, "Command4"},
            {9, &ICameraContext::Generic<1>, "Command9"},
            {10, &ICameraContext::Generic<1>, "Command10"},
            {11, &ICameraContext::Generic<1>, "Command11"},
            {12, &ICameraContext::Generic<0>, "Command12"},
            {13, &ICameraContext::Generic<2>, "Command13"},
            {14, &ICameraContext::Generic<0>, "Command14"},
            {16, &ICameraContext::Generic<0>, "Command16"},
            {17, &ICameraContext::Generic<1>, "Command17"},
            {18, &ICameraContext::Generic<0>, "Command18"},
            {19, &ICameraContext::Generic<0>, "Command19"},
            {24, &ICameraContext::Generic<0>, "Command24"},
            {25, &ICameraContext::Generic<0>, "Command25"},
            {29, &ICameraContext::Generic<0>, "Command29"},
            {30, &ICameraContext::Generic<1>, "Command30"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    Result OpenDevice(Out<u64> out_device, std::array<char, 16> name) {
        LOG_DEBUG(Service_AM, "camera open device name={}", std::string_view{name.data(), strnlen(name.data(), 16)});
        const std::string_view device{name.data(), strnlen(name.data(), 16)};
        R_UNLESS(device == "Default" || device == "External", Result(static_cast<ErrorModule>(0xf3), 0x140));
        *out_device = 0x200000002ULL;
        R_SUCCEED();
    }

    // Probe commands: the camera session's client stubs in MKW (main+0x1c6c000..0x1c6e700) give each
    // command's request size and reply layout (OUT u64 words after the result) but the camera
    // module's handlers were not located, so these accept any request and reply with success and
    // zeroed outputs. Request sizes in bytes: 4/12/14/18/19/21/24: 8, 9/10/11/16/17: 8 to 40, 13: 8,
    // 25/29: 48, 30: 24.
    template <u32 OutWords>
    void Generic(HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "camera command {} (success, {} output words)", ctx.GetCommand(), OutWords);
        IPC::ResponseBuilder rb{ctx, 2 + OutWords * 2};
        rb.Push(ResultSuccess);
        for (u32 i = 0; i < OutWords; ++i) {
            rb.Push<u64>(0);
        }
    }

    // Probe: Welcome Tour sends command 21 with one 8-byte argument (value 1) right after OpenDevice.
    // Its camera-module handler was not located, so it is accepted as a setting-style command with no
    // output.
    Result Command21(u64 value) {
        LOG_DEBUG(Service_AM, "camera command 21 value={:#x}", value);
        R_SUCCEED();
    }
};

// Session returned by command 1121. The SDK's client (sdk+0x411e2c) sends TIPC command 0 with a u64
// (a tick-like value) right after getting the handle and requires the reply to repeat that value
// (reply header 0x3_00000010: result and the u64); it then uses command 11 with two u64 values.
// Command 11 (header 0x4_0000001b: two u64 arguments; reply header 0x3_0000001b: result and one u64,
// client sdk+0x411e68) is only a probe: the real server class was not located in gsv1, so the
// arguments are logged and the u64 reply is 0. Other commands are logged and rejected.
class INativeSession1121 final : public ServiceFramework<INativeSession1121> {
public:
    explicit INativeSession1121(Core::System& system) : ServiceFramework{system, "INativeSession1121"} {
        static const FunctionInfo functions[] = {
            {0, &INativeSession1121::Echo, "Echo"},
            {11, &INativeSession1121::Command11, "Command11"},
        };
        RegisterHandlersTipc(functions);
    }

    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override {
        if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
            return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
        }
        const auto guard = LockService();
        LOG_WARNING(Service_AM, "native session 1121 unsupported command={}", ctx.GetCommand());
        InvokeRequestTipc(ctx);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented.raw);
        return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
    }

private:
    void Command11(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        u64 first{};
        u64 second{};
        if (packet[0] == 0x1b && packet[1] == 4) {
            first = u64{packet[2]} | (u64{packet[3]} << 32);
            second = u64{packet[4]} | (u64{packet[5]} << 32);
            result = ResultSuccess;
        }
        LOG_INFO(Service_AM, "native session 1121 command 11 (probe) args={:#x}, {:#x}, result={:#x}",
                 first, second, result.raw);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(result.raw);
        rb.Push<u64>(0);
    }

    void Echo(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        u64 value{};
        if (packet[0] == 0x10 && packet[1] == 2) {
            value = u64{packet[2]} | (u64{packet[3]} << 32);
            result = ResultSuccess;
        }
        LOG_DEBUG(Service_AM, "native session 1121 echo value={:#x}, result={:#x}", value, result.raw);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(result.raw);
        rb.Push(value);
    }
};

// Session returned by command 1202. The SDK reads a maximum count with command 1, then passes a
// count no larger than that to command 11, which returns one event handle.
class IEventSupervisor final : public ServiceFramework<IEventSupervisor> {
public:
    explicit IEventSupervisor(Core::System& system)
        : ServiceFramework{system, "IEventSupervisor"}, m_context{system, "EventSupervisor"} {
        static const FunctionInfo functions[] = {
            {1, &IEventSupervisor::GetMaximumEventCount, "GetMaximumEventCount"},
            {11, &IEventSupervisor::CreateEvent, "CreateEvent"},
            {103, &IEventSupervisor::BindEvent, "BindEvent"},
            {121, &IEventSupervisor::CreateMetadataPool, "CreateMetadataPool"},
        };
        RegisterHandlersTipc(functions);
    }

    ~IEventSupervisor() override {
        for (auto* event : m_events) {
            m_context.CloseEvent(event);
        }
    }

    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override {
        if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
            return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
        }
        const auto guard = LockService();
        LOG_DEBUG(Service_AM, "event supervisor unsupported command={}", ctx.GetCommand());
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
    }

    void CreateEvent(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        Kernel::KReadableEvent* readable{};
        const auto count = GraphicsSupervisorProtocol::DecodeEventSupervisorCreate(
            std::span<const u32>{packet, IPC::COMMAND_BUFFER_LENGTH});
        if (count) {
            if (*count > MaximumEventCount) {
                result = Kernel::ResultOutOfRange;
            } else if (auto* event = m_context.CreateEvent("EventSupervisorEvent")) {
                m_events.push_back(event);
                readable = &event->GetReadableEvent();
                readable->Open(kernel);
                result = ResultSuccess;
            } else {
                result = Kernel::ResultOutOfResource;
            }
        }
        LOG_DEBUG(Service_AM, "event supervisor create event count={}, result={:#x}", count.value_or(0),
                  result.raw);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
        ctx.AddMoveObject(readable);
    }

    // nn::chat::gsv::CreateMetadataPool (MKW sdk+0x6c2500, raw 0x89 with 5 words): u64 client pool
    // pointer, u64 size and the client-allocated pool index; the reply is the result only. The real
    // server handler was not located in the gsv modules, so only the client-proven wire format is
    // implemented: the pool is recorded by index and nothing else is validated.
    void CreateMetadataPool(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x89 && packet[1] == 5) {
            const u64 pool = (static_cast<u64>(packet[3]) << 32) | packet[2];
            const u64 size = (static_cast<u64>(packet[5]) << 32) | packet[4];
            m_metadata_pools[packet[6]] = {pool, size};
            result = ResultSuccess;
            LOG_DEBUG(Service_AM, "event supervisor metadata pool index={}, pool={:#x}, size={:#x}",
                      packet[6], pool, size);
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }

    void BindEvent(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        const auto bind = GraphicsSupervisorProtocol::DecodeEventSupervisorBind(
            std::span<const u32>{packet, IPC::COMMAND_BUFFER_LENGTH});
        if (bind) {
            result = bind->slot < MaximumEventCount ? ResultSuccess : Kernel::ResultOutOfRange;
        }
        LOG_DEBUG(Service_AM, "event supervisor bind name={:#x}, slot={}, result={:#x}",
                  bind ? bind->name : 0, bind ? bind->slot : 0, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }

    KernelHelpers::ServiceContext m_context;
    std::vector<Kernel::KEvent*> m_events;
    std::map<u32, std::pair<u64, u64>> m_metadata_pools;
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
        std::array<float, 3> output_levels{};
        bool output_levels_set{};
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
            {31, &IVi2Session::CreateRequestGroupEvent, "CreateRequestGroupEvent"},
            {32, &IVi2Session::DuplicateRequestGroupEvent, "DuplicateRequestGroupEvent"},
            {104, &IVi2Session::GetHdrStatus, "GetHdrStatus"},
            {105, &IVi2Session::GetDisplayState105, "GetDisplayState105"},
            {106, &IVi2Session::GetBufferMaxResolution, "GetBufferMaxResolution"},
            {107, &IVi2Session::GetDisplayState107, "GetDisplayState107"},
            // 261..272: register (odd) / unregister (even) the request group in "value" as a listener
            // of event kind (command - 261) / 2 on a display.
            {261, &IVi2Session::RegisterRequestGroupMonitor, "RegisterDisplayEventMonitor"},
            {262, &IVi2Session::UnregisterRequestGroupMonitor, "UnregisterDisplayEventMonitor"},
            {263, &IVi2Session::RegisterRequestGroupMonitor, "RegisterDisplayMonitor263"},
            {264, &IVi2Session::UnregisterRequestGroupMonitor, "UnregisterDisplayMonitor264"},
            {265, &IVi2Session::RegisterRequestGroupMonitor, "RegisterHdrStatusMonitor"},
            {266, &IVi2Session::UnregisterRequestGroupMonitor, "UnregisterHdrStatusMonitor"},
            {267, &IVi2Session::RegisterRequestGroupMonitor, "RegisterDisplayMonitor267"},
            {268, &IVi2Session::UnregisterRequestGroupMonitor, "UnregisterDisplayMonitor268"},
            {269, &IVi2Session::RegisterRequestGroupMonitor, "RegisterBufferMaxResolutionMonitor"},
            {270, &IVi2Session::UnregisterRequestGroupMonitor, "UnregisterBufferMaxResolutionMonitor"},
            {271, &IVi2Session::RegisterRequestGroupMonitor, "RegisterDisplayMonitor271"},
            {272, &IVi2Session::UnregisterRequestGroupMonitor, "UnregisterDisplayMonitor272"},
            {301, &IVi2Session::SetDisplayParameter301, "SetDisplayParameter301"},
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
            {352, &IVi2Session::SetClientOutputLevels, "SetClientOutputLevels"},
            {354, &IVi2Session::GetDisplayState354, "GetDisplayState354"},
            {251, &IVi2Session::BindClientDisplay, "BindClientDisplay"},
            {252, &IVi2Session::UnbindClientDisplay, "UnbindClientDisplay"},
        };
        RegisterHandlersTipc(functions);
    }

    ~IVi2Session() override {
        if (m_release_poll) system.CoreTiming().UnscheduleEvent(m_release_poll);
        if (m_group_tick) system.CoreTiming().UnscheduleEvent(m_group_tick);
        if (m_display_service) {
            for (auto& state : m_client_states) {
                if (state.layer) m_display_service->GetContainer()->DestroyManagedLayer(*state.layer);
            }
        }
        if (m_default_display) { m_display_service->CloseDisplay(*m_default_display); }
        m_context.CloseEvent(m_event);
        for (auto* event : m_request_group_events) m_context.CloseEvent(event);
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
    void SetDisplayParameter301(HLERequestContext& ctx) {
        const Result result = GraphicsSupervisorProtocol::IsSupportedDisplayParameter301(
                                  std::span<const u32>{ctx.CommandBuffer(),
                                                       IPC::COMMAND_BUFFER_LENGTH})
                                  ? ResultSuccess
                                  : Kernel::ResultInvalidCombination;
        LOG_DEBUG(Service_AM, "VI2 display parameter 301 result={:#x}", result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }

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
        const std::scoped_lock images_lock{m_images->mutex};
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
                if (state.producer->DetachReleasedBuffer(*state.queued[slot]) != android::Status::NoError) continue;
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
        if (m_images->module) m_images->DrainRetiredMemories();
    }
    Result QueueNativeImage(ClientState& state, const u32* packet) {
        const u32 slot = GraphicsSupervisorProtocol::DecodeBufferSlot(packet[4]);
        R_UNLESS(!state.player_display, Kernel::ResultNotImplemented);
        R_UNLESS(state.layer && state.display && slot < 15 && !state.queued[slot], Kernel::ResultInvalidState);
        const std::scoped_lock images_lock{m_images->mutex};
        const auto image = std::ranges::find(m_images->images, packet[5], &NativeImage::id);
        R_UNLESS(image != m_images->images.end() && image->window_pool && m_images->module, Kernel::ResultNotFound);
        LOG_DEBUG(Service_AM,
                  "Native present layout offset={}, dimensions={:#x}/{:#x}, layout={}, block={}, format={}, encoding={:#x}",
                  packet[6], packet[7], image->preference[2], packet[8], image->preference[4],
                  image->preference[6], state.output_encoding);
        // The observed presentation is a four-byte color image with the
        // backend's 16-GOB block height and no byte offset within its pool.
        const u32 present_width = (packet[7] & 0xffffU) + 1;
        const u32 present_height = (packet[7] >> 16) + 1;
        const u32 image_width = (image->preference[2] & 0xffffU) + 1;
        const u32 image_height = (image->preference[2] >> 16) + 1;
        R_UNLESS(packet[6] == 0 && present_width <= image_width &&
                 present_height <= image_height && packet[8] == 3 &&
                 image->preference[4] == 7 &&
                 (image->preference[6] == 1 || image->preference[6] == 4) &&
                 state.output_encoding == 0x14,
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
        native->format = image->preference[6] == 4 ? android::PixelFormat::Rgba1010102
                                                     : android::PixelFormat::Rgba8888;
        native->external_format = native->format;
        native->buffer_id = image->memory_id;
        const auto backing = std::ranges::find(m_images->backings, image->memory_id,
                                               &NativeImageRegistry::Backing::memory_id);
        if (backing != m_images->backings.end()) {
            R_UNLESS(backing->offset <= std::numeric_limits<u32>::max(), Kernel::ResultInvalidSize);
            // Preserve the GPU mapping used by rendering instead of pinning a second alias.
            native->buffer_id = backing->parent_id;
            native->offset = static_cast<u32>(backing->offset);
        }
        auto buffer = std::make_shared<android::GraphicBuffer>(m_images->module->GetContainer().GetNvMapFile(), native, m_images);
        s32 queue_slot{};
        R_UNLESS(state.producer->AttachBuffer(&queue_slot, buffer) == android::Status::NoError, Kernel::ResultOutOfResource);
        android::QueueBufferInput input{};
        android::QueueBufferOutput output{};
        input.crop = {0, 0, static_cast<s32>(present_width), static_cast<s32>(present_height)};
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
    // vi2 0x63 handler 0xf4170 (raw 0x170, 5 words): client token, then three floats. The first two must be
    // within [0, 1] and the third not negative (otherwise VI result 4); the handler stores them with a
    // set flag in the client's state. MKW sends (0.0, 1.0, 0.8) after the output encoding.
    void SetClientOutputLevels(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 token = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        std::array<float, 3> levels{};
        if (packet[0] == 0x170 && packet[1] == 5) {
            std::memcpy(levels.data(), packet + 4, sizeof(levels));
            const auto state = std::ranges::find(m_client_states, token, &ClientState::token);
            result = Kernel::ResultNotFound;
            if (token && state != m_client_states.end()) {
                result = Kernel::ResultInvalidCombination;
                if (levels[0] >= 0.0f && levels[0] <= 1.0f && levels[1] >= 0.0f && levels[1] <= 1.0f &&
                    levels[2] >= 0.0f) {
                    state->output_levels = levels;
                    state->output_levels_set = true;
                    result = ResultSuccess;
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 output levels token={:#x}, levels={},{},{}, result={:#x}", token,
                  levels[0], levels[1], levels[2], result.raw);
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
    void GetHdrStatus(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        u64 format{};
        if (packet[0] == 0x78 && packet[1] == 2) {
            result = Result{ErrorModule::VI, 7};
            if (name == 0x00746c7561666564ULL && m_default_display && m_display_service) {
                // The display backend exposes SDR output only.
                format = 1;
                result = ResultSuccess;
            }
        }
        LOG_DEBUG(Service_AM, "VI2 HDR status name={:#x}, format={:#x}, result={:#x}", name,
                  format, result.raw);
        IPC::ResponseBuilder rb{ctx, 5};
        rb.Push(result.raw);
        rb.Push(format);
        rb.Push<u32>(0);
    }

    // Commands 105 (u16 reply) and 354 (u64 reply) share one request: the display name, looked up in the
    // vi2 server's display table (name of all zero bytes -> VI 4, unknown name -> VI 7). The reply is a
    // field of the display's driver record, which reads as 0 (105) and 1 (354) while no record exists.
    void ReplyDisplayState(HLERequestContext& ctx, u32 header, u32 reply_words, u64 value) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == header && packet[1] == 2) {
            result = name == 0 ? Result{ErrorModule::VI, 4}
                               : IsKnownDisplayName(name) ? ResultSuccess : Result{ErrorModule::VI, 7};
        }
        LOG_DEBUG(Service_AM, "VI2 display state command={}, name={:#x}, value={:#x}, result={:#x}",
                  ctx.GetCommand(), name, value, result.raw);
        IPC::ResponseBuilder rb{ctx, reply_words};
        rb.Push(result.raw);
        if (reply_words == 3) {
            rb.Push<u32>(static_cast<u32>(value));
        } else {
            rb.Push(value);
        }
    }

    void GetDisplayState105(HLERequestContext& ctx) {
        ReplyDisplayState(ctx, 0x79, 3, 0);
    }

    // Command 107 (DragXDrive): request header 0x7b with the display name, reply header 0x7b with seven
    // words (result and three u64). vi2.autogen 0xef0a0 reads the three values from the display driver
    // record (+0x48/+0x50/+0x58); with no record they read as 1, 1 and 0.
    void GetDisplayState107(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x7b && packet[1] == 2) {
            result = name == 0 ? Result{ErrorModule::VI, 4}
                               : IsKnownDisplayName(name) ? ResultSuccess : Result{ErrorModule::VI, 7};
        }
        const bool ok = result.IsSuccess();
        LOG_DEBUG(Service_AM, "VI2 display state 107 name={:#x}, result={:#x}", name, result.raw);
        IPC::ResponseBuilder rb{ctx, 8};
        rb.Push(result.raw);
        rb.Push<u64>(ok ? 1 : 0);
        rb.Push<u64>(ok ? 1 : 0);
        rb.Push<u64>(0);
    }

    void GetDisplayState354(HLERequestContext& ctx) {
        ReplyDisplayState(ctx, 0x172, 4, 1);
    }

    void GetBufferMaxResolution(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        Result result = Kernel::ResultInvalidCombination;
        u64 resolution{};
        if (packet[0] == 0x7a && packet[1] == 2) {
            result = Result{ErrorModule::VI, 7};
            if (name == 0x00746c7561666564ULL && m_default_display && m_display_service) {
                s64 width{}, height{};
                result = m_display_service->GetDisplayResolution(&width, &height, *m_default_display);
                if (result.IsSuccess()) {
                    if (width == 1280 && height == 720) {
                        resolution = 1;
                    } else {
                        result = Kernel::ResultNotImplemented;
                    }
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 maximum buffer resolution name={:#x}, mode={:#x}, result={:#x}", name, resolution, result.raw);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(result.raw);
        rb.Push(resolution);
    }

    // Display names of the real vi2 server's 40-entry display table. Registration looks the name up
    // there; the display does not have to be open.
    static bool IsKnownDisplayName(u64 name) {
        char text[9]{};
        std::memcpy(text, &name, sizeof(name));
        static constexpr std::array<std::string_view, 39> known{
            "default", "cpyrght", "full", "overlay", "overlay0", "overlay1", "overlay2", "test",
            "adraw:s0", "adraw:s1", "adraw:s2", "adraw:c0", "adraw:c1", "adraw:i0", "player1",
            "player2", "player3", "chat0", "chat1", "chat2", "chat3", "chat4", "chat5", "chat6",
            "chat7", "chat8", "chat9", "chat10", "chat11", "chat12", "chat13", "chat14", "chat15",
            "chvcam", "null", "#ld", "#ld0", "#ld1", "#ovl0"};
        const std::string_view view{text};
        return std::ranges::find(known, view) != known.end() || view == "#ld01" || view == "#ldv" ||
               view == "#ovl1";
    }

    // vi2.autogen IUserQuickSession 261..272 (dispatcher 0xeaed0, impls 0xf1fe0/0xf25a0/0xf2950/
    // 0xf2cf0/0xf3090/0xf3430, common registrar 0xfa2c0). Request {u64 display name, u32 group}.
    // Kind 0 (261) links the group's listener into the display native-event list and is only
    // signalled by that event source; kinds 1..5 (263, 265, 267, 269, 271) also signal the group
    // event once at registration.
    void RegisterRequestGroupMonitor(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        const u32 group = packet[4];
        const u32 kind = (ctx.GetCommand() - 261) / 2;
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == ctx.GetCommand() + 16 && packet[1] == 3) {
            if (group >= m_request_group_events.size() || !name) {
                result = Result{ErrorModule::VI, 4};
            } else if (!IsKnownDisplayName(name) || !m_request_group_events[group]) {
                result = Result{ErrorModule::VI, 7};
            } else if (m_request_group_displays[group]) {
                result = Result{ErrorModule::VI, 5};
            } else {
                m_request_group_displays[group] = DisplayMonitor{name, kind};
                if (kind != 0) {
                    m_request_group_events[group]->Signal(kernel);
                } else {
                    StartGroupTick();
                }
                result = ResultSuccess;
            }
        }
        LOG_DEBUG(Service_AM, "VI2 display monitor command={}, name={:#x}, group={}, result={:#x}", ctx.GetCommand(), name, group, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    // The unregister handlers never fail: the real impls return nothing and the reply result is
    // always 0. A registration that matches (display, kind, group) is removed and the group event
    // is signalled; anything else is ignored.
    void UnregisterRequestGroupMonitor(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        const u32 group = packet[4];
        const u32 kind = (ctx.GetCommand() - 262) / 2;
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == ctx.GetCommand() + 16 && packet[1] == 3) {
            result = ResultSuccess;
            if (group < m_request_group_events.size() && name && m_request_group_displays[group] &&
                m_request_group_displays[group]->display == name &&
                m_request_group_displays[group]->kind == kind) {
                m_request_group_displays[group].reset();
                m_request_group_events[group]->Signal(kernel);
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    // Kind 0 is a display native event (the vi2 user native-event pool, raised by the display
    // worker). It is the vsync event (nn::vi::detail::DefaultDisplay::DuplicateVsyncEvent creates it through
    // nn::vi2::CreateVsyncEvent); it is delivered
    // once per 60 Hz refresh while a group is registered for it.
    void StartGroupTick() {
        if (m_group_tick) return;
        m_group_tick = Core::Timing::CreateEvent("NativeViGroupTick", [this](s64, std::chrono::nanoseconds) -> std::optional<std::chrono::nanoseconds> {
            const auto guard = LockService();
            for (size_t group = 0; group < m_request_group_displays.size(); ++group) {
                if (m_request_group_displays[group] && m_request_group_displays[group]->kind == 0 &&
                    m_request_group_events[group]) {
                    m_request_group_events[group]->Signal(kernel);
                }
            }
            return std::nullopt;
        });
        system.CoreTiming().ScheduleLoopingEvent(std::chrono::microseconds{16667},
                                                 std::chrono::microseconds{16667}, m_group_tick);
    }

    void DuplicateRequestGroupEvent(HLERequestContext& ctx) {
        using namespace GraphicsSupervisorProtocol;

        const auto request = DecodeRequestGroupEventDuplicate(
            std::span<const u32>{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH});
        Result result = Kernel::ResultInvalidCombination;
        Kernel::KReadableEvent* readable{};
        if (request) {
            auto copied =
                ctx.GetObjectFromHandle<Kernel::KReadableEvent>(ctx.GetCopyHandle(0));
            if (copied.IsNull()) {
                result = Kernel::ResultInvalidHandle;
            } else {
                Kernel::KEvent* event = request->group < m_request_group_events.size()
                                            ? m_request_group_events[request->group]
                                            : nullptr;
                const auto status = ClassifyRequestGroupEventDuplicate(
                    request->group, true, event != nullptr,
                    event && copied.GetPointerUnsafe() == &event->GetReadableEvent());
                switch (status) {
                case RequestGroupDuplicateStatus::OutOfRange:
                    result = Result{ErrorModule::VI, 4};
                    break;
                case RequestGroupDuplicateStatus::NotConfigured:
                case RequestGroupDuplicateStatus::MissingEvent:
                    result = Result{ErrorModule::VI, 7};
                    break;
                case RequestGroupDuplicateStatus::IdentityMismatch:
                    result = Kernel::ResultInvalidHandle;
                    break;
                case RequestGroupDuplicateStatus::Ready:
                    readable = &event->GetReadableEvent();
                    readable->Open(kernel);
                    result = ResultSuccess;
                    break;
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 duplicate request group event index={}, result={:#x}",
                  request ? request->group : std::numeric_limits<u32>::max(), result.raw);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
        ctx.AddMoveObject(readable);
    }

    void CreateRequestGroupEvent(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        Kernel::KReadableEvent* readable{};
        if (packet[0] == 0x2f && packet[1] == 1) {
            const u32 group = packet[2];
            if (group >= m_request_group_events.size()) {
                result = Result{ErrorModule::VI, 4};
            } else if (m_request_group_events[group]) {
                // Only the driver being up is required; ConfigureRequestGroups is not.
                result = Result{ErrorModule::VI, 5};
            } else {
                auto*& event = m_request_group_events[group];
                event = m_context.CreateEvent("Vi2RequestCompletion");
                result = event ? ResultSuccess : Kernel::ResultOutOfResource;
                if (event) {
                    readable = &event->GetReadableEvent();
                    readable->Open(kernel);
                }
            }
        }
        LOG_DEBUG(Service_AM, "VI2 request group event index={}, result={:#x}", packet[2], result.raw);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
        ctx.AddMoveObject(readable);
    }

    void ConfigureRequestGroups(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x43 && packet[1] == 1) {
            if (packet[2] >= 8) {
                result = Result{ErrorModule::VI, 4};
            } else if (m_request_groups_initialized) {
                result = Result{ErrorModule::VI, 5};
            } else {
                m_request_group_limit = packet[2];
                m_request_groups_initialized = true;
                result = ResultSuccess;
            }
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
    std::shared_ptr<Core::Timing::EventType> m_group_tick;
    std::array<ClientState, 16> m_client_states{};
    u32 m_client_slots{};
    std::shared_ptr<VI::IApplicationDisplayService> m_display_service;
    std::optional<u64> m_default_display;
    u32 m_display_references{};
    u32 m_display_notification{};
    std::array<u32, 3> m_player_display_references{};
    std::array<u32, 3> m_player_display_notifications{};
    std::array<Kernel::KEvent*, 8> m_request_group_events{};
    struct DisplayMonitor {
        u64 display;
        u32 kind;
    };
    std::array<std::optional<DisplayMonitor>, 8> m_request_group_displays{};
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
      m_syncpoint_cache_event{m_context}, m_native_event_1121{m_context}, m_process{process}, m_aruid{aruid} {
    m_process->Open(kernel);
    m_image_registry = std::make_shared<NativeImageRegistry>(kernel, m_process);
    static const FunctionInfo functions[] = {
        {4, &IGraphicsSupervisor::Initialize, "Initialize"},
        {11, &IGraphicsSupervisor::BindProcess, "BindProcess"},
        {101, &IGraphicsSupervisor::CompleteBootstrap, "CompleteBootstrap"},
        {126, &IGraphicsSupervisor::QueryMemoryMode, "QueryMemoryMode"},
        {115, &IGraphicsSupervisor::ConfigureNativeMemory, "ConfigureNativeMemory"},
        {111, &IGraphicsSupervisor::CreateNativeMemory, "CreateNativeMemory"},
        {112, &IGraphicsSupervisor::DestroyNativeMemory, "DestroyNativeMemory"},
        {114, &IGraphicsSupervisor::BindNativeMemory, "BindNativeMemory"},
        {201, &IGraphicsSupervisor::InitializeSyncpointContext, "InitializeSyncpointContext"},
        {202, &IGraphicsSupervisor::GetSyncpointSession, "GetSyncpointSession"},
        {204, &IGraphicsSupervisor::BindSyncpointMemory, "BindSyncpointMemory"},
        {301, &IGraphicsSupervisor::InitializeDisplayContext, "InitializeDisplayContext"},
        {1111, &IGraphicsSupervisor::SetGraphicsClass1111, "SetGraphicsClass1111"},
        {1121, &IGraphicsSupervisor::GetNativeEvent1121, "GetNativeEvent1121"},
        {1201, &IGraphicsSupervisor::SequenceCommand, "SequenceCommand1201"},
        {1202, &IGraphicsSupervisor::SequenceCommand, "SequenceCommand1202"},
        {1203, &IGraphicsSupervisor::SequenceCommand, "SequenceCommand1203"},
        {303, &IGraphicsSupervisor::FinalizeDisplayContext, "FinalizeDisplayContext"},
        {305, &IGraphicsSupervisor::OpenDisplaySession, "OpenDisplaySession"},
        {314, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "DestroyNativeWindowAliasImagePool"},
        {315, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "CreateNativeWindowAliasImagePool"},
        {401, &IGraphicsSupervisor::InitializeGpuContext, "InitializeGpuContext"},
        {402, &IGraphicsSupervisor::InitializeGpuContext, "ActivateGpuContext"},
        {423, &IGraphicsSupervisor::BindNativeGpuMemory, "BindNativeGpuMemory"},
        {429, &IGraphicsSupervisor::CreateNativeImage, "CreateNativeImage"},
        {428, &IGraphicsSupervisor::QueryVideoGpuImage, "QueryNativeGpuImage"},
        {426, &IGraphicsSupervisor::MapVideoGpuImage, "MapNativeGpuImage"},
        {427, &IGraphicsSupervisor::UnmapVideoGpuImage, "UnmapNativeGpuImage"},
        {430, &IGraphicsSupervisor::DestroyNativeImage, "DestroyNativeImage"},
        {434, &IGraphicsSupervisor::RegisterGpuAddressSpace, "RegisterGpuAddressSpace"},
        {497, &IGraphicsSupervisor::OpenGpuSession, "OpenGpuSession"},
        {912, &IGraphicsSupervisor::DestroyVideoImagePool, "DestroyVideoImagePool"},
        {911, &IGraphicsSupervisor::CreateVideoImagePool, "CreateVideoImagePool"},
        {991, &IGraphicsSupervisor::CalculateVideoImageRequirements, "CalculateVideoImageRequirements"},
        {961, &IGraphicsSupervisor::QueryVideoImage, "QueryVideoImage"},
        {1001, &IGraphicsSupervisor::InitializeVideoService, "InitializeVideoEncoder"},
        {1002, &IGraphicsSupervisor::OpenVideoEncoderContext, "OpenVideoEncoderContext"},
        {902, &IGraphicsSupervisor::OpenVideoDecoderContext, "OpenVideoDecoderContext902"},
        {906, &IGraphicsSupervisor::OpenVideoDecoderContext, "OpenVideoDecoderContext"},
        {901, &IGraphicsSupervisor::InitializeVideoService, "InitializeVideoDecoder"},
        {903, &IGraphicsSupervisor::FinalizeVideoService, "FinalizeVideoDecoder"},
        {1003, &IGraphicsSupervisor::FinalizeVideoService, "FinalizeVideoEncoder"},
        {1101, &IGraphicsSupervisor::InitializeFrameContext, "InitializeFrameContext"},
        {1102, &IGraphicsSupervisor::InitializeFrameContext, "ActivateFrameContext"},
        {1402, &IGraphicsSupervisor::GetNvidiaDriver, "GetNvidiaDriver"},
        {1501, &IGraphicsSupervisor::InitializeImageContext, "InitializeImageContext1501"},
        {435, &IGraphicsSupervisor::CalculateImageRequirements, "CalculateNativeImageSize"},
        {1510, &IGraphicsSupervisor::CalculateImageRequirements, "CalculateImageRequirements"},
        {1291, &IGraphicsSupervisor::CalculateRequiredBufferMemory, "CalculateRequiredBufferMemory"},
        {1211, &IGraphicsSupervisor::CreateChatImagePool, "CreateChatImagePool"},
        {601, &IGraphicsSupervisor::CreateCameraContext, "CreateCameraContext"},
        {602, &IGraphicsSupervisor::CreateCameraContext, "OpenCameraSession"},
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
        while (!m_native_memories.empty()) {
            const auto id = m_native_memories.back();
            if (ReleaseNativeMemory(id).IsSuccess()) continue;
            if (const auto info = nvmap.FreeHandle(id, false); info && info->can_unlock && info->address != 0) {
                m_process->GetPageTable().UnlockForDeviceAddressSpace(info->address, info->size);
            }
            m_native_memories.pop_back();
        }
        if (m_native_memory_session) {
            if (!m_image_registry->retired_memories.empty()) {
                m_image_registry->backing_session = Nvidia::NvCore::SessionId{*m_native_memory_session};
            } else {
                m_nv_module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*m_native_memory_session});
            }
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
        if (process.IsNull() || process.GetPointerUnsafe() != m_process) {
            result = Kernel::ResultInvalidHandle;
        } else if (m_phase != Phase::Created) {
            result = Kernel::ResultInvalidState;
        } else if (request->applet_resource_user_id != m_aruid) {
            result = Kernel::ResultInvalidArgument;
        } else if (!GraphicsSupervisorProtocol::IsLocalBootstrap(*request)) {
            result = Kernel::ResultNotImplemented;
        } else {
            m_phase = Phase::Initialized;
            m_native_gpu_profile =
                GraphicsSupervisorProtocol::NativeGpuProfileForBootstrap(request->flags);
            // Welcome Tour bootstraps with flags 5 but only embeds an sm_88 CUDA image; the driver's
            // link step (subsdk4+0x1d16684 returns 3) rejects it for the 0x807 (sm_87) profile.
            if (m_native_gpu_profile == 0x807 && m_process->GetProgramId() == 0x040057C02159A000ULL) {
                m_native_gpu_profile = 0x808;
            }
            result = ResultSuccess;
        }
        LOG_DEBUG(Service_AM, "graphics bootstrap aruid={:#x}, flags={:#x}, parameters=[{:#x}, {:#x}, {:#x}, {:#x}], result={:#x}",
                  request->applet_resource_user_id, request->flags, request->parameters[0],
                  request->parameters[1], request->parameters[2], request->parameters[3], result.raw);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenVideoDecoderContext(HLERequestContext& ctx) {
    if (ctx.GetCommand() == 902) {
        // Argument-less decoder open, the counterpart of OpenVideoEncoderContext (SDKs that do
        // not send the library version request of command 906).
        const auto* packet = ctx.CommandBuffer();
        Kernel::KClientSession* client{};
        Result result = Kernel::ResultInvalidCombination;
        if (packet[0] == 0x396 && packet[1] == 0) {
            result = Kernel::ResultInvalidState;
            if (m_video_decoder_initialized) {
                result = CreateGraphicsSession(system, ctx,
                    m_video_service->CreateContext(NativeVideoKind::Decoder), &client);
            }
        }
        LOG_DEBUG(Service_AM, "native video context open (902) result={:#x}", result.raw);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
        ctx.AddMoveObject(client);
        return;
    }
    const auto request = GraphicsSupervisorProtocol::DecodeVideoContext(
        std::span<const u32>{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH});
    Kernel::KClientSession* client{};
    Result result = Kernel::ResultInvalidCombination;
    if (request) {
        result = Kernel::ResultInvalidState;
        if (m_video_decoder_initialized) {
            result = Kernel::ResultNotImplemented;
            if (!request->remote_memory && request->library_version == 0x300030000ULL) {
                result = CreateGraphicsSession(system, ctx, m_video_service->CreateContext(NativeVideoKind::Decoder), &client);
            }
        }
    }
    LOG_DEBUG(Service_AM, "native video context open result={:#x}", result.raw);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

void IGraphicsSupervisor::DestroyVideoImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x3a0 && packet[1] == 1) {
        result = m_video_service ? m_video_service->DestroyImagePool(packet[2]) : Kernel::ResultInvalidState;
        if (m_image_registry->module) {
            const std::scoped_lock lock{m_image_registry->mutex};
            m_image_registry->DrainRetiredMemories();
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::MapVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    u64 address{}, size{};
    if (packet[0] == 0x1ba && packet[1] == 4 && packet[3] == 1 &&
        ((packet[5] & 0xff) == 1 || (packet[5] & 0xff) == 3)) {
        result = Kernel::ResultNotFound;
        if (m_video_service) {
            for (const auto& session : m_gpu_sessions) {
                const auto handler = session.lock();
                const auto as = handler ? handler->GetRegisteredAddressSpace(packet[4]) : nullptr;
                if (!as) continue;
                result = m_video_service->QueryGpuImage(packet[2], descriptor);
                if (result.IsSuccess()) result = m_video_service->MapGpuImage(as, packet[2], address, size);
                if (result.IsSuccess()) descriptor[5] &= 0xffffffff00000000ULL;
                break;
            }
        }
    }
    LOG_DEBUG(Service_AM, "native GPU image map id={}, as={}, address={:#x}, size={:#x}, result={:#x}",
              packet[2], packet[4], address, size, result.raw);
    IPC::ResponseBuilder rb{ctx, 39};
    rb.Push(result.raw);
    rb.Push(address);
    rb.Push<u64>(size ? address + size - 1 : 0);
    for (u64 word : descriptor) rb.Push(word);
    rb.Push<u32>(0);
}

void IGraphicsSupervisor::UnmapVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1bb && packet[1] == 6) {
        result = Kernel::ResultNotFound;
        if (m_video_service) {
            for (const auto& session : m_gpu_sessions) {
                const auto handler = session.lock();
                const auto as = handler ? handler->GetRegisteredAddressSpace(packet[3]) : nullptr;
                if (!as) continue;
                const u64 address = u64{packet[4]} | (u64{packet[5]} << 32);
                const u64 end = u64{packet[6]} | (u64{packet[7]} << 32);
                result = m_video_service->UnmapGpuImage(as, packet[2], address, end);
                break;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::QueryVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    if (packet[0] == 0x1bc && packet[1] == 1) {
        result = m_video_service ? m_video_service->QueryGpuImage(packet[2], descriptor) : Kernel::ResultInvalidState;
    }
    LOG_DEBUG(Service_AM, "native GPU image query id={}, result={:#x}", packet[2], result.raw);
    IPC::ResponseBuilder rb{ctx, 39};
    rb.Push(result.raw);
    rb.Push<u64>(0);
    rb.Push<u64>(0);
    for (u64 word : descriptor) rb.Push(word);
    rb.Push<u32>(0);
}

void IGraphicsSupervisor::QueryVideoImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    if (packet[0] == 0x3d1 && packet[1] == 1) {
        result = m_video_service && m_video_decoder_initialized
                     ? m_video_service->QueryVideoImage(packet[2], descriptor)
                     : Kernel::ResultInvalidState;
    }
    LOG_DEBUG(Service_AM, "native video image descriptor id={}, result={:#x}", packet[2], result.raw);
    IPC::ResponseBuilder rb{ctx, 35};
    rb.Push(result.raw);
    for (u64 word : descriptor) rb.Push(word);
}

void IGraphicsSupervisor::CreateVideoImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 first_image{};
    const u32 count = packet[18];
    if (packet[0] == 0x39f && packet[1] == 18) {
        result = Kernel::ResultInvalidState;
        if (m_video_service && m_video_decoder_initialized) {
            result = Kernel::ResultNotFound;
            if (std::ranges::find(m_native_memories, packet[19]) != m_native_memories.end()) {
                result = Kernel::ResultOutOfResource;
                if (m_next_native_image_id && count && u64{m_next_native_image_id} + count <= (u64{1} << 32)) {
                    result = m_video_service->CreateImagePool(std::span{packet + 2, 16}, count,
                                                             packet[19], m_next_native_image_id);
                    if (result.IsSuccess()) {
                        first_image = m_next_native_image_id;
                        m_next_native_image_id += count;
                    }
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "native video image pool create memory={}, count={}, first_image={}, result={:#x}",
              packet[19], count, first_image, result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(first_image);
}

void IGraphicsSupervisor::CalculateVideoImageRequirements(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::optional<NativeVideoImageLayout> layout;
    if (packet[0] == 0x3ef && packet[1] == 17) {
        result = Kernel::ResultInvalidState;
        if (m_video_decoder_initialized) {
            layout = CalculateNativeVideoImageLayout(std::span{packet + 2, 16}, packet[18]);
            result = layout ? ResultSuccess : Kernel::ResultNotImplemented;
        }
    }
    LOG_DEBUG(Service_AM, "native video image pool preference={:08x}, count={}, bytes={:#x}, alignment={:#x}, result={:#x}",
              fmt::join(std::span{packet + 2, 16}, ","), packet[18], layout ? layout->size : 0,
              layout ? layout->alignment : 0, result.raw);
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push<u64>(layout ? layout->size : 0);
    rb.Push<u64>(layout ? layout->alignment : 0);
}

void IGraphicsSupervisor::OpenVideoEncoderContext(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Kernel::KClientSession* client{};
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x3fa && packet[1] == 0) {
        result = Kernel::ResultInvalidState;
        if (m_video_encoder_initialized) {
            result = CreateGraphicsSession(system, ctx,
                m_video_service->CreateContext(NativeVideoKind::Encoder), &client);
        }
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

void IGraphicsSupervisor::InitializeVideoService(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if ((ctx.GetCommand() == 901 || ctx.GetCommand() == 1001) &&
        packet[0] == ctx.GetCommand() + 16 && packet[1] == 0) {
        result = m_phase == Phase::Ready && m_nv_module ? ResultSuccess : Kernel::ResultInvalidState;
        if (result.IsSuccess()) {
            if (!m_video_service) {
                if (!m_image_registry->module) {
                    m_image_registry->module = m_nv_module;
                    m_image_registry->memory_session = m_nv_module->GetContainer().OpenSession(m_process);
                }
                m_video_service = std::make_shared<NativeVideoService>(system, m_nv_module, m_process, m_image_registry);
            }
            if (ctx.GetCommand() == 901) m_video_decoder_initialized = true;
            else m_video_encoder_initialized = true;
        }
    }
    LOG_DEBUG(Service_AM, "native video initialize command={}, result={:#x}", ctx.GetCommand(), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::FinalizeVideoService(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if ((ctx.GetCommand() == 903 || ctx.GetCommand() == 1003) &&
        packet[0] == ctx.GetCommand() + 16 && packet[1] == 0) {
        auto& initialized = ctx.GetCommand() == 903 ? m_video_decoder_initialized : m_video_encoder_initialized;
        result = initialized ? ResultSuccess : Kernel::ResultInvalidState;
        if (result.IsSuccess()) {
            initialized = false;
            if (!m_video_decoder_initialized && !m_video_encoder_initialized) m_video_service.reset();
        }
    }
    LOG_DEBUG(Service_AM, "native video finalize command={}, result={:#x}", ctx.GetCommand(), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::QueryMemoryMode(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x8e && packet[1] == 0) {
        result = m_phase == Phase::Ready ? ResultSuccess : Kernel::ResultInvalidState;
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push<u32>(0); // The local backend has no remote-memory mode.
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

void IGraphicsSupervisor::FinalizeDisplayContext(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x13f && packet[1] == 0) {
        m_display_context_initialized = false;
        result = ResultSuccess;
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenDisplaySession(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Kernel::KClientSession* client{};
    const u64 flags = u64{packet[2]} | (u64{packet[3]} << 32);
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x141 && packet[1] == 2 && GraphicsSupervisorProtocol::IsSupportedDisplaySession(flags)) {
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

// Command 1121 (TIPC header 0x471, no data) returns an event handle (gsv1 0x8cc5c-0x8cf30 gate it on
// one capability bit of the session, then fetch a handle from a per-process object and reply with
// result plus one copy handle). Which native event it is, and what signals it, is not decoded; the
// event is handed out unsignalled.
void IGraphicsSupervisor::GetNativeEvent1121(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    if (packet[0] == 0x471 && packet[1] == 0) {
        result = CreateGraphicsSession(system, ctx, std::make_shared<INativeSession1121>(system), &client);
    }
    LOG_DEBUG(Service_AM, "graphics command 1121, result={:#x}", result.raw);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}
void IGraphicsSupervisor::SetGraphicsClass1111(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (GraphicsSupervisorProtocol::IsSupportedCommand1111(
            std::span<const u32>{packet, IPC::COMMAND_BUFFER_LENGTH})) {
        result = m_phase == Phase::Ready ? ResultSuccess : Kernel::ResultInvalidState;
    }
    LOG_DEBUG(Service_AM, "graphics command 1111 value={:#x}, result={:#x}", packet[2],
              result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::SequenceCommand(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const u32 command = ctx.GetCommand();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    if (GraphicsSupervisorProtocol::IsSupportedSequenceCommand(
            std::span<const u32>{packet, IPC::COMMAND_BUFFER_LENGTH}, command)) {
        if (m_phase != Phase::Ready) {
            result = Kernel::ResultInvalidState;
        } else if (command == 1201) {
            m_sequence_started = true;
            result = ResultSuccess;
        } else if (command == 1202 && m_sequence_started) {
            result = CreateGraphicsSession(system, ctx, std::make_shared<IEventSupervisor>(system),
                                           &client);
            m_sequence_session_returned = result.IsSuccess();
        } else if (command == 1203 && m_sequence_session_returned) {
            m_sequence_finished = true;
            result = ResultSuccess;
        } else {
            result = Kernel::ResultInvalidState;
        }
    }
    LOG_DEBUG(Service_AM, "graphics command {} result={:#x}", command, result.raw);
    if (client == nullptr) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess.raw);
    rb.PushMoveObjects(ctx, client);
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
        std::make_shared<Nvidia::NVDRV>(system, m_nv_module, "nvdrv:gsv", m_process, m_aruid,
                                        m_native_gpu_profile),
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
            // gsv1 0x4cf44 (435) and 0x909a0 (1510): one image's block storage times the count in
            // word 18 (a valid layout with count 0 is gsv result 0x100f3); the format word does not
            // scale it and the alignment is 4096.
            requirements = CalculateNativeImageRequirements(std::span{packet + 2, 16}, packet[18]);
            result = requirements ? ResultSuccess : Kernel::ResultNotImplemented;
            if (!packet[18] && CalculateNativeImageRequirements(std::span{packet + 2, 16}, 1)) {
                result = Result{static_cast<ErrorModule>(0xf3), 0x80};
            }
        }
    }
    LOG_DEBUG(Service_AM, "native image requirements dimensions={:#x}, block={}, pitch={}, format={}, layout={:#x}, count={}, size={:#x}, alignment={:#x}, result={:#x}",
              packet[4], packet[6], packet[7], packet[8], packet[9], packet[18], requirements ? requirements->size : 0,
              requirements ? requirements->alignment : 0, result.raw);
    IPC::ResponseBuilder rb{ctx, gpu_image ? 4U : 6U};
    rb.Push(result.raw);
    rb.Push<u64>(requirements ? requirements->size : 0);
    if (!gpu_image) { rb.Push<u64>(requirements ? requirements->alignment : 0); }
}
void IGraphicsSupervisor::CalculateRequiredBufferMemory(HLERequestContext& ctx) {
    // nn::chat::gsv::CalculateRequiredBufferMemory (MKW sdk+0x6c0d40), gsv1 handler 0x8e2cc -> 0x8e7c8.
    // Request: 64-byte image preference in words 2..17 and the image count in word 18. The real
    // validator (gsv1 0x90510) accepts an all-zero preference or one whose first u64 is 0x100000000
    // with word 5 zero. Only the second form is implemented here: the sizer (gsv1 0x82d0c) pads the
    // dimension word to ((low16 & 0xfff0) + 0x10) x (((word >> 16) & 0xff80) + 0x80) pixels of 4 bytes,
    // raises one image to at least 4096 bytes, spaces the images by 8192-byte multiples and returns
    // (count - 1) * stride + one image with a fixed alignment of 4096. A zero count, a pixel count over
    // 2^30 or a padded size over 32 bits is gsv result 0x100f3.
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u64 total = 0;
    u64 alignment = 0;
    if (packet[0] == 0x51b && packet[1] == 17) {
        result = Kernel::ResultInvalidState;
        if (m_phase == Phase::Ready && m_image_formats_initialized) {
            result = Kernel::ResultNotImplemented;
            if (packet[2] == 0 && packet[3] == 1 && packet[5] == 0) {
                const u64 width = (packet[4] & 0xfff0U) + 0x10U;
                const u64 height = ((packet[4] >> 16) & 0xff80U) + 0x80U;
                const u64 pixels = width * height;
                const u32 count = packet[18];
                result = Result{static_cast<ErrorModule>(0xf3), 0x80};
                if (!(pixels >> 32) && !(static_cast<u32>(pixels) >> 30) && count) {
                    const u32 bytes = static_cast<u32>(pixels) << 2;
                    const u32 image = std::max<u32>(bytes, 0x1000);
                    const u64 stride = (u64{image} + 0x1fff) & 0xffffe000ULL;
                    total = (count - 1) * stride + image;
                    alignment = 0x1000;
                    result = ResultSuccess;
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "required buffer memory preference={:#x},{:#x}, dimensions={:#x}, count={}, total={:#x}, alignment={:#x}, result={:#x}",
              packet[2], packet[3], packet[4], packet[18], total, alignment, result.raw);
    IPC::ResponseBuilder rb{ctx, 6U};
    rb.Push(result.raw);
    rb.Push<u64>(total);
    rb.Push<u64>(alignment);
}
void IGraphicsSupervisor::CreateChatImagePool(HLERequestContext& ctx) {
    // nn::chat::gsv::CreateImagePool (MKW sdk+0x6c0a10 -> 0x6c16f0), gsv1 handler 0x8e240 -> 0x8e688.
    // Request: 64-byte image preference in words 2..17, image count in word 18, native memory id in
    // word 19; reply: result and one u32 pool id. The real handler validates the preference (0x90510) and
    // sizes it with the CalculateRequiredBufferMemory sizer (0x82920, count 0 or an overflow is 0x100f3),
    // then looks the memory id up in the session's native memories (unknown id is 0x280f3) before it
    // builds the pool. The pool is built by the native video image pool service, whose RGBA layout
    // (kind 5) is the same sizing as this preference, and the reply is the id of the pool's first
    // image like command 911: the game queries that image with command 428 right afterwards.
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 pool_id{};
    if (packet[0] == 0x4cb && packet[1] == 18) {
        result = Kernel::ResultInvalidState;
        if (m_phase == Phase::Ready && m_image_formats_initialized) {
            result = Kernel::ResultNotImplemented;
            if (packet[2] == 0 && packet[3] == 1 && packet[5] == 0) {
                const u64 width = (packet[4] & 0xfff0U) + 0x10U;
                const u64 height = ((packet[4] >> 16) & 0xff80U) + 0x80U;
                const u64 pixels = width * height;
                result = Result{static_cast<ErrorModule>(0xf3), 0x80};
                if (!(pixels >> 32) && !(static_cast<u32>(pixels) >> 30) && packet[18]) {
                    result = Result{static_cast<ErrorModule>(0xf3), 0x140};
                    if (std::ranges::find(m_native_memories, packet[19]) != m_native_memories.end()) {
                        result = Kernel::ResultInvalidState;
                        if (m_nv_module && m_next_native_image_id &&
                            u64{m_next_native_image_id} + packet[18] <= (u64{1} << 32)) {
                            if (!m_video_service) {
                                if (!m_image_registry->module) {
                                    m_image_registry->module = m_nv_module;
                                    m_image_registry->memory_session =
                                        m_nv_module->GetContainer().OpenSession(m_process);
                                }
                                m_video_service = std::make_shared<NativeVideoService>(
                                    system, m_nv_module, m_process, m_image_registry);
                            }
                            std::array<u32, 16> video_preference{};
                            video_preference[1] = 5;
                            video_preference[2] = packet[4];
                            result = m_video_service->CreateImagePool(video_preference, packet[18], packet[19],
                                                                     m_next_native_image_id);
                            if (result.IsSuccess()) {
                                pool_id = m_next_native_image_id;
                                m_next_native_image_id += packet[18];
                            }
                        }
                    }
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "chat image pool preference={:#x},{:#x}, dimensions={:#x}, memory={}, pool={}, result={:#x}",
              packet[2], packet[3], packet[4], packet[19], pool_id, result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(pool_id);
}
void IGraphicsSupervisor::CreateCameraContext(HLERequestContext& ctx) {
    // nn::camera::sdk::CreateCameraContext (MKW sdk+0x2be980) sends 601 with no data and then 602 with
    // one u64 (device type 0 or 1) to get the camera session. gsv1 dispatcher 0x7a820: 601 (raw 0x269) only
    // checks the process permission bit and replies with the result; 602 (raw 0x26a, 2 words, value
    // above 1 is 0x280f3) returns a new session handle (reply descriptor 0x20).
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    if (ctx.GetCommand() == 601 && packet[0] == 0x269 && packet[1] == 0) {
        result = m_phase == Phase::Ready ? ResultSuccess : Kernel::ResultInvalidState;
        LOG_DEBUG(Service_AM, "camera context create result={:#x}", result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    if (ctx.GetCommand() == 602 && packet[0] == 0x26a && packet[1] == 2) {
        result = Result{static_cast<ErrorModule>(0xf3), 0x140};
        if (!packet[3] && packet[2] <= 1) {
            result = m_phase == Phase::Ready ? ResultSuccess : Kernel::ResultInvalidState;
            if (result.IsSuccess()) {
                result = CreateGraphicsSession(system, ctx, std::make_shared<ICameraContext>(system), &client);
            }
        }
    }
    LOG_DEBUG(Service_AM, "camera session open device={}, result={:#x}", packet[2], result.raw);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

void IGraphicsSupervisor::CreateNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const u64 size = u64{packet[3]} | (u64{packet[4]} << 32);
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    // gsv1 0x328d0/0x32ca8: flags (low byte) 0..3; size 0 is gsv result 0x100f3; no other size limit
    // in this layer.
    if (packet[0] == 0x7f && packet[1] == 3 && (packet[2] & 0xff) <= 3) {
        result = Kernel::ResultInvalidState;
        if (m_phase == Phase::Ready && m_image_formats_initialized && m_nv_module) {
            result = Result{static_cast<ErrorModule>(0xf3), 0x80};
            if (size && size <= (1ULL << 48)) {
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
    const std::scoped_lock images_lock{m_image_registry->mutex};
    const auto it = std::ranges::find(m_native_memories, id);
    R_UNLESS(m_nv_module && it != m_native_memories.end(), Kernel::ResultNotFound);
    auto& nvmap = m_nv_module->GetContainer().GetNvMapFile();
    auto handle = nvmap.GetHandle(id);
    R_UNLESS(handle, Kernel::ResultNotFound);
    {
        const std::scoped_lock lock{handle->mutex};
        R_UNLESS(handle->dupes == 1, Kernel::ResultBusy);
        if (handle->pins || handle->internal_dupes) {
            R_UNLESS(m_image_registry->module, Kernel::ResultBusy);
            // Keep the backing locked until display/image references have retired.
            m_image_registry->retired_memories.push_back(id);
            m_native_memories.erase(it);
            R_SUCCEED();
        }
    }
    const bool allocated = handle->allocated;
    handle.reset();
    const auto info = nvmap.FreeHandle(id, false);
    m_native_memories.erase(it);
    m_image_registry->ReleaseBacking(id);
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
    const std::scoped_lock images_lock{m_image_registry->mutex};
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
                    result = Kernel::ResultOutOfResource;
                    if (parent->Duplicate(true) == Nvidia::NvResult::Success) {
                        auto& nvmap = container.GetNvMapFile();
                        const auto pinned = nvmap.PinHandle(packet[7], false);
                        if (pinned != 0) {
                            result = BindNativeMemoryBacking(packet[2], address, size, 2, 0);
                            if (result.IsSuccess()) {
                                if (!m_image_registry->module) {
                                    m_image_registry->module = m_nv_module;
                                    m_image_registry->memory_session = container.OpenSession(m_process);
                                }
                                m_image_registry->backings.push_back({packet[2], packet[7], offset});
                            } else {
                                nvmap.UnpinHandle(packet[7]);
                            }
                        }
                        if (result.IsFailure()) {
                            nvmap.FreeHandle(packet[7], true);
                        }
                    }
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
                m_image_registry->DrainRetiredMemories();
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
