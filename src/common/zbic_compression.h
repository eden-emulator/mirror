// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <vector>
#include "common/common_types.h"

namespace Common::Compression {

[[nodiscard]] bool IsZBIC(const void* src, size_t src_size);

[[nodiscard]] std::vector<u8> DecompressDataZBIC(std::span<const u8> compressed, std::size_t uncompressed_size);

[[nodiscard]] int DecompressDataZBIC(void* dst, size_t dst_size, const void* src, size_t src_size);

} // namespace Common::Compression
