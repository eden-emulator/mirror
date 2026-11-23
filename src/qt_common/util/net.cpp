// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/fs/path_util.h"
#include "common/scm_rev.h"

#include "qt_common/abstract/frontend.h"
#include "qt_common/util/compress.h"
#include "qt_common/util/net.h"

#include "common/logging.h"

#include <QDesktopServices>
#include <QProcess>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>

#include <openssl/evp.h>

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

#undef GetSaveFileName

import hydrocarbon;

namespace QtCommon::Net {

// Quick RAII wrapper around EVP_MD_CTX
// TODO: make common
inline void free_md_ctx(EVP_MD_CTX* c) {
    if (c) {
        EVP_MD_CTX_free(c);
    }
}

using RAII_MD_CTX = std::unique_ptr<EVP_MD_CTX, decltype(&free_md_ctx)>;

// Internal
static DownloadStatus downloadFileOnce(const std::string& url, std::string* write_data,
                                       std::optional<std::filesystem::path> path,
                                       std::optional<std::string> expected_hash,
                                       QtCommon::Frontend::QtProgressDialog* progress) {
    if (write_data)
        write_data->clear();

    // crypto init
    auto ctx = RAII_MD_CTX{EVP_MD_CTX_new(), &free_md_ctx};
    if (!ctx) {
        LOG_ERROR(Frontend, "Failed to initialize OpenSSL crypto context");
        QtCommon::Frontend::Critical(
            tr("Failed to initialize crypto context"),
            tr("OpenSSL failed to initialize its crypto context. Please try again later."));

        return DownloadStatus::UnknownFatal;
    }

    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), NULL) != 1) {
        LOG_ERROR(Frontend, "Failed to initialize sha256 hasher");
        QtCommon::Frontend::Critical(
            tr("Failed to initialize SHA256 hash context"),
            tr("OpenSSL failed to initialize its SHA256 hash context. Please try again later."));

        return DownloadStatus::UnknownFatal;
    }

    // file
    std::ofstream file;
    bool write_failed = false;

    // write data if needed, update digest if needed
    const auto on_data = [&](std::string_view input) {
        try {
            if (write_data)
                write_data->operator+=(input);

            if (expected_hash)
                EVP_DigestUpdate(ctx.get(), input.data(), input.size());

            if (path)
                file.write(input.data(), input.size());
        } catch (std::exception& e) {
            const std::string filename = path->filename().string();

            LOG_ERROR(Frontend, "Could not write {} bytes to file {}, error={}", input.size(),
                      filename, e.what());

            QtCommon::Frontend::Critical(
                tr("Failed to save file"),
                tr("Could not write to file %1.\nOS returned error: %2")
                    .arg(QString::fromStdString(filename), QString::fromUtf8(e.what())));

            write_failed = true;
            return false;
        }

        return true;
    };

    // begin
    if (path) {
        LOG_INFO(Frontend, "Downloading {} to {}", url, path->string());

        file = std::ofstream(path->string(), std::ios::out | std::ios::binary);

        if (!file) {
            LOG_ERROR(Frontend, "Failed to open file {}", path->string());

            QtCommon::Frontend::Critical(tr("Failed to open file"),
                                         tr("Failed to open file %1 for writing. Ensure you have "
                                            "the necessary permissions and try again.")
                                             .arg(QString::fromStdString(path->string())));

            return DownloadStatus::UnknownFatal;
        }

        file.exceptions(std::ios::failbit | std::ios::badbit);
    } else {
        LOG_INFO(Frontend, "Downloading {}", url);
    }

    auto callback = [&progress, &on_data](const hydro::DownloadState& state) {
        if (!on_data(state.buffer))
            return false;

        if (!progress)
            return true;

        QGuiApplication::processEvents();
        if (state.content_length > 0)
            progress->setValue(int((state.bytes_received * 100) / state.content_length));

        return !progress->wasCanceled();
    };

    hydro::RequestOptions opts;
#ifdef YUZU_BUNDLED_OPENSSL
    opts.connect.cert = std::string{kCert};
#endif

    opts.connect.connectionTimeout = std::chrono::milliseconds{10000};
    opts.callback = callback;

    auto res = hydro::get(url, opts);

    const auto cleanupFile = [&]() {
        if (!path)
            return;

        try {
            file.flush();
            file.close();

            std::filesystem::remove(path.value());
        } catch (...) {
        }
    };

    if (progress && progress->wasCanceled()) {
        LOG_ERROR(Frontend, "URL {} download cancelled by user", url);

        cleanupFile();

        QtCommon::Frontend::Information(tr("Download cancelled"),
                                        tr("Download was cancelled by the user."));

        return DownloadStatus::Cancelled;
    } else if (!res) {
        LOG_ERROR(Frontend, "URL {} returned error {}", url, res.error());

        cleanupFile();

        // this is retryable
        return DownloadStatus::NetworkError;
    } else if (res->status_code < hydro::Status::Ok ||
               res->status_code >= hydro::Status::MultipleChoices) {
        LOG_ERROR(Frontend, "URL {} returned status {}", url, res->status_code);

        // NB: this path (should) never write a file at all
        // This is included for completeness.
        cleanupFile();

        // this is retryable
        return DownloadStatus::NetworkError;
    } else if (write_failed) {
        cleanupFile();

        file.clear();

        // on_data already posted the dialog + log
        return DownloadStatus::IOError;
    }

    if (path) {
        try {
            file.flush();
            file.close();
        } catch (const std::exception& e) {
            std::error_code ec;
            std::filesystem::remove(path.value(), ec);

            LOG_WARNING(Frontend, "Could not commit to file {}, OS returned error {}",
                        path->string(), e.what());

            QtCommon::Frontend::Critical(
                tr("Failed to save file"),
                tr("Could not commit to file %1.\nOS returned error: %2")
                    .arg(QString::fromStdString(path->string()), QString::fromUtf8(e.what())));

            return DownloadStatus::IOError;
        }
    }

    // verify hash, if applicable
    if (expected_hash) {
        char output[65];
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len;

        EVP_DigestFinal_ex(ctx.get(), hash, &hash_len);

        for (unsigned int i = 0; i < hash_len; i++) {
            sprintf(output + (i * 2), "%02x", hash[i]);
        }
        output[hash_len * 2] = '\0';

        // TODO: sha512 support
        std::string actual = std::format("sha256:{}", output);
        auto expected = expected_hash.value();

        if (actual != expected) {
            LOG_ERROR(Frontend, "Hash mismatch, expected {}, got {}", expected, actual);

            if (path) {
                std::error_code ec;
                std::filesystem::remove(path.value(), ec);
            }

            // this is retryable
            return DownloadStatus::HashMismatch;
        }
    }

    return DownloadStatus::Success;
}

bool DownloadFile(const std::string& url, std::string* out_data,
                  std::optional<std::filesystem::path> path,
                  std::optional<std::string> expected_hash,
                  QtCommon::Frontend::QtProgressDialog* progress, int max_attempts) {
    if (max_attempts <= 0) {
        QtCommon::Frontend::Critical(
            tr("No max attempts configured"),
            tr("A function attempted to call QtCommon::Net::downloadFile, but set the max attempts "
               "to zero. This is a no-op and should be reported upstream."));

        return false;
    }

    DownloadStatus res = DownloadStatus::Success;

    if (progress)
        progress->setLabelText(tr("Downloading..."));

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (progress) {
            progress->setValue(0);
            progress->setMaximum(100);
        }

        switch (res = downloadFileOnce(url, out_data, path, expected_hash, progress)) {
        case DownloadStatus::NetworkError:
        case DownloadStatus::HashMismatch: {
            if (progress) {
                progress->setLabelText(
                    tr("Retrying (%1/%2)...").arg(attempt + 1).arg(max_attempts));
            }

            break;
        }
        case DownloadStatus::Success:
            return true;
        case DownloadStatus::Cancelled:
        case DownloadStatus::IOError:
        case DownloadStatus::UnknownFatal:
            return false;
        }
    }

    // retryables
    if (res == DownloadStatus::HashMismatch) {
        QtCommon::Frontend::Critical(
            tr("Verification failed"),
            tr("File was successfully downloaded, but did not match the expected sha256 hash. "
               "Check your logs for more information."));
    } else if (res == DownloadStatus::NetworkError) {
        QtCommon::Frontend::Critical(
            tr("Network Error"),
            tr("A network error occurred during download. Check your logs for more information."));
    }

    // caller handles progress close
    return false;
}

void DownloadAsset(const Common::Net::Asset& asset) {
    namespace fs = std::filesystem;
    const auto appDir =
        fs::path{QCoreApplication::applicationDirPath().toStdString()}.make_preferred();

    const auto parentDir = appDir.parent_path();
    const auto appDirName = appDir.filename().string();
    const auto oldDir =
        parentDir / std::format("{}_{}", appDirName, std::string{Common::g_build_version});
    const auto stagingDir = parentDir / std::format("{}_{}", appDirName, "staging");

    std::error_code ec;
    auto tmpDir = fs::temp_directory_path(ec);

    if (ec) {
        // tmp dir creation failed, just use cache
        LOG_WARNING(Frontend, "Failed to create temporary directory: {}", ec.message());
        tmpDir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "update" / "download";
    } else {
        tmpDir = tmpDir / "eden" / "update" / "download";
    }

    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    const auto filename = asset.name;
    const auto absFilename = tmpDir / filename;

    // Progress dialog.
    auto progress =
        QtCommon::Frontend::newProgressDialog(tr("Downloading..."), tr("Cancel"), 0, 100);
    progress->show();

    QGuiApplication::processEvents();

    // now download
    const auto download_res = DownloadFile(asset.browser_download_url, nullptr, absFilename,
                                           asset.digest, progress.get());

    if (!download_res) {
        progress->close();
        return;
    }

    // Download is complete.
    if (filename.ends_with(".dmg") || filename.ends_with(".exe") || filename.ends_with(".tar.gz")) {
        progress->close();

        // dmg, exe, tarball will just open it and let the OS handle the rest.
        auto localUrl = QUrl::fromLocalFile(QString::fromStdString(absFilename.string()));
        QDesktopServices::openUrl(localUrl);
    } else if (filename.ends_with(".zip")) {
        // This path should only ever trigger on Windows.
        // If it did somehow anyways, open it and let the OS handle it.
#ifndef _WIN32
        progress->close();

        auto localUrl = QUrl::fromLocalFile(QString::fromStdString(absFilename.string()));
        QDesktopServices::openUrl(localUrl);
        return;
#endif

        // request the user to keep the old version or not...
        auto button = QtCommon::Frontend::Question(
            tr("Download Complete"),
            tr("Would you like to keep the old version of Eden in a separate folder (%1)?")
                .arg(QString::fromStdString(oldDir.filename().string())));

        const auto qstaging = QString::fromStdString(stagingDir.string());

        // ...then extract to a staging dir...
        std::error_code st_ec;
        fs::create_directories(stagingDir, st_ec);

        if (st_ec) {
            LOG_ERROR(Frontend, "Could not create staging directory {}: {}", stagingDir.string(),
                      st_ec.message());

            QtCommon::Frontend::Critical(
                tr("Failed to create staging directory"),
                tr("Could not create the staging directory %1.\n\nOS returned error: %2\n\nEnsure "
                   "you have write permissions in the current app directory, or try manually "
                   "updating.")
                    .arg(qstaging, QString::fromStdString(st_ec.message())));

            return;
        }

        auto progress_callback = [&](size_t total_size, size_t processed_size) {
            QGuiApplication::processEvents();

            if (total_size > 0)
                progress->setValue(int((processed_size * 100) / total_size));
            return !progress->wasCanceled();
        };

        progress->setLabelText(tr("Extracting..."));
        progress->setValue(0);
        progress->setMaximum(100);
        progress->show();

        // WARNING
        // close() sets wasCanceled to true, this is not documented but it WILL mess with callback
        // functions like this one!
        const auto extracted = QtCommon::Compress::extractDir(
            QString::fromStdString(absFilename.string()), qstaging, progress_callback);

        if (progress->wasCanceled()) {
            progress->close();

            QtCommon::Frontend::Information(tr("Extraction cancelled"),
                                            tr("Extraction was cancelled by the user."));

            fs::remove_all(stagingDir, ec);
            return;
        }

        progress->close();

        bool found = false;
        for (const auto& file : std::as_const(extracted)) {
            if (file.contains(QStringLiteral("eden.exe"))) {
                found = true;
                break;
            }
        }

        if (!found) {
            LOG_ERROR(Frontend, "Extracted no valid files from {}, staging={}",
                      absFilename.string(), stagingDir.string());

            QtCommon::Frontend::Critical(tr("Failed to extract downloaded file"),
                                         tr("No valid files were successfully extracted from %1.")
                                             .arg(QString::fromStdString(absFilename.string())));

            return;
        }

        // ...then run a small updater shim
        const auto updater = appDir / "eden-updater.exe";
        const auto cached_updater =
            Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "eden-updater.exe";

        const auto qcachedUpdater = QString::fromStdString(cached_updater.string());
        const auto qupdater = QString::fromStdString(updater.string());
        const auto qappDir = QString::fromStdString(appDir.string());
        const auto qoldDir = QString::fromStdString(oldDir.string());

        std::error_code copy_ec;
        // ignore remove failures
        fs::remove_all(cached_updater, copy_ec);
        fs::copy_file(updater.native(), cached_updater.native(), copy_ec);

        if (copy_ec) {
            LOG_ERROR(Frontend, "Failed to copy updater executable: {}", copy_ec.message());

            QtCommon::Frontend::Critical(
                tr("Failed to run updater"),
                tr("Could not copy the updater executable (%1) to the local cache directory "
                   "(%2).\nOS returned error: %3\n\nThe new release is extracted in %4.")
                    .arg(qupdater, qcachedUpdater, QString::fromStdString(copy_ec.message()),
                         qstaging));

            return;
        }

        const auto pid = QString::number(QCoreApplication::applicationPid());
        const auto keep_old =
            button == QtCommon::Frontend::Yes ? QStringLiteral("1") : QStringLiteral("0");
        const auto pwd = QString::fromStdString(cached_updater.parent_path().string());

        // clang-format off
        auto pres = QProcess::startDetached(
            qcachedUpdater,
            {pid, qappDir, qoldDir, qstaging, keep_old},
            pwd);
        // clang-format on

        if (!pres) {
            LOG_ERROR(Frontend, "Failed to start updater executable");

            QtCommon::Frontend::Critical(tr("Failed to run updater"),
                                         tr("Could not run the updater executable (%1)\n\nThe new "
                                            "release is extracted in %4.")
                                             .arg(qupdater, qstaging));

            return;
        }

        QCoreApplication::exit(0);
    }
}

} // namespace QtCommon::Net
