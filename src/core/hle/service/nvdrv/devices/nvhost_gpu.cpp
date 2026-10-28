// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <fstream>
#include <cstring>
#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_context.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/memory.h"
#include "video_core/control/channel_state.h"
#include "video_core/engines/puller.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/host1x/host1x.h"

namespace Service::Nvidia::Devices {
namespace {
Tegra::CommandHeader BuildFenceAction(Tegra::Engines::Puller::FenceOperation op, u32 syncpoint_id) {
    Tegra::Engines::Puller::FenceAction result{};
    result.op.Assign(op);
    result.syncpoint_id.Assign(syncpoint_id);
    return {result.raw};
}
} // namespace

nvhost_gpu::nvhost_gpu(Core::System& system_, EventInterface& events_interface_,
                       NvCore::Container& core_)
    : nvdevice{system_, Type::GpuChannel}, events_interface{events_interface_}, core{core_},
      syncpoint_manager{core_.GetSyncpointManager()}, nvmap{core.GetNvMapFile()},
      channel_state{system.GPU().AllocateChannel()} {
    channel_syncpoint = syncpoint_manager.AllocateSyncpoint(false);
    sm_exception_breakpoint_int_report_event =
        events_interface.CreateEvent("GpuChannelSMExceptionBreakpointInt");
    sm_exception_breakpoint_pause_report_event =
        events_interface.CreateEvent("GpuChannelSMExceptionBreakpointPause");
    error_notifier_event = events_interface.CreateEvent("GpuChannelErrorNotifier");
}

nvhost_gpu::~nvhost_gpu() {
    events_interface.FreeEvent(sm_exception_breakpoint_int_report_event);
    events_interface.FreeEvent(sm_exception_breakpoint_pause_report_event);
    events_interface.FreeEvent(error_notifier_event);
    syncpoint_manager.FreeSyncpoint(channel_syncpoint);
}

NvResult nvhost_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<u8> output) {
    switch (command.group) {
    case 0x0:
        switch (command.cmd) {
        case 0x3:
            return WrapFixed(this, &nvhost_gpu::GetWaitbase, input, output);
        default:
            break;
        }
        break;
    case 'H':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_gpu::SetNVMAPfd, input, output);
        case 0x3:
            return WrapFixed(this, &nvhost_gpu::ChannelSetTimeout, input, output);
        case 0x8:
            return WrapFixedVariable(this, &nvhost_gpu::SubmitGPFIFOBase1, input, output, fd, false);
        case 0x9:
            return WrapFixed(this, &nvhost_gpu::AllocateObjectContext, input, output);
        case 0xb:
            return WrapFixed(this, &nvhost_gpu::ZCullBind, input, output);
        case 0xc:
            return WrapFixed(this, &nvhost_gpu::SetErrorNotifier, input, output);
        case 0xd:
            return WrapFixed(this, &nvhost_gpu::SetChannelPriority, input, output);
        case 0x18:
            return WrapFixed(this, &nvhost_gpu::AllocGPFIFOEx, input, output, fd);
        case 0x1a:
            return WrapFixed(this, &nvhost_gpu::AllocGPFIFOEx2, input, output, fd);
        case 0x1b:
            return WrapFixedVariable(this, &nvhost_gpu::SubmitGPFIFOBase1, input, output, fd, true);
        case 0x1d:
            return WrapFixed(this, &nvhost_gpu::ChannelSetTimeslice, input, output);
        default:
            break;
        }
        break;
    case 'G':
        switch (command.cmd) {
        case 0x14:
            return WrapFixed(this, &nvhost_gpu::SetClientData, input, output);
        case 0x15:
            return WrapFixed(this, &nvhost_gpu::GetClientData, input, output);
        default:
            break;
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
};

NvResult nvhost_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<const u8> inline_input, std::span<u8> output) {
    switch (command.group) {
    case 'H':
        switch (command.cmd) {
        case 0x1b:
            return WrapFixedInlIn(this, &nvhost_gpu::SubmitGPFIFOBase2, input, inline_input,
                                  output);
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<u8> output, std::span<u8> inline_output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {
    sessions[fd] = session_id;
    channel_state->native_abi = UsesNativeAbi();
}

void nvhost_gpu::OnClose(DeviceFD fd) {
    sessions.erase(fd);
}

NvResult nvhost_gpu::SetNVMAPfd(IoctlSetNvmapFD& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={}", params.nvmap_fd);

    nvmap_fd = params.nvmap_fd;
    return NvResult::Success;
}

NvResult nvhost_gpu::SetClientData(IoctlClientData& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    user_data = params.data;
    return NvResult::Success;
}

NvResult nvhost_gpu::GetClientData(IoctlClientData& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.data = user_data;
    return NvResult::Success;
}

NvResult nvhost_gpu::ZCullBind(IoctlZCullBind& params) {
    zcull_params = params;
    LOG_DEBUG(Service_NVDRV, "called, gpu_va={:X}, mode={:X}", zcull_params.gpu_va,
              zcull_params.mode);
    return NvResult::Success;
}

NvResult nvhost_gpu::SetErrorNotifier(IoctlSetErrorNotifier& params) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, offset={:X}, size={:X}, mem={:X}", params.offset,
                params.size, params.mem);
    return NvResult::Success;
}

NvResult nvhost_gpu::SetChannelPriority(IoctlChannelSetPriority& params) {
    channel_priority = params.priority;
    LOG_INFO(Service_NVDRV, "called, priority={:X}", channel_priority);

    switch (static_cast<ChannelPriority>(channel_priority)) {
    case ChannelPriority::Low: channel_timeslice = 1300; break;
    case ChannelPriority::Medium: channel_timeslice = 2600; break;
    case ChannelPriority::High: channel_timeslice = 5200; break;
    default : return NvResult::BadParameter;
    }

    return NvResult::Success;
}

NvResult nvhost_gpu::AllocGPFIFOEx(IoctlAllocGpfifoEx& params, DeviceFD fd) {
    LOG_DEBUG(Service_NVDRV, "called, num_entries={:X}, flags={:X}, reserved1={:X}, "
              "reserved2={:X}, reserved3={:X}",
              params.num_entries, params.flags, params.reserved[0], params.reserved[1],
              params.reserved[2]);

    if (channel_state->initialized) {
        LOG_DEBUG(Service_NVDRV, "Channel already initialized; AllocGPFIFOEx returning AlreadyAllocated");
        return NvResult::AlreadyAllocated;
    }

    u64 program_id{};
    if (auto* const session = core.GetSession(sessions[fd]); session != nullptr) {
        program_id = session->process->GetProgramId();
    }

    // Store program id for later lazy initialization
    channel_state->program_id = program_id;

    // If address space is not yet bound, defer channel initialization.
    if (!channel_state->memory_manager) {
        params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);
        return NvResult::Success;
    }

    system.GPU().InitChannel(*channel_state, program_id);

    params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);

    return NvResult::Success;
}

NvResult nvhost_gpu::AllocGPFIFOEx2(IoctlAllocGpfifoEx& params, DeviceFD fd) {
    LOG_DEBUG(Service_NVDRV,
              "called, num_entries={:X}, flags={:X}, reserved1={:X}, "
              "reserved2={:X}, reserved3={:X}",
              params.num_entries, params.flags, params.reserved[0], params.reserved[1],
              params.reserved[2]);

    if (channel_state->initialized) {
        LOG_DEBUG(Service_NVDRV, "Channel already initialized; AllocGPFIFOEx2 returning AlreadyAllocated");
        return NvResult::AlreadyAllocated;
    }

    u64 program_id{};
    if (auto* const session = core.GetSession(sessions[fd]); session != nullptr) {
        program_id = session->process->GetProgramId();
    }

    // Store program id for later lazy initialization
    channel_state->program_id = program_id;

    // If address space is not yet bound, defer channel initialization.
    if (!channel_state->memory_manager) {
        params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);
        return NvResult::Success;
    }

    system.GPU().InitChannel(*channel_state, program_id);

    params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);

    return NvResult::Success;
}

s32_le nvhost_gpu::GetObjectContextClassNumberIndex(CtxClasses class_number) {
    constexpr s32_le invalid_class_number_index = -1;
    switch (class_number) {
    case CtxClasses::Ctx2D: return 0;
    case CtxClasses::Ctx3D: return 1;
    case CtxClasses::CtxCompute: return 2;
    case CtxClasses::CtxKepler: return 3;
    case CtxClasses::CtxDMA: return 4;
    case CtxClasses::CtxChannelGPFIFO: return 5;
    default: return invalid_class_number_index;
    }
}

NvResult nvhost_gpu::AllocateObjectContext(IoctlAllocObjCtx& params) {
    LOG_DEBUG(Service_NVDRV, "called, class_num={:#x}, flags={:#x}, obj_id={:#x}", params.class_num,
              params.flags, params.obj_id);

    // Do not require channel initialization here: some clients allocate contexts before binding.
    if (!channel_state) {
        LOG_ERROR(Service_NVDRV, "No channel state available!");
        return NvResult::InvalidState;
    }

    std::scoped_lock lk(channel_mutex);

    if (params.flags) {
        LOG_WARNING(Service_NVDRV, "non-zero flags={:#x} for class={:#x}", params.flags,
                    params.class_num);

        constexpr u32 allowed_mask{};
        params.flags = allowed_mask;
    }

    s32_le ctx_class_number_index =
        GetObjectContextClassNumberIndex(static_cast<CtxClasses>(params.class_num));
    if (const auto native_slot = GetNativeGpuContextSlot(params.class_num)) {
        if (UsesNativeAbi()) { ctx_class_number_index = *native_slot; }
    }
    if (ctx_class_number_index < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid class number for object context: {:#x}",
                  params.class_num);
        return NvResult::BadParameter;
    }

    if (ctxObjs[ctx_class_number_index].has_value()) {
        LOG_WARNING(Service_NVDRV, "Object context for class {:#x} already allocated on this channel",
                    params.class_num);
        return NvResult::AlreadyAllocated;
    }

    // Defer actual hardware context binding until channel is initialized.
    ctxObjs[ctx_class_number_index] = params;

    return NvResult::Success;
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildWaitCommandList(
    NvFence fence) {
    return {
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointPayload, 1,
                                  Tegra::SubmissionMode::Increasing),
        {fence.value},
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointOperation, 1,
                                  Tegra::SubmissionMode::Increasing),
        BuildFenceAction(Tegra::Engines::Puller::FenceOperation::Acquire, fence.id),
    };
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildIncrementCommandList(
    NvFence fence, u32 increment_count = 2) {
    boost::container::small_vector<Tegra::CommandHeader, 512> result{
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointPayload, 1,
                                  Tegra::SubmissionMode::Increasing),
        {}};

    for (u32 count = 0; count < increment_count; ++count) {
        result.push_back(Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointOperation, 1,
                                                   Tegra::SubmissionMode::Increasing));
        result.push_back(
            BuildFenceAction(Tegra::Engines::Puller::FenceOperation::Increment, fence.id));
    }

    return result;
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildIncrementWithWfiCommandList(
    NvFence fence, u32 increment_count = 2) {
    boost::container::small_vector<Tegra::CommandHeader, 512> result{
        Tegra::BuildCommandHeader(Tegra::BufferMethods::WaitForIdle, 1,
                                  Tegra::SubmissionMode::Increasing),
        {}};
    auto increment_list{BuildIncrementCommandList(fence, increment_count)};
    result.insert(result.end(), increment_list.begin(), increment_list.end());
    return result;
}

NvResult nvhost_gpu::SubmitGPFIFOImpl(IoctlSubmitGpfifo& params, Tegra::CommandList&& entries) {
    LOG_TRACE(Service_NVDRV, "called, gpfifo={:X}, num_entries={:X}, flags={:X}", params.address,
              params.num_entries, params.flags.raw);

    auto& gpu = system.GPU();

    std::scoped_lock lock(channel_mutex);

    // Lazily initialize channel when address space is available
    if (!channel_state->initialized && channel_state->memory_manager) {
        system.GPU().InitChannel(*channel_state, channel_state->program_id);
    }

    if (UsesNativeAbi() && native_submit_logs++ < 8) {
        LOG_DEBUG(Service_NVDRV, "native submit channel={}, initialized={}, entries={}, flags={:#x}, fence={}:{}",
                  channel_state->bind_id, channel_state->initialized, entries.command_lists.size(),
                  params.flags.raw, params.fence.id, params.fence.value);
        for (size_t i = 0; i < std::min<size_t>(entries.command_lists.size(), 4); ++i) {
            const auto& entry = entries.command_lists[i];
            std::array<u32, 16> words{};
            const size_t bytes = std::min<size_t>(entry.size * sizeof(u32), sizeof(words));
            const bool mapped = channel_state->memory_manager && bytes &&
                channel_state->memory_manager->IsFullyMappedRange(entry.addr, bytes);
            if (mapped) { channel_state->memory_manager->ReadBlockUnsafe(entry.addr, words.data(), bytes); }
            if (mapped && entry.size > 1000 && std::getenv("EDEN_NATIVE_FRAME_CAPTURE")) {
                std::vector<u32> contents(static_cast<size_t>(entry.size));
                channel_state->memory_manager->ReadBlockUnsafe(entry.addr, contents.data(), contents.size() * sizeof(u32));
                std::ofstream dump("native-init-channel-" + std::to_string(channel_state->bind_id) + "-" + std::to_string(i) + ".bin", std::ios::binary);
                dump.write(reinterpret_cast<const char*>(contents.data()), contents.size() * sizeof(u32));
            }
            LOG_DEBUG(Service_NVDRV, "native pushbuffer entry={} raw={:#x}, address={:#x}, words={}, mapped={}, data={:08x}",
                      i, entry.raw, static_cast<u64>(entry.addr), static_cast<u64>(entry.size),
                      mapped, fmt::join(words, ","));
        }
    }

    if (UsesNativeAbi() && std::getenv("EDEN_NATIVE_FRAME_CAPTURE") && native_submit_logs < 100) {
        for (size_t i = 0; i < entries.command_lists.size(); ++i) {
            const auto& entry = entries.command_lists[i];
            const auto bytes = static_cast<size_t>(entry.size) * sizeof(u32);
            if (!bytes || bytes > 0x100000 || !channel_state->memory_manager->IsFullyMappedRange(entry.addr, bytes)) continue;
            std::vector<u32> contents(static_cast<size_t>(entry.size));
            channel_state->memory_manager->ReadBlockUnsafe(entry.addr, contents.data(), bytes);
            std::ofstream dump("native-submit-" + std::to_string(channel_state->bind_id) + "-" + std::to_string(native_submit_logs) + "-" + std::to_string(i) + ".bin", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(contents.data()), bytes);
        }
    }

    const auto bind_id = channel_state->bind_id;

    auto& flags = params.flags;

    if (flags.fence_wait.Value()) {
        if (flags.increment_value.Value()) {
            return NvResult::BadParameter;
        }

        if (!syncpoint_manager.IsFenceSignalled(params.fence)) {
            gpu.PushGPUEntries(bind_id, Tegra::CommandList{BuildWaitCommandList(params.fence)});
        }
    }

    const u32 native_completion = UsesNativeAbi() && flags.increment_value.Value() && !flags.fence_increment.Value() ? params.fence.value : 0;
    if (native_completion > 64) return NvResult::BadParameter;
    params.fence.id = channel_syncpoint;

    u32 increment{(flags.fence_increment.Value() != 0 ? 2 : 0) +
                  (flags.increment_value.Value() != 0 ? params.fence.value : 0)};
    params.fence.value = syncpoint_manager.IncrementSyncpointMaxExt(channel_syncpoint, increment);
    gpu.PushGPUEntries(bind_id, std::move(entries));

    if (flags.fence_increment.Value()) {
        if (flags.suppress_wfi.Value()) {
            gpu.PushGPUEntries(bind_id,
                               Tegra::CommandList{BuildIncrementCommandList(params.fence)});
        } else {
            gpu.PushGPUEntries(bind_id,
                               Tegra::CommandList{BuildIncrementWithWfiCommandList(params.fence)});
        }
    }

    if (native_completion) {
        // The native C56F stream reports its completion count without emitting
        // host syncpoint methods. Order actual increments after that GPU work.
        gpu.PushGPUEntries(bind_id, Tegra::CommandList{flags.suppress_wfi.Value()
            ? BuildIncrementCommandList(params.fence, native_completion)
            : BuildIncrementWithWfiCommandList(params.fence, native_completion)});
        LOG_DEBUG(Service_NVDRV, "Native ordered completion: syncpoint={}, increments={}, target={}", channel_syncpoint, native_completion, params.fence.value);
    }
    flags.raw = 0;

    return NvResult::Success;
}

Core::Memory::Memory& nvhost_gpu::GetSessionMemory(DeviceFD fd) {
    if (const auto it = sessions.find(fd); it != sessions.end())
        if (auto* const session = core.GetSession(it->second);
            session != nullptr && session->process != nullptr)
            return session->process->GetMemory();

    LOG_ERROR(Service_NVDRV, "No session for fd={}, falling back to application memory", fd);
    return system.ApplicationMemory();
}

NvResult nvhost_gpu::SubmitGPFIFOBase1(IoctlSubmitGpfifo& params,
                                       std::span<Tegra::CommandListHeader> commands, DeviceFD fd,
                                       bool kickoff) {
    if (params.num_entries > commands.size()) {
        UNIMPLEMENTED();
        return NvResult::InvalidSize;
    }

    Tegra::CommandList entries(params.num_entries);
    if (kickoff) {
        this->GetSessionMemory(fd).ReadBlock(params.address, entries.command_lists.data(),
                                             params.num_entries * sizeof(Tegra::CommandListHeader));
    } else {
        std::memcpy(entries.command_lists.data(), commands.data(),
                    params.num_entries * sizeof(Tegra::CommandListHeader));
    }

    return SubmitGPFIFOImpl(params, std::move(entries));
}

NvResult nvhost_gpu::SubmitGPFIFOBase2(IoctlSubmitGpfifo& params,
                                       std::span<const Tegra::CommandListHeader> commands) {
    if (params.num_entries > commands.size()) {
        UNIMPLEMENTED();
        return NvResult::InvalidSize;
    }

    Tegra::CommandList entries(params.num_entries);
    std::memcpy(entries.command_lists.data(), commands.data(),
                params.num_entries * sizeof(Tegra::CommandListHeader));
    return SubmitGPFIFOImpl(params, std::move(entries));
}

NvResult nvhost_gpu::GetWaitbase(IoctlGetWaitbase& params) {
    LOG_INFO(Service_NVDRV, "called, unknown={:#x}", params.unknown);

    params.value = 0; // Seems to be hard coded at 0
    return NvResult::Success;
}

NvResult nvhost_gpu::ChannelSetTimeout(IoctlChannelSetTimeout& params) {
    LOG_INFO(Service_NVDRV, "called, timeout={:#x}", params.timeout);

    return NvResult::Success;
}

NvResult nvhost_gpu::ChannelSetTimeslice(IoctlSetTimeslice& params) {
    LOG_INFO(Service_NVDRV, "called, timeslice={:#x}", params.timeslice);

    if (params.timeslice < 1000 || params.timeslice > 5000) {
        return NvResult::BadParameter;
    }

    channel_timeslice = params.timeslice;

    return NvResult::Success;
}

Kernel::KEvent* nvhost_gpu::QueryEvent(u32 event_id) {
    switch (event_id) {
    case 1:
        return sm_exception_breakpoint_int_report_event;
    case 2:
        return sm_exception_breakpoint_pause_report_event;
    case 3:
        return error_notifier_event;
    case 7:
        // The native SDK requests this event immediately before configuring
        // CHANNEL_SET_ERROR_NOTIFIER, then wraps it in its channel error wait.
        return UsesNativeAbi() ? error_notifier_event : nullptr;
    default:
        LOG_CRITICAL(Service_NVDRV, "Unknown Ctrl GPU Event {}", event_id);
        return nullptr;
    }
}

NvResult nvhost_gpu::BindToTsg(DeviceFD group, Kernel::KProcess* process, std::shared_ptr<Tegra::NativeMacroContext> macros) {
    const std::scoped_lock lock{tsg_mutex};
    if (tsg_group) { return NvResult::Busy; }
    bool owned{};
    for (const auto& [fd, session_id] : sessions) {
        const auto* session = core.GetSession(session_id);
        owned |= session && session->process == process;
    }
    if (!owned) { return NvResult::AccessDenied; }
    if (channel_state->native_abi) {
        channel_state->native_macro_context = std::move(macros);
        if (channel_state->payload) {
            channel_state->payload->maxwell_3d.SetNativeMacroContext(channel_state->native_macro_context);
        }
    }
    tsg_group = group;
    return NvResult::Success;
}

void nvhost_gpu::UnbindFromTsg(DeviceFD group) {
    const std::scoped_lock lock{tsg_mutex};
    if (tsg_group == group) { tsg_group.reset(); }
}

bool nvhost_gpu::UsesNativeAbi() const {
    for (const auto& [fd, session_id] : sessions) {
        const auto* session = core.GetSession(session_id);
        if (session && session->process && session->process->UsesPAuthReturns()) { return true; }
    }
    return false;
}

} // namespace Service::Nvidia::Devices
