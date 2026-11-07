// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QKeyEvent>
#include <QList>
#include <QPointer>
#include <QProxyStyle>
#include <QToolButton>
#include <QWidget>

#include "common/controller_console_state.h"

namespace Core {
class System;
}

class QCheckBox;
class QString;
class QTimer;
class QTabBar;
class QTabWidget;

class ConfigureInputAdvanced;
class ConfigureInputPlayer;
class ConfigurePlayerAssignment;

class InputProfiles;

namespace InputCommon {
class InputSubsystem;
}

namespace Ui {
class ConfigureInput;
}

void OnDockedModeChanged(bool last_state, bool new_state, Core::System& system);

/// The settings tab bar in the automatic modes: player tabs drawn as bubbles in their colour
class PlayerTabStyle : public QProxyStyle {
public:
    explicit PlayerTabStyle(const QString& base);
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size,
                           const QWidget* widget) const override;
};

/// Under Eden's dark themes the style sheet draws the tabs: this layer paints the player tabs'
/// bubbles instead
class PlayerTabOverlay : public QWidget {
public:
    PlayerTabOverlay(QTabBar* bar, QColor background);
    /// A player tab being dragged (-1: none), drawn this far from its place
    void SetDrag(int index, int offset);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTabBar* bar;
    /// The theme's own background, which covers its tab under the bubble
    QColor background;
    int drag_index = -1;
    int drag_offset = 0;
};

class ConfigureInput : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureInput(Core::System& system_, QWidget* parent = nullptr);
    ~ConfigureInput() override;

    /// Initializes the input dialog with the given input subsystem.
    void Initialize(InputCommon::InputSubsystem* input_subsystem_, std::size_t max_players = 8);

    /// Save all button configurations to settings file.
    void ApplyConfiguration();

    QList<QWidget*> GetSubTabs() const;

    /// A player's tab (0-based player)
    QWidget* PlayerTab(int player) const;

private:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

    /// Player tabs dropped in a new order: the controllers take that order, the tabs go back
    void FinishTabDrag();
    /// The players' new order, as their tabs were dropped: the controllers move to match
    void ApplyDroppedOrder(const std::vector<int>& dropped, int dragged_player);
    /// Eden's own drag of a player tab, under the dark themes (Qt's would be the theme's tab)
    bool DragPlayerTab(QEvent* event);
    /// The arrows on each player tab, swapping with the player before or after it
    void UpdateTabArrows();
    void SwapPlayers(int a, int b);
    void RetranslateUI();
    void ClearAll();

    void UpdateDockedState(bool is_handheld);
    void UpdateAllInputDevices();
    void UpdateAllInputProfiles(std::size_t player_index);
    // Enable preceding controllers or disable following ones
    void PropagatePlayerNumberChanged(size_t player_index, bool checked,
                                      bool reconnect_current = false);

    /// Load configuration settings.
    void LoadConfiguration();
    void LoadPlayerControllerIndices();

    /// Restore all buttons to their default values.
    void RestoreDefaults();

    std::unique_ptr<Ui::ConfigureInput> ui;

    std::unique_ptr<InputProfiles> profiles;

    std::array<ConfigureInputPlayer*, 8> player_controllers{};
    ConfigurePlayerAssignment* assignment = nullptr;
    /// Redraws the player tabs when the engine reloads the controllers
    QTimer* follow_timer = nullptr;
    unsigned long long followed_generation = 0;
    std::function<void()> mirror_console_mode;
    /// The settings dialog's tab widget whose tab clicks rumble the players' controllers
    QTabWidget* rumble_tabs = nullptr;
    std::array<QWidget*, 8> player_tabs{};
    /// Dragging the player tabs: the tab bar, and the order when the drag began
    QTabWidget* drag_tabs = nullptr;
    /// The tab bar drawn with PlayerTabStyle (its player tabs as bubbles)
    QPointer<QTabBar> pill_bar;
    QPointer<PlayerTabOverlay> tab_overlay;
    /// The players can be reordered from their tabs (the automatic modes, on this page)
    bool tabs_editable = false;
    /// Eden's own tab drag: the tab pressed, where, and whether it is being dragged
    int own_drag_index = -1;
    int own_drag_press_x = 0;
    bool own_dragging = false;
    QList<QWidget*> tab_order;
    QWidget* dragged_tab = nullptr;
    bool tab_dragging = false;
    std::array<QString, 8> player_tab_titles{};
    std::vector<Common::ControllerConsoleState::PlayerRow> last_rows;
    /// How each player tab is lit (0 idle, 1 input held, 2 green blink, 3 red blink)
    std::array<int, 8> tab_lit{};
    /// The battery each player tab shows (percent * 2 + charging, -1 none)
    std::array<int, 8> tab_battery{-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<QPointer<QToolButton>, 8> tab_lefts;
    std::array<QPointer<QToolButton>, 8> tab_rights;
    /// The tab to select once its player shows (the controller a drag or an arrow moved)
    int pending_select = -1;
    long long pending_select_until = 0;
    // Checkboxes representing the "Connected Controllers".
    std::array<QCheckBox*, 8> connected_controller_checkboxes{};
    ConfigureInputAdvanced* advanced = nullptr;

    Core::System& system;
};
