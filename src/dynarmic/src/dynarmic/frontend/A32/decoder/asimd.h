// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include <mcl/bit/bit_count.hpp>
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/decoder/decoder_detail.h"
#include "dynarmic/frontend/decoder/matcher.h"

namespace Dynarmic::A32 {

template<typename Visitor>
using ASIMDMatcher = Decoder::Matcher<Visitor, u32>;

template<typename V>
std::vector<ASIMDMatcher<V>> GetASIMDDecodeTable() {
    std::vector<std::pair<const char*, ASIMDMatcher<V>>> table = {
#define INST(fn, name, bitstring) { name, DYNARMIC_DECODER_GET_MATCHER(ASIMDMatcher, fn, name, Decoder::detail::StringToArray<32>(bitstring)) },
#include "./asimd.inc"
#undef INST
    };
    // Exceptions to the rule of thumb.
    const std::set<std::string> comes_first{
        "VBIC, VMOV, VMVN, VORR (immediate)",
        "VEXT",
        "VTBL",
        "VTBX",
        "VDUP (scalar)",
    };
    const std::set<std::string> comes_last{
        "VMLA (scalar)",
        "VMLAL (scalar)",
        "VQDMLAL/VQDMLSL (scalar)",
        "VMUL (scalar)",
        "VMULL (scalar)",
        "VQDMULL (scalar)",
        "VQDMULH (scalar)",
        "VQRDMULH (scalar)",
    };
    const auto sort_begin = std::stable_partition(table.begin(), table.end(), [&](const auto& e) {
        return comes_first.count(e.first) > 0;
    });
    const auto sort_end = std::stable_partition(table.begin(), table.end(), [&](const auto& e) {
        return comes_last.count(e.first) == 0;
    });
    // If a matcher has more bits in its mask it is more specific, so it should come first.
    std::stable_sort(sort_begin, sort_end, [](const auto& a, const auto& b) {
        return mcl::bit::count_ones(a.second.GetMask()) > mcl::bit::count_ones(b.second.GetMask());
    });
    std::vector<ASIMDMatcher<V>> final_table;
    std::transform(table.cbegin(), table.cend(), final_table.begin(), [](auto const& e) {
        return e.second;
    });
    return final_table;
}

template<typename V>
std::optional<std::reference_wrapper<const ASIMDMatcher<V>>> DecodeASIMD(u32 instruction) {
    static const auto table = GetASIMDDecodeTable<V>();

    const auto matches_instruction = [instruction](const auto& matcher) { return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? std::optional<std::reference_wrapper<const ASIMDMatcher<V>>>(*iter) : std::nullopt;
}

}  // namespace Dynarmic::A32
