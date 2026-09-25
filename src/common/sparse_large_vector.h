// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* virtual_buffer.h */
// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>

#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "common/alignment.h"
#include "common/assert.h"

namespace Common {

#ifdef _WIN32
constexpr u64 HostPageSize = 0x1000;
constexpr u64 HostPageBits = 12;
constexpr u64 HostPageMask = ~(HostPageSize - 1);
bool CommitVectorPage(uintptr_t addr, bool write) noexcept;
#else
inline const u64 HostPageSize = static_cast<u64>(sysconf(_SC_PAGESIZE));
inline const u64 HostPageBits = std::countr_zero(HostPageSize);
inline const u64 HostPageMask = ~(HostPageSize - 1);
#endif

void* AllocateMemoryPages(std::size_t size) noexcept;
void FreeMemoryPages(void* base, std::size_t size) noexcept;
void DecommitVectorPage(uintptr_t base) noexcept;

/// A large page-aligned buffer that has optimized memory usage for zero-writes.
template <typename T>
    // MSVC doesn't regard structs with atomics as trivially copyable
    // requires std::is_trivially_copyable_v<T>
class SparseLargeVector final {
public:
    SparseLargeVector() = default;

    explicit SparseLargeVector(std::size_t count) noexcept {
        if (count > SIZE_MAX / sizeof(T)) {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector size overflow: {} elements", count);
            return;
        }
        Allocate(count * sizeof(T));
    }

    ~SparseLargeVector() noexcept {
        Release();
    }

    SparseLargeVector(const SparseLargeVector&) = delete;
    SparseLargeVector& operator=(const SparseLargeVector&) = delete;
    SparseLargeVector(SparseLargeVector&& other) = delete;
    SparseLargeVector& operator=(SparseLargeVector&& other) = delete;

    void ResizeAndClear(std::size_t count) noexcept {
        if (count > SIZE_MAX / sizeof(T)) {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector resize overflow: {} elements", count);
            return;
        }
        const std::size_t new_size = count * sizeof(T);
        if (new_size == alloc_size) {
            ZeroRegion(0, alloc_size / sizeof(T));
            return;
        }
        Release();
        Allocate(new_size);
    }

    T& GetAndFault(std::size_t index) noexcept {
        if (base_ptr == nullptr || index >= size()) [[unlikely]] {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector RW access out of bounds @ {} (size {})", index, size());
            std::abort();
        }
        const u64 byte_offset = static_cast<u64>(index) * sizeof(T);
        if (!CommitPage(byte_offset)) [[unlikely]] {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector commit failed @ {} (offset {:#x})", index, byte_offset);
            std::abort();
        }
        return base_ptr[index];
    }

    const T& GetOrDefault(std::size_t index) const noexcept {
        if (base_ptr == nullptr || index >= size()) [[unlikely]] {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector RO access out of bounds @ {}", index);
            return DefaultValue();
        }
#ifdef _WIN32
        if (!IsPageCommitted(static_cast<u64>(index) * sizeof(T))) {
            return DefaultValue();
        }
#endif
        return base_ptr[index];
    }

    void Set(std::size_t index, const T& value) noexcept {
        if (base_ptr == nullptr || index >= size()) [[unlikely]] {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector write out of bounds @ {}", index);
            return;
        }
        const u64 byte_offset = static_cast<u64>(index) * sizeof(T);
        if (!CommitPage(byte_offset)) [[unlikely]] {
            LOG_CRITICAL(Common_Memory, "SparseLargeVector commit failed for write @ {}", index);
            return;
        }
        base_ptr[index] = value;
    }

    void ZeroRegion(std::size_t start, std::size_t end_) noexcept {
        if (base_ptr == nullptr || start >= end_) return;

        const u64 start_off = static_cast<u64>(start) * sizeof(T);
        const u64 end_off = static_cast<u64>(end_) * sizeof(T);
        const u64 first_page_end = (start_off + HostPageSize - 1) & HostPageMask;

        if (start_off < first_page_end) {
            const u64 chunk_end = (std::min)(first_page_end, end_off);
            const u64 chunk_size = chunk_end - start_off;
            if (chunk_size != 0 && IsPageCommitted(start_off)) {
                std::memset(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(base_ptr) + start_off), 0, chunk_size);
            }
            if (end_off <= first_page_end) return;
        }

        for (u64 off = first_page_end; off < end_off; off += HostPageSize) {
            if (!IsPageCommitted(off)) continue;
            const u64 remaining = end_off - off;
            if (remaining >= HostPageSize) {
                DecommitPage(off);
            } else {
                std::memset(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(base_ptr) + off), 0, remaining);
            }
        }
    }

    void CommitRegion(std::size_t index, std::size_t end_) noexcept {
        if (base_ptr == nullptr || index >= end_) return;
        const u64 start_off = static_cast<u64>(index) * sizeof(T);
        const u64 end_off = static_cast<u64>(end_) * sizeof(T);
        const u64 start_page = start_off & HostPageMask;
        for (u64 off = start_page; off < end_off; off += HostPageSize) {
            if (!IsPageCommitted(off)) {
                (void)CommitPage(off);
            }
        }
    }

    T& GetUnchecked(std::size_t index) noexcept { return base_ptr[index]; }

    [[nodiscard]] const T& operator[](std::size_t index) const noexcept { return GetOrDefault(index); }
    [[nodiscard]] const T* data() const noexcept { return base_ptr; }
    [[nodiscard]] std::size_t size() const noexcept { return alloc_size / sizeof(T); }

private:
    void Allocate(std::size_t new_size) noexcept {
        alloc_size = new_size;
        if (alloc_size == 0) {
            base_ptr = nullptr;
            committed_pages.reset();
            return;
        }
        base_ptr = static_cast<T*>(AllocateMemoryPages(alloc_size));
        const std::size_t num_pages = NumPages();
        const std::size_t num_words = (num_pages + 63) / 64;
        committed_pages = std::make_unique<std::atomic<u64>[]>(num_words);
    }

    void Release() noexcept {
        if (base_ptr != nullptr) {
            FreeMemoryPages(base_ptr, alloc_size);
            base_ptr = nullptr;
        }
        committed_pages.reset();
        alloc_size = 0;
    }

    [[nodiscard]] u64 NumPages() const noexcept {
        return (alloc_size + HostPageSize - 1) >> HostPageBits;
    }

    [[nodiscard]] bool IsPageCommitted(u64 byte_offset) const noexcept {
        const u64 page_index = byte_offset >> HostPageBits;
        if (committed_pages == nullptr || page_index >= NumPages()) return false;
        const auto val = committed_pages[page_index >> 6].load(std::memory_order_acquire);
        return (val >> (page_index & 63)) & 1;
    }

    void SetPageBit(u64 page_index, bool value) noexcept {
        if (committed_pages == nullptr) return;
        const u64 bit = 1ULL << (page_index & 63);
        auto& atom = committed_pages[page_index >> 6];
        if (value) {
            atom.fetch_or(bit, std::memory_order_release);
        } else {
            atom.fetch_and(~bit, std::memory_order_release);
        }
    }

    bool CommitPage(u64 byte_offset) noexcept {
        const u64 page_index = byte_offset >> HostPageBits;
        const uintptr_t page_addr = (reinterpret_cast<uintptr_t>(base_ptr) + byte_offset) & HostPageMask;

        if (IsPageCommitted(byte_offset)) return true;

#if defined(_WIN32)
        if (!CommitVectorPage(page_addr, true)) return false;
#else
        if (mprotect(reinterpret_cast<void*>(page_addr), HostPageSize, PROT_READ | PROT_WRITE) != 0) {
            LOG_ERROR(Common_Memory, "mprotect failed at {:#x}: {}", page_addr, std::strerror(errno));
            return false;
        }
#endif
        SetPageBit(page_index, true);
        return true;
    }

    void DecommitPage(u64 byte_offset) noexcept {
        const u64 page_index = byte_offset >> HostPageBits;
        const uintptr_t page_addr = (reinterpret_cast<uintptr_t>(base_ptr) + byte_offset) & HostPageMask;
        DecommitVectorPage(page_addr);
        SetPageBit(page_index, false);
    }

    [[nodiscard]] const T& DefaultValue() const noexcept {
        return *reinterpret_cast<const T*>(&default_val);
    }

    std::size_t alloc_size{};
    T* base_ptr{};
    std::unique_ptr<std::atomic<u64>[]> committed_pages{};
    alignas(T) const std::array<u8, sizeof(T)> default_val{};
};

} // namespace Common