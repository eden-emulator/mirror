// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include <mutex>

#include "common/assert.h"
#include "common/fiber.h"
#include "common/virtual_buffer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <boost/context/detail/fcontext.hpp>
#endif

namespace Common {

#ifdef __OPENORBIS__
constexpr size_t DEFAULT_STACK_SIZE = 128 * 4096;
#else
constexpr size_t DEFAULT_STACK_SIZE = 512 * 4096;
#endif
constexpr u32 CANARY_VALUE = 0xDEADBEEF;

#ifdef __EMSCRIPTEN__
struct Fiber::FiberImpl {
    FiberImpl() {}

    u32 canary_1 = CANARY_VALUE;
    std::array<u8, DEFAULT_STACK_SIZE> stack{};
    std::array<u8, DEFAULT_STACK_SIZE> astack{};
    u32 canary_2 = CANARY_VALUE;

    emscripten_fiber_t* context{nullptr};

    std::mutex guard;
    std::function<void()> entry_point;
    std::shared_ptr<Fiber> previous_fiber;
    bool is_thread_fiber = false;
    bool released = false;
};

Fiber::Fiber(std::function<void()>&& entry_point_func) : impl{std::make_unique<FiberImpl>()} {
    impl->entry_point = std::move(entry_point_func);
    emscripten_fiber_init(impl->context, [](void *user_data) -> void {
        auto* fiber = static_cast<Fiber*>(user_data);
        ASSERT(fiber && fiber->impl && fiber->impl->previous_fiber && fiber->impl->previous_fiber->impl);
        ASSERT(fiber->impl->canary_1 == CANARY_VALUE);
        ASSERT(fiber->impl->canary_2 == CANARY_VALUE);
        fiber->impl->previous_fiber->impl->context = fiber->impl->context;
        fiber->impl->previous_fiber->impl->guard.unlock();
        fiber->impl->previous_fiber.reset();
        fiber->impl->entry_point();
        UNREACHABLE();
    }, impl.get(), impl->stack.data(), impl->stack.size(), impl->astack.data(), impl->astack.size());
}

Fiber::Fiber() : impl{std::make_unique<FiberImpl>()} {}

Fiber::~Fiber() {
    if (!impl->released) {
        // Make sure the Fiber is not being used
        const bool locked = impl->guard.try_lock();
        ASSERT(locked && "Destroying a fiber that's still running");
        if (locked) {
            impl->guard.unlock();
        }
    }
}

void Fiber::Exit() {
    ASSERT(impl->is_thread_fiber && "Exiting non main thread fiber");
    if (impl->is_thread_fiber) {
        impl->guard.unlock();
        impl->released = true;
    }
}

void Fiber::YieldTo(std::weak_ptr<Fiber> weak_from, Fiber& to) {
    to.impl->guard.lock();
    to.impl->previous_fiber = weak_from.lock();
    emscripten_fiber_swap(to.impl->context, to.impl->previous_fiber->impl->context);
    // "from" might no longer be valid if the thread was killed
    if (auto from = weak_from.lock()) {
        if (from->impl->previous_fiber == nullptr) {
            ASSERT(false && "previous_fiber is nullptr!");
        } else {
            from->impl->previous_fiber->impl->guard.unlock();
            from->impl->previous_fiber.reset();
        }
    }
}

std::shared_ptr<Fiber> Fiber::ThreadToFiber() {
    std::shared_ptr<Fiber> fiber = std::shared_ptr<Fiber>{new Fiber()};
    fiber->impl->guard.lock();
    fiber->impl->is_thread_fiber = true;
    return fiber;
}
#else
struct Fiber::FiberImpl {
    FiberImpl() {}

    u32 canary_1 = CANARY_VALUE;
    std::array<u8, DEFAULT_STACK_SIZE> stack{};
    u32 canary_2 = CANARY_VALUE;

    boost::context::detail::fcontext_t context{};

    std::mutex guard;
    std::function<void()> entry_point;
    std::shared_ptr<Fiber> previous_fiber;

    u8* stack_limit = nullptr;
    bool is_thread_fiber = false;
    bool released = false;
};

Fiber::Fiber(std::function<void()>&& entry_point_func) : impl{std::make_unique<FiberImpl>()} {
    impl->entry_point = std::move(entry_point_func);
    impl->stack_limit = impl->stack.data();
    u8* stack_base = impl->stack_limit + DEFAULT_STACK_SIZE;
    impl->context = boost::context::detail::make_fcontext(stack_base, impl->stack.size(), [](boost::context::detail::transfer_t transfer) -> void {
        auto* fiber = static_cast<Fiber*>(transfer.data);
        ASSERT(fiber && fiber->impl && fiber->impl->previous_fiber && fiber->impl->previous_fiber->impl);
        ASSERT(fiber->impl->canary_1 == CANARY_VALUE);
        ASSERT(fiber->impl->canary_2 == CANARY_VALUE);
        fiber->impl->previous_fiber->impl->context = transfer.fctx;
        fiber->impl->previous_fiber->impl->guard.unlock();
        fiber->impl->previous_fiber.reset();
        fiber->impl->entry_point();
        UNREACHABLE();
    });
}

Fiber::Fiber() : impl{std::make_unique<FiberImpl>()} {}

Fiber::~Fiber() {
    if (!impl->released) {
        // Make sure the Fiber is not being used
        const bool locked = impl->guard.try_lock();
        ASSERT(locked && "Destroying a fiber that's still running");
        if (locked) {
            impl->guard.unlock();
        }
    }
}

void Fiber::Exit() {
    ASSERT(impl->is_thread_fiber && "Exiting non main thread fiber");
    if (impl->is_thread_fiber) {
        impl->guard.unlock();
        impl->released = true;
    }
}

void Fiber::YieldTo(std::weak_ptr<Fiber> weak_from, Fiber& to) {
    to.impl->guard.lock();
    to.impl->previous_fiber = weak_from.lock();

    auto transfer = boost::context::detail::jump_fcontext(to.impl->context, &to);
    // "from" might no longer be valid if the thread was killed
    if (auto from = weak_from.lock()) {
        if (from->impl->previous_fiber == nullptr) {
            ASSERT(false && "previous_fiber is nullptr!");
        } else {
            from->impl->previous_fiber->impl->context = transfer.fctx;
            from->impl->previous_fiber->impl->guard.unlock();
            from->impl->previous_fiber.reset();
        }
    }
}

std::shared_ptr<Fiber> Fiber::ThreadToFiber() {
    std::shared_ptr<Fiber> fiber = std::shared_ptr<Fiber>{new Fiber()};
    fiber->impl->guard.lock();
    fiber->impl->is_thread_fiber = true;
    return fiber;
}
#endif

} // namespace Common
