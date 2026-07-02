// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <ankerl/unordered_dense.h>
#include "common/assert.h"
#include "common/common_types.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"

using Vector = Dynarmic::A64::Vector;

class A64TestEnv : public Dynarmic::A64::UserCallbacks {
public:
    ankerl::unordered_dense::map<u64, u8> modified_memory;
    std::vector<u32> code_mem;
    u64 ticks_left = 0;
    u64 code_mem_start_address = 0;
    bool code_mem_modified_by_guest = false;

    bool IsInCodeMem(u64 vaddr) const {
        return vaddr >= code_mem_start_address && vaddr < code_mem_start_address + code_mem.size() * 4;
    }

    std::optional<std::uint32_t> MemoryReadCode(u64 vaddr) override {
        if (!IsInCodeMem(vaddr))
            return 0x14000000;  // B .
        const size_t index = (vaddr - code_mem_start_address) / 4;
        return code_mem[index];
    }

    u64 MemoryRead(u64 vaddr, size_t size) override {
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
                return reinterpret_cast<u8*>(code_mem.data())[vaddr - code_mem_start_address];
            if (auto const it = modified_memory.find(vaddr); it != modified_memory.end())
                return it->second;
            return u8(vaddr);
        }
        default:
            std::abort();
        }
    }

    Vector MemoryRead128(u64 vaddr) override {
        return {
            MemoryRead(vaddr, sizeof(u64)),
            MemoryRead(vaddr + 8, sizeof(u64))
        };
    }

    void MemoryWrite(Dynarmic::A64::VAddr vaddr, u64 value, size_t size) override {
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
            if (IsInCodeMem(vaddr)) {
                code_mem_modified_by_guest = true;
            }
            modified_memory[vaddr] = value;
            break;
        default:
            std::abort();
        }
    }
    void MemoryWrite128(u64 vaddr, Vector value) override {
        MemoryWrite(vaddr, value[0], sizeof(u64));
        MemoryWrite(vaddr + 8, value[1], sizeof(u64));
    }

    bool MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, [[maybe_unused]] std::uint8_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u8));
        return true;
    }
    bool MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, [[maybe_unused]] std::uint16_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u16));
        return true;
    }
    bool MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, [[maybe_unused]] std::uint32_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u32));
        return true;
    }
    bool MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, [[maybe_unused]] std::uint64_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u64));
        return true;
    }
    bool MemoryWriteExclusive128(u64 vaddr, Vector value, [[maybe_unused]] Vector expected) override {
        MemoryWrite128(vaddr, value);
        return true;
    }

    void CallSVC(std::uint32_t swi) override {
        UNREACHABLE(); //ASSERT(false && "CallSVC({})", swi);
    }

    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception /*exception*/) override {
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
    std::uint64_t GetCNTPCT() override {
        return 0x10000000000 - ticks_left;
    }
};

class A64FastmemTestEnv final : public Dynarmic::A64::UserCallbacks {
public:
    u64 ticks_left = 0;
    char* backing_memory = nullptr;
    bool ignore_invalid_insn = false;

    explicit A64FastmemTestEnv(char* addr) : backing_memory(addr) {}

    template<typename T>
    T read(u64 vaddr) {
        T value;
        std::memcpy(&value, backing_memory + vaddr, sizeof(T));
        return value;
    }
    template<typename T>
    void write(u64 vaddr, const T& value) {
        std::memcpy(backing_memory + vaddr, &value, sizeof(T));
    }

    std::optional<std::uint32_t> MemoryReadCode(u64 vaddr) override {
        return read<std::uint32_t>(vaddr);
    }

    u64 MemoryRead(u64 vaddr, size_t size) override {
        switch (size) {
        case sizeof(u64): return read<u64>(vaddr);
        case sizeof(u32): return read<u32>(vaddr);
        case sizeof(u16): return read<u16>(vaddr);
        case sizeof(u8): return read<u8>(vaddr);
        default: std::abort();
        }
    }
    Vector MemoryRead128(u64 vaddr) override {
        return read<Vector>(vaddr);
    }

    void MemoryWrite(u64 vaddr, std::uint64_t value, size_t size) override {
        switch (size) {
        case sizeof(u64): return write<u64>(vaddr, u64(value));
        case sizeof(u32): return write<u32>(vaddr, u32(value));
        case sizeof(u16): return write<u16>(vaddr, u16(value));
        case sizeof(u8): return write<u8>(vaddr, u8(value));
        default: std::abort();
        }
    }
    void MemoryWrite128(u64 vaddr, Vector value) override {
        write(vaddr, value);
    }

    bool MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, [[maybe_unused]] std::uint8_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u8));
        return true;
    }
    bool MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, [[maybe_unused]] std::uint16_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u16));
        return true;
    }
    bool MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, [[maybe_unused]] std::uint32_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u32));
        return true;
    }
    bool MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, [[maybe_unused]] std::uint64_t expected) override {
        MemoryWrite(vaddr, value, sizeof(u64));
        return true;
    }
    bool MemoryWriteExclusive128(u64 vaddr, Vector value, [[maybe_unused]] Vector expected) override {
        MemoryWrite128(vaddr, value);
        return true;
    }

    void CallSVC(std::uint32_t swi) override {
        UNREACHABLE(); //ASSERT(false && "CallSVC({})", swi);
    }

    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception) override {
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
    std::uint64_t GetCNTPCT() override {
        return 0x10000000000 - ticks_left;
    }
};
