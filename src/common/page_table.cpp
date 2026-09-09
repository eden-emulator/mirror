// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/page_table.h"
#include "common/scope_exit.h"

namespace Common {

PageTable::PageTable() = default;

PageTable::~PageTable() noexcept = default;

void PageTable::Resize(std::size_t address_space_width_in_bits, std::size_t page_bits) {
    auto const num_page_table_entries = 1ULL << (address_space_width_in_bits - page_bits);
    entries.ResizeAndClear(num_page_table_entries);
    current_address_space_width_in_bits = address_space_width_in_bits;
    current_page_bits = page_bits;
}

} // namespace Common
