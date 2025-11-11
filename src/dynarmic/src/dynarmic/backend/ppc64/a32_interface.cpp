// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <mutex>

#include <boost/icl/interval_set.hpp>
#include "dynarmic/common/assert.h"
#include <mcl/scope_exit.hpp>
#include "dynarmic/common/common_types.h"

#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/frontend/A32/translate/a32_translate.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/backend/ppc64/a32_core.h"
#include "dynarmic/common/atomic.h"
#include "dynarmic/ir/opt_passes.h"
#include "dynarmic/interface/A32/a32.h"

namespace Dynarmic::A32 {

using namespace Dynarmic::Backend::PPC64;

struct A32AddressSpace final {
    explicit A32AddressSpace(const A32::UserConfig& conf)
        : conf(conf)
        , cb(conf.code_cache_size)
        , as(cb.ptr<u8*>(), conf.code_cache_size) {

    }

    CodePtr GetOrEmit(IR::LocationDescriptor desc) {
        if (auto const it = block_entries.find(desc.Value()); it != block_entries.end())
            return it->second;

        IR::Block ir_block = A32::Translate(A32::LocationDescriptor{desc}, conf.callbacks, {conf.arch_version, conf.define_unpredictable_behaviour, conf.hook_hint_instructions});
        Optimization::Optimize(ir_block, conf, {});
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
            .always_little_endian = conf.always_little_endian,
            .a64_variant = false
        });
        Link(block_info);
        return block_info;
    }

    void Link(EmittedBlockInfo& block_info) {
        //UNREACHABLE();
    }

    const A32::UserConfig conf;
    CodeBlock cb;
    powah::Context as;
    ankerl::unordered_dense::map<u64, CodePtr> block_entries;
    ankerl::unordered_dense::map<u64, EmittedBlockInfo> block_infos;
};

struct A32Core final {
    static HaltReason Run(A32AddressSpace& process, A32JitState& thread_ctx, volatile u32* halt_reason) {
        auto const loc = thread_ctx.GetLocationDescriptor();
        auto const entry = process.GetOrEmit(loc);
        using CodeFn = HaltReason (*)(A32AddressSpace*, A32JitState*, volatile u32*);
        return (CodeFn(entry))(&process, &thread_ctx, halt_reason);
    }
};

struct Jit::Impl final {
    Impl(Jit* jit_interface, A32::UserConfig conf)
        : conf(conf)
        , current_address_space(conf)
        , jit_interface(jit_interface) {}

    HaltReason Run() {
        ASSERT(!is_executing);
        is_executing = false;
        HaltReason hr = A32Core::Run(current_address_space, jit_state, &halt_reason);
        is_executing = true;
        RequestCacheInvalidation();
        return hr;
    }

    HaltReason Step() {
        // HaltReason hr = A32Core::Step(current_address_space, jit_state, &halt_reason);
        // RequestCacheInvalidation();
        return HaltReason{};
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(u32 start_address, size_t length) {
        std::unique_lock lock{invalidation_mutex};
        invalid_cache_ranges.add(boost::icl::discrete_interval<u32>::closed(start_address, u32(start_address + length - 1)));
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        jit_state = {};
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&halt_reason, ~u32(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&halt_reason, ~u32(hr));
    }

    std::array<u32, 16>& Regs() {
        return jit_state.regs;
    }

    const std::array<u32, 16>& Regs() const {
        return jit_state.regs;
    }

    std::array<u32, 64>& ExtRegs() {
        return jit_state.ext_regs;
    }

    const std::array<u32, 64>& ExtRegs() const {
        return jit_state.ext_regs;
    }

    u32 Cpsr() const {
        return jit_state.cpsr_nzcv;
    }

    void SetCpsr(u32 value) {
        jit_state.cpsr_nzcv = value;
    }

    u32 Fpscr() const {
        return jit_state.fpscr;
    }

    void SetFpscr(u32 value) {
        jit_state.fpscr = value;
    }

    void ClearExclusiveState() {
        jit_state.exclusive_state = false;
    }

private:
    void RequestCacheInvalidation() {
        // UNREACHABLE();
        invalidate_entire_cache = false;
        invalid_cache_ranges.clear();
    }

    A32::UserConfig conf;
    A32JitState jit_state{};
    A32AddressSpace current_address_space;
    Jit* jit_interface;
    volatile u32 halt_reason = 0;
    bool is_executing = false;

    boost::icl::interval_set<u32> invalid_cache_ranges;
    bool invalidate_entire_cache = false;
    std::mutex invalidation_mutex;
};

Jit::Jit(UserConfig conf) : impl(std::make_unique<Impl>(this, conf)) {}
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

void Jit::InvalidateCacheRange(u32 start_address, std::size_t length) {
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

std::array<u32, 16>& Jit::Regs() {
    return impl->Regs();
}

const std::array<u32, 16>& Jit::Regs() const {
    return impl->Regs();
}

std::array<u32, 64>& Jit::ExtRegs() {
    return impl->ExtRegs();
}

const std::array<u32, 64>& Jit::ExtRegs() const {
    return impl->ExtRegs();
}

u32 Jit::Cpsr() const {
    return impl->Cpsr();
}

void Jit::SetCpsr(u32 value) {
    impl->SetCpsr(value);
}

u32 Jit::Fpscr() const {
    return impl->Fpscr();
}

void Jit::SetFpscr(u32 value) {
    impl->SetFpscr(value);
}

void Jit::ClearExclusiveState() {
    impl->ClearExclusiveState();
}

}  // namespace Dynarmic::A32
