// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <mutex>
#include <sys/mman.h>
#include <powah_emit.hpp>
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/backend/ppc64/hostloc.h"
#include "dynarmic/common/spin_lock.h"
#include "dynarmic/common/assert.h"

namespace Dynarmic {

/*
void acquire(atomic_flag* lock) {
    while(atomic_flag_test_and_set_explicit( lock, memory_order_acquire))
        ;
}
*/
void EmitSpinLockLock(powah::Context& code, powah::GPR const ptr, powah::GPR const tmp) {

}

/*
void release(atomic_flag* lock) {
    atomic_flag_clear_explicit(lock, memory_order_release);
}
*/
void EmitSpinLockUnlock(powah::Context& code, powah::GPR const ptr, powah::GPR const tmp) {

}

namespace {

struct SpinLockImpl {
    void Initialize();
    powah::Context code;
    void* page = nullptr;
    void (*lock)(volatile int*);
    void (*unlock)(volatile int*);
};

std::once_flag flag;
SpinLockImpl impl;

void SpinLockImpl::Initialize() {
    page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    ASSERT(page != nullptr);
    code = powah::Context(page, 4096);
    lock = reinterpret_cast<void (*)(volatile int*)>(code.base);
    EmitSpinLockLock(code, Backend::PPC64::ABI_PARAM1, Backend::PPC64::ABI_PARAM2);
    code.BLR();
    unlock = reinterpret_cast<void (*)(volatile int*)>(code.base);
    EmitSpinLockUnlock(code, Backend::PPC64::ABI_PARAM1, Backend::PPC64::ABI_PARAM2);
    code.BLR();
    // TODO: free the page, rework the stupid spinlock API
}

}  // namespace

void SpinLock::Lock() noexcept {
    std::call_once(flag, &SpinLockImpl::Initialize, impl);
    impl.lock(&storage);
}

void SpinLock::Unlock() noexcept {
    std::call_once(flag, &SpinLockImpl::Initialize, impl);
    impl.unlock(&storage);
}

}  // namespace Dynarmic
