// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>

namespace VideoCommon {
inline bool NativeDrawDiagnosticsEnabled() {
    static const bool enabled = std::getenv("EDEN_NATIVE_DRAW_DIAGNOSTICS") != nullptr;
    return enabled;
}
inline bool NativeFrameCaptureEnabled() {
    static const bool enabled = std::getenv("EDEN_NATIVE_FRAME_CAPTURE") != nullptr;
    return enabled;
}
inline bool NativeDrawCaptureEnabled() {
    static const bool enabled = std::getenv("EDEN_NATIVE_DRAW_CAPTURE") != nullptr;
    return enabled;
}
} // namespace VideoCommon
