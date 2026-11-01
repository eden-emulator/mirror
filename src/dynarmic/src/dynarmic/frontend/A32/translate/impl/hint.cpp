// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"
#include "dynarmic/interface/A32/config.h"

namespace Dynarmic::A32 {

bool TranslatorVisitor::arm_PLD_imm(bool /*add*/, bool R, Reg /*n*/, Imm<12> /*imm12*/) {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    const auto exception = R ? Exception::PreloadData : Exception::PreloadDataWithIntentToWrite;
    return RaiseException(exception);
}

bool TranslatorVisitor::arm_PLD_reg(bool /*add*/, bool R, Reg /*n*/, Imm<5> /*imm5*/, ShiftType /*shift*/, Reg /*m*/) {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    const auto exception = R ? Exception::PreloadData : Exception::PreloadDataWithIntentToWrite;
    return RaiseException(exception);
}

bool TranslatorVisitor::arm_SEV() {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    return RaiseException(Exception::SendEvent);
}

bool TranslatorVisitor::arm_SEVL() {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    return RaiseException(Exception::SendEventLocal);
}

bool TranslatorVisitor::arm_WFE() {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    return RaiseException(Exception::WaitForEvent);
}

bool TranslatorVisitor::arm_WFI() {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    return RaiseException(Exception::WaitForInterrupt);
}

bool TranslatorVisitor::arm_YIELD() {
    if (!conf.hook_hint_instructions) {
        return true;
    }

    return RaiseException(Exception::Yield);
}

}  // namespace Dynarmic::A32
