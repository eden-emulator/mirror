// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <set>
#include <vector>

#include <QRect>
#include <QString>

#include "yuzu/memory_scanner.h"

class GRenderWindow;
class QEvent;

/// The running game's cheats and tools drawn over the game (top right), so they can be used also
/// where no window shows over the game (Steam's Game Mode). Its arrow button tucks it away
/// against the screen's right edge, where its < button brings it back. Its ☰ button opens
/// its menu of pages (Cheats, Game info); it works without a cheat list too. On the Cheats page
/// the eye button chooses which cheats show (remembered per game). A controller can take it
/// (focus): rows are highlighted, moved through and used with the controller's buttons.
/// Drawn as an overlay panel.
class CheatOverlay {
public:
    /// What a controller does while the panel has the focus
    enum class Move {
        Up,
        Down,
        PageUp,
        PageDown,
        /// Switch the highlighted cheat (while choosing: hide or show it)
        Activate,
        /// Choose which cheats show, or be done choosing (the eye button)
        Choose,
        /// Open the menu of pages, or go back to the page it was opened from
        Menu,
        /// Left and right (the memory editor's type and digits)
        Left,
        Right,
    };

    /// What the panel shows
    enum class Page {
        Menu,
        Cheats,
        Memory,
        Info,
    };

    struct Cheat {
        QString name;
        bool enabled = false;
        /// A master code: always on, can't be turned off
        bool locked = false;
    };

    /// Turns a cheat (its index in the list) on or off: the menu's own action does it, so the
    /// menu and what is remembered stay in step
    /// `ask_value` asks for a number in a dialog (the memory editor's value, by touch or mouse);
    /// nothing when cancelled
    CheatOverlay(std::function<void(int)> toggle_cheat,
                 std::function<std::optional<double>(double)> ask_value);

    /// What to show: the game's ids, version and cheats (maybe none); hidden when not shown
    void Update(bool shown, const QString& game_id, const QString& build_id, const QString& version,
                std::vector<Cheat> cheats);

    /// The mouse over the panel (hover, clicks, the wheel to scroll): true when it took the
    /// event, so the game doesn't get it
    bool HandleMouse(QEvent* event, const GRenderWindow& window);

    /// Whether the panel is up (what the focus needs)
    [[nodiscard]] bool Showing() const;
    /// The controllers take the panel (it opens out, a cheat is highlighted and the buttons'
    /// hints show), or give it back
    void SetFocused(bool focused);
    void Navigate(Move move);
    /// A step back with a controller (B): out of editing or choosing, then from a page to the
    /// menu; false on the menu (the controllers go back to the game)
    bool Back();
    /// Tucked away against the screen's edge (its < button brings it back)
    void Collapse();
    [[nodiscard]] bool Collapsed() const {
        return collapsed;
    }
    /// The controller combo that gives the panel to the controllers, as shown ("L3+R3"; ""
    /// none): said under the cheats while the controllers don't have it
    void SetCombo(const QString& combo);
    /// Called often while the panel is up: the memory editor's values are read again
    void Tick();

private:
    void Render();
    /// A cheat (its index) clicked or activated: switched, or while choosing hidden or shown
    void Activate(int index);
    /// Choosing which cheats show, or done
    void ToggleChoosing();
    /// Moves the cheats in view by some rows (within the list)
    void ScrollBy(int rows);
    /// The cheats listed (their indices): the shown ones, or every one while choosing
    [[nodiscard]] std::vector<int> Listed() const;
    /// The menu's entries, in order
    [[nodiscard]] static std::vector<Page> MenuPages();
    /// Shows a page (the menu remembers where it was opened from)
    void Open(Page to);
    [[nodiscard]] static QString PageTitle(Page page);
    /// The memory editor's rows: type, value, first scan, next scan, then the places found
    [[nodiscard]] int MemoryRowCount() const;
    void MemoryActivate(int row);
    void MemoryMove(Move move);
    [[nodiscard]] QString FormatValue(double value) const;

    std::function<void(int)> toggle_cheat;
    std::function<std::optional<double>(double)> ask_value;
    /// The memory editor: what it searches for and changes to, and the places found
    MemoryScanner scanner;
    MemoryScanner::Type mem_type = MemoryScanner::Type::U32;
    MemoryScanner::Compare mem_compare = MemoryScanner::Compare::Equal;
    double mem_value = 0;
    int mem_row = 0;
    int mem_scroll = 0;
    /// Editing the value digit by digit (with a controller), and the digit (0: ones)
    bool mem_editing = false;
    /// A scan is about to run or running (the panel says so; presses wait)
    bool mem_busy = false;
    /// Runs a scan after saying so on the panel
    void RunScan(bool first);
    int mem_digit = 0;
    QString mem_status;
    std::vector<std::pair<int, QRect>> mem_rects;
    bool shown = false;
    bool collapsed = false;
    /// Tucked away because the game had no cheat list; they open it when they arrive
    bool auto_collapsed = false;
    /// Choosing which cheats show (the eye button): every cheat listed, each with its eye
    bool choosing = false;
    /// The cheats hidden from the panel (by name), for this game
    std::set<QString> hidden;
    QString game_id;
    QString build_id;
    QString version;
    Page page = Page::Cheats;
    /// The page was chosen for the player (not by them): it follows the cheats arriving
    bool auto_page = true;
    /// The controllers' highlight is on the header's buttons (☰, and the eye on the cheats), and
    /// which one
    bool on_header = false;
    int header_pos = 0;
    /// The header's buttons the controllers can reach on this page
    [[nodiscard]] int HeaderButtons() const;
    /// The page the menu was opened from, and the menu's highlighted entry
    Page menu_from = Page::Cheats;
    int menu_pos = 0;
    std::vector<Cheat> cheats;
    /// The first cheat in view, when there are more than fit
    int scroll = 0;
    /// The controllers have the panel, and the highlighted cheat (its place among the listed)
    bool focused = false;
    int focus_pos = 0;
    QString combo;
    /// The cheat (or a button: menu, scroll up, scroll down) under the cursor, and the one
    /// pressed; -1 none
    int hover = -1;
    int pressed = -1;

    /// Where things are in the drawn image, for the mouse
    QRect menu_rect;
    QRect up_rect;
    QRect down_rect;
    QRect eye_rect;
    QRect pages_rect;
    std::vector<std::pair<Page, QRect>> page_rects;
    std::vector<std::pair<int, QRect>> cheat_rects;
    /// What was drawn last, to draw again only when it changes
    QString drawn;
};
