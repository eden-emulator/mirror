// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <type_traits>
#include <array>
#include <cstddef>

#include "core/file_sys/errors.h"
#include "core/file_sys/fssystem/fs_i_storage.h"
#include "core/file_sys/fssystem/fssystem_bucket_tree.h"
#include "core/file_sys/fssystem/fssystem_bucket_tree_utils.h"
#include "core/file_sys/vfs/vfs_offset.h"

namespace FileSys {

class IndirectStorage : public IReadOnlyStorage {
public:
    static constexpr s32 StorageCount = 2;
    static constexpr size_t NodeSize = 16_KiB;

    struct Entry {
        std::array<u8, sizeof(s64)> virt_offset;
        std::array<u8, sizeof(s64)> phys_offset;
        s32 storage_index;

        void SetVirtualOffset(const s64& ofs) {
            std::memcpy(this->virt_offset.data(), std::addressof(ofs), sizeof(s64));
        }

        s64 GetVirtualOffset() const {
            s64 offset;
            std::memcpy(std::addressof(offset), this->virt_offset.data(), sizeof(s64));
            return offset;
        }

        void SetPhysicalOffset(const s64& ofs) {
            std::memcpy(this->phys_offset.data(), std::addressof(ofs), sizeof(s64));
        }

        s64 GetPhysicalOffset() const {
            s64 offset;
            std::memcpy(std::addressof(offset), this->phys_offset.data(), sizeof(s64));
            return offset;
        }
    };
    static_assert(std::is_trivially_default_constructible_v<Entry> && std::is_trivially_copyable_v<Entry>);
    static_assert(sizeof(Entry) == 0x14);

    struct EntryData {
        s64 virt_offset;
        s64 phys_offset;
        s32 storage_index;

        void Set(const Entry& entry) {
            this->virt_offset = entry.GetVirtualOffset();
            this->phys_offset = entry.GetPhysicalOffset();
            this->storage_index = entry.storage_index;
        }
    };
    static_assert(std::is_trivially_default_constructible_v<EntryData> && std::is_trivially_copyable_v<EntryData>);

public:
    IndirectStorage() : m_table(), m_data_storage() {}
    virtual ~IndirectStorage() {
        this->Finalize();
    }

    Result Initialize(VirtualFile table_storage);
    void Finalize();

    bool IsInitialized() const {
        return m_table.IsInitialized();
    }

    Result Initialize(VirtualFile node_storage, VirtualFile entry_storage, s32 entry_count) {
        R_RETURN(
            m_table.Initialize(node_storage, entry_storage, NodeSize, sizeof(Entry), entry_count));
    }

    void SetStorage(s32 idx, VirtualFile storage) {
        ASSERT(0 <= idx && idx < StorageCount);
        m_data_storage[idx] = storage;
        if (idx == 0) {
            m_original_missing = storage == nullptr || storage->GetSize() == 0;
        }
    }

    template <typename T>
    void SetStorage(s32 idx, T storage, s64 offset, s64 size) {
        ASSERT(0 <= idx && idx < StorageCount);
        m_data_storage[idx] = std::make_shared<OffsetVfsFile>(storage, size, offset);
    }

    Result GetEntryList(Entry* out_entries, s32* out_entry_count, s32 entry_count, s64 offset,
                        s64 size);

    virtual size_t GetSize() const override {
        BucketTree::Offsets offsets{};
        m_table.GetOffsets(std::addressof(offsets));

        return offsets.end_offset;
    }

    virtual size_t Read(u8* buffer, size_t size, size_t offset) const override;

public:
    static constexpr s64 QueryHeaderStorageSize() {
        return BucketTree::QueryHeaderStorageSize();
    }

    static constexpr s64 QueryNodeStorageSize(s32 entry_count) {
        return BucketTree::QueryNodeStorageSize(NodeSize, sizeof(Entry), entry_count);
    }

    static constexpr s64 QueryEntryStorageSize(s32 entry_count) {
        return BucketTree::QueryEntryStorageSize(NodeSize, sizeof(Entry), entry_count);
    }

protected:
    BucketTree& GetEntryTable() {
        return m_table;
    }

    VirtualFile& GetDataStorage(s32 index) {
        ASSERT(0 <= index && index < StorageCount);
        return m_data_storage[index];
    }

    template <bool ContinuousCheck, bool RangeCheck, typename F>
    Result OperatePerEntry(s64 offset, s64 size, F func);
    // Launching another game makes the original storage inaccessable.
    // This is a helper for multi-nca games.
    void ReportMissingOriginal(s64 offset, s64 size);

private:
    struct ContinuousReadingEntry {
        static constexpr size_t FragmentSizeMax = 4_KiB;

        IndirectStorage::Entry entry;

        s64 GetVirtualOffset() const {
            return this->entry.GetVirtualOffset();
        }

        s64 GetPhysicalOffset() const {
            return this->entry.GetPhysicalOffset();
        }

        bool IsFragment() const {
            return this->entry.storage_index != 0;
        }
    };
    static_assert(std::is_trivially_default_constructible_v<ContinuousReadingEntry> && std::is_trivially_copyable_v<ContinuousReadingEntry>);

private:
    mutable BucketTree m_table;
    std::array<VirtualFile, StorageCount> m_data_storage;
    bool m_original_missing{false};
    bool m_reported_missing_original{false};
};

template <bool ContinuousCheck, bool RangeCheck, typename F>
Result IndirectStorage::OperatePerEntry(s64 offset, s64 size, F func) {
    // Validate preconditions.
    ASSERT(offset >= 0);
    ASSERT(size >= 0);
    ASSERT(this->IsInitialized());

    // Succeed if there's nothing to operate on.
    R_SUCCEED_IF(size == 0);

    // Get the table offsets.
    BucketTree::Offsets table_offsets{};
    R_TRY(m_table.GetOffsets(std::addressof(table_offsets)));

    // Validate arguments.
    R_UNLESS(table_offsets.IsInclude(offset, size), ResultOutOfRange);

    // Find the offset in our tree.
    BucketTree::Visitor visitor;
    R_TRY(m_table.Find(std::addressof(visitor), offset));
    {
        const auto entry_offset = visitor.Get<Entry>()->GetVirtualOffset();
        R_UNLESS(0 <= entry_offset && table_offsets.IsInclude(entry_offset),
                 ResultInvalidIndirectEntryOffset);
    }

    // Prepare to operate in chunks.
    auto cur_offset = offset;
    const auto end_offset = offset + static_cast<s64>(size);
    BucketTree::ContinuousReadingInfo cr_info;

    while (cur_offset < end_offset) {
        // Get the current entry.
        const auto cur_entry = *visitor.Get<Entry>();

        // Get and validate the entry's offset.
        const auto cur_entry_offset = cur_entry.GetVirtualOffset();
        R_UNLESS(cur_entry_offset <= cur_offset, ResultInvalidIndirectEntryOffset);

        // Validate the storage index.
        R_UNLESS(0 <= cur_entry.storage_index && cur_entry.storage_index < StorageCount,
                 ResultInvalidIndirectEntryStorageIndex);

        // If we need to check the continuous info, do so.
        if constexpr (ContinuousCheck) {
            // Scan, if we need to.
            if (cr_info.CheckNeedScan()) {
                R_TRY(visitor.ScanContinuousReading<ContinuousReadingEntry>(
                    std::addressof(cr_info), cur_offset,
                    static_cast<size_t>(end_offset - cur_offset)));
            }

            // Process a base storage entry.
            if (cr_info.CanDo()) {
                // Ensure that we can process.
                R_UNLESS(cur_entry.storage_index == 0, ResultInvalidIndirectEntryStorageIndex);

                // Ensure that we remain within range.
                const auto data_offset = cur_offset - cur_entry_offset;
                const auto cur_entry_phys_offset = cur_entry.GetPhysicalOffset();
                const auto cur_size = static_cast<s64>(cr_info.GetReadSize());

                // If we should, verify the range.
                if constexpr (RangeCheck) {
                    // Get the current data storage's size.
                    s64 cur_data_storage_size = m_data_storage[0]->GetSize();

                    R_UNLESS(0 <= cur_entry_phys_offset &&
                                 cur_entry_phys_offset <= cur_data_storage_size,
                             ResultInvalidIndirectEntryOffset);
                    R_UNLESS(cur_entry_phys_offset + data_offset + cur_size <=
                                 cur_data_storage_size,
                             ResultInvalidIndirectStorageSize);
                }

                // Operate.
                R_TRY(func(m_data_storage[0], cur_entry_phys_offset + data_offset, cur_offset,
                           cur_size));

                // Mark as done.
                cr_info.Done();
            }
        }

        // Get and validate the next entry offset.
        s64 next_entry_offset;
        if (visitor.CanMoveNext()) {
            R_TRY(visitor.MoveNext());
            next_entry_offset = visitor.Get<Entry>()->GetVirtualOffset();
            R_UNLESS(table_offsets.IsInclude(next_entry_offset), ResultInvalidIndirectEntryOffset);
        } else {
            next_entry_offset = table_offsets.end_offset;
        }
        R_UNLESS(cur_offset < next_entry_offset, ResultInvalidIndirectEntryOffset);

        // Get the offset of the entry in the data we read.
        const auto data_offset = cur_offset - cur_entry_offset;
        const auto data_size = (next_entry_offset - cur_entry_offset);
        ASSERT(data_size > 0);

        // Determine how much is left.
        const auto remaining_size = end_offset - cur_offset;
        const auto cur_size = std::min<s64>(remaining_size, data_size - data_offset);
        ASSERT(cur_size <= size);

        // Operate, if we need to.
        bool needs_operate;
        if constexpr (!ContinuousCheck) {
            needs_operate = true;
        } else {
            needs_operate = !cr_info.IsDone() || cur_entry.storage_index != 0;
        }

        if (needs_operate) {
            const auto cur_entry_phys_offset = cur_entry.GetPhysicalOffset();

            if (cur_entry.storage_index == 0 && m_original_missing) {
                this->ReportMissingOriginal(cur_offset, cur_size);
            }

            if constexpr (RangeCheck) {
                // Get the current data storage's size.
                s64 cur_data_storage_size = m_data_storage[cur_entry.storage_index]->GetSize();

                // Ensure that we remain within range.
                R_UNLESS(0 <= cur_entry_phys_offset &&
                             cur_entry_phys_offset <= cur_data_storage_size,
                         ResultIndirectStorageCorrupted);
                R_UNLESS(cur_entry_phys_offset + data_offset + cur_size <= cur_data_storage_size,
                         ResultIndirectStorageCorrupted);
            }

            R_TRY(func(m_data_storage[cur_entry.storage_index], cur_entry_phys_offset + data_offset,
                       cur_offset, cur_size));
        }

        cur_offset += cur_size;
    }

    R_SUCCEED();
}

template <typename EntryType>
Result BucketTree::ScanContinuousReading(ContinuousReadingInfo* out_info,
                                         const ContinuousReadingParam<EntryType>& param) const {
    static_assert(std::is_trivially_default_constructible_v<ContinuousReadingParam<EntryType>> && std::is_trivially_copyable_v<ContinuousReadingParam<EntryType>>);

           // Validate our preconditions.
    ASSERT(this->IsInitialized());
    ASSERT(out_info != nullptr);
    ASSERT(m_entry_size == sizeof(EntryType));

           // Reset the output.
    out_info->Reset();

           // If there's nothing to read, we're done.
    R_SUCCEED_IF(param.size == 0);

           // If we're reading a fragment, we're done.
    R_SUCCEED_IF(param.entry.IsFragment());

           // Validate the first entry.
    auto entry = param.entry;
    auto cur_offset = param.offset;
    R_UNLESS(entry.GetVirtualOffset() <= cur_offset, ResultOutOfRange);

           // Create a pooled buffer for our scan.
    std::vector<char> pool(m_node_size);
    s64 entry_storage_size = m_entry_storage->GetSize();

           // Read the node.
    u8* buffer = reinterpret_cast<u8*>(pool.data());
    const auto ofs = param.entry_set.index * s64(m_node_size);
    R_UNLESS(m_node_size + ofs <= size_t(entry_storage_size),
             ResultInvalidBucketTreeNodeEntryCount);

    m_entry_storage->Read(buffer, m_node_size, ofs);

           // Calculate extents.
    const auto end_offset = cur_offset + s64(param.size);
    s64 phys_offset = entry.GetPhysicalOffset();

           // Start merge tracking.
    s64 merge_size = 0;
    s64 readable_size = 0;
    bool merged = false;

           // Iterate.
    auto entry_index = param.entry_index;
    for (const auto entry_count = param.entry_set.count; entry_index < entry_count; ++entry_index) {
        // If we're past the end, we're done.
        if (end_offset <= cur_offset) {
            break;
        }

               // Validate the entry offset.
        const auto entry_offset = entry.GetVirtualOffset();
        R_UNLESS(entry_offset <= cur_offset, ResultInvalidIndirectEntryOffset);

               // Get the next entry.
        EntryType next_entry = {};
        s64 next_entry_offset;

        if (entry_index + 1 < entry_count) {
            const auto offset = impl::GetBucketTreeEntryOffset(0, m_entry_size, entry_index + 1);
            std::memcpy(std::addressof(next_entry), buffer + offset, m_entry_size);

            next_entry_offset = next_entry.GetVirtualOffset();
            R_UNLESS(param.offsets.IsInclude(next_entry_offset), ResultInvalidIndirectEntryOffset);
        } else {
            next_entry_offset = param.entry_set.offset;
        }

               // Validate the next entry offset.
        R_UNLESS(cur_offset < next_entry_offset, ResultInvalidIndirectEntryOffset);

               // Determine the much data there is.
        const auto data_size = next_entry_offset - cur_offset;
        ASSERT(data_size > 0);

               // Determine how much data we should read.
        const auto remaining_size = end_offset - cur_offset;
        const size_t read_size = static_cast<size_t>(std::min(data_size, remaining_size));
        ASSERT(read_size <= param.size);

               // Update our merge tracking.
        if (entry.IsFragment()) {
            // If we can't merge, stop looping.
            if (EntryType::FragmentSizeMax <= read_size || remaining_size <= data_size) {
                break;
            }

                   // Otherwise, add the current size to the merge size.
            merge_size += read_size;
        } else {
            //  If we can't merge, stop looping.
            if (phys_offset != entry.GetPhysicalOffset()) {
                break;
            }

                   // Add the size to the readable amount.
            readable_size += merge_size + read_size;
            ASSERT(readable_size <= static_cast<s64>(param.size));

                   // Update whether we've merged.
            merged |= merge_size > 0;
            merge_size = 0;
        }

               // Advance.
        cur_offset += read_size;
        ASSERT(cur_offset <= end_offset);

        phys_offset += next_entry_offset - entry_offset;
        entry = next_entry;
    }

           // If we merged, set our readable size.
    if (merged) {
        out_info->SetReadSize(static_cast<size_t>(readable_size));
    }
    out_info->SetSkipCount(entry_index - param.entry_index);

    R_SUCCEED();
}

template <typename EntryType>
Result BucketTree::Visitor::ScanContinuousReading(ContinuousReadingInfo* out_info, s64 offset,
                                                  size_t size) const {
    static_assert(std::is_trivially_default_constructible_v<EntryType> && std::is_trivially_copyable_v<EntryType>);
    ASSERT(this->IsValid());

           // Create our parameters.
    ContinuousReadingParam<EntryType> param = {
                                               .offset = offset,
                                               .size = size,
                                               .entry_set = m_entry_set.header,
                                               .entry_index = m_entry_index,
                                               .offsets{},
                                               .entry{},
                                               };
    std::memcpy(std::addressof(param.offsets), std::addressof(m_offsets),
                sizeof(BucketTree::Offsets));
    std::memcpy(std::addressof(param.entry), m_entry, sizeof(EntryType));

           // Scan.
    R_RETURN(m_tree->ScanContinuousReading<EntryType>(out_info, param));
}

} // namespace FileSys
