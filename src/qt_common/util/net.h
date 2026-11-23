// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include "common/net/net.h"
#include "qt_common/abstract/progress.h"

namespace QtCommon::Net {

enum class DownloadStatus {
    // :)
    Success,

    // network error, will retry
    NetworkError,

    // mismatched hash, will retry
    HashMismatch,

    // user cancelled, don't retry
    Cancelled,

    // I/O error, don't retry
    IOError,

    // Some other fatal error
    UnknownFatal,
};

/**
 * @brief DownloadFile Download a file, write to disk, report progress, and verify its hash
 * @param url URL to download from
 * @param data Write the data to this string pointer. Omit or set to nullptr to disable
 * @param path Path to write to. Omit or set empty to disable file writing
 * @param expected_hash Expected hash of the file, must be in format sha256:<hex>. Omit or set empty
 * to disable verification
 * @param dialog Progress dialog. Omit or set to nullptr to disable
 * @param max_attempts How many times to retry a failed or corrupted download (default 3)
 * @return True on success
 */
bool DownloadFile(const std::string& url, std::string* data = nullptr,
                  std::optional<std::filesystem::path> path = {},
                  std::optional<std::string> expected_hash = {},
                  QtCommon::Frontend::QtProgressDialog* progress = nullptr, int max_attempts = 3);

void DownloadAsset(const Common::Net::Asset& asset);

} // namespace QtCommon::Net
