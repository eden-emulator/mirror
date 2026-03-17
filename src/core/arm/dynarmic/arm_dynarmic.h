// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <dynarmic/interface/halt_reason.h>
#include "core/arm/arm_interface.h"

namespace Core {

inline constexpr Dynarmic::HaltReason StepThread = Dynarmic::HaltReason::Step;
inline constexpr Dynarmic::HaltReason DataAbort = Dynarmic::HaltReason::MemoryAbort;
inline constexpr Dynarmic::HaltReason BreakLoop = Dynarmic::HaltReason::UserDefined2;
inline constexpr Dynarmic::HaltReason SupervisorCall = Dynarmic::HaltReason::UserDefined3;
inline constexpr Dynarmic::HaltReason InstructionBreakpoint = Dynarmic::HaltReason::UserDefined4;
inline constexpr Dynarmic::HaltReason PrefetchAbort = Dynarmic::HaltReason::UserDefined6;

[[nodiscard]] inline constexpr HaltReason TranslateHaltReason(Dynarmic::HaltReason hr) {
    static_assert(u64(HaltReason::StepThread) == u64(StepThread));
    static_assert(u64(HaltReason::DataAbort) == u64(DataAbort));
    static_assert(u64(HaltReason::BreakLoop) == u64(BreakLoop));
    static_assert(u64(HaltReason::SupervisorCall) == u64(SupervisorCall));
    static_assert(u64(HaltReason::InstructionBreakpoint) == u64(InstructionBreakpoint));
    static_assert(u64(HaltReason::PrefetchAbort) == u64(PrefetchAbort));
    return HaltReason(hr);
}

} // namespace Core
