// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: 2022 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/heap_mapper.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"

using Core::Memory::YUZU_PAGESIZE;
constexpr size_t BIG_PAGE_SIZE = YUZU_PAGESIZE * 16;

namespace Service::Nvidia::NvCore {
NvMap::Handle::Handle(u64 size_, Id id_)
    : size(size_), aligned_size(size), orig_size(size), id(id_) {
    flags.raw = 0;
}

NvResult NvMap::Handle::Alloc(Flags pFlags, u32 pAlign, u8 pKind, u64 pAddress, NvCore::SessionId pSessionId) {
    // Handles cannot be allocated twice
    if (allocated) {
        return NvResult::AccessDenied;
    }
    flags = pFlags;
    kind = pKind;
    align = pAlign < YUZU_PAGESIZE ? YUZU_PAGESIZE : pAlign;
    session_id = pSessionId;
    // This flag is only applicable for handles with an address passed
    if (pAddress) {
        flags.keep_uncached_after_free.Assign(0);
    } else {
        LOG_CRITICAL(Service_NVDRV, "Mapping nvmap handles without a CPU side address is unimplemented!");
    }
    size = Common::AlignUp(size, YUZU_PAGESIZE);
    aligned_size = Common::AlignUp(size, align);
    address = pAddress;
    allocated = true;
    return NvResult::Success;
}

NvResult NvMap::Handle::Duplicate(bool internal_session) {
    // Unallocated handles cannot be duplicated as duplication requires memory accounting (in HOS)
    if (!allocated) [[unlikely]] {
        return NvResult::BadValue;
    }
    // If we internally use FromId the duplication tracking of handles won't work accurately due to
    // us not implementing per-process handle refs.
    if (internal_session) {
        internal_dupes++;
    } else {
        dupes++;
    }
    return NvResult::Success;
}

NvMap::NvMap(Container& core_, Tegra::Host1x::Host1x& host1x_) : host1x{host1x_}, core{core_} {}

void NvMap::AddHandle(Handle&& handle_description) {
    std::scoped_lock l(handles_lock);
    handles.insert_or_assign(handle_description.id, std::move(handle_description));
}

void NvMap::UnmapHandle(Handle& handle_description) {
    // Remove pending unmap queue entry if needed
    if (handle_description.unmap_queue_entry) {
        unmap_queue.erase(*handle_description.unmap_queue_entry);
        handle_description.unmap_queue_entry.reset();
    }

    // Free and unmap the handle from Host1x GMMU
    if (handle_description.pin_virt_address) {
        host1x.gmmu_manager.Unmap(static_cast<GPUVAddr>(handle_description.pin_virt_address),
                            handle_description.aligned_size);
        host1x.Allocator().Free(handle_description.pin_virt_address,
                                static_cast<u32>(handle_description.aligned_size));
        handle_description.pin_virt_address = 0;
    }

    // Free and unmap the handle from the SMMU
    const size_t map_size = handle_description.aligned_size;
    if (!handle_description.in_heap) {
        auto& smmu = host1x.MemoryManager();
        size_t aligned_up = Common::AlignUp(map_size, BIG_PAGE_SIZE);
        smmu.Unmap(handle_description.d_address, map_size);
        smmu.Free(handle_description.d_address, static_cast<size_t>(aligned_up));
        handle_description.d_address = 0;
        return;
    }
    const VAddr vaddress = handle_description.address;
    auto* session = core.GetSession(handle_description.session_id);
    session->mapper->Unmap(vaddress, map_size);
    handle_description.d_address = 0;
    handle_description.in_heap = false;
}

bool NvMap::TryRemoveHandle(const Handle& handle_description) {
    // No dupes left, we can remove from handle map
    if (handle_description.dupes == 0 && handle_description.internal_dupes == 0) {
        std::scoped_lock l(handles_lock);
        auto it = handles.find(handle_description.id);
        if (it != handles.end()) {
            handles.erase(it);
        }
        return true;
    } else {
        return false;
    }
}

NvResult NvMap::CreateHandle(u64 size, Handle::Id& out_handle) {
    if (!Common::AlignUp(size, YUZU_PAGESIZE)) {
        return NvResult::BadValue;
    }
    u32 id = next_handle_id.fetch_add(HandleIdIncrement, std::memory_order_relaxed);
    AddHandle(Handle(size, id));
    out_handle = id;
    return NvResult::Success;
}

std::optional<std::reference_wrapper<NvMap::Handle>> NvMap::GetHandle(Handle::Id handle) {
    if (auto const it = handles.find(handle); it != handles.end())
        return {it->second};
    return std::nullopt;
}

DAddr NvMap::GetHandleAddress(Handle::Id handle) {
    if (auto const it = handles.find(handle); it != handles.end())
        return it->second.d_address;
    return 0;
}

DAddr NvMap::PinHandle(NvMap::Handle::Id handle, bool low_area_pin) {
    std::scoped_lock lock(handles_lock);
    auto o = GetHandle(handle);
    if (!o) [[unlikely]] {
        return 0;
    }

    auto handle_description = &o->get();

    const auto map_low_area = [&] {
        if (handle_description->pin_virt_address == 0) {
            u32 address = host1x.Allocator().Allocate(u32(handle_description->aligned_size));
            host1x.GMMU().Map(GPUVAddr(address), handle_description->d_address, handle_description->aligned_size);
            handle_description->pin_virt_address = address;
        }
    };
    if (!handle_description->pins) {
        // If we're in the unmap queue we can just remove ourselves and return since we're already
        // mapped
        {
            // Lock now to prevent our queue entry from being removed for allocation in-between the
            // following check and erase
            std::scoped_lock ql(unmap_queue_lock);
            if (handle_description->unmap_queue_entry) {
                unmap_queue.erase(*handle_description->unmap_queue_entry);
                handle_description->unmap_queue_entry.reset();
                if (low_area_pin) {
                    map_low_area();
                    handle_description->pins++;
                    return DAddr(handle_description->pin_virt_address);
                }
                handle_description->pins++;
                return handle_description->d_address;
            }
        }

        using namespace std::placeholders;
        // If not then allocate some space and map it
        DAddr address{};
        auto& smmu = host1x.MemoryManager();
        auto* session = core.GetSession(handle_description->session_id);
        const VAddr vaddress = handle_description->address;
        const size_t map_size = handle_description->aligned_size;
        if (session->has_preallocated_area && session->mapper->IsInBounds(vaddress, map_size)) {
            handle_description->d_address = session->mapper->Map(vaddress, map_size);
            handle_description->in_heap = true;
        } else {
            size_t aligned_up = Common::AlignUp(map_size, BIG_PAGE_SIZE);
            while ((address = smmu.Allocate(aligned_up)) == 0) {
                // Free handles until the allocation succeeds
                std::scoped_lock queueLock(unmap_queue_lock);
                if (auto free_handle = handles.find(unmap_queue.front()); free_handle != handles.end()) {
                    // Handles in the unmap queue are guaranteed not to be pinned so don't bother
                    // checking if they are before unmapping
                    if (handle_description->d_address)
                        UnmapHandle(free_handle->second);
                } else {
                    LOG_CRITICAL(Service_NVDRV, "Ran out of SMMU address space!");
                }
            }

            handle_description->d_address = address;
            smmu.Map(address, vaddress, map_size, session->asid, true);
            handle_description->in_heap = false;
        }
    }

    if (low_area_pin) {
        map_low_area();
    }

    handle_description->pins++;
    if (low_area_pin) {
        return DAddr(handle_description->pin_virt_address);
    }
    return handle_description->d_address;
}

void NvMap::UnpinHandle(Handle::Id handle) {
    std::scoped_lock lock(handles_lock);
    if (auto o = GetHandle(handle); o) {
        auto handle_description = &o->get();
        if (--handle_description->pins < 0) {
            LOG_WARNING(Service_NVDRV, "Pin count imbalance detected!");
        } else if (!handle_description->pins) {
            std::scoped_lock ql(unmap_queue_lock);
            // Add to the unmap queue allowing this handle's memory to be freed if needed
            unmap_queue.push_back(handle);
            handle_description->unmap_queue_entry = std::prev(unmap_queue.end());
        }
    }
}

void NvMap::DuplicateHandle(Handle::Id handle, bool internal_session) {
    std::scoped_lock lock(handles_lock);
    auto o = GetHandle(handle);
    if (!o) {
        LOG_CRITICAL(Service_NVDRV, "Unregistered handle!");
        return;
    }
    auto result = o->get().Duplicate(internal_session);
    if (result != NvResult::Success) {
        LOG_CRITICAL(Service_NVDRV, "Could not duplicate handle!");
    }
}

std::optional<NvMap::FreeInfo> NvMap::FreeHandle(Handle::Id handle, bool internal_session) {
    // We use a weak ptr here so we can tell when the handle has been freed and report that back to guest
    std::scoped_lock lock(handles_lock);
    if (auto o = GetHandle(handle); o) {
        auto handle_description = &o->get();
        if (internal_session) {
            if (--handle_description->internal_dupes < 0)
                LOG_WARNING(Service_NVDRV, "Internal duplicate count imbalance detected!");
        } else {
            if (--handle_description->dupes < 0) {
                LOG_WARNING(Service_NVDRV, "User duplicate count imbalance detected!");
            } else if (handle_description->dupes == 0) {
                // Force unmap the handle
                if (handle_description->d_address) {
                    std::scoped_lock ql(unmap_queue_lock);
                    UnmapHandle(*handle_description);
                }
                handle_description->pins = 0;
            }
        }
        // Try to remove the shared ptr to the handle from the map, if nothing else is using the
        // handle then it will now be freed when `handle_description` goes out of scope
        if (TryRemoveHandle(*handle_description)) {
            LOG_DEBUG(Service_NVDRV, "Removed nvmap handle: {}", handle);
        } else {
            LOG_DEBUG(Service_NVDRV, "Tried to free nvmap handle: {} but didn't as it still has duplicates", handle);
        }
        // // If the handle hasn't been freed from memory, mark that
        // if (!hWeak.expired()) {
        //     LOG_DEBUG(Service_NVDRV, "nvmap handle: {} wasn't freed as it is still in use", handle);
        //     freeInfo.can_unlock = false;
        // }
        return FreeInfo{
            .address = handle_description->address,
            .size = handle_description->size,
            .was_uncached = handle_description->flags.map_uncached.Value() != 0,
            .can_unlock = true,
        };
    } else {
        return std::nullopt;
    }
}

void NvMap::UnmapAllHandles(NvCore::SessionId session_id) {
    std::scoped_lock lk{handles_lock};
    for (auto it = handles.begin(); it != handles.end(); ++it) {
        if (it->second.session_id.id != session_id.id || it->second.dupes <= 0) {
            continue;
        }
        FreeHandle(it->first, false);
    }
}

} // namespace Service::Nvidia::NvCore
