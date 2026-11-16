// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>
#include <vector>
#include <QWidget>
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_graphics_metal.h"
#include "yuzu/configuration/configuration_shared.h"
#include "yuzu/configuration/configure_graphics_metal.h"
#include "yuzu/configuration/shared_widget.h"

ConfigureGraphicsMetal::ConfigureGraphicsMetal(
    const Core::System& system_, std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
    const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureGraphicsMetal>()}, system{system_} {

    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();
}

ConfigureGraphicsMetal::~ConfigureGraphicsMetal() = default;

void ConfigureGraphicsMetal::SetConfiguration() {}

void ConfigureGraphicsMetal::Setup(const ConfigurationShared::Builder& builder) {
    auto& layout = *ui->populate_target->layout();

    std::map<u32, QWidget*> hold{};

    for (auto setting : Settings::values.linkage.by_category[Settings::Category::RendererMetal]) {
        auto* widget = builder.BuildWidget(setting, apply_funcs);
        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }
        hold.emplace(setting->Id(), widget);
    }

    for (const auto& [id, widget] : hold) {
        layout.addWidget(widget);
    }
}

void ConfigureGraphicsMetal::ApplyConfiguration() {
    const bool is_powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(is_powered_on);
    }
}

void ConfigureGraphicsMetal::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGraphicsMetal::RetranslateUI() {
    ui->retranslateUi(this);
}
