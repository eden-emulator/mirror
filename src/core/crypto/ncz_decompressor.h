// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

#include "core/file_sys/vfs/vfs_types.h"

namespace Core::Crypto {

/**
 * Opens an NCZBLOCK-compressed NCA (nsz -B / .xcz block format) as a VirtualFile backed by
 * on-demand block decoding. Nothing is written to disk; each read decodes only the blocks it
 * covers and re-encrypts CTR sections. Solid NCZ is not supported.
 *
 * @param ncz_file    The raw NCZ payload (e.g. "<id>.ncz" entry from an NSZ/XCZ PFS0).
 * @param output_name Name to give the returned VirtualFile (e.g. "<id>.nca").
 *
 * @return a VirtualFile for the NCA, or nullptr if the NCZ is malformed or not NCZBLOCK.
 */
[[nodiscard]] FileSys::VirtualFile DecompressNCZ(const FileSys::VirtualFile& ncz_file,
                                                 std::string_view output_name);

/**
 * Returns true if the given file is an NCZ payload in block-compressed (NCZBLOCK) form,
 * i.e. one that DecompressNCZ() can open. Solid NCZ returns false.
 */
[[nodiscard]] bool IsBlockCompressedNCZ(const FileSys::VirtualFile& ncz_file);

// Install to NAND: same as DecompressNCZ, but solid NCZ is decoded on demand too.
[[nodiscard]] FileSys::VirtualFile DecompressNCZForInstall(
    const FileSys::VirtualFile& ncz_file, std::string_view output_name);

} // namespace Core::Crypto