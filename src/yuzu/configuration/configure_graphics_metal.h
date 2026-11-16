// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QWidget>
#include "yuzu/configuration/configuration_shared.h"

namespace Core {
class System;
}

namespace Ui {
class ConfigureGraphicsMetal;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureGraphicsMetal : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureGraphicsMetal(
        const Core::System& system_, std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
        const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureGraphicsMetal() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void Setup(const ConfigurationShared::Builder& builder);
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    std::unique_ptr<Ui::ConfigureGraphicsMetal> ui;

    const Core::System& system;

    std::vector<std::function<void(bool)>> apply_funcs;
};
