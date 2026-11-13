// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QObject>
#include "core/memory/dmnt_cheat_types.h"

class QAction;
class QMenu;

namespace Core {
class System;
}

class RuntimeCheatMenu final : public QObject {
public:
    RuntimeCheatMenu(QMenu& menu, Core::System& system, std::vector<Core::Memory::RuntimeCheatInfo> cheats);
    ~RuntimeCheatMenu() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void SetEnabled(u32 id, bool enabled);
    void UpdateChecks();

    QMenu& menu;
    Core::System& system;
    std::vector<Core::Memory::RuntimeCheatInfo> cheats;
    std::vector<QAction*> actions; // indexed by menu id
    std::vector<QMenu*> submenus;
};
