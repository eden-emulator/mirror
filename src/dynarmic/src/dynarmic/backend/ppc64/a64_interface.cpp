// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <mutex>

#include <boost/icl/interval_set.hpp>
#include "dynarmic/common/assert.h"
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/translate/a64_translate.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/backend/ppc64/a64_core.h"
#include "dynarmic/common/atomic.h"
#include "dynarmic/ir/opt_passes.h"
#include "dynarmic/interface/A64/a64.h"

namespace Dynarmic::A64 {

using namespace Dynarmic::Backend::PPC64;
using CodePtr = std::uint32_t*;

struct A64AddressSpace final {
    explicit A64AddressSpace(const A64::UserConfig& conf)
        : conf(conf)
        , cb(conf.code_cache_size)
        , as(cb.ptr<u8*>(), conf.code_cache_size) {

    }

    CodePtr GetOrEmit(IR::LocationDescriptor desc) {
        if (auto const it = block_entries.find(desc.Value()); it != block_entries.end())
            return it->second;
        auto const get_code = [this](u64 vaddr) {
            return conf.callbacks->MemoryReadCode(vaddr);
        };
        IR::Block ir_block = A64::Translate(A64::LocationDescriptor{desc}, get_code, {conf.define_unpredictable_behaviour, conf.wall_clock_cntpct});
        Optimization::Optimize(ir_block, conf, {});
        fmt::print("IR:\n{}\n", IR::DumpBlock(ir_block));
        const EmittedBlockInfo block_info = Emit(std::move(ir_block));
        block_infos.insert_or_assign(desc.Value(), block_info);
        block_entries.insert_or_assign(desc.Value(), block_info.entry_point);
        return block_info.entry_point;
    }

    void ClearCache() {
        block_entries.clear();
        block_infos.clear();
    }

    EmittedBlockInfo Emit(IR::Block block) {
        EmittedBlockInfo block_info = EmitPPC64(as, std::move(block), {
            .enable_cycle_counting = conf.enable_cycle_counting,
            .always_little_endian = true,
            .a64_variant = true
        });
        Link(block_info);
        return block_info;
    }

    void Link(EmittedBlockInfo& block_info) {
        // TODO(lizzie): Block linking
        // UNREACHABLE();
    }

    const A64::UserConfig conf;
    CodeBlock cb;
    powah::Context as;
    ankerl::unordered_dense::map<u64, CodePtr> block_entries;
    ankerl::unordered_dense::map<u64, EmittedBlockInfo> block_infos;
};

__attribute__((noinline)) HaltReason test_thunk(A64AddressSpace* a, A64JitState* b, volatile u32* c, void* d) {
    printf("%p,%p,%p,%p\n", a, b, c, d);
    return HaltReason::UserDefined2;
}

struct A64Core final {
    static HaltReason Run(A64AddressSpace& process, A64JitState& thread_ctx, volatile u32* halt_reason) {
        const auto loc = thread_ctx.GetLocationDescriptor();
        const auto entry = process.GetOrEmit(loc);
        using AbstractCodeFn = HaltReason (*)(A64AddressSpace*, A64JitState*, volatile u32*, void (*fn)());
        using CodeFn = HaltReason (*)(A64AddressSpace*, A64JitState*, volatile u32*, AbstractCodeFn fn);
        return (CodeFn(entry))(&process, &thread_ctx, halt_reason, AbstractCodeFn(test_thunk));
    }
};

struct Jit::Impl final {
    Impl(Jit* jit_interface, A64::UserConfig conf)
        : conf(conf)
        , current_address_space(conf)
        , jit_interface(jit_interface) {}

    HaltReason Run() {
        ASSERT(!is_executing);
        is_executing = true;
        HaltReason hr = A64Core::Run(current_address_space, jit_state, &halt_reason);
        is_executing = false;
        RequestCacheInvalidation();
        return hr;
    }

    HaltReason Step() {
        // HaltReason hr = A64Core::Step(current_address_space, jit_state, &halt_reason);
        // RequestCacheInvalidation();
        return HaltReason{};
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(u64 start_address, size_t length) {
        std::unique_lock lock{invalidation_mutex};
        const auto end_address = u64(start_address + length - 1);
        invalid_cache_ranges.add(boost::icl::discrete_interval<u64>::closed(start_address, end_address));
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        ASSERT(!is_executing);
        jit_state = {};
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&jit_state.halt_reason, u32(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&jit_state.halt_reason, ~u32(hr));
    }

    u64 GetSP() const {
        return jit_state.sp;
    }

    void SetSP(u64 value) {
        jit_state.sp = value;
    }

    u64 GetPC() const {
        return jit_state.pc;
    }

    void SetPC(u64 value) {
        jit_state.pc = value;
    }

    u64 GetRegister(size_t index) const {
        return index == 31 ? GetSP() : jit_state.regs.at(index);
    }

    void SetRegister(size_t index, u64 value) {
        if (index == 31)
            return SetSP(value);
        jit_state.regs.at(index) = value;
    }

    std::array<u64, 31> GetRegisters() const {
        return jit_state.regs;
    }

    void SetRegisters(const std::array<u64, 31>& value) {
        jit_state.regs = value;
    }

    Vector GetVector(size_t index) const {
        return {jit_state.vec.at(index * 2), jit_state.vec.at(index * 2 + 1)};
    }

    void SetVector(size_t index, Vector value) {
        jit_state.vec.at(index * 2) = value[0];
        jit_state.vec.at(index * 2 + 1) = value[1];
    }

    std::array<Vector, 32> GetVectors() const {
        std::array<Vector, 32> ret;
        static_assert(sizeof(ret) == sizeof(jit_state.vec));
        std::memcpy(ret.data(), jit_state.vec.data(), sizeof(jit_state.vec));
        return ret;
    }

    void SetVectors(const std::array<Vector, 32>& value) {
        static_assert(sizeof(value) == sizeof(jit_state.vec));
        std::memcpy(jit_state.vec.data(), value.data(), sizeof(jit_state.vec));
    }

    u32 GetFpcr() const {
        return jit_state.fpcr;
    }

    void SetFpcr(u32 value) {
        jit_state.fpcr = value;
    }

    u32 GetFpsr() const {
        return jit_state.fpsr;
    }

    void SetFpsr(u32 value) {
        jit_state.fpsr = value;
    }

    u32 GetPstate() const {
        return jit_state.pstate;
    }

    void SetPstate(u32 value) {
        jit_state.pstate = value;
    }

    void ClearExclusiveState() {
        jit_state.exclusive_state = 0;
    }

    bool IsExecuting() const {
        return is_executing;
    }

    std::string Disassemble() const {
        // const size_t size = reinterpret_cast<const char*>(block_of_code.getCurr()) - reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        // auto const* p = reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        // return Common::DisassemblePPC64(p, p + size);
        return {};
    }

private:
    void RequestCacheInvalidation() {
        // UNREACHABLE();
        invalidate_entire_cache = false;
        invalid_cache_ranges.clear();
    }

    A64::UserConfig conf;
    A64JitState jit_state{};
    A64AddressSpace current_address_space;
    Jit* jit_interface;
    volatile u32 halt_reason = 0;
    bool is_executing = false;

    boost::icl::interval_set<u64> invalid_cache_ranges;
    bool invalidate_entire_cache = false;
    std::mutex invalidation_mutex;
};

Jit::Jit(UserConfig conf) : impl(std::make_unique<Jit::Impl>(this, conf)) {}
Jit::~Jit() = default;

HaltReason Jit::Run() {
    return impl->Run();
}

HaltReason Jit::Step() {
    return impl->Step();
}

void Jit::ClearCache() {
    impl->ClearCache();
}

void Jit::InvalidateCacheRange(u64 start_address, size_t length) {
    impl->InvalidateCacheRange(start_address, length);
}

void Jit::Reset() {
    impl->Reset();
}

void Jit::HaltExecution(HaltReason hr) {
    impl->HaltExecution(hr);
}

void Jit::ClearHalt(HaltReason hr) {
    impl->ClearHalt(hr);
}

u64 Jit::GetSP() const {
    return impl->GetSP();
}

void Jit::SetSP(u64 value) {
    impl->SetSP(value);
}

u64 Jit::GetPC() const {
    return impl->GetPC();
}

void Jit::SetPC(u64 value) {
    impl->SetPC(value);
}

u64 Jit::GetRegister(size_t index) const {
    return impl->GetRegister(index);
}

void Jit::SetRegister(size_t index, u64 value) {
    impl->SetRegister(index, value);
}

std::array<u64, 31> Jit::GetRegisters() const {
    return impl->GetRegisters();
}

void Jit::SetRegisters(const std::array<u64, 31>& value) {
    impl->SetRegisters(value);
}

Vector Jit::GetVector(size_t index) const {
    return impl->GetVector(index);
}

void Jit::SetVector(size_t index, Vector value) {
    impl->SetVector(index, value);
}

std::array<Vector, 32> Jit::GetVectors() const {
    return impl->GetVectors();
}

void Jit::SetVectors(const std::array<Vector, 32>& value) {
    impl->SetVectors(value);
}

u32 Jit::GetFpcr() const {
    return impl->GetFpcr();
}

void Jit::SetFpcr(u32 value) {
    impl->SetFpcr(value);
}

u32 Jit::GetFpsr() const {
    return impl->GetFpsr();
}

void Jit::SetFpsr(u32 value) {
    impl->SetFpsr(value);
}

u32 Jit::GetPstate() const {
    return impl->GetPstate();
}

void Jit::SetPstate(u32 value) {
    impl->SetPstate(value);
}

void Jit::ClearExclusiveState() {
    impl->ClearExclusiveState();
}

bool Jit::IsExecuting() const {
    return impl->IsExecuting();
}

std::string Jit::Disassemble() const {
    return impl->Disassemble();
}

}  // namespace Dynarmic::A64
