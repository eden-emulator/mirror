// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include <powah_emit.hpp>
#include <ankerl/unordered_dense.h>

#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"
#include "dynarmic/backend/ppc64/stack_layout.h"
#include "dynarmic/backend/ppc64/hostloc.h"
#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/value.h"

namespace Dynarmic::Backend::PPC64 {

struct HostLocInfo final {
    std::vector<const IR::Inst*> values;
    size_t uses_this_inst = 0;
    size_t accumulated_uses = 0;
    size_t expected_uses = 0;
    /// @brief Lock usage of this register UNTIL a DefineValue() is issued
    bool locked = false;
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

struct RegAlloc;

/// @brief Allows to use RAII to denote liveness/locking of a given register
/// this basically means that we can use temporals and not need to go thru
/// any weird deallocation stuffs :)
template<typename T> struct RegLock {
    constexpr RegLock(RegAlloc& reg_alloc, T const value) noexcept
        : reg_alloc{reg_alloc}
        , value{value}
    {
        SetLock(true);
    }
    inline ~RegLock() noexcept { SetLock(false); }
    constexpr operator T const&() noexcept { return value; }
    constexpr operator T() const noexcept { return value; }
    inline void SetLock(bool v) noexcept;
    RegAlloc& reg_alloc;
    const T value;
};

struct RegAlloc {
    explicit RegAlloc(powah::Context& code) : code{code} {}
    bool IsValueLive(IR::Inst* inst) const;
    void DefineAsExisting(IR::Inst* inst, IR::Value arg);

    void SpillAll();
    void UpdateAllUses();
    void AssertNoMoreUses() const;

    RegLock<powah::GPR> ScratchGpr();
    RegLock<powah::GPR> UseGpr(IR::Value arg);
    void DefineValue(IR::Inst* inst, powah::GPR const gpr) noexcept;
    void DefineValue(IR::Inst* inst, IR::Value arg) noexcept;
private:
    template<typename T>
    friend struct RegLock;

    std::optional<u32> AllocateRegister(const std::array<HostLocInfo, 32>& regs) const;
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

template<> inline void RegLock<powah::GPR>::SetLock(bool v) noexcept {
    reg_alloc.gprs[value.index].locked = v;
}
template<> inline void RegLock<powah::FPR>::SetLock(bool v) noexcept {
    reg_alloc.fprs[value.index].locked = v;
}
template<> inline void RegLock<powah::VPR>::SetLock(bool v) noexcept {
    reg_alloc.vprs[value.index].locked = v;
}

}  // namespace Dynarmic::Backend::RV64
