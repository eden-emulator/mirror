// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "common/settings.h"
#include "core/core.h"
#include "yuzu/configuration/configure_modern_per_game.h"
#include "yuzu/configuration/shared_widget.h"

ConfigureModernPerGame::ConfigureModernPerGame(
    Core::System& system_, std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
    const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group, parent), system{system_} {
    auto* layout = new QVBoxLayout(this);
    auto* box = new QGroupBox(tr("Modern Console"), this);
    auto* box_layout = new QVBoxLayout(box);
    auto* note = new QLabel(tr("For this game only. Each option follows the one in Emulation > "
                               "Configure > Controls > Controllers until it is set here."),
                            box);
    note->setWordWrap(true);
    box_layout->addWidget(note);
    for (Settings::BasicSetting* setting : std::initializer_list<Settings::BasicSetting*>{
             &Settings::values.console_joycon_auto, &Settings::values.console_button_labels,
             &Settings::values.console_pause_disconnect, &Settings::values.console_player_lights,
             &Settings::values.console_debug_overlay, &Settings::values.console_hints}) {
        ConfigurationShared::Widget* widget = builder.BuildWidget(setting, apply_funcs);
        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }
        box_layout->addWidget(widget);
    }
    layout->addWidget(box);
    layout->addStretch(1);
}

ConfigureModernPerGame::~ConfigureModernPerGame() = default;

void ConfigureModernPerGame::SetConfiguration() {}

void ConfigureModernPerGame::ApplyConfiguration() {
    // (While a game runs, only the options it can change in game are applied)
    const bool powered_on = system.IsPoweredOn();
    for (const auto& apply : apply_funcs) {
        apply(powered_on);
    }
}
