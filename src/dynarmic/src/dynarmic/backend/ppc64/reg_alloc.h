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
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::Backend::PPC64 {

class RegAlloc;

enum class HostLoc : uint8_t {
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9,
    R10, R11, R12, R13, R14, R15, R16, R17, R18, R19,
    R20, R21, R22, R23, R24, R25, R26, R27, R28, R29,
    R30, R31,
    VR0, VR1, VR2, VR3, VR4, VR5, VR6, VR7, VR8, VR9,
    VR10, VR11, VR12, VR13, VR14, VR15, VR16, VR17, VR18, VR19,
    VR20, VR21, VR22, VR23, VR24, VR25, VR26, VR27, VR28, VR29,
    VR30, VR31,
    FirstSpill,
};

struct Argument {
public:
    using copyable_reference = std::reference_wrapper<Argument>;

    IR::Type GetType() const;
    bool IsImmediate() const;

    bool GetImmediateU1() const;
    u8 GetImmediateU8() const;
    u16 GetImmediateU16() const;
    u32 GetImmediateU32() const;
    u64 GetImmediateU64() const;
    IR::Cond GetImmediateCond() const;
    IR::AccType GetImmediateAccType() const;

private:
    friend class RegAlloc;
    explicit Argument(RegAlloc& reg_alloc)
            : reg_alloc{reg_alloc} {}

    bool allocated = false;
    RegAlloc& reg_alloc;
    IR::Value value;
};

struct HostLocInfo final {
    std::vector<const IR::Inst*> values;
    size_t locked = 0;
    size_t uses_this_inst = 0;
    size_t accumulated_uses = 0;
    size_t expected_uses = 0;
    bool realized = false;

    bool Contains(const IR::Inst*) const;
    void SetupScratchLocation();
    bool IsCompletelyEmpty() const;
    void UpdateUses();
};

class RegAlloc {
public:
    using ArgumentInfo = std::array<Argument, IR::max_arg_count>;

    explicit RegAlloc(powah::Context& as) : as{as} {}

    ArgumentInfo GetArgumentInfo(IR::Inst* inst);
    bool IsValueLive(IR::Inst* inst) const;
    void DefineAsExisting(IR::Inst* inst, Argument& arg);

    void SpillAll();
    void UpdateAllUses();
    void AssertNoMoreUses() const;

    powah::GPR ScratchGpr(std::optional<std::initializer_list<HostLoc>> desired_locations = {});
    void Use(Argument& arg, HostLoc host_loc);
    void UseScratch(Argument& arg, HostLoc host_loc);
    powah::GPR UseGpr(Argument& arg);
    powah::GPR UseScratchGpr(Argument& arg);
    void DefineValue(IR::Inst* inst, powah::GPR const gpr) noexcept;
    void DefineValue(IR::Inst* inst, Argument& arg) noexcept;
private:
    u32 AllocateRegister(const std::array<HostLocInfo, 32>& regs, const std::vector<u32>& order) const;
    void SpillGpr(u32 index);
    void SpillFpr(u32 index);
    u32 FindFreeSpill() const;

    std::optional<HostLoc> ValueLocation(const IR::Inst* value) const;
    HostLocInfo& ValueInfo(HostLoc host_loc);
    HostLocInfo& ValueInfo(const IR::Inst* value);

    powah::Context& as;
    std::array<HostLocInfo, 32> gprs;
    std::array<HostLocInfo, 32> fprs;
    std::array<HostLocInfo, SpillCount> spills;
};

}  // namespace Dynarmic::Backend::RV64
