// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QIcon>
#include <QMessageBox>
#include "yuzu/configuration/configure_web.h"

#include <QRegularExpressionValidator>

#include <QtConcurrentRun>
#include "common/settings.h"
#include "qt_common/config/uisettings.h"
#include "ui_configure_web.h"

ConfigureWeb::ConfigureWeb(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureWeb>()) {
    ui->setupUi(this);

    QString user_regex = QStringLiteral(".{4,20}");

    QRegularExpressionValidator* username_validator = new QRegularExpressionValidator(this);
    username_validator->setRegularExpression(QRegularExpression(user_regex));

    ui->edit_username->setValidator(username_validator);

#ifndef USE_DISCORD_PRESENCE
    ui->discord_group->setVisible(false);
#endif

    SetConfiguration();
    RetranslateUI();
}

ConfigureWeb::~ConfigureWeb() = default;

void ConfigureWeb::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureWeb::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureWeb::SetConfiguration() {
    connect(ui->edit_username, &QLineEdit::textChanged, this, &ConfigureWeb::VerifyLogin);

    ui->edit_username->setText(QString::fromStdString(Settings::values.eden_username.GetValue()));

    VerifyLogin();

    ui->toggle_discordrpc->setChecked(UISettings::values.enable_discord_presence.GetValue());
}

void ConfigureWeb::ApplyConfiguration() {
    UISettings::values.enable_discord_presence = ui->toggle_discordrpc->isChecked();
    Settings::values.eden_username = ui->edit_username->text().toStdString();
}

void ConfigureWeb::VerifyLogin() {
    const QPixmap checked = QIcon::fromTheme(QStringLiteral("checked")).pixmap(16);
    const QPixmap failed = QIcon::fromTheme(QStringLiteral("failed")).pixmap(16);

    const bool username_good = ui->edit_username->hasAcceptableInput();

    if (username_good) {
        ui->label_username_verified->setPixmap(checked);
        ui->label_username_verified->setToolTip(tr("All Good", "Tooltip"));
    } else {
        ui->label_username_verified->setPixmap(failed);
        ui->label_username_verified->setToolTip(tr("Must be between 4-20 characters", "Tooltip"));
    }
}

void ConfigureWeb::SetWebServiceConfigEnabled(bool enabled) {
    ui->label_disable_info->setVisible(!enabled);
    ui->groupBoxWebConfig->setEnabled(enabled);
}
