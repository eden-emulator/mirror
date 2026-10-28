// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include "common/common_types.h"
#include "video_core/pte_kind.h"

namespace Service::Nvidia::Devices {

inline std::optional<u8> TranslateNativeUncompressedKind(u8 kind, u8 fallback) {
    // NVIDIA's Turing MMU uses 6 for GENERIC_MEMORY and 7 for INVALID:
    // open-gpu-doc/manuals/turing/tu104/dev_mmu.ref.txt. Eden's older kind
    // numbering uses 0xfe for generic memory. Equal kinds need no compression.
    if (kind != fallback) { return std::nullopt; }
    switch (kind) {
    case 0: return static_cast<u8>(Tegra::PTEKind::PITCH);
    case 1: return static_cast<u8>(Tegra::PTEKind::Z16);
    case 3: return static_cast<u8>(Tegra::PTEKind::S8Z24);
    case 6: return static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2);
    default: return std::nullopt;
    }
}

struct NativeMemoryKind {
    u8 backend;
    u8 selected;
};

inline std::optional<NativeMemoryKind> SelectNativeMemoryKind(u8 requested, u8 fallback) {
    if (const auto kind = TranslateNativeUncompressedKind(requested, fallback)) {
        return NativeMemoryKind{*kind, requested};
    }
    // The native caller supplies an uncompressed alternative explicitly. Return
    // that selected native kind so the SDK invalidates its compressed preference.
    if (requested == 8 && fallback == 6) {
        return NativeMemoryKind{static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2), 6};
    }
    // TU104 kind 0x0c is the compressible S8Z24 variant; kind 3 is
    // the caller's uncompressed S8Z24 alternative (dev_mmu.ref.txt).
    if (requested == 0xb && fallback == 1) {
        return NativeMemoryKind{static_cast<u8>(Tegra::PTEKind::Z16), 1};
    }
    if (requested == 0xc && fallback == 3) {
        return NativeMemoryKind{static_cast<u8>(Tegra::PTEKind::S8Z24), 3};
    }
    return std::nullopt;
}

} // namespace Service::Nvidia::Devices
