// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2023 merryhime <https://mary.rs>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>

#include "core/arm/nce/interpreter_visitor.h"

namespace Core {

namespace {

u64 SignExtend(u64 value, size_t bitsize, size_t regsize) {
    const u64 mask = u64{1} << (bitsize - 1);
    const u64 extended = ((value & ((u64{1} << bitsize) - 1)) ^ mask) - mask;
    if (regsize == 64) {
        return extended;
    }
    return static_cast<u32>(extended);
}

u128 VectorGetElement(u128 value, size_t bitsize) {
    if (bitsize >= 128) {
        return value;
    }
    if (bitsize >= 64) {
        return {value[0], 0};
    }
    return {value[0] & ((u64{1} << bitsize) - 1), 0};
}

} // namespace

u64 InterpreterVisitor::ExtendReg(Reg reg, Imm<3> option, u8 shift) {
    const size_t len = size_t{8} << option.Bits<0, 1, size_t>();
    u64 val = this->GetReg(reg);
    if (len < 64) {
        val &= (u64{1} << len) - 1;
        if (option.Bit<2>()) {
            val = SignExtend(val, len, 64);
        }
    }
    return val << shift;
}

bool InterpreterVisitor::Ordered(size_t size, bool load, Reg Rn, Reg Rt) {
    const size_t dbytes = size_t{1} << size;
    const u64 address = this->GetRegSp(Rn);
    if (load) {
        u64 value = 0;
        m_memory.ReadBlock(address, &value, dbytes);
        this->SetReg(Rt, value);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return true;
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    u64 value = this->GetReg(Rt);
    m_memory.WriteBlock(address, &value, dbytes);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return true;
}

bool InterpreterVisitor::LoadLiteral(bool wide, Imm<19> imm19, Reg Rt) {
    size_t size = 4;
    if (wide) {
        size = 8;
    }
    u64 data = 0;
    m_memory.ReadBlock(m_pc + (imm19.SignExtend<u64>() << 2), &data, size);
    this->SetReg(Rt, data);
    return true;
}

bool InterpreterVisitor::LoadLiteralSimd(Imm<2> opc, Imm<19> imm19, Vec Vt) {
    if (opc == 0b11) {
        return false;
    }
    u128 data{};
    m_memory.ReadBlock(m_pc + (imm19.SignExtend<u64>() << 2), &data,
                       size_t{4} << opc.ZeroExtend<size_t>());
    this->SetVec(Vt, data);
    return true;
}

bool InterpreterVisitor::Pair(Imm<2> opc, bool not_postindex, bool wback, bool load, Imm<7> imm7,
                              Reg Rt2, Reg Rn, Reg Rt) {
    if (!not_postindex && !wback) {
        return false;
    }
    const bool signed_ = opc.Bit<0>();
    if (opc == 0b11 || (!load && signed_)) {
        return false;
    }
    if (load && (Rt == Rt2 || (wback && (Rt == Rn || Rt2 == Rn) && Rn != Reg::R31))) {
        return false;
    }
    if (!load && wback && (Rt == Rn || Rt2 == Rn) && Rn != Reg::R31) {
        return false;
    }

    const size_t scale = 2 + opc.Bit<1>();
    const size_t dbytes = size_t{1} << scale;
    const u64 offset = imm7.SignExtend<u64>() << scale;
    u64 address = this->GetRegSp(Rn);
    if (not_postindex) {
        address += offset;
    }
    if (load) {
        u64 data1 = 0, data2 = 0;
        m_memory.ReadBlock(address, &data1, dbytes);
        m_memory.ReadBlock(address + dbytes, &data2, dbytes);
        if (signed_) {
            data1 = SignExtend(data1, dbytes * 8, 64);
            data2 = SignExtend(data2, dbytes * 8, 64);
        }
        this->SetReg(Rt, data1);
        this->SetReg(Rt2, data2);
    } else {
        u64 data1 = this->GetReg(Rt);
        u64 data2 = this->GetReg(Rt2);
        m_memory.WriteBlock(address, &data1, dbytes);
        m_memory.WriteBlock(address + dbytes, &data2, dbytes);
    }
    if (wback) {
        if (!not_postindex) {
            address += offset;
        }
        this->SetRegSp(Rn, address);
    }
    return true;
}

bool InterpreterVisitor::PairSimd(Imm<2> opc, bool not_postindex, bool wback, bool load,
                                  Imm<7> imm7, Vec Vt2, Reg Rn, Vec Vt) {
    if (!not_postindex && !wback) {
        return false;
    }
    if (opc == 0b11 || (load && Vt == Vt2)) {
        return false;
    }

    const size_t scale = 2 + opc.ZeroExtend<size_t>();
    const size_t dbytes = size_t{1} << scale;
    const u64 offset = imm7.SignExtend<u64>() << scale;
    u64 address = this->GetRegSp(Rn);
    if (not_postindex) {
        address += offset;
    }
    if (load) {
        u128 data1{}, data2{};
        m_memory.ReadBlock(address, &data1, dbytes);
        m_memory.ReadBlock(address + dbytes, &data2, dbytes);
        this->SetVec(Vt, data1);
        this->SetVec(Vt2, data2);
    } else {
        u128 data1 = VectorGetElement(this->GetVec(Vt), dbytes * 8);
        u128 data2 = VectorGetElement(this->GetVec(Vt2), dbytes * 8);
        m_memory.WriteBlock(address, &data1, dbytes);
        m_memory.WriteBlock(address + dbytes, &data2, dbytes);
    }
    if (wback) {
        if (!not_postindex) {
            address += offset;
        }
        this->SetRegSp(Rn, address);
    }
    return true;
}

bool InterpreterVisitor::RegisterImmediate(bool wback, bool postindex, u64 offset, Imm<2> size,
                                           Imm<2> opc, Reg Rn, Reg Rt) {
    const bool signed_ = opc.Bit<1>();
    if (signed_ && (size == 0b11 || (size == 0b10 && opc.Bit<0>()))) {
        return false;
    }

    const size_t datasize = size_t{8} << size.ZeroExtend<size_t>();
    size_t regsize = 32;
    if (size == 0b11 || (signed_ && !opc.Bit<0>())) {
        regsize = 64;
    }
    u64 address = this->GetRegSp(Rn);
    if (!postindex) {
        address += offset;
    }
    if (signed_ || opc.Bit<0>()) {
        u64 data = 0;
        m_memory.ReadBlock(address, &data, datasize / 8);
        if (signed_) {
            data = SignExtend(data, datasize, regsize);
        }
        this->SetReg(Rt, data);
    } else {
        u64 data = this->GetReg(Rt);
        m_memory.WriteBlock(address, &data, datasize / 8);
    }
    if (wback) {
        if (postindex) {
            address += offset;
        }
        this->SetRegSp(Rn, address);
    }
    return true;
}

bool InterpreterVisitor::RegisterOffset(bool S, Imm<2> size, Imm<1> opc_1, Imm<1> opc_0, Reg Rm,
                                        Imm<3> option, Reg Rn, Reg Rt) {
    if (!option.Bit<1>()) {
        return false;
    }
    const bool high = opc_1 == 1;
    const bool wide = size == 0b11;
    if (high && opc_0 == 1 && (wide || size == 0b10)) {
        return false;
    }
    if (high && wide) {
        return true;
    }

    const size_t scale = size.ZeroExtend<size_t>();
    const size_t datasize = size_t{8} << scale;
    size_t regsize = 32;
    if (wide || (high && opc_0 == 0)) {
        regsize = 64;
    }
    u8 shift = 0;
    if (S) {
        shift = static_cast<u8>(scale);
    }
    const u64 address = this->GetRegSp(Rn) + this->ExtendReg(Rm, option, shift);
    if (high || opc_0 == 1) {
        u64 data = 0;
        m_memory.ReadBlock(address, &data, datasize / 8);
        if (high) {
            data = SignExtend(data, datasize, regsize);
        }
        this->SetReg(Rt, data);
        return true;
    }
    u64 data = this->GetReg(Rt);
    m_memory.WriteBlock(address, &data, datasize / 8);
    return true;
}

bool InterpreterVisitor::SimdImmediate(bool wback, bool postindex, size_t scale, u64 offset,
                                       bool load, Reg Rn, Vec Vt) {
    if (scale > 4) {
        return false;
    }

    const size_t dbytes = size_t{1} << scale;
    u64 address = this->GetRegSp(Rn);
    if (!postindex) {
        address += offset;
    }
    if (load) {
        u128 data{};
        m_memory.ReadBlock(address, &data, dbytes);
        this->SetVec(Vt, data);
    } else {
        u128 data = VectorGetElement(this->GetVec(Vt), dbytes * 8);
        m_memory.WriteBlock(address, &data, dbytes);
    }
    if (wback) {
        if (postindex) {
            address += offset;
        }
        this->SetRegSp(Rn, address);
    }
    return true;
}

bool InterpreterVisitor::SimdOffset(size_t scale, bool S, bool load, Reg Rm, Imm<3> option, Reg Rn,
                                    Vec Vt) {
    if (scale > 4 || !option.Bit<1>()) {
        return false;
    }

    const size_t dbytes = size_t{1} << scale;
    u8 shift = 0;
    if (S) {
        shift = static_cast<u8>(scale);
    }
    const u64 address = this->GetRegSp(Rn) + this->ExtendReg(Rm, option, shift);
    if (load) {
        u128 data{};
        m_memory.ReadBlock(address, &data, dbytes);
        this->SetVec(Vt, data);
        return true;
    }
    u128 data = VectorGetElement(this->GetVec(Vt), dbytes * 8);
    m_memory.WriteBlock(address, &data, dbytes);
    return true;
}

bool InterpreterVisitor::Execute(u32 inst) {
    const Imm<2> size{inst >> 30};
    const Imm<2> opc{(inst >> 22) & 3};
    const Imm<3> option{(inst >> 13) & 7};
    const Imm<19> imm19{(inst >> 5) & 0x7FFFF};
    const Reg Rn = static_cast<Reg>((inst >> 5) & 31);
    const Reg Rt = static_cast<Reg>(inst & 31);
    const Vec Vt = static_cast<Vec>(inst & 31);
    const bool load = (inst & (1U << 22)) != 0;
    const bool not_postindex = (inst & (1U << 11)) != 0;
    const bool scaled = (inst & (1U << 12)) != 0;
    const size_t vscale = ((inst >> 21) & 4) | (inst >> 30);
    const u64 imm9 = Imm<9>{(inst >> 12) & 0x1FF}.SignExtend<u64>();

    switch ((inst >> 24) & 0x3F) {
    case 0b001000:
        if ((inst & 0x00BF7C00) != 0x009F7C00) {
            return false;
        }
        return this->Ordered(size.ZeroExtend<size_t>(), load, Rn, Rt);
    case 0b011000:
        if ((inst & 0x80000000) != 0) {
            return false;
        }
        return this->LoadLiteral((inst & 0x40000000) != 0, imm19, Rt);
    case 0b011100:
        return this->LoadLiteralSimd(size, imm19, Vt);
    case 0b101000:
    case 0b101001:
        return this->Pair(size, (inst & (1U << 24)) != 0, (inst & (1U << 23)) != 0, load,
                          Imm<7>{(inst >> 15) & 0x7F}, static_cast<Reg>((inst >> 10) & 31), Rn, Rt);
    case 0b101100:
    case 0b101101:
        return this->PairSimd(size, (inst & (1U << 24)) != 0, (inst & (1U << 23)) != 0, load,
                              Imm<7>{(inst >> 15) & 0x7F}, static_cast<Vec>((inst >> 10) & 31), Rn,
                              Vt);
    case 0b111000:
        if ((inst & (1U << 21)) != 0) {
            if ((inst & 0x00000C00) != 0x00000800) {
                return false;
            }
            return this->RegisterOffset(scaled, size, Imm<1>{(inst >> 23) & 1},
                                        Imm<1>{(inst >> 22) & 1},
                                        static_cast<Reg>((inst >> 16) & 31), option, Rn, Rt);
        }
        if ((inst & (1U << 10)) != 0) {
            return this->RegisterImmediate(true, !not_postindex, imm9, size, opc, Rn, Rt);
        }
        if (not_postindex) {
            return false;
        }
        return this->RegisterImmediate(false, false, imm9, size, opc, Rn, Rt);
    case 0b111001:
        return this->RegisterImmediate(false, false,
                                       u64{(inst >> 10) & 0xFFF} << size.ZeroExtend<size_t>(), size,
                                       opc, Rn, Rt);
    case 0b111100:
        if ((inst & (1U << 21)) != 0) {
            if ((inst & 0x00000C00) != 0x00000800) {
                return false;
            }
            return this->SimdOffset(vscale, scaled, load, static_cast<Reg>((inst >> 16) & 31),
                                    option, Rn, Vt);
        }
        if ((inst & (1U << 10)) != 0) {
            return this->SimdImmediate(true, !not_postindex, vscale, imm9, load, Rn, Vt);
        }
        if (not_postindex) {
            return false;
        }
        return this->SimdImmediate(false, false, vscale, imm9, load, Rn, Vt);
    case 0b111101:
        return this->SimdImmediate(false, false, vscale, u64{(inst >> 10) & 0xFFF} << vscale, load,
                                   Rn, Vt);
    }
    return false;
}

std::optional<u64> MatchAndExecuteOneInstruction(Core::Memory::Memory& memory, mcontext_t* context,
                                                 fpsimd_context* fpsimd_context) {
    std::span<u64, 31> regs(reinterpret_cast<u64*>(context->regs), 31);
    std::span<u128, 32> vregs(reinterpret_cast<u128*>(fpsimd_context->vregs), 32);
    u64& sp = *reinterpret_cast<u64*>(&context->sp);
    const u64& pc = *reinterpret_cast<u64*>(&context->pc);

    InterpreterVisitor visitor(memory, regs, vregs, sp, pc);
    if (!visitor.Execute(memory.Read32(pc))) {
        return std::nullopt;
    }
    return pc + 4;
}

} // namespace Core
