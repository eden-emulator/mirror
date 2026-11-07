// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QWidget;

// Steam's Game Mode runs Eden under gamescope, which shows one window at a time, scaled to fit
// the screen, with no title bars, and only one popup over it
namespace Gamescope {

/// Whether Eden runs under gamescope
bool IsRunningUnder();

/// Under gamescope only: fits Eden's dialogs and menus to how gamescope shows windows
/// (see gamescope.cpp). `main` is the main window.
void FitDialogsAndMenus(QWidget* main);

} // namespace Gamescope
