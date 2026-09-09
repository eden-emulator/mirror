// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

#include "common/common_types.h"
#include "common/sparse_large_vector.h"
#include "common/typed_address.h"

namespace Common {

enum class PageType : u8 {
    /// Page is unmapped and should cause an access error.
    Unmapped = 0b00,
    /// Page is mapped to regular memory. This is the only type you can get pointers to.
    Memory = 0b01,
    /// Page is mapped to regular memory, but inaccessible from CPU fastmem and must use
    /// the callbacks.
    DebugMemory = 0b10,
    /// Page is mapped to regular memory, but also needs to check for rasterizer cache flushing and
    /// invalidation
    RasterizerCachedMemory = 0b11,
};

/**
 * A (reasonably) fast way of allowing switchable and remappable process address spaces. It loosely
 * mimics the way a real CPU page table works.
 */
struct PageTable {
    struct TraversalEntry {
        u64 phys_addr{};
        std::size_t block_size{};
    };

    struct TraversalContext {
        u64 next_page{};
        u64 next_offset{};
    };

    /// Masks out bits reserved for attribute tagging.
    static constexpr u64 ATTRIBUTE_MASK = ((1ULL << 44) - 1) << 12;

    /// Specifies sign bit for page table entries.
    static constexpr u64 SIGN_BIT = 45 + 12; // 44 bits of data + page offset

    /**
     * Atomic tuple of host pointer, page type, and block id.
     * This uses the lower bits of a given pointer to store the attributes.
     * Writing and reading the pointer attribute pair is guaranteed to be atomic for the same method
     * call. In other words, they are guaranteed to be synchronized at all times.
     */
    class PageEntryData {
    public:
        struct Data {
            Data(bool marked_, PageType type_, u16 block_, u64 page_)
                : marked(static_cast<u64>(marked_)       & 0b1)
                , type(static_cast<u64>(type_)           & ((1ULL << 2) - 1))
                , block(static_cast<u64>(block_)         & ((1ULL << 9) - 1))
                , page((page_ >> 12)                     & ((1ULL << 45) - 1))
                , block2((static_cast<u64>(block_) >> 9) & ((1ULL << 7) - 1)) {}
            u64 marked : 1;
            u64 type   : 2;
            u64 block  : 9;
            u64 page   : 45; // 44 bits of actual data (64 - page offset (12) - reserved (8)) + a sign bit
            u64 block2 : 7;
        };

        [[nodiscard]] Data Raw() const noexcept {
            return std::bit_cast<Data>(data_raw.load(std::memory_order_relaxed));
        }

        /// Returns the page pointer
        [[nodiscard]] uintptr_t Pointer(bool ignored_marked = false) const noexcept {
            return ExtractPointer(std::bit_cast<Data>(data_raw.load(std::memory_order_relaxed)), ignored_marked);
        }

        /// Returns the page type attribute
        [[nodiscard]] PageType Type() const noexcept {
            return static_cast<PageType>(std::bit_cast<Data>(data_raw.load(std::memory_order_relaxed)).type);
        }

        /// Returns the block identifier.
        [[nodiscard]] u16 Block() const noexcept {
            return ExtractBlock(std::bit_cast<Data>(data_raw.load(std::memory_order_relaxed)));
        }

        /// Returns the page pointer and attribute pair, extracted from the same atomic read
        [[nodiscard]] std::tuple<uintptr_t, PageType, u16> PointerTypeBlock(bool ignore_marked = false) const noexcept {
            const auto non_atomic_raw = std::bit_cast<Data>(data_raw.load(std::memory_order_relaxed));
            return {ExtractPointer(non_atomic_raw, ignore_marked), static_cast<PageType>(non_atomic_raw.type), ExtractBlock(non_atomic_raw)};
        }

        /// Write page info atomically
        constexpr void Store(bool marked, PageType type, u16 block, uintptr_t pointer) noexcept {
            data_raw.store(std::bit_cast<u64>(Data{marked, type, block, pointer}));
        }

        constexpr void MarkRasterizerCached() noexcept {
            data_raw.fetch_or(0b111);
        }

        constexpr void MarkDebug(u64 ptr, u16 block) noexcept {
            Store(true, PageType::DebugMemory, block, ptr);
        }

        /// Unpack a pointer from a page info raw representation
        [[nodiscard]] static uintptr_t ExtractPointer(Data raw, bool ignore_marked = false) noexcept {
            return raw.marked && !ignore_marked ? 0
                // shift raw.page's fake sign bit to the actual sign bit, then sign extend
                : ((s64)(raw.page << (64 - 44))) >> (64 - 44 - 12);
        }

        [[nodiscard]] static u16 ExtractBlock(Data raw) noexcept {
            return static_cast<u16>(raw.block | (raw.block2 << 9));
        }

    private:
        std::atomic<u64> data_raw;
        static_assert(sizeof(Data) == sizeof(std::atomic<u64>));
    };

    PageTable();
    ~PageTable() noexcept;

    PageTable(const PageTable&) = delete;
    PageTable& operator=(const PageTable&) = delete;
    PageTable(PageTable&&) noexcept = delete;
    PageTable& operator=(PageTable&&) noexcept = delete;

    /**
     * Resizes the page table to be able to accommodate enough pages within
     * a given address space.
     *
     * @param address_space_width_in_bits The address size width in bits.
     * @param page_size_in_bits           The page size in bits.
     */
    void Resize(std::size_t address_space_width_in_bits, std::size_t page_size_in_bits);

    std::size_t GetAddressSpaceBits() const {
        return current_address_space_width_in_bits;
    }

    /// Vector of memory pointers backing each page. An entry can only be non-null if the
    /// corresponding attribute element is of type `Memory`.
    SparseLargeVector<PageEntryData> entries;
    static_assert(sizeof(PageEntryData) == 8);

    u8* fastmem_arena{};
    std::size_t current_address_space_width_in_bits{};
    std::size_t current_page_bits{};
};

} // namespace Common
