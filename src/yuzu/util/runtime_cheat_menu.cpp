// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <QAction>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include "core/core.h"
#include "yuzu/util/runtime_cheat_menu.h"

using Core::Memory::CHEAT_MENU_ROOT;
using Core::Memory::CheatMenuEntryType;
using Core::Memory::RuntimeCheatInfo;

namespace {
QString CheatName(const RuntimeCheatInfo& cheat) {
    return cheat.name.empty() ? QCoreApplication::translate("MainWindow", "Unnamed Cheat") : QString::fromUtf8(cheat.name);
}
}

RuntimeCheatMenu::RuntimeCheatMenu(QMenu& menu_, Core::System& system_, std::vector<RuntimeCheatInfo> cheats_)
    : menu{menu_}, system{system_}, cheats{std::move(cheats_)} {
    menu.installEventFilter(this);
    auto* disable_all = menu.addAction(QCoreApplication::translate("MainWindow", "Disable All Cheats"));
    connect(disable_all, &QAction::triggered, this, [this] { SetEnabled(CHEAT_MENU_ROOT, false); });
    auto* enable_all = menu.addAction(QCoreApplication::translate("MainWindow", "Enable All Cheats"));
    connect(enable_all, &QAction::triggered, this, [this] { SetEnabled(CHEAT_MENU_ROOT, true); });
    menu.addSeparator();

    actions.reserve(cheats.size());
    for (const auto& cheat : cheats) {
        auto* parent = cheat.parent_id == CHEAT_MENU_ROOT ? &menu : actions[cheat.parent_id]->menu();
        QAction* action;
        if (cheat.type == CheatMenuEntryType::Submenu) {
            auto* submenu = new QMenu{&menu};
            submenu->installEventFilter(this);
            submenus.push_back(submenu);
            action = parent->addMenu(submenu);
        } else {
            action = parent->addAction(CheatName(cheat));
            action->setEnabled(!cheat.is_master);
        }
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, id = cheat.id](bool enabled) { SetEnabled(id, enabled); });
        actions.push_back(action);
    }
    UpdateChecks();
}

RuntimeCheatMenu::~RuntimeCheatMenu() {
    menu.removeEventFilter(this);
    menu.clear();
    for (auto* submenu : submenus) {
        delete submenu;
    }
}

void RuntimeCheatMenu::SetEnabled(u32 id, bool enabled) {
    system.SetCheatEnabled(id, enabled);
    for (const auto& cheat : system.GetRuntimeCheats()) {
        if (cheat.id < cheats.size()) {
            cheats[cheat.id].enabled = cheat.enabled;
        }
    }
    UpdateChecks();
}

void RuntimeCheatMenu::UpdateChecks() {
    std::vector<std::pair<u32, u32>> counts(cheats.size());
    for (const auto& cheat : cheats) {
        if (cheat.type != CheatMenuEntryType::Cheat) {
            continue;
        }
        for (auto parent = cheat.parent_id; parent != CHEAT_MENU_ROOT; parent = cheats[parent].parent_id) {
            counts[parent].first += cheat.enabled ? 1 : 0;
            ++counts[parent].second;
        }
    }
    for (const auto& cheat : cheats) {
        auto* action = actions[cheat.id];
        if (cheat.type == CheatMenuEntryType::Cheat) {
            action->setChecked(cheat.enabled);
            continue;
        }
        const auto [enabled, total] = counts[cheat.id];
        action->setCheckable(total != 0);
        action->setChecked(total != 0 && enabled == total);
        action->setText(CheatName(cheat) + QStringLiteral(" (%1/%2)").arg(enabled).arg(total));
    }
}

bool RuntimeCheatMenu::eventFilter(QObject* watched, QEvent* event) {
    auto* watched_menu = qobject_cast<QMenu*>(watched);
    if (!watched_menu) {
        return QObject::eventFilter(watched, event);
    }
    QAction* action = nullptr;
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            action = watched_menu->actionAt(mouse->pos());
        }
    } else if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        auto* active = watched_menu->activeAction();
        if (key->key() == Qt::Key_Space ||
            ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && active &&
             !active->menu())) {
            action = active;
        }
    }
    if (action && action->isEnabled() && !action->isSeparator() && (action->isCheckable() || !action->menu())) {
        action->trigger();
        return true;
    }
    return QObject::eventFilter(watched, event);
}
