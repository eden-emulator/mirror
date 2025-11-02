// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Dynarmic::Backend::PPC64 {

constexpr powah::GPR RJIT = powah::R31;

constexpr std::initializer_list<u32> GPR_ORDER{8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 5, 6, 7, 28, 29, 10, 11, 12, 13, 14, 15, 16, 17};
constexpr std::initializer_list<u32> FPR_ORDER{8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

}  // namespace Dynarmic::Backend::RV64
