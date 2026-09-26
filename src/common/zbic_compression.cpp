// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "common/zbic_compression.h"

#define ZSTD_ZBIC_SUPPORT 1
#define ZSTDLIB_VISIBLE static
#define ZSTDLIB_HIDDEN static
#define ZSTDERRORLIB_VISIBLE static
#define ZSTDERRORLIB_HIDDEN static
#undef ZSTD_MULTITHREAD

#include "zstd.h"
#include "zstd.c"

namespace Common::Compression {

bool IsZBIC(std::span<const u8> src) {
    if (src.size() < sizeof(u32)) {
        return false;
    }
    u32 magic = 0;
    std::memcpy(&magic, src.data(), sizeof(u32));
    return magic == ZSTD_MAGICNUMBER; // 0x4349425A ("ZBIC")
}

int DecompressDataZBIC(std::span<u8> dst, std::span<const u8> src) {
    if (dst.empty() || src.empty()) {
        return -1;
    }
    const size_t res = ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size());
    if (ZSTD_isError(res)) {
        return -1;
    }
    return static_cast<int>(res);
}

} // namespace Common::Compression
