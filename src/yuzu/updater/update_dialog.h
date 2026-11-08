// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>

#include <QDialog>
#include "qt_common/abstract/progress.h"
#include "common/net/net.h"

class QRadioButton;
namespace Ui {
class UpdateDialog;
}

class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(const Common::Net::Release& release, QWidget* parent = nullptr);
    ~UpdateDialog();

private slots:
    void Download();

private:
    Ui::UpdateDialog* ui;
    QList<QRadioButton*> m_buttons;
    Common::Net::NamedAsset m_asset;

    /**
     * @brief downloadFile Download a file, write to disk, and report progress
     * @param data Write the data to this string reference
     * @param url URL to download from
     * @param path Path to write to (set empty to not write at all)
     * @param dialog Progress dialog
     * @return True on success
     */
    bool downloadFile(
        std::string& data, const std::string &url, std::optional<std::filesystem::path> path,
        QtCommon::Frontend::QtProgressDialog *dialog);
};
