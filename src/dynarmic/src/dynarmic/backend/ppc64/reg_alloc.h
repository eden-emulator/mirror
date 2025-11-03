// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include <powah_emit.hpp>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include <ankerl/unordered_dense.h>

#include "dynarmic/backend/ppc64/stack_layout.h"
#include "dynarmic/backend/ppc64/hostloc.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::Backend::PPC64 {

class RegAlloc;

struct Argument {
public:
    IR::Type GetType() const {
        return value.GetType();
    }
    bool IsImmediate() const {
        return value.IsImmediate();
    }
    bool GetImmediateU1() const {
        return value.GetU1();
    }
    u8 GetImmediateU8() const {
        const u64 imm = value.GetImmediateAsU64();
        ASSERT(imm < 0x100);
        return u8(imm);
    }
    u16 GetImmediateU16() const {
        const u64 imm = value.GetImmediateAsU64();
        ASSERT(imm < 0x10000);
        return u16(imm);
    }
    u32 GetImmediateU32() const {
        const u64 imm = value.GetImmediateAsU64();
        ASSERT(imm < 0x100000000);
        return u32(imm);
    }
    u64 GetImmediateU64() const {
        return value.GetImmediateAsU64();
    }
    IR::Cond GetImmediateCond() const {
        ASSERT(IsImmediate() && GetType() == IR::Type::Cond);
        return value.GetCond();
    }
    IR::AccType GetImmediateAccType() const {
        ASSERT(IsImmediate() && GetType() == IR::Type::AccType);
        return value.GetAccType();
    }
private:
    friend class RegAlloc;
    explicit Argument(RegAlloc& reg_alloc) : reg_alloc{reg_alloc} {}
    RegAlloc& reg_alloc;
    IR::Value value;
    bool allocated = false;
};

struct HostLocInfo final {
    std::vector<const IR::Inst*> values;
    size_t locked = 0;
    size_t uses_this_inst = 0;
    size_t accumulated_uses = 0;
    size_t expected_uses = 0;
    bool realized = false;

    bool Contains(const IR::Inst* value) const {
        return std::find(values.begin(), values.end(), value) != values.end();
    }
    void SetupScratchLocation() {
        ASSERT(IsCompletelyEmpty());
        realized = true;
    }
    bool IsCompletelyEmpty() const {
        return values.empty() && !locked && !realized && !accumulated_uses && !expected_uses && !uses_this_inst;
    }
    void UpdateUses();
};

class RegAlloc {
public:
    using ArgumentInfo = std::array<Argument, IR::max_arg_count>;

    explicit RegAlloc(powah::Context& code) : code{code} {}

    ArgumentInfo GetArgumentInfo(IR::Inst* inst);
    bool IsValueLive(IR::Inst* inst) const;
    void DefineAsExisting(IR::Inst* inst, Argument& arg);

    void SpillAll();
    void UpdateAllUses();
    void AssertNoMoreUses() const;

    powah::GPR ScratchGpr();
    powah::GPR UseGpr(Argument& arg);
    powah::GPR UseScratchGpr(Argument& arg);
    void DefineValue(IR::Inst* inst, powah::GPR const gpr) noexcept;
    void DefineValue(IR::Inst* inst, Argument& arg) noexcept;
private:
    std::optional<u32> AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<u32>& order) const;
    void SpillGpr(u32 index);
    void SpillFpr(u32 index);
    u32 FindFreeSpill() const;

    std::optional<HostLoc> ValueLocation(const IR::Inst* value) const;
    HostLocInfo& ValueInfo(HostLoc host_loc);
    HostLocInfo& ValueInfo(const IR::Inst* value);

    powah::Context& code;
    std::array<HostLocInfo, 32> gprs;
    std::array<HostLocInfo, 32> fprs;
    std::array<HostLocInfo, 32> vprs;
    std::array<HostLocInfo, SpillCount> spills;
};

}  // namespace Dynarmic::Backend::RV64
