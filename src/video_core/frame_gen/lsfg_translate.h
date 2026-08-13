// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

#include "common/common_types.h"

namespace VideoCore::FrameGen {

[[nodiscard]] std::vector<u32> TranslateComputeShader(std::span<const u8> dxbc);

} // namespace VideoCore::FrameGen
