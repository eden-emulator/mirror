// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2023 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/x64/verbose_debugging_output.h"

#include <iterator>

#include <format>
#include <print>

#include "dynarmic/backend/x64/hostloc.h"

namespace Dynarmic::Backend::X64 {

void PrintVerboseDebuggingOutputLine(RegisterData& reg_data, HostLoc hostloc, size_t inst_index, size_t bitsize) {
    std::print("dynarmic debug: %{:05} = ", inst_index);

    Vector value = [&]() -> Vector {
        if (HostLocIsGPR(hostloc)) {
            return {reg_data.gprs[HostLocToReg64(hostloc).getIdx()], 0};
        } else if (HostLocIsXMM(hostloc)) {
            return reg_data.xmms[HostLocToXmm(hostloc).getIdx()];
        } else if (HostLocIsSpill(hostloc)) {
            return (*reg_data.spill)[size_t(hostloc) - size_t(HostLoc::FirstSpill)];
        } else {
            std::print("invalid hostloc! ");
            return {0, 0};
        }
    }();

    switch (bitsize) {
    case 8:
        std::print("{:02x}", value[0] & 0xff);
        break;
    case 16:
        std::print("{:04x}", value[0] & 0xffff);
        break;
    case 32:
        std::print("{:08x}", value[0] & 0xffffffff);
        break;
    case 64:
        std::print("{:016x}", value[0]);
        break;
    case 128:
        std::print("{:016x}{:016x}", value[1], value[0]);
        break;
    default:
        std::print("invalid bitsize!");
        break;
    }

    std::print("\n");
}

}  // namespace Dynarmic::Backend::X64
