// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QColor>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QWidget;

/// The Controllers tab: how players are assigned, the handheld's own controller, and the live
/// list of players.
class ConfigurePlayerAssignment : public QWidget {
    Q_OBJECT

public:
    explicit ConfigurePlayerAssignment(QWidget* parent = nullptr);
    ~ConfigurePlayerAssignment() override;

    void ApplyConfiguration();

    /// How to bring a player's tab to the front (the settings dialog owns the tabs)
    void SetOpenPlayerTab(std::function<void(int)> open);

    /// Puts a widget above everything else in the tab
    void AddTopWidget(QWidget* widget);

    /// Rounded fields and outlined buttons, as the Controllers tab draws them
    static QString ModernComboStyle();
    static QString OutlinedButtonStyle();
    /// A style sheet for the current theme: under a dark look the palette colours become
    /// dark-friendly ones ("muted-text" is a note's colour)
    static QString Themed(QString style);
    /// The accent colour, as Themed gives palette(highlight)
    static QColor Accent(const QWidget* widget);
    /// A player's colour, as the overlay and the player badges draw it
    static QColor PlayerColor(int player);
    /// A controller's battery as a small cell (20x12), as the overlay draws it
    static QPixmap Battery(int percent, bool charging);
    /// A ✕ inside a drop-down, left of its arrow (hidden or shown by the caller)
    static QToolButton* AddClearButton(QComboBox* combo);
    /// Lets the open lists of a widget's drop-downs follow ModernComboStyle
    static void ModernizeCombos(QWidget* root);

    /// A row of joined buttons, one checked at a time (the group gets them, with these ids)
    static QWidget* MakeSegmented(QWidget* parent, const QStringList& labels,
                                  const std::vector<int>& ids, QButtonGroup* group);

private:
    void LoadConfiguration();
    /// The controller applet mode the controls select
    int SelectedMode() const;
    void OnModeChanged();
    void UpdateModeUi();
    void Refresh();
    void RefreshBuiltin();
    void RefreshPlayers();
    void OnBuiltinPicked(int index);
    void OnIdentifyPressed();
    void OnSwapPressed(int player);
    void RequestSwap(int a, int b);
    void RequestRumble(int player);

    /// The handler (Manual, Modern Console): under Manual the controller applet, under Modern
    /// Console whether this is a handheld
    enum Handler { HandlerManual, HandlerModern };
    QButtonGroup* handler_group = nullptr;
    QFrame* manual_panel = nullptr;
    QButtonGroup* manual_group = nullptr;
    QFrame* handheld_panel = nullptr;
    QAbstractButton* handheld_switch = nullptr;
    /// Bypass Steam Input (under Steam), a card of its own for both Modern Console modes
    QWidget* steam_panel = nullptr;
    QAbstractButton* steam_switch = nullptr;
    QLabel* steam_restart = nullptr;
    bool original_bypass = false;
    /// The Gameplay card
    QWidget* gameplay_box = nullptr;
    QAbstractButton* joycon_switch = nullptr;
    bool original_joycon = true;
    QButtonGroup* layout_group = nullptr;
    bool original_labels = false;
    QAbstractButton* pause_switch = nullptr;
    bool original_pause = false;
    QAbstractButton* lights_switch = nullptr;
    bool original_lights = true;
    QLabel* mode_info = nullptr;
    QWidget* display_box = nullptr;
    QWidget* about_box = nullptr;
    /// The scrolled page the cards stack in
    QVBoxLayout* content_layout = nullptr;
    QAbstractButton* overlay_toggle = nullptr;
    bool original_overlay = false;
    QAbstractButton* hints_toggle = nullptr;
    bool original_hints = true;
    /// The overlay's button history options (indented under Controller Overlay)
    QWidget* history_options = nullptr;
    QAbstractButton* history_toggle = nullptr;
    QSlider* history_size = nullptr;
    QSlider* history_seconds = nullptr;
    QAbstractButton* gaps_toggle = nullptr;
    bool original_history = true;
    int original_history_size = 10;
    int original_history_seconds = 5;
    bool original_history_gaps = false;
    /// Debug Probes, in the Troubleshooting card
    QWidget* debug_box = nullptr;
    QAbstractButton* probes_toggle = nullptr;
    bool original_probes = false;
    QWidget* now_box = nullptr;

    QWidget* builtin_row = nullptr;
    QComboBox* builtin_picker = nullptr;
    QPushButton* builtin_identify = nullptr;
    QLabel* builtin_status = nullptr;
    /// Removing the handheld's controller (the ✕), and allowing a removed one again
    QToolButton* builtin_remove = nullptr;
    QWidget* blocked_row = nullptr;
    QLabel* blocked_label = nullptr;
    QPushButton* blocked_allow = nullptr;
    QString builtin_signature;

    QWidget* players_box = nullptr;
    QVBoxLayout* players_list = nullptr;
    QLabel* players_note = nullptr;
    QString players_signature;
    /// A player's name is being typed (the rows wait)
    bool editing_name = false;
    /// The player whose Swap was pressed first, waiting for the second (-1: none)
    int swap_from = -1;
    /// The rows and the player each shows, for restyling them while a controller is active
    std::vector<QFrame*> player_rows;
    std::vector<int> row_players;
    /// When each player's row was clicked to rumble (blue for a moment)
    std::map<int, long long> rumbled_at;
    std::function<void(int)> open_player_tab;

    QTimer* timer = nullptr;
    /// The mode when the dialog opened, put back unless the dialog applies
    int original_mode = 0;
    bool applied = false;
};
