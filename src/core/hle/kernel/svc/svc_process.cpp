// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/input.h"
#include "core/core.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/sm/sm.h"
#include "core/loader/nro.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/resource_manager.h"

namespace Kernel::Svc {

namespace {

constexpr size_t HomebrewNextLoadPathSize = 0x200;
constexpr size_t HomebrewNextLoadArgvSize = 0x800;

std::string ReadHomebrewString(Core::Memory::Memory& memory, KProcessAddress address,
                               size_t max_size) {
    if (GetInteger(address) == 0) {
        return {};
    }
    return memory.ReadCString(Common::ProcessAddress{GetInteger(address)}, max_size);
}

} // namespace

/// Exits the current process
void ExitProcess(Core::System& system, std::span<uint64_t, 8> args) {
    auto* current_process = GetCurrentProcessPointer(system.Kernel());
    auto* current_thread = GetCurrentThreadPointer(system.Kernel());

    LOG_INFO(Kernel_SVC, "Process {} exiting", current_process->GetProcessId());
    const auto next_load_path_addr = current_process->GetHomebrewNextLoadPathAddr();
    const auto next_load_argv_addr = current_process->GetHomebrewNextLoadArgvAddr();
    if (GetInteger(next_load_path_addr) != 0) {
        auto& memory = current_process->GetMemory();
        const auto next_load_path =
            ReadHomebrewString(memory, next_load_path_addr, HomebrewNextLoadPathSize);
        const auto next_load_argv =
            ReadHomebrewString(memory, next_load_argv_addr, HomebrewNextLoadArgvSize);
        if (!next_load_path.empty()) {
            auto guest_path = Common::FS::SanitizePath(next_load_path);
            constexpr std::string_view SdmcPrefix{"sdmc:"};
            FileSys::VirtualFile file{};

            const bool is_sdmc_path = guest_path.rfind(SdmcPrefix, 0) == 0;
            const bool is_absolute_guest_path = !guest_path.empty() && guest_path.front() == '/';
            if (is_sdmc_path || is_absolute_guest_path) {
                auto relative_path =
                    is_sdmc_path ? guest_path.substr(SdmcPrefix.size()) : guest_path;
                while (!relative_path.empty() && relative_path.front() == '/') {
                    relative_path.erase(relative_path.begin());
                }

                const auto host_path = Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) /
                                       std::filesystem::path{Common::FS::ToU8String(relative_path)};
                const auto host_path_string = Common::FS::PathToUTF8String(host_path);
                file = Core::GetGameFileFromPath(system.GetFilesystem(), host_path_string);
                if (!file) {
                    LOG_WARNING(Kernel_SVC,
                                "NextLoad: failed to open guest_path='{}', host_path='{}'",
                                next_load_path, host_path_string);
                }
            } else {
                file = Core::GetGameFileFromPath(system.GetFilesystem(), guest_path);
                if (!file) {
                    LOG_WARNING(Kernel_SVC, "NextLoad: failed to open guest_path='{}'",
                                next_load_path);
                }
            }

            if (file) {
                const auto nvdrv =
                    system.ServiceManager().GetService<Service::Nvidia::NVDRV>("nvdrv:s");
                if (!nvdrv) {
                    LOG_WARNING(Kernel_SVC, "NextLoad: NVDRV service unavailable for reset");
                } else {
                    nvdrv->GetModule()->ResetForProcess(current_process);
                }

                auto& page_table = current_process->GetPageTable();
                const u64 heap_start = GetInteger(page_table.GetHeapRegionStart());
                const u64 heap_size = page_table.GetHeapRegionSize();
                const u64 heap_end = heap_start + heap_size;
                if (heap_start != 0 && heap_size != 0 && heap_end > heap_start) {
                    struct DeviceSharedBlock {
                        u64 address;
                        u64 size;
                        u16 device_use_count;
                    };

                    std::vector<DeviceSharedBlock> blocks;
                    for (u64 cursor = heap_start; cursor < heap_end;) {
                        KMemoryInfo info;
                        PageInfo page_info;
                        const auto query_result =
                            page_table.QueryInfo(std::addressof(info), std::addressof(page_info),
                                                 cursor);
                        if (query_result.IsError()) {
                            LOG_WARNING(Kernel_SVC,
                                        "NextLoad: DeviceShared heap cleanup query failed "
                                        "address=0x{:016X}, result={:#X}",
                                        cursor, query_result.raw);
                            break;
                        }

                        const u64 block_end = (std::min<u64>)(info.GetEndAddress(), heap_end);
                        if (block_end <= cursor) {
                            LOG_WARNING(Kernel_SVC,
                                        "NextLoad: DeviceShared heap cleanup walk stalled "
                                        "cursor=0x{:016X}, block=0x{:016X}/0x{:X}",
                                        cursor, info.GetAddress(), info.GetSize());
                            break;
                        }

                        const bool is_device_shared =
                            True(info.GetState() & KMemoryState::FlagCanDeviceMap) &&
                            info.GetAttribute() == KMemoryAttribute::DeviceShared &&
                            info.m_device_use_count > 0;
                        if (is_device_shared) {
                            blocks.push_back(DeviceSharedBlock{
                                .address = cursor,
                                .size = block_end - cursor,
                                .device_use_count = info.m_device_use_count,
                            });
                        }
                        cursor = block_end;
                    }

                    for (const auto& block : blocks) {
                        for (u16 unlock = 0; unlock < block.device_use_count; unlock++) {
                            const auto unlock_result =
                                page_table.UnlockForDeviceAddressSpace(block.address, block.size);
                            if (unlock_result.IsError()) {
                                LOG_WARNING(Kernel_SVC,
                                            "NextLoad: DeviceShared heap cleanup unlock failed "
                                            "address=0x{:016X}, size=0x{:X}, remaining={}, "
                                            "result={:#X}",
                                            block.address, block.size,
                                            block.device_use_count - unlock - 1, unlock_result.raw);
                                break;
                            }
                        }
                    }
                }

                if (Loader::LoadNroInPlace(system, *current_process, *current_thread, file,
                                           next_load_path, next_load_argv)) {
                    const auto aruid = current_process->GetProcessId();
                    if (const auto hid =
                            system.ServiceManager().GetService<Service::HID::IHidServer>("hid")) {
                        const auto resource_manager = hid->GetResourceManager();
                        resource_manager->UnregisterAppletResourceUserId(aruid);
                        const auto register_result =
                            resource_manager->RegisterAppletResourceUserId(aruid, true);
                        if (register_result.IsError()) {
                            LOG_WARNING(Kernel_SVC,
                                        "NextLoad: failed to register HID applet resource "
                                        "aruid={}, result={:#X}",
                                        aruid, register_result.raw);
                        }
                    } else {
                        LOG_WARNING(Kernel_SVC, "NextLoad: HID service unavailable for reset");
                    }

                    auto& hid_core = system.HIDCore();
                    hid_core.DisableAllControllerConfiguration();
                    hid_core.SetSupportedStyleTag({Core::HID::NpadStyleSet::All});
                    hid_core.ReloadInputDevices();

                    const auto activate_controller = [&](Core::HID::NpadIdType npad_id) {
                        auto* controller = hid_core.GetEmulatedController(npad_id);
                        if (controller == nullptr) {
                            return;
                        }

                        (void)controller->SetPollingMode(Core::HID::EmulatedDeviceIndex::AllDevices,
                                                         Common::Input::PollingMode::Active);
                    };

                    activate_controller(Core::HID::NpadIdType::Player1);
                    activate_controller(Core::HID::NpadIdType::Handheld);

                    system.Kernel().CurrentPhysicalCore().LoadContext(current_thread);

                    const auto& context = current_thread->GetContext();
                    for (size_t i = 0; i < args.size(); i++) {
                        args[i] = context.r[i];
                    }

                    return;
                }
            }
        }
    }
    ASSERT_MSG(current_process->GetState() == KProcess::State::Running,
               "Process has already exited");

    system.Exit();
}

/// Gets the ID of the specified process or a specified thread's owning process.
Result GetProcessId(Core::System& system, u64* out_process_id, Handle handle) {
    LOG_DEBUG(Kernel_SVC, "called handle={:#08x}", handle);

    // Get the object from the handle table.
    KScopedAutoObject obj = GetCurrentProcess(system.Kernel())
        .GetHandleTable()
        .GetObject<KAutoObject>(system.Kernel(), Handle(handle));
    R_UNLESS(obj.IsNotNull(), ResultInvalidHandle);

    // Get the process from the object.
    KProcess* process = nullptr;
    if (KProcess* p = obj->DynamicCast<KProcess*>(); p != nullptr) {
        // The object is a process, so we can use it directly.
        process = p;
    } else if (KThread* t = obj->DynamicCast<KThread*>(); t != nullptr) {
        // The object is a thread, so we want to use its parent.
        process = reinterpret_cast<KThread*>(obj.GetPointerUnsafe())->GetOwnerProcess();
    } else {
        // TODO(bunnei): This should also handle debug objects before returning.
        UNIMPLEMENTED_MSG("Debug objects not implemented");
    }

    // Make sure the target process exists.
    R_UNLESS(process != nullptr, ResultInvalidHandle);

    // Get the process id.
    *out_process_id = process->GetId();

    R_SUCCEED();
}

Result GetProcessList(Core::System& system, s32* out_num_processes, u64 out_process_ids,
                      int32_t out_process_ids_size) {
    LOG_DEBUG(Kernel_SVC, "called. out_process_ids={:#016x}, out_process_ids_size={}",
              out_process_ids, out_process_ids_size);

    // If the supplied size is negative or greater than INT32_MAX / sizeof(u64), bail.
    if ((out_process_ids_size & 0xF0000000) != 0) {
        LOG_ERROR(Kernel_SVC,
                  "Supplied size outside [0, 0x0FFFFFFF] range. out_process_ids_size={}",
                  out_process_ids_size);
        R_THROW(ResultOutOfRange);
    }

    auto& kernel = system.Kernel();
    const auto total_copy_size = out_process_ids_size * sizeof(u64);

    if (out_process_ids_size > 0 &&
        !GetCurrentProcess(kernel).GetPageTable().Contains(out_process_ids, total_copy_size)) {
        LOG_ERROR(Kernel_SVC, "Address range outside address space. begin={:#016x}, end={:#016x}",
                  out_process_ids, out_process_ids + total_copy_size);
        R_THROW(ResultInvalidCurrentMemory);
    }

    auto& memory = GetCurrentMemory(kernel);
    auto process_list = kernel.GetProcessList();
    auto it = process_list.begin();

    const auto num_processes = process_list.size();
    const auto copy_amount =
        (std::min)(static_cast<std::size_t>(out_process_ids_size), num_processes);

    for (std::size_t i = 0; i < copy_amount && it != process_list.end(); ++i, ++it) {
        memory.Write64(out_process_ids, (*it)->GetProcessId());
        out_process_ids += sizeof(u64);
    }

    *out_num_processes = static_cast<u32>(num_processes);
    R_SUCCEED();
}

Result GetProcessInfo(Core::System& system, s64* out, Handle process_handle,
                      ProcessInfoType info_type) {
    LOG_DEBUG(Kernel_SVC, "called, handle={:#08x}, type={:#x}", process_handle, info_type);

    const auto& handle_table = GetCurrentProcess(system.Kernel()).GetHandleTable();
    KScopedAutoObject process = handle_table.GetObject<KProcess>(system.Kernel(), process_handle);
    if (process.IsNull()) {
        LOG_ERROR(Kernel_SVC, "Process handle does not exist, process_handle={:#08x}",
                  process_handle);
        R_THROW(ResultInvalidHandle);
    }

    if (info_type != ProcessInfoType::ProcessState) {
        LOG_ERROR(Kernel_SVC, "Expected info_type to be ProcessState but got {} instead",
                  info_type);
        R_THROW(ResultInvalidEnumValue);
    }

    *out = static_cast<s64>(process->GetState());
    R_SUCCEED();
}

Result CreateProcess(Core::System& system, Handle* out_handle, uint64_t parameters, uint64_t caps,
                     int32_t num_caps) {
    UNIMPLEMENTED();
    R_THROW(ResultNotImplemented);
}

Result StartProcess(Core::System& system, Handle process_handle, int32_t priority, int32_t core_id,
                    uint64_t main_thread_stack_size) {
    UNIMPLEMENTED();
    R_THROW(ResultNotImplemented);
}

Result TerminateProcess(Core::System& system, Handle process_handle) {
    UNIMPLEMENTED();
    R_THROW(ResultNotImplemented);
}

void ExitProcess64(Core::System& system, std::span<uint64_t, 8> args) {
    ExitProcess(system, args);
}

Result GetProcessId64(Core::System& system, uint64_t* out_process_id, Handle process_handle) {
    R_RETURN(GetProcessId(system, out_process_id, process_handle));
}

Result GetProcessList64(Core::System& system, int32_t* out_num_processes, uint64_t out_process_ids,
                        int32_t max_out_count) {
    R_RETURN(GetProcessList(system, out_num_processes, out_process_ids, max_out_count));
}

Result CreateProcess64(Core::System& system, Handle* out_handle, uint64_t parameters, uint64_t caps,
                       int32_t num_caps) {
    R_RETURN(CreateProcess(system, out_handle, parameters, caps, num_caps));
}

Result StartProcess64(Core::System& system, Handle process_handle, int32_t priority,
                      int32_t core_id, uint64_t main_thread_stack_size) {
    R_RETURN(StartProcess(system, process_handle, priority, core_id, main_thread_stack_size));
}

Result TerminateProcess64(Core::System& system, Handle process_handle) {
    R_RETURN(TerminateProcess(system, process_handle));
}

Result GetProcessInfo64(Core::System& system, int64_t* out_info, Handle process_handle,
                        ProcessInfoType info_type) {
    R_RETURN(GetProcessInfo(system, out_info, process_handle, info_type));
}

void ExitProcess64From32(Core::System& system, std::span<uint64_t, 8> args) {
    ExitProcess(system, args);
}

Result GetProcessId64From32(Core::System& system, uint64_t* out_process_id, Handle process_handle) {
    R_RETURN(GetProcessId(system, out_process_id, process_handle));
}

Result GetProcessList64From32(Core::System& system, int32_t* out_num_processes,
                              uint32_t out_process_ids, int32_t max_out_count) {
    R_RETURN(GetProcessList(system, out_num_processes, out_process_ids, max_out_count));
}

Result CreateProcess64From32(Core::System& system, Handle* out_handle, uint32_t parameters,
                             uint32_t caps, int32_t num_caps) {
    R_RETURN(CreateProcess(system, out_handle, parameters, caps, num_caps));
}

Result StartProcess64From32(Core::System& system, Handle process_handle, int32_t priority,
                            int32_t core_id, uint64_t main_thread_stack_size) {
    R_RETURN(StartProcess(system, process_handle, priority, core_id, main_thread_stack_size));
}

Result TerminateProcess64From32(Core::System& system, Handle process_handle) {
    R_RETURN(TerminateProcess(system, process_handle));
}

Result GetProcessInfo64From32(Core::System& system, int64_t* out_info, Handle process_handle,
                              ProcessInfoType info_type) {
    R_RETURN(GetProcessInfo(system, out_info, process_handle, info_type));
}

} // namespace Kernel::Svc
