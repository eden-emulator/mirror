// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QWidget>
#include "qt_common/config/shared_translation.h"
#include "yuzu/configuration/configuration_shared.h"

class QComboBox;

namespace Core {
class System;
}

namespace Ui {
class ConfigureCpu;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureCpu : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureCpu(const Core::System& system_,
                          std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                          const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureCpu() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void UpdateGroup();

    void Setup(const ConfigurationShared::Builder& builder);

    std::unique_ptr<Ui::ConfigureCpu> ui;

    const Core::System& system;

    const ConfigurationShared::ComboboxTranslationMap& combobox_translations;
    std::vector<std::function<void(bool)>> apply_funcs{};

    QComboBox* accuracy_combobox;
    QComboBox* backend_combobox;
};
