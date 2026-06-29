// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"

namespace AudioCore {
/**
 * Responsible for allocating up a workbuffer into multiple pieces.
 * Takes in a buffer and size (it does not own them), and allocates up the buffer via Allocate.
 */
class WorkbufferAllocator {
public:
    explicit WorkbufferAllocator(std::span<u8> buffer_)
        : buffer{buffer_}
    {}

    /**
     * Allocate the given count of T elements, aligned to alignment.
     *
     * @param count     - The number of elements to allocate.
     * @param alignment - The required starting alignment.
     * @return Non-owning container of allocated elements.
     */
    template <typename T>
    std::span<T> Allocate(u64 count, u64 alignment) {
        u64 out{0};
        u64 byte_size = count * sizeof(T);
        if (byte_size > 0) {
            auto current{uintptr_t(buffer.data()) + offset};
            auto aligned_buffer{Common::AlignUp(current, alignment)};
            if (aligned_buffer + byte_size <= uintptr_t(buffer.data()) + buffer.size()) {
                out = aligned_buffer;
                offset = byte_size - uintptr_t(buffer.data()) + aligned_buffer;
            } else {
                LOG_ERROR(
                    Service_Audio,
                    "Allocated buffer was too small to hold new alloc.\nAllocator size={:08X}, "
                    "offset={:08X}.\nAttempting to allocate {:08X} with alignment={:02X}",
                    buffer.size(), offset, byte_size, alignment);
                count = 0;
            }
        }
        return std::span<T>(reinterpret_cast<T*>(out), count);
    }

    /// @brief Align the current offset to the given alignment.
    /// @param alignment - The required starting alignment.
    void Align(u64 alignment) {
        auto current{uintptr_t(buffer.data()) + offset};
        auto aligned_buffer{Common::AlignUp(current, alignment)};
        offset = 0 - uintptr_t(buffer.data()) + aligned_buffer;
    }

    /**
     * Get the current buffer offset.
     *
     * @return The current allocating offset.
     */
    u64 GetCurrentOffset() const {
        return offset;
    }

    /**
     * Get the current buffer size.
     *
     * @return The size of the current buffer.
     */
    u64 GetSize() const {
        return buffer.size();
    }

    /**
     * Get the remaining size that can be allocated.
     *
     * @return The remaining size left in the buffer.
     */
    u64 GetRemainingSize() const {
        return buffer.size() - offset;
    }

private:
    const std::span<u8> buffer;
    /// Current offset into the buffer, an error will be thrown if it exceeds size.
    u64 offset{};
};

} // namespace AudioCore
