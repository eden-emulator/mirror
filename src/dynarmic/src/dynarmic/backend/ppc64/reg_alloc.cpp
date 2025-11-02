// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/ppc64/reg_alloc.h"

#include <algorithm>
#include <array>
#include <ranges>

#include "dynarmic/common/assert.h"
#include <mcl/mp/metavalue/lift_value.hpp>
#include "dynarmic/common/common_types.h"

#include "dynarmic/common/always_false.h"

namespace Dynarmic::Backend::PPC64 {

constexpr size_t spill_offset = offsetof(StackLayout, spill);
constexpr size_t spill_slot_size = sizeof(decltype(StackLayout::spill)::value_type);

static bool IsValuelessType(IR::Type type) {
    return type == IR::Type::Table;
}

IR::Type Argument::GetType() const {
    return value.GetType();
}

bool Argument::IsImmediate() const {
    return value.IsImmediate();
}

bool Argument::GetImmediateU1() const {
    return value.GetU1();
}

u8 Argument::GetImmediateU8() const {
    const u64 imm = value.GetImmediateAsU64();
    ASSERT(imm < 0x100);
    return u8(imm);
}

u16 Argument::GetImmediateU16() const {
    const u64 imm = value.GetImmediateAsU64();
    ASSERT(imm < 0x10000);
    return u16(imm);
}

u32 Argument::GetImmediateU32() const {
    const u64 imm = value.GetImmediateAsU64();
    ASSERT(imm < 0x100000000);
    return u32(imm);
}

u64 Argument::GetImmediateU64() const {
    return value.GetImmediateAsU64();
}

IR::Cond Argument::GetImmediateCond() const {
    ASSERT(IsImmediate() && GetType() == IR::Type::Cond);
    return value.GetCond();
}

IR::AccType Argument::GetImmediateAccType() const {
    ASSERT(IsImmediate() && GetType() == IR::Type::AccType);
    return value.GetAccType();
}

bool HostLocInfo::Contains(const IR::Inst* value) const {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void HostLocInfo::SetupScratchLocation() {
    ASSERT(IsCompletelyEmpty());
    realized = true;
}

bool HostLocInfo::IsCompletelyEmpty() const {
    return values.empty() && !locked && !realized && !accumulated_uses && !expected_uses && !uses_this_inst;
}

void HostLocInfo::UpdateUses() {
    accumulated_uses += uses_this_inst;
    uses_this_inst = 0;

    if (accumulated_uses == expected_uses) {
        values.clear();
        accumulated_uses = 0;
        expected_uses = 0;
    }
}

RegAlloc::ArgumentInfo RegAlloc::GetArgumentInfo(IR::Inst* inst) {
    ArgumentInfo ret = {Argument{*this}, Argument{*this}, Argument{*this}, Argument{*this}};
    for (size_t i = 0; i < inst->NumArgs(); i++) {
        const IR::Value arg = inst->GetArg(i);
        ret[i].value = arg;
        if (!arg.IsImmediate() && !IsValuelessType(arg.GetType())) {
            ASSERT(ValueLocation(arg.GetInst()) && "argument must already been defined");
            ValueInfo(arg.GetInst()).uses_this_inst++;
        }
    }
    return ret;
}

bool RegAlloc::IsValueLive(IR::Inst* inst) const {
    return !!ValueLocation(inst);
}

void RegAlloc::UpdateAllUses() {
    for (auto& gpr : gprs) {
        gpr.UpdateUses();
    }
    for (auto& fpr : fprs) {
        fpr.UpdateUses();
    }
    for (auto& spill : spills) {
        spill.UpdateUses();
    }
}

void RegAlloc::DefineAsExisting(IR::Inst* inst, Argument& arg) {
    ASSERT(!ValueLocation(inst));

    if (arg.value.IsImmediate()) {
        inst->ReplaceUsesWith(arg.value);
        return;
    }

    auto& info = ValueInfo(arg.value.GetInst());
    info.values.emplace_back(inst);
    info.expected_uses += inst->UseCount();
}

void RegAlloc::AssertNoMoreUses() const {
    const auto is_empty = [](const auto& i) { return i.IsCompletelyEmpty(); };
    ASSERT(std::all_of(gprs.begin(), gprs.end(), is_empty));
    ASSERT(std::all_of(fprs.begin(), fprs.end(), is_empty));
    ASSERT(std::all_of(spills.begin(), spills.end(), is_empty));
}

u32 RegAlloc::AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<u32>& order) const {
    const auto empty = std::find_if(order.begin(), order.end(), [&](u32 i) { return regs[i].values.empty() && !regs[i].locked; });
    if (empty != order.end())
        return *empty;

    std::vector<u32> candidates;
    std::copy_if(order.begin(), order.end(), std::back_inserter(candidates), [&](u32 i) { return !regs[i].locked; });
    // TODO: LRU
    return candidates[0];
}

void RegAlloc::SpillGpr(u32 index) {
    ASSERT(!gprs[index].locked && !gprs[index].realized);
    if (gprs[index].values.empty())
        return;
    const u32 new_location_index = FindFreeSpill();
    //as.SD(powah::GPR{index}, spill_offset + new_location_index * spill_slot_size, powah::sp);
    spills[new_location_index] = std::exchange(gprs[index], {});
}

void RegAlloc::SpillFpr(u32 index) {
    ASSERT(!fprs[index].locked && !fprs[index].realized);
    if (fprs[index].values.empty()) {
        return;
    }
    const u32 new_location_index = FindFreeSpill();
    //as.FSD(powah::FPR{index}, spill_offset + new_location_index * spill_slot_size, powah::sp);
    spills[new_location_index] = std::exchange(fprs[index], {});
}

u32 RegAlloc::FindFreeSpill() const {
    const auto iter = std::find_if(spills.begin(), spills.end(), [](const HostLocInfo& info) { return info.values.empty(); });
    ASSERT(iter != spills.end() && "All spill locations are full");
    return u32(iter - spills.begin());
}

std::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const {
    const auto fn = [value](const HostLocInfo& info) {
        return info.Contains(value);
    };
    if (const auto iter = std::ranges::find_if(gprs, fn); iter != gprs.end())
        return HostLoc(u32(HostLoc::R0) + u32(iter - gprs.begin()));
    else if (const auto iter = std::ranges::find_if(fprs, fn); iter != fprs.end())
        return HostLoc(u32(HostLoc::FR0) + u32(iter - fprs.begin()));
    else if (const auto iter = std::ranges::find_if(vprs, fn); iter != vprs.end())
        return HostLoc(u32(HostLoc::VR0) + u32(iter - vprs.begin()));
    else if (const auto iter = std::ranges::find_if(spills, fn); iter != spills.end())
        return HostLoc(u32(HostLoc::FirstSpill) + u32(iter - spills.begin()));
    return std::nullopt;
}

HostLocInfo& RegAlloc::ValueInfo(HostLoc host_loc) {
    // switch (host_loc.kind) {
    // case HostLoc::Kind::Gpr:
    //     return gprs[size_t(host_loc.index)];
    // case HostLoc::Kind::Fpr:
    //     return fprs[size_t(host_loc.index)];
    // case HostLoc::Kind::Spill:
    //     return spills[size_t(host_loc.index)];
    // }
    UNREACHABLE();
}

HostLocInfo& RegAlloc::ValueInfo(const IR::Inst* value) {
    const auto fn = [value](const HostLocInfo& info) {
        return info.Contains(value);
    };
    if (const auto iter = std::find_if(gprs.begin(), gprs.end(), fn); iter != gprs.end())
        return *iter;
    else if (const auto iter = std::find_if(fprs.begin(), fprs.end(), fn); iter != fprs.end())
        return *iter;
    else if (const auto iter = std::find_if(spills.begin(), spills.end(), fn); iter != spills.end())
        return *iter;
    UNREACHABLE();
}

powah::GPR RegAlloc::ScratchGpr(std::optional<std::initializer_list<HostLoc>> desired_locations) {
    UNREACHABLE();
}

void RegAlloc::Use(Argument& arg, HostLoc host_loc) {
    UNREACHABLE();
}

void RegAlloc::UseScratch(Argument& arg, HostLoc host_loc) {
    UNREACHABLE();
}

powah::GPR RegAlloc::UseGpr(Argument& arg) {
    UNREACHABLE();
}

powah::GPR RegAlloc::UseScratchGpr(Argument& arg) {
    UNREACHABLE();
}

void RegAlloc::DefineValue(IR::Inst* inst, powah::GPR const gpr) noexcept {
    UNREACHABLE();
}

void RegAlloc::DefineValue(IR::Inst* inst, Argument& arg) noexcept {
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::RV64
