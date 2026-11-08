// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <utility>

#include <QRadioButton>
#include <QSaveFile>
#include <QStandardPaths>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "qt_common/abstract/frontend.h"
#include "qt_common/abstract/progress.h"
#include "qt_common/util/compress.h"
#include "ui_update_dialog.h"
#include "update_dialog.h"

#include "common/scm_rev.h"

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

#include <openssl/evp.h>

#include <QDesktopServices>
#include <QProcess>

#undef GetSaveFileName

import hydrocarbon;

static std::string sha256_hash(EVP_MD_CTX* ctx, const std::string& input) {
    char output[65];
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, input.c_str(), input.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);

    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[hash_len * 2] = '\0';

    return std::string(output);
}

UpdateDialog::UpdateDialog(const Common::Net::Release& release, QWidget* parent)
    : QDialog(parent), ui(new Ui::UpdateDialog) {
    ui->setupUi(this);

    ui->version->setText(
        tr("%1 is available for download.").arg(QString::fromStdString(release.name)));
    ui->url->setText(
        tr("<a href=\"%1\">View on Forgejo</a>").arg(QString::fromStdString(release.html_url)));

    std::string text{release.body};
    if (auto pos = text.find("# Packages"); pos != std::string::npos) {
        text = text.substr(0, pos);
    }

    ui->body->setMarkdown(QString::fromStdString(text));

    // TODO(crueter): Find a way to set default
    const auto assets = Common::Net::GetPlatformAssets(release);

    if (assets.empty()) {
        ui->groupBox->setHidden(true);
        connect(this, &QDialog::accepted, this, [release]() {
            QDesktopServices::openUrl(QUrl{QString::fromStdString(release.html_url)});
        });
    } else if (assets.size() == 1) {
        ui->groupBox->setHidden(true);
        m_asset = assets[0];

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    } else {
        u32 i = 0;
        for (const Common::Net::NamedAsset& a : assets) {
            QRadioButton* r = new QRadioButton(tr(a.name.c_str()), this);
            connect(r, &QRadioButton::toggled, this, [a, this](bool checked) {
                if (checked)
                    m_asset = a;
            });

            if (i == 0)
                r->setChecked(true);
            ++i;

            ui->radioButtons->addWidget(r);
        }

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    }
}

UpdateDialog::~UpdateDialog() {
    delete ui;
}

// TODO: migrate progress callback download to Common::Net?
// TODO: migrate to QtCommon
void UpdateDialog::Download() {
    namespace fs = std::filesystem;
    const auto appDir = fs::path{QCoreApplication::applicationDirPath().toStdString()}.make_preferred();

    const auto parentDir = appDir.parent_path();
    const auto appDirName = appDir.filename().string();
    const auto oldDir = parentDir / std::format("{}_{}", appDirName, std::string{Common::g_build_version});
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

    const auto filename = m_asset.asset.name;
    const auto absFilename = tmpDir / filename;

    // Progress dialog.
    auto progress =
        QtCommon::Frontend::newProgressDialog(tr("Downloading..."), tr("Cancel"), 0, 100);
    progress->show();

    QGuiApplication::processEvents();

    // NOTE: This loads the entire file content into memory
    // Since this is always <80MB this is actually fine for now, but is a target for future
    // optimization (e.g. incr. read from disk) if assets are ever >200MB.
    // Not that it matters because to be able to run games you need >1GB lol.
    std::string tmp_data;
    const auto download_res =
        downloadFile(tmp_data, m_asset.asset.browser_download_url, absFilename, progress.get());

    if (!download_res) {
        progress->close();
        return;
    }

    // TODO(crueter): Test
    if (m_asset.asset.digest) {
        progress->setLabelText(tr("Verifying..."));
        progress->setValue(0);
        progress->setMaximum(0);

        auto ctx = EVP_MD_CTX_new();
        auto actual = std::format("sha256:{}", sha256_hash(ctx, tmp_data));
        auto expected = m_asset.asset.digest.value();

        EVP_MD_CTX_free(ctx);

        // TODO: auto-retry
        if (actual != expected) {
            LOG_ERROR(Frontend, "Hash mismatch, expected {}, got {}", expected, actual);
            QtCommon::Frontend::Critical(
                tr("Failed to download file"),
                tr("File has invalid hash.\nGot:%1\nExpected: %2\nPlease re-launch Eden and try "
                   "again.")
                    .arg(QString::fromStdString(expected), QString::fromStdString(actual)));

            return;
        }
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
            LOG_ERROR(Frontend, "Could not create staging directory {}: {}", stagingDir.string(), st_ec.message());

            QtCommon::Frontend::Critical(
                tr("Failed to create staging directory"),
                tr("Could not create the staging directory %1.\n\nOS returned error: %2\n\nEnsure "
                   "you have write permissions in the current app directory, or try manually "
                   "updating.").arg(qstaging, QString::fromStdString(st_ec.message())));

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
        const auto extracted = QtCommon::Compress::extractDir(QString::fromStdString(absFilename.string()),
                                       qstaging, progress_callback);

        if (progress->wasCanceled()) {
            progress->close();

            QtCommon::Frontend::Information(
                tr("Extraction cancelled"),
                tr("Extraction was cancelled by the user."));

            fs::remove_all(stagingDir, ec);
            return;
        }

        progress->close();

        bool found = false;
        for (const auto &file : std::as_const(extracted)) {
            if (file.contains(QStringLiteral("eden.exe"))) {
                found = true;
                break;
            }
        }

        if (!found) {
            LOG_ERROR(Frontend, "Extracted no valid files from {}, staging={}", absFilename.string(), stagingDir.string());

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
                    .arg(qupdater,
                         qcachedUpdater,
                         QString::fromStdString(copy_ec.message()),
                         qstaging));

            return;
        }

        const auto pid = QString::number(QCoreApplication::applicationPid());
        const auto keep_old = button == QtCommon::Frontend::Yes ? QStringLiteral("1") : QStringLiteral("0");
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

bool UpdateDialog::downloadFile(
    std::string& write_data, const std::string &url, std::optional<std::filesystem::path> path,
    QtCommon::Frontend::QtProgressDialog *progress) {
    std::function<bool(std::string_view)> on_data;

    std::ofstream file;
    std::string filename;

    if (path) {
        LOG_INFO(Frontend, "Downloading {} to {}", url, path->string());

        filename = path->filename().string();
        file = std::ofstream(path->string(), std::ios::out | std::ios::binary);

        // write file in chunks
        on_data = [&write_data, filename, &file](std::string_view t_data) {
            write_data += t_data;

            try {
                file.write(t_data.data(), t_data.size());
            } catch (std::exception& e) {
                LOG_ERROR(Frontend, "Could not write {} bytes to file {}, error{}=", t_data.size(),
                          filename, e.what());
                QtCommon::Frontend::Critical(
                    tr("Failed to save file"),
                    tr("Could not write to file %1.").arg(QString::fromStdString(filename)));
                return false;
            }

            return true;
        };
    } else {
        LOG_INFO(Frontend, "Downloading {}", url);

        on_data = [&write_data](std::string_view t_data) {
            write_data += t_data;
            return true;
        };
    }

    auto callback = [&progress, &on_data](const hydro::DownloadState& state) {
        if (!on_data(state.buffer))
            return false;

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

    if (progress->wasCanceled()) {
        LOG_ERROR(Frontend, "URL {} download cancelled by user", url);

        if (path) {
            try {
                file.flush();
                file.close();

                std::error_code ec;
                std::filesystem::remove_all(path.value());
            } catch (...) {
            }
        }

        QtCommon::Frontend::Information(
            tr("Download cancelled"),
            tr("Download was cancelled by the user."));
        return false;
    } else if (!res) {
        LOG_ERROR(Frontend, "URL {} returned error {}", url, res.error());

        QtCommon::Frontend::Critical(
            tr("Failed to download file"),
            tr("URL %1 returned error %2.")
                .arg(QString::fromStdString(url), QString::fromStdString(std::format("{}", res.error()))));
        return false;
    } else if (res->status_code < hydro::Status::Ok || res->status_code >= hydro::Status::MultipleChoices) {
        LOG_ERROR(Frontend, "URL {} returned status {}", url, std::to_underlying(res->status_code));

        QtCommon::Frontend::Critical(
            tr("Failed to download file"),
            tr("URL %1 returned status code %2.")
                .arg(QString::fromStdString(url), QString::number(std::to_underlying(res->status_code))));
        return false;
    }

    if (path) {
        try {
            file.flush();
            file.close();
        } catch (const std::exception& e) {
            LOG_WARNING(Frontend, "Could not commit to file {}, OS returned error {}", filename, e.what());

            QtCommon::Frontend::Critical(
                tr("Failed to save file"),
                tr("Could not commit to file %1.").arg(QString::fromStdString(filename)));
        }
    }

    return true;
}
