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

namespace Dynarmic::Backend::PPC64 {

A32AddressSpace::A32AddressSpace(const A32::UserConfig& conf)
    : conf(conf)
    , cb(conf.code_cache_size)
    , as(cb.ptr<u8*>(), conf.code_cache_size) {

}

IR::Block A32AddressSpace::GenerateIR(IR::LocationDescriptor descriptor) const {
    IR::Block ir_block = A32::Translate(A32::LocationDescriptor{descriptor}, conf.callbacks, {conf.arch_version, conf.define_unpredictable_behaviour, conf.hook_hint_instructions});
    Optimization::Optimize(ir_block, conf, {});
    return ir_block;
}

CodePtr A32AddressSpace::Get(IR::LocationDescriptor descriptor) {
    auto const it = block_entries.find(descriptor.Value());
    return it != block_entries.end() ? it->second : nullptr;
}

CodePtr A32AddressSpace::GetOrEmit(IR::LocationDescriptor descriptor) {
    if (CodePtr block_entry = Get(descriptor); block_entry != nullptr)
        return block_entry;

    IR::Block ir_block = GenerateIR(descriptor);
    const EmittedBlockInfo block_info = Emit(std::move(ir_block));

    block_infos.insert_or_assign(descriptor.Value(), block_info);
    block_entries.insert_or_assign(descriptor.Value(), block_info.entry_point);
    return block_info.entry_point;
}

void A32AddressSpace::ClearCache() {
    block_entries.clear();
    block_infos.clear();
}

EmittedBlockInfo A32AddressSpace::Emit(IR::Block block) {
    EmittedBlockInfo block_info = EmitPPC64(as, std::move(block), {
        .enable_cycle_counting = conf.enable_cycle_counting,
        .always_little_endian = conf.always_little_endian,
    });
    Link(block_info);
    return block_info;
}

void A32AddressSpace::Link(EmittedBlockInfo& block_info) {
    //UNREACHABLE();
}

}

namespace Dynarmic::A32 {

using namespace Dynarmic::Backend::PPC64;

struct Jit::Impl final {
    Impl(Jit* jit_interface, A32::UserConfig conf)
        : jit_interface(jit_interface)
        , conf(conf)
        , current_address_space(conf)
        , core(conf) {}

    HaltReason Run() {
        HaltReason hr = core.Run(current_address_space, current_state, &halt_reason);
        RequestCacheInvalidation();
        return hr;
    }

    HaltReason Step() {
        RequestCacheInvalidation();
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
        current_state = {};
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&halt_reason, ~u32(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&halt_reason, ~u32(hr));
    }

    std::array<u32, 16>& Regs() {
        return current_state.regs;
    }

    const std::array<u32, 16>& Regs() const {
        return current_state.regs;
    }

    std::array<u32, 64>& ExtRegs() {
        return current_state.ext_regs;
    }

    const std::array<u32, 64>& ExtRegs() const {
        return current_state.ext_regs;
    }

    u32 Cpsr() const {
        return current_state.cpsr_nzcv;
    }

    void SetCpsr(u32 value) {
        current_state.cpsr_nzcv = value;
    }

    u32 Fpscr() const {
        return current_state.fpscr;
    }

    void SetFpscr(u32 value) {
        current_state.fpscr = value;
    }

    void ClearExclusiveState() {
        current_state.exclusive_state = false;
    }

private:
    void RequestCacheInvalidation() {
        // UNREACHABLE();
        invalidate_entire_cache = false;
        invalid_cache_ranges.clear();
    }

    Jit* jit_interface;
    A32::UserConfig conf;
    A32JitState current_state{};
    A32AddressSpace current_address_space;
    A32Core core;
    volatile u32 halt_reason = 0;
    std::mutex invalidation_mutex;
    boost::icl::interval_set<u32> invalid_cache_ranges;
    bool invalidate_entire_cache = false;
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
