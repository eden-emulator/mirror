// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <vector>

#include "common/common_types.h"

namespace Common::Compression {

/**
 * Compresses a source memory region with Zstandard and returns the compressed data in a vector.
 *
 * @param source            The uncompressed source memory region.
 * @param source_size       The size of the uncompressed source memory region.
 * @param compression_level The used compression level. Should be between 1 and 22.
 *
 * @return the compressed data.
 */
[[nodiscard]] std::vector<u8> CompressDataZSTD(const u8* source, std::size_t source_size,
                                               s32 compression_level);

/**
 * Compresses a source memory region with Zstandard with the default compression level and returns
 * the compressed data in a vector.
 *
 * @param source      The uncompressed source memory region.
 * @param source_size The size of the uncompressed source memory region.
 *
 * @return the compressed data.
 */
[[nodiscard]] std::vector<u8> CompressDataZSTDDefault(const u8* source, std::size_t source_size);

/**
 * Decompresses a source memory region with Zstandard and returns the uncompressed data in a vector.
 *
 * @param compressed the compressed source memory region.
 *
 * @return the decompressed data.
 */
[[nodiscard]] std::vector<u8> DecompressDataZSTD(std::span<const u8> compressed);

/**
 * Decompresses a single Zstandard frame into a caller-provided buffer of known size.
 *
 * @param compressed the compressed source memory region.
 * @param output     the destination buffer. Its size is the maximum number of bytes to write.
 *
 * @return the number of bytes written, or 0 if the frame is invalid or doesn't fit. An empty
 *         frame also returns 0.
 */
[[nodiscard]] std::size_t DecompressDataZSTD(std::span<const u8> compressed, std::span<u8> output);

} // namespace Common::Compression
