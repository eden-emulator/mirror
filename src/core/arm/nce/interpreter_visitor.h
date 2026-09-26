// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2023 merryhime <https://mary.rs>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <span>
#include <signal.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <dynarmic/frontend/A64/a64_types.h>
#include <dynarmic/frontend/imm.h>
#pragma GCC diagnostic pop

#include "core/memory.h"

namespace Core {

class InterpreterVisitor {
public:
    explicit InterpreterVisitor(Core::Memory::Memory& memory, std::span<u64, 31> regs,
                                std::span<u128, 32> fpsimd_regs, u64& sp, const u64& pc)
        : m_memory(memory), m_regs(regs), m_fpsimd_regs(fpsimd_regs), m_sp(sp), m_pc(pc) {}

    bool Execute(u32 inst);

private:
    template <size_t BitSize>
    using Imm = Dynarmic::Imm<BitSize>;
    using Reg = Dynarmic::A64::Reg;
    using Vec = Dynarmic::A64::Vec;

    u128 GetVec(Vec v) const {
        return m_fpsimd_regs[static_cast<u32>(v)];
    }
    void SetVec(Vec v, u128 value) {
        m_fpsimd_regs[static_cast<u32>(v)] = value;
    }
    u64 GetReg(Reg r) const {
        return m_regs[static_cast<u32>(r)];
    }
    void SetReg(Reg r, u64 value) {
        m_regs[static_cast<u32>(r)] = value;
    }
    u64 GetRegSp(Reg r) const {
        if (r == Reg::SP) {
            return m_sp;
        }
        return m_regs[static_cast<u32>(r)];
    }
    void SetRegSp(Reg r, u64 value) {
        if (r == Reg::SP) {
            m_sp = value;
            return;
        }
        m_regs[static_cast<u32>(r)] = value;
    }

    u64 ExtendReg(Reg reg, Imm<3> option, u8 shift);

    bool Ordered(size_t size, bool load, Reg Rn, Reg Rt);
    bool LoadLiteral(bool wide, Imm<19> imm19, Reg Rt);
    bool LoadLiteralSimd(Imm<2> opc, Imm<19> imm19, Vec Vt);
    bool Pair(Imm<2> opc, bool not_postindex, bool wback, bool load, Imm<7> imm7, Reg Rt2, Reg Rn,
              Reg Rt);
    bool PairSimd(Imm<2> opc, bool not_postindex, bool wback, bool load, Imm<7> imm7, Vec Vt2,
                  Reg Rn, Vec Vt);
    bool RegisterImmediate(bool wback, bool postindex, u64 offset, Imm<2> size, Imm<2> opc, Reg Rn,
                           Reg Rt);
    bool RegisterOffset(bool S, Imm<2> size, Imm<1> opc_1, Imm<1> opc_0, Reg Rm, Imm<3> option,
                        Reg Rn, Reg Rt);
    bool SimdImmediate(bool wback, bool postindex, size_t scale, u64 offset, bool load, Reg Rn,
                       Vec Vt);
    bool SimdOffset(size_t scale, bool S, bool load, Reg Rm, Imm<3> option, Reg Rn, Vec Vt);

    Core::Memory::Memory& m_memory;
    std::span<u64, 31> m_regs;
    std::span<u128, 32> m_fpsimd_regs;
    u64& m_sp;
    const u64& m_pc;
};

std::optional<u64> MatchAndExecuteOneInstruction(Core::Memory::Memory& memory, mcontext_t* context,
                                                 fpsimd_context* fpsimd_context);

} // namespace Core
