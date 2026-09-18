// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "dynarmic/frontend/A32/translate/translate_callbacks.h"
#include "dynarmic/interface/A32/a32.h"

template<typename InstructionType_, u32 infinite_loop_u32>
class A32TestEnv : public Dynarmic::A32::UserCallbacks {
public:
    using InstructionType = InstructionType_;
    using RegisterArray = std::array<u32, 16>;
    using ExtRegsArray = std::array<u32, 64>;

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4309)  // C4309: 'static_cast': truncation of constant value
#endif
    static constexpr InstructionType infinite_loop = static_cast<InstructionType>(infinite_loop_u32);
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

    u64 ticks_left = 0;
    bool code_mem_modified_by_guest = false;
    std::vector<InstructionType> code_mem;
    std::map<u32, u8> modified_memory;
    std::vector<std::string> interrupts;

    void PadCodeMem() {
        do {
            code_mem.push_back(infinite_loop);
        } while (code_mem.size() % 2 != 0);
    }

    bool IsInCodeMem(u32 vaddr) const {
        return vaddr < sizeof(InstructionType) * code_mem.size();
    }

    std::optional<std::uint32_t> MemoryReadCode(u32 vaddr) override {
        if (IsInCodeMem(vaddr)) {
            u32 value;
            std::memcpy(&value, &code_mem[vaddr / sizeof(InstructionType)], sizeof(u32));
            return value;
        }
        return infinite_loop_u32;  // B .
    }

    u64 MemoryRead(u32 vaddr, size_t size) override {
        switch (size) {
        case sizeof(u64):
            return MemoryRead(vaddr, sizeof(u32))
                | MemoryRead(vaddr + sizeof(u32), sizeof(u32)) << 32;
        case sizeof(u32):
            return MemoryRead(vaddr, sizeof(u16))
                | MemoryRead(vaddr + sizeof(u16), sizeof(u16)) << 16;
        case sizeof(u16):
            return MemoryRead(vaddr, sizeof(u8))
                | MemoryRead(vaddr + sizeof(u8), sizeof(u8)) << 8;
        case sizeof(u8): {
            if (IsInCodeMem(vaddr))
                return reinterpret_cast<u8*>(code_mem.data())[vaddr];
            if (auto iter = modified_memory.find(vaddr); iter != modified_memory.end())
                return iter->second;
            return u8(vaddr);
        }
        default:
            std::abort();
        }
    }

    void MemoryWrite(Dynarmic::A32::VAddr vaddr, u64 value, size_t size) override {
        switch (size) {
        case sizeof(u64):
            MemoryWrite(vaddr, u32(value), sizeof(u32));
            MemoryWrite(vaddr + 4, u32(value >> 32), sizeof(u32));
            break;
        case sizeof(u32):
            MemoryWrite(vaddr, u16(value), sizeof(u16));
            MemoryWrite(vaddr + 2, u16(value >> 16), sizeof(u16));
            break;
        case sizeof(u16):
            MemoryWrite(vaddr, u8(value), sizeof(u8));
            MemoryWrite(vaddr + 1, u8(value >> 8), sizeof(u8));
            break;
        case sizeof(u8):
            if (vaddr < code_mem.size() * sizeof(u32))
                code_mem_modified_by_guest = true;
            modified_memory[vaddr] = value;
            break;
        default:
            std::abort();
        }
    }

    void CallSVC(std::uint32_t swi) override {
        UNREACHABLE(); //ASSERT(false && "CallSVC({})", swi);
    }

    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception /*exception*/) override {
        UNREACHABLE(); //ASSERT(false && "ExceptionRaised({:08x}) code = {:08x}", pc, *MemoryReadCode(pc));
    }

    void AddTicks(std::uint64_t ticks) override {
        if (ticks > ticks_left) {
            ticks_left = 0;
            return;
        }
        ticks_left -= ticks;
    }
    std::uint64_t GetTicksRemaining() override {
        return ticks_left;
    }
};

using ArmTestEnv = A32TestEnv<u32, 0xEAFFFFFE>;
using ThumbTestEnv = A32TestEnv<u16, 0xE7FEE7FE>;

class A32FastmemTestEnv final : public Dynarmic::A32::UserCallbacks {
public:
    u64 ticks_left = 0;
    char* backing_memory = nullptr;

    explicit A32FastmemTestEnv(char* addr)
            : backing_memory(addr) {}

    template<typename T>
    T read(std::uint32_t vaddr) {
        T value;
        memcpy(&value, backing_memory + vaddr, sizeof(T));
        return value;
    }
    template<typename T>
    void write(std::uint32_t vaddr, const T& value) {
        memcpy(backing_memory + vaddr, &value, sizeof(T));
    }

    std::optional<std::uint32_t> MemoryReadCode(std::uint32_t vaddr) override {
        return read<std::uint32_t>(vaddr);
    }

    u64 MemoryRead(u32 vaddr, size_t size) override {
        switch (size) {
        case sizeof(u64): return read<u64>(vaddr);
        case sizeof(u32): return read<u32>(vaddr);
        case sizeof(u16): return read<u16>(vaddr);
        case sizeof(u8): return read<u8>(vaddr);
        default:
            std::abort();
        }
    }

    void MemoryWrite(Dynarmic::A32::VAddr vaddr, std::uint64_t value, size_t size) override {
        switch (size) {
        case sizeof(u64): return write<u64>(vaddr, u64(value));
        case sizeof(u32): return write<u32>(vaddr, u32(value));
        case sizeof(u16): return write<u16>(vaddr, u16(value));
        case sizeof(u8): return write<u8>(vaddr, u8(value));
        default: std::abort();
        }
    }

    bool MemoryWriteExclusive8(Dynarmic::A32::VAddr vaddr, std::uint8_t value, [[maybe_unused]] std::uint8_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u8));
        return true;
    }
    bool MemoryWriteExclusive16(Dynarmic::A32::VAddr vaddr, std::uint16_t value, [[maybe_unused]] std::uint16_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u16));
        return true;
    }
    bool MemoryWriteExclusive32(Dynarmic::A32::VAddr vaddr, std::uint32_t value, [[maybe_unused]] std::uint32_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u32));
        return true;
    }
    bool MemoryWriteExclusive64(Dynarmic::A32::VAddr vaddr, std::uint64_t value, [[maybe_unused]] std::uint64_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u64));
        return true;
    }

    void CallSVC(std::uint32_t swi) override {
        UNREACHABLE(); //ASSERT(false && "CallSVC({})", swi);
    }

    void ExceptionRaised(std::uint32_t pc, Dynarmic::A32::Exception) override {
        UNREACHABLE(); //ASSERT(false && "ExceptionRaised({:016x})", pc);
    }

    void AddTicks(std::uint64_t ticks) override {
        if (ticks > ticks_left) {
            ticks_left = 0;
            return;
        }
        ticks_left -= ticks;
    }
    std::uint64_t GetTicksRemaining() override {
        return ticks_left;
    }
};
