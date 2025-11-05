// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later


#include <algorithm>
#include <array>
#include <ranges>

#include "dynarmic/backend/ppc64/reg_alloc.h"
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/common/always_false.h"

namespace Dynarmic::Backend::PPC64 {

constexpr size_t spill_offset = offsetof(StackLayout, spill);
constexpr size_t spill_slot_size = sizeof(decltype(StackLayout::spill)::value_type);

static bool IsValuelessType(IR::Type type) {
    return type == IR::Type::Table;
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

bool RegAlloc::IsValueLive(IR::Inst* inst) const {
    return !!ValueLocation(inst);
}

void RegAlloc::UpdateAllUses() {
    for (auto& gpr : gprs)
        gpr.UpdateUses();
    for (auto& fpr : fprs)
        fpr.UpdateUses();
    for (auto& spill : spills)
        spill.UpdateUses();
}

void RegAlloc::DefineAsExisting(IR::Inst* inst, IR::Value arg) {
    ASSERT(!ValueLocation(inst));
    if (arg.IsImmediate()) {
        inst->ReplaceUsesWith(arg);
    } else {
        auto& info = ValueInfo(arg.GetInst());
        info.values.emplace_back(inst);
        info.expected_uses += inst->UseCount();
    }
}

void RegAlloc::AssertNoMoreUses() const {
    const auto is_empty = [](const auto& i) { return i.IsCompletelyEmpty(); };
    ASSERT(std::all_of(gprs.begin(), gprs.end(), is_empty));
    ASSERT(std::all_of(fprs.begin(), fprs.end(), is_empty));
    ASSERT(std::all_of(spills.begin(), spills.end(), is_empty));
}

std::optional<u32> RegAlloc::AllocateRegister(const std::array<HostLocInfo, 32>& regs) const {
    auto const order = PPC64::GPR_ORDER;
    if (auto const it = std::find_if(order.begin(), order.end(), [&](u32 i) {
        return regs[i].values.empty() && !regs[i].locked;
    }); it != order.end())
        return *it;
    // TODO: Actual proper LRU
    return std::nullopt;
}

void RegAlloc::SpillGpr(u32 index) {
    ASSERT(!gprs[index].locked && !gprs[index].realized);
    if (!gprs[index].values.empty()) {
        const u32 new_location_index = FindFreeSpill();
        code.STD(powah::GPR{index}, powah::R1, spill_offset + new_location_index * spill_slot_size);
        spills[new_location_index] = std::exchange(gprs[index], {});
    }
}

void RegAlloc::SpillFpr(u32 index) {
    ASSERT(!fprs[index].locked && !fprs[index].realized);
    if (!fprs[index].values.empty()) {
        const u32 new_location_index = FindFreeSpill();
        //code.FSD(powah::FPR{index}, spill_offset + new_location_index * spill_slot_size, powah::sp);
        spills[new_location_index] = std::exchange(fprs[index], {});
    }
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

inline bool HostLocIsGpr(HostLoc h) noexcept {
    return u8(h) >= u8(HostLoc::R0) && u8(h) <= u8(HostLoc::R31);
}
inline bool HostLocIsFpr(HostLoc h) noexcept {
    return u8(h) >= u8(HostLoc::FR0) && u8(h) <= u8(HostLoc::FR31);
}
inline bool HostLocIsVpr(HostLoc h) noexcept {
    return u8(h) >= u8(HostLoc::VR0) && u8(h) <= u8(HostLoc::VR31);
}
inline std::variant<powah::GPR, powah::FPR> HostLocToReg(HostLoc h) noexcept {
    if (HostLocIsGpr(h))
        return powah::GPR{uint32_t(h)};
    ASSERT(false && "unimp");
}

HostLocInfo& RegAlloc::ValueInfo(HostLoc h) {
    if (u8(h) >= u8(HostLoc::R0) && u8(h) <= u8(HostLoc::R31))
        return gprs[size_t(h)];
    else if (u8(h) >= u8(HostLoc::FR0) && u8(h) <= u8(HostLoc::FR31))
        return gprs[size_t(h) - size_t(HostLoc::FR0)];
    else if (u8(h) >= u8(HostLoc::VR0) && u8(h) <= u8(HostLoc::VR31))
        return vprs[size_t(h) - size_t(HostLoc::VR0)];
    auto const index = size_t(h) - size_t(HostLoc::FirstSpill);
    ASSERT(index <= spills.size());
    return spills[index];
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
    ASSERT(false && "unimp");
}

/// @brief Defines a register RegLock to use (and locks it)
RegLock<powah::GPR> RegAlloc::ScratchGpr() {
    auto const r = AllocateRegister(gprs);
    return RegLock(*this, powah::GPR{*r});
}

/// @brief Uses the given GPR of the argument
RegLock<powah::GPR> RegAlloc::UseGpr(IR::Value arg) {
    if (arg.IsImmediate()) {
        // HOLY SHIT EVIL HAXX
        auto const reg = ScratchGpr();
        auto const imm = arg.GetImmediateAsU64();
        if (imm >= 0xffffffff) {
            auto const lo = uint32_t(imm >> 0), hi = uint32_t(imm >> 32);
            if (lo == hi) {
                code.LIS(reg, imm >> 16);
                code.ORI(reg, reg, imm & 0xffff);
                code.RLDIMI(reg, reg, 32, 0);
            } else {
                ASSERT(false && "larger >32bit imms");
            }
        } else if (imm > 0xffff && imm <= 0xffffffff) {
            code.LIS(reg, imm >> 16);
            code.ORI(reg, reg, imm & 0xffff);
        } else if (imm <= 0xffff) {
            code.LI(reg, imm);
        }
        return reg;
    } else {
        auto const loc = ValueLocation(arg.GetInst());
        ASSERT(loc && HostLocIsGpr(*loc));
        return RegLock(*this, std::get<powah::GPR>(HostLocToReg(*loc)));
    }
}

void RegAlloc::DefineValue(IR::Inst* inst, powah::GPR const gpr) noexcept {
    ASSERT(!ValueLocation(inst) && "inst has already been defined");
    ValueInfo(HostLoc(gpr.index)).values.push_back(inst);
}

void RegAlloc::DefineValue(IR::Inst* inst, IR::Value arg) noexcept {
    ASSERT(!ValueLocation(inst) && "inst has already been defined");
    if (arg.IsImmediate()) {
        HostLoc const loc{u8(ScratchGpr().value.index)};
        ValueInfo(loc).values.push_back(inst);
        auto const value = arg.GetImmediateAsU64();
        if (value >= 0x7fff) {
            ASSERT(false && "unimp");
        } else {
            //code.LI(HostLocToReg(loc), value);
        }
    } else {
        ASSERT(ValueLocation(arg.GetInst()) && "arg must already be defined");
        const HostLoc loc = *ValueLocation(arg.GetInst());
        ValueInfo(loc).values.push_back(inst);
    }
}

}  // namespace Dynarmic::Backend::RV64
