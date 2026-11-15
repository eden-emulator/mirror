// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>
#include <QObject>
#include <QString>
#include "core/memory/dmnt_cheat_types.h"

class QAction;
class QMenu;

namespace Core {
class System;
}

/// The cheat's name after the names of the groups it is in ("Group \u203a Cheat")
QString CheatPath(const std::vector<Core::Memory::RuntimeCheatInfo>& cheats, u32 id);

class RuntimeCheatMenu final : public QObject {
public:
    /// Called with every cheat's state after the user changed any
    using Changed = std::function<void(const std::vector<Core::Memory::RuntimeCheatInfo>&)>;

    RuntimeCheatMenu(QMenu& menu, Core::System& system,
                     std::vector<Core::Memory::RuntimeCheatInfo> cheats, Changed changed = {});
    ~RuntimeCheatMenu() override;

    /// Every entry, indexed by menu id, with the current states
    const std::vector<Core::Memory::RuntimeCheatInfo>& Cheats() const {
        return cheats;
    }
    /// Turns a cheat on or off as a click on it would
    void Toggle(u32 id);

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
    Changed changed;
};
