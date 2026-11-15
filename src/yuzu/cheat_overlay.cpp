// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/overlay_panels.h"
#include "yuzu/cheat_overlay.h"
#include "yuzu/overlay_panel.h"

namespace {

constexpr int PanelWidth = 300;
constexpr int HeaderHeight = 30;
constexpr int InfoHeight = 38;
constexpr int RowHeight = 26;
constexpr int Gap = 4;
constexpr int Inner = 8;
/// The most cheats in view at once; the wheel scrolls through the rest
constexpr int VisibleRows = 9;
/// The buttons stand for themselves in the hover and press state
constexpr int MenuButton = -2;
constexpr int ScrollUp = -3;
constexpr int ScrollDown = -4;
constexpr int EyeButton = -5;
constexpr int PagesButton = -6;
/// A menu entry (its index) in the hover and press state: PageRow - index
constexpr int PageRow = -100;
/// A memory editor row (its index) in the hover and press state: MemRow - index
constexpr int MemRow = -1000;
/// The memory editor's places found in view at once
constexpr int MemVisible = 6;
constexpr int MemFixedRows = 4;
constexpr int FooterHeight = 22;
/// With the controllers' focus: the buttons' hints under the cheats
constexpr int HintHeight = 22;

/// The cheats hidden from the overlay, per game (by cheat name)
QString HiddenFile() {
    return QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "cheat_overlay.ini"));
}

Common::OverlayPanel& Panel() {
    return Common::GetOverlayPanel(Common::OverlayPanelId::Cheats);
}

/// The menu button's icon: three bars
void MenuIcon(QPainter& painter, const QRectF& rect) {
    painter.setPen(QPen(QColor(245, 245, 245), 2.0, Qt::SolidLine, Qt::RoundCap));
    const qreal left = rect.center().x() - 6;
    const qreal right = rect.center().x() + 6;
    for (int bar = -1; bar <= 1; ++bar) {
        const qreal y = rect.center().y() + bar * 4.5;
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    }
    painter.setPen(Qt::NoPen);
}

/// An arrow head pointing right (tuck away), left (bring back), up or down (scroll), in the pen
/// colour given
void ArrowIcon(QPainter& painter, const QRectF& rect, Qt::ArrowType arrow, const QColor& color) {
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QPointF c = rect.center();
    constexpr qreal s = 4.0;
    if (arrow == Qt::RightArrow || arrow == Qt::LeftArrow) {
        const qreal d = arrow == Qt::RightArrow ? 1.0 : -1.0;
        const QPointF points[3] = {{c.x() - d * s / 2, c.y() - s},
                                   {c.x() + d * s / 2, c.y()},
                                   {c.x() - d * s / 2, c.y() + s}};
        painter.drawPolyline(points, 3);
    } else {
        const qreal d = arrow == Qt::UpArrow ? -1.0 : 1.0;
        const QPointF points[3] = {{c.x() - s, c.y() - d * s / 2},
                                   {c.x(), c.y() + d * s / 2},
                                   {c.x() + s, c.y() - d * s / 2}};
        painter.drawPolyline(points, 3);
    }
    painter.setPen(Qt::NoPen);
}

/// An eye (shown), or an eye struck through (hidden)
void EyeIcon(QPainter& painter, const QRectF& rect, bool open, const QColor& color) {
    const QPointF c = rect.center();
    painter.setPen(QPen(color, 1.6));
    painter.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(c.x() - 7, c.y());
    eye.quadTo(c.x(), c.y() - 7, c.x() + 7, c.y());
    eye.quadTo(c.x(), c.y() + 7, c.x() - 7, c.y());
    painter.drawPath(eye);
    painter.setBrush(color);
    painter.drawEllipse(c, 2.0, 2.0);
    if (!open) {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(c.x() - 7, c.y() + 6), QPointF(c.x() + 7, c.y() - 6));
    }
    painter.setPen(Qt::NoPen);
}

/// A controller button's hint: its name in a circle, then what it does; returns where the next
/// hint goes
qreal ButtonHint(QPainter& painter, qreal x, qreal center_y, const QString& button,
                 const QString& does) {
    namespace Style = GameOverlay::Style;
    // (A pill for a longer name, such as L/R)
    const qreal circle_w =
        (std::max)(qreal{14}, QFontMetrics{Style::SmallFont()}.horizontalAdvance(button) + 8.0);
    const QRectF circle(x, center_y - 7, circle_w, 14);
    Style::Pill(painter, circle, QColor(255, 255, 255, 60));
    painter.setFont(Style::SmallFont());
    painter.setPen(Style::Text);
    painter.drawText(circle, Qt::AlignCenter, button);
    const qreal text_w = QFontMetrics{Style::SmallFont()}.horizontalAdvance(does);
    painter.setPen(Style::DimText);
    painter.drawText(QRectF(circle.right() + 4, center_y - 8, text_w + 2, 16),
                     Qt::AlignVCenter | Qt::AlignLeft, does);
    return circle.right() + 4 + text_w + 12;
}

} // Anonymous namespace

CheatOverlay::CheatOverlay(std::function<void(int)> toggle_cheat_,
                           std::function<std::optional<double>(double)> ask_value_)
    : toggle_cheat{std::move(toggle_cheat_)}, ask_value{std::move(ask_value_)} {}

void CheatOverlay::Update(bool shown_, const QString& game_id_, const QString& build_id_,
                          const QString& version_, std::vector<Cheat> cheats_) {
    shown = shown_;
    if (!game_id_.isEmpty() && game_id_ != game_id) {
        // Another game: its own hidden cheats
        QSettings settings{HiddenFile(), QSettings::IniFormat};
        const QStringList names =
            settings.value(QStringLiteral("hidden/%1").arg(game_id_)).toStringList();
        hidden = {names.begin(), names.end()};
        choosing = false;
        scroll = 0;
        // (A game with cheats opens on them, one without on the menu)
        page = cheats_.empty() ? Page::Menu : Page::Cheats;
        auto_page = true;
        // (Without a cheat list it starts tucked away, its < button at the edge)
        collapsed = cheats_.empty();
        auto_collapsed = collapsed;
        // (The places found belong to the game before)
        scanner.Reset();
        mem_status.clear();
        mem_row = 0;
        mem_scroll = 0;
    }
    if (!game_id_.isEmpty()) {
        game_id = game_id_;
    }
    // (The cheats arrive after the game: its page follows, unless one was chosen)
    if (auto_page && page == Page::Menu && !cheats_.empty()) {
        page = Page::Cheats;
    }
    if (auto_collapsed && !cheats_.empty()) {
        collapsed = false;
        auto_collapsed = false;
    }
    build_id = build_id_;
    version = version_;
    cheats = std::move(cheats_);
    scroll =
        (std::clamp)(scroll, 0, (std::max)(0, static_cast<int>(Listed().size()) - VisibleRows));
    focus_pos = (std::clamp)(focus_pos, 0, (std::max)(0, static_cast<int>(Listed().size()) - 1));
    if (!shown) {
        hover = -1;
        pressed = -1;
    }
    Render();
}

bool CheatOverlay::HandleMouse(QEvent* event, const GRenderWindow& window) {
    // (The pointer moving with no button held comes to the render window as a hover move)
    const auto type = event->type() == QEvent::HoverMove ? QEvent::MouseMove : event->type();
    if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress &&
        type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick &&
        type != QEvent::Wheel) {
        return false;
    }
    if (!GameOverlay::Shown(Panel())) {
        return false;
    }
    const auto cursor = GameOverlay::CursorOn(Panel(), window);
    if (!cursor.inside) {
        if (type == QEvent::MouseButtonRelease) {
            pressed = -1;
        }
        if (hover != -1) {
            hover = -1;
            Render();
        }
        return false;
    }
    int target = -1;
    if (menu_rect.contains(cursor.at)) {
        target = MenuButton;
    } else if (up_rect.contains(cursor.at)) {
        target = ScrollUp;
    } else if (down_rect.contains(cursor.at)) {
        target = ScrollDown;
    } else if (eye_rect.contains(cursor.at)) {
        target = EyeButton;
    } else if (pages_rect.contains(cursor.at)) {
        target = PagesButton;
    }
    for (std::size_t i = 0; i < page_rects.size(); ++i) {
        if (page_rects[i].second.contains(cursor.at)) {
            target = PageRow - static_cast<int>(i);
        }
    }
    for (const auto& [row, rect] : mem_rects) {
        if (rect.contains(cursor.at)) {
            target = MemRow - row;
        }
    }
    for (const auto& [index, rect] : cheat_rects) {
        if (rect.contains(cursor.at)) {
            target = index;
        }
    }
    switch (type) {
    case QEvent::MouseMove:
        if (hover != target) {
            hover = target;
            Render();
        }
        break;
    case QEvent::MouseButtonPress:
        pressed = target;
        break;
    case QEvent::MouseButtonRelease:
        if (pressed == target && target == MenuButton) {
            collapsed = !collapsed;
            auto_collapsed = false;
            hover = -1;
            Render();
        } else if (pressed == target && (target == ScrollUp || target == ScrollDown)) {
            // A page at a time
            ScrollBy(target == ScrollUp ? -VisibleRows : VisibleRows);
        } else if (pressed == target && target == EyeButton) {
            hover = -1;
            ToggleChoosing();
        } else if (pressed == target && target == PagesButton) {
            hover = -1;
            Open(page == Page::Menu ? menu_from : Page::Menu);
        } else if (pressed == target && target <= MemRow) {
            mem_row = MemRow - target;
            if (mem_row == 1) {
                // The value, by touch or mouse: asked in a dialog
                if (const auto value = ask_value ? ask_value(mem_value) : std::nullopt) {
                    mem_value = *value;
                }
                Render();
            } else {
                MemoryActivate(mem_row);
            }
        } else if (pressed == target && target <= PageRow &&
                   PageRow - target < static_cast<int>(page_rects.size())) {
            hover = -1;
            Open(page_rects[static_cast<std::size_t>(PageRow - target)].first);
        } else if (pressed == target && target >= 0) {
            Activate(target);
        }
        pressed = -1;
        break;
    case QEvent::Wheel:
        if (!collapsed && page == Page::Cheats) {
            ScrollBy(-static_cast<QWheelEvent*>(event)->angleDelta().y() / 120);
        } else if (!collapsed && page == Page::Memory) {
            const int places = static_cast<int>(scanner.Results().size());
            mem_scroll =
                (std::clamp)(mem_scroll - static_cast<QWheelEvent*>(event)->angleDelta().y() / 120,
                             0, (std::max)(0, places - MemVisible));
            Render();
        }
        break;
    default:
        break;
    }
    // (Over the panel, the game gets no touch)
    event->accept();
    return true;
}

void CheatOverlay::Activate(int index) {
    if (index < 0 || index >= static_cast<int>(cheats.size())) {
        return;
    }
    const auto& cheat = cheats[static_cast<std::size_t>(index)];
    if (choosing) {
        if (!hidden.erase(cheat.name)) {
            hidden.insert(cheat.name);
        }
        QSettings settings{HiddenFile(), QSettings::IniFormat};
        const QStringList names{hidden.begin(), hidden.end()};
        if (names.isEmpty()) {
            settings.remove(QStringLiteral("hidden/%1").arg(game_id));
        } else {
            settings.setValue(QStringLiteral("hidden/%1").arg(game_id), names);
        }
        Render();
    } else if (!cheat.locked) {
        // (The menu's action redraws the panel through Update)
        toggle_cheat(index);
    }
}

void CheatOverlay::ToggleChoosing() {
    // Choosing what shows lists every cheat; done, only the shown ones. The highlight stays on
    // its cheat when that one is listed both ways.
    const std::vector<int> before = Listed();
    const int at = focus_pos < static_cast<int>(before.size())
                       ? before[static_cast<std::size_t>(focus_pos)]
                       : -1;
    choosing = !choosing;
    const std::vector<int> after = Listed();
    const auto found = std::find(after.begin(), after.end(), at);
    focus_pos = found != after.end() ? static_cast<int>(found - after.begin()) : 0;
    scroll = (std::clamp)(focus_pos - VisibleRows / 2, 0,
                          (std::max)(0, static_cast<int>(after.size()) - VisibleRows));
    Render();
}

void CheatOverlay::SetCombo(const QString& combo_) {
    if (combo != combo_) {
        combo = combo_;
        Render();
    }
}

void CheatOverlay::Collapse() {
    collapsed = true;
    auto_collapsed = false;
    hover = -1;
    Render();
}

bool CheatOverlay::Showing() const {
    return shown;
}

int CheatOverlay::HeaderButtons() const {
    return page == Page::Cheats ? 3 : 2;
}

bool CheatOverlay::Back() {
    if (on_header) {
        on_header = false;
    } else if (mem_editing) {
        mem_editing = false;
    } else if (choosing) {
        ToggleChoosing();
        return true;
    } else if (page != Page::Menu) {
        Open(Page::Menu);
        return true;
    } else {
        return false;
    }
    Render();
    return true;
}

std::vector<CheatOverlay::Page> CheatOverlay::MenuPages() {
    return {Page::Cheats, Page::Memory, Page::Info};
}

QString CheatOverlay::PageTitle(Page page) {
    switch (page) {
    case Page::Menu:
        return QObject::tr("Menu");
    case Page::Cheats:
        return QObject::tr("Cheats");
    case Page::Memory:
        return QObject::tr("Memory editor");
    case Page::Info:
        return QObject::tr("Game info");
    }
    return {};
}

void CheatOverlay::Open(Page to) {
    if (to == Page::Menu) {
        if (page != Page::Menu) {
            menu_from = page;
        }
        const auto pages = MenuPages();
        const auto at = std::find(pages.begin(), pages.end(), menu_from);
        menu_pos = at != pages.end() ? static_cast<int>(at - pages.begin()) : 0;
    }
    page = to;
    auto_page = false;
    if (page != Page::Cheats && choosing) {
        choosing = false;
    }
    Render();
}

void CheatOverlay::SetFocused(bool focused_) {
    if (focused == focused_) {
        return;
    }
    focused = focused_;
    on_header = false;
    if (!focused) {
        mem_editing = false;
    }
    if (focused) {
        collapsed = false;
        auto_collapsed = false;
        const int listed = static_cast<int>(Listed().size());
        focus_pos = (std::clamp)(scroll, 0, (std::max)(0, listed - 1));
    }
    Render();
}

void CheatOverlay::Navigate(Move move) {
    if (!focused || !Showing()) {
        return;
    }
    if (move == Move::Menu) {
        on_header = false;
        Open(page == Page::Menu ? menu_from : Page::Menu);
        return;
    }
    // On the header's buttons: left and right between them, down back to the rows
    if (on_header) {
        const int buttons = HeaderButtons();
        switch (move) {
        case Move::Left:
        case Move::Right:
            header_pos = (header_pos + (move == Move::Left ? buttons - 1 : 1)) % buttons;
            break;
        case Move::Down:
            on_header = false;
            break;
        case Move::Activate:
            // (Left to right: tuck away, the eye on the cheats, the menu)
            if (header_pos == 0) {
                on_header = false;
                Collapse();
            } else if (header_pos == buttons - 1) {
                on_header = false;
                Open(page == Page::Menu ? menu_from : Page::Menu);
            } else {
                ToggleChoosing();
            }
            return;
        default:
            break;
        }
        Render();
        return;
    }
    // Up from the first row: onto the header's buttons
    const int first_row = page == Page::Menu     ? menu_pos
                          : page == Page::Memory ? mem_row + (mem_editing ? 1 : 0)
                          : page == Page::Cheats ? focus_pos
                                                 : 0;
    if (move == Move::Up && first_row == 0 && (page != Page::Memory || mem_scroll == 0)) {
        // (The menu button first: the right-most)
        on_header = true;
        header_pos = HeaderButtons() - 1;
        Render();
        return;
    }
    if (page == Page::Menu) {
        const auto pages = MenuPages();
        const int n = static_cast<int>(pages.size());
        if (move == Move::Up || move == Move::Down) {
            menu_pos = (menu_pos + (move == Move::Up ? -1 : 1) + n) % n;
            Render();
        } else if (move == Move::Activate) {
            Open(pages[static_cast<std::size_t>((std::clamp)(menu_pos, 0, n - 1))]);
        }
        return;
    }
    if (page == Page::Memory) {
        MemoryMove(move);
        return;
    }
    if (page != Page::Cheats) {
        return;
    }
    const std::vector<int> listed = Listed();
    const int count = static_cast<int>(listed.size());
    switch (move) {
    case Move::Up:
    case Move::Down:
    case Move::PageUp:
    case Move::PageDown: {
        if (count == 0) {
            return;
        }
        const int step = move == Move::Up       ? -1
                         : move == Move::Down   ? 1
                         : move == Move::PageUp ? -VisibleRows
                                                : VisibleRows;
        // (One at a time wraps around the ends; a page stops at them)
        int to = focus_pos + step;
        if (move == Move::Up || move == Move::Down) {
            to = (to + count) % count;
        }
        focus_pos = (std::clamp)(to, 0, count - 1);
        // The highlight stays in view
        if (focus_pos < scroll) {
            scroll = focus_pos;
        } else if (focus_pos >= scroll + VisibleRows) {
            scroll = focus_pos - VisibleRows + 1;
        }
        Render();
        break;
    }
    case Move::Activate:
        if (focus_pos < count) {
            Activate(listed[static_cast<std::size_t>(focus_pos)]);
        }
        break;
    case Move::Choose:
        ToggleChoosing();
        break;
    case Move::Menu:
    case Move::Left:
    case Move::Right:
        // (The menu is handled above; left and right do nothing on the cheats)
        break;
    }
}

std::vector<int> CheatOverlay::Listed() const {
    std::vector<int> listed;
    for (int i = 0; i < static_cast<int>(cheats.size()); ++i) {
        if (choosing || !hidden.contains(cheats[static_cast<std::size_t>(i)].name)) {
            listed.push_back(i);
        }
    }
    return listed;
}

void CheatOverlay::ScrollBy(int rows) {
    const int last = (std::max)(0, static_cast<int>(Listed().size()) - VisibleRows);
    const int moved = (std::clamp)(scroll + rows, 0, last);
    if (moved != scroll) {
        scroll = moved;
        Render();
    }
}

void CheatOverlay::Tick() {
    // The places' values change in the game: drawn again (only when they did)
    if (shown && !collapsed && page == Page::Memory && scanner.HasResults()) {
        Render();
    }
}

int CheatOverlay::MemoryRowCount() const {
    const int places = static_cast<int>(scanner.Results().size());
    return MemFixedRows + (std::min)(places, MemVisible);
}

QString CheatOverlay::FormatValue(double value) const {
    if (mem_type == MemoryScanner::Type::F32) {
        return QString::number(value, 'f', 2);
    }
    return QString::number(static_cast<qulonglong>(std::llround((std::max)(0.0, value))));
}

void CheatOverlay::MemoryActivate(int row) {
    if (mem_busy) {
        return;
    }
    switch (row) {
    case 0: {
        // The type, the next one round (the places found are for one type)
        const int next = (static_cast<int>(mem_type) + 1) % 5;
        mem_type = static_cast<MemoryScanner::Type>(next);
        scanner.Reset();
        mem_status.clear();
        break;
    }
    case 1:
        mem_editing = !mem_editing;
        mem_digit = 0;
        break;
    case 2:
        RunScan(true);
        return;
    case 3:
        if (scanner.HasResults()) {
            RunScan(false);
            return;
        }
        break;
    default: {
        // A place found: set to the value
        const std::size_t index = static_cast<std::size_t>(mem_scroll + row - MemFixedRows);
        if (index < scanner.Results().size()) {
            const bool written = scanner.Write(scanner.Results()[index], mem_value);
            mem_status = written ? QObject::tr("Set to %1").arg(FormatValue(mem_value))
                                 : QObject::tr("Couldn't write there");
        }
        break;
    }
    }
    Render();
}

void CheatOverlay::RunScan(bool first) {
    if (mem_busy) {
        return;
    }
    // Said on the panel first: the scan holds Eden's window while it runs, but the game keeps
    // drawing the panel as it was
    mem_busy = true;
    mem_status = first ? QObject::tr("Searching the game's memory\u2026")
                       : QObject::tr("Checking the places found\u2026");
    Render();
    QTimer::singleShot(60, [this, first] {
        if (first) {
            const std::size_t found = scanner.FirstScan(mem_type, mem_value);
            mem_status =
                scanner.Truncated()
                    ? QObject::tr("%1+ found: change it in the game, then Next scan").arg(found)
                    : QObject::tr("%1 found").arg(found);
        } else {
            const std::size_t left = scanner.NextScan(mem_compare, mem_value);
            mem_status = QObject::tr("%1 left").arg(left);
        }
        mem_scroll = 0;
        mem_row = (std::min)(mem_row, MemoryRowCount() - 1);
        mem_busy = false;
        Render();
    });
}

void CheatOverlay::MemoryMove(Move move) {
    if (mem_busy) {
        return;
    }
    if (mem_editing) {
        // Left and right pick the digit, up and down change it, A is done
        const double step = std::pow(10.0, mem_digit);
        switch (move) {
        case Move::Left:
            mem_digit = (std::min)(mem_digit + 1, 9);
            break;
        case Move::Right:
            mem_digit = (std::max)(mem_digit - 1, 0);
            break;
        case Move::Up:
            mem_value += step;
            break;
        case Move::Down:
            mem_value = (std::max)(0.0, mem_value - step);
            break;
        case Move::Activate:
            mem_editing = false;
            break;
        default:
            break;
        }
        Render();
        return;
    }
    const int rows = MemoryRowCount();
    const int places = static_cast<int>(scanner.Results().size());
    switch (move) {
    case Move::Up:
        if (mem_row == MemFixedRows && mem_scroll > 0) {
            --mem_scroll;
        } else {
            mem_row = (mem_row - 1 + rows) % rows;
        }
        break;
    case Move::Down:
        if (mem_row == rows - 1 && mem_row >= MemFixedRows && mem_scroll + MemVisible < places) {
            ++mem_scroll;
        } else {
            mem_row = (mem_row + 1) % rows;
        }
        break;
    case Move::PageUp:
        mem_scroll = (std::max)(0, mem_scroll - MemVisible);
        break;
    case Move::PageDown:
        mem_scroll = (std::clamp)(mem_scroll + MemVisible, 0, (std::max)(0, places - MemVisible));
        break;
    case Move::Left:
    case Move::Right:
        if (mem_row == 0) {
            const int step = move == Move::Left ? 4 : 1;
            mem_type = static_cast<MemoryScanner::Type>((static_cast<int>(mem_type) + step) % 5);
            scanner.Reset();
            mem_status.clear();
        } else if (mem_row == 3) {
            // What the next scan keeps
            const int step = move == Move::Left ? 4 : 1;
            mem_compare =
                static_cast<MemoryScanner::Compare>((static_cast<int>(mem_compare) + step) % 5);
        }
        break;
    case Move::Activate:
        MemoryActivate(mem_row);
        return;
    default:
        break;
    }
    Render();
}

void CheatOverlay::Render() {
    auto& panel = Panel();
    if (!shown) {
        if (!drawn.isEmpty()) {
            GameOverlay::Publish(panel, nullptr);
            drawn.clear();
        }
        return;
    }
    // Drawn again only when what it shows changes (each image is uploaded again)
    QString state = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                        .arg(combo)
                        .arg(focused ? focus_pos : -1)
                        .arg(static_cast<int>(collapsed))
                        .arg(static_cast<int>(choosing))
                        .arg(hover)
                        .arg(scroll)
                        .arg(game_id, build_id)
                        .arg(cheats.size());
    state += QStringLiteral("|p%1|m%2|%3|h%4")
                 .arg(static_cast<int>(page))
                 .arg(menu_pos)
                 .arg(version)
                 .arg(focused && on_header ? header_pos : -1);
    if (page == Page::Memory) {
        state += QStringLiteral("|b%1").arg(static_cast<int>(mem_busy));
        state += QStringLiteral("|M%1|%2|%3|%4|%5|%6|%7|%8|c%9")
                     .arg(static_cast<int>(mem_type))
                     .arg(mem_value)
                     .arg(mem_row)
                     .arg(mem_scroll)
                     .arg(static_cast<int>(mem_editing))
                     .arg(mem_digit)
                     .arg(mem_status)
                     .arg(scanner.Results().size())
                     .arg(static_cast<int>(mem_compare));
        const auto& places = scanner.Results();
        for (int k = mem_scroll;
             k < (std::min)(static_cast<int>(places.size()), mem_scroll + MemVisible); ++k) {
            state += QStringLiteral("|%1").arg(scanner.Read(places[static_cast<std::size_t>(k)]));
        }
    }
    for (const auto& cheat : cheats) {
        state += QStringLiteral("|%1%2%3%4")
                     .arg(static_cast<int>(cheat.enabled))
                     .arg(static_cast<int>(cheat.locked))
                     .arg(cheat.name)
                     .arg(static_cast<int>(hidden.contains(cheat.name)));
    }
    if (state == drawn) {
        return;
    }
    drawn = state;

    namespace Style = GameOverlay::Style;
    const QFont title_font = Style::LabelFont();
    const QFont text_font = Style::TextFont();
    const QFont small_font = Style::SmallFont();
    const QFontMetrics text_fm{text_font};
    const int on_count = static_cast<int>(std::count_if(
        cheats.begin(), cheats.end(), [](const Cheat& cheat) { return cheat.enabled; }));
    const QColor hover_fill(45, 95, 200, 150);

    menu_rect = {};
    up_rect = {};
    down_rect = {};
    eye_rect = {};
    pages_rect = {};
    page_rects.clear();
    mem_rects.clear();
    cheat_rects.clear();

    // Tucked away: only the button that brings it back, against the screen's edge (a green dot
    // while any cheat is on)
    if (collapsed) {
        QImage image{HeaderHeight + 12, HeaderHeight + 2, QImage::Format_RGBA8888};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF pill(1, 1, HeaderHeight + 10, HeaderHeight);
        Style::Pill(painter, pill, hover == MenuButton ? hover_fill : Style::Fill);
        // (An arrow back out, the opposite of the one that tucked it away)
        ArrowIcon(painter, pill, Qt::LeftArrow, QColor(245, 245, 245));
        if (on_count > 0) {
            painter.setBrush(QColor(120, 220, 140));
            painter.drawEllipse(QPointF(pill.right() - 7, pill.top() + 7), 3.5, 3.5);
        }
        painter.end();
        menu_rect = pill.toRect();
        GameOverlay::Publish(panel, &image);
        return;
    }

    // The header's ☰ button: the menu of pages (lit while the menu shows)
    const auto pages_button = [&](QPainter& painter, const QRectF& header, qreal right) {
        const QRectF button(right - 30, header.top() + 4, 30, HeaderHeight - 8);
        Style::Pill(painter, button,
                    page == Page::Menu || hover == PagesButton ? hover_fill
                                                               : QColor(255, 255, 255, 30));
        MenuIcon(painter, button);
        if (focused && on_header && header_pos == HeaderButtons() - 1) {
            painter.setPen(QPen(QColor(245, 245, 245, 230), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(button.adjusted(-1.5, -1.5, 1.5, 1.5), 8, 8);
            painter.setPen(Qt::NoPen);
        }
        pages_rect = button.toRect();
        return button;
    };

    // The menu and the info page: rows of a label and a value
    if (page != Page::Cheats) {
        struct Row {
            QString label;
            QString value;
            bool selectable;
        };
        std::vector<Row> rows;
        if (page == Page::Menu) {
            for (const Page entry : MenuPages()) {
                QString value;
                if (entry == Page::Cheats) {
                    value = cheats.empty() ? QObject::tr("none for this game")
                                           : QObject::tr("%1 of %2 on")
                                                 .arg(on_count)
                                                 .arg(static_cast<int>(cheats.size()));
                } else if (entry == Page::Info) {
                    value = build_id;
                }
                rows.push_back({PageTitle(entry), value, true});
            }
        } else if (page == Page::Memory) {
            QString value = FormatValue(mem_value);
            if (mem_editing && mem_type != MemoryScanner::Type::F32) {
                // The digit being changed, in brackets
                const QString digits = value.rightJustified(mem_digit + 1, QLatin1Char('0'));
                const int at = static_cast<int>(digits.size()) - 1 - mem_digit;
                value = digits.left(at) + QLatin1Char('[') + digits.mid(at, 1) + QLatin1Char(']') +
                        digits.mid(at + 1);
            }
            rows.push_back(
                {QObject::tr("Type"),
                 QStringLiteral("‹ %1 ›").arg(QString::fromLatin1(MemoryScanner::Name(mem_type))),
                 true});
            rows.push_back({QObject::tr("Value"), value, true});
            rows.push_back(
                {scanner.HasResults() ? QObject::tr("New scan") : QObject::tr("First scan"),
                 QObject::tr("find the value"), true});
            rows.push_back({QObject::tr("Next scan"),
                            scanner.HasResults()
                                ? QStringLiteral("\u2039 %1 \u203a")
                                      .arg(QString::fromLatin1(MemoryScanner::Name(mem_compare)))
                                : QObject::tr("first scan first"),
                            true});
            const auto& places = scanner.Results();
            for (int k = mem_scroll;
                 k < (std::min)(static_cast<int>(places.size()), mem_scroll + MemVisible); ++k) {
                const u64 address = places[static_cast<std::size_t>(k)];
                rows.push_back({QStringLiteral("%1").arg(address, 10, 16, QLatin1Char('0')),
                                FormatValue(scanner.Read(address)), true});
            }
        } else {
            rows.push_back({QObject::tr("Game ID"), game_id, false});
            rows.push_back({QObject::tr("Version"), version, false});
            rows.push_back({QObject::tr("Build ID"), build_id, false});
            rows.push_back({QObject::tr("Cheat file"), build_id + QStringLiteral(".txt"), false});
            rows.push_back(
                {QObject::tr("In"), QStringLiteral("load/%1/<mod>/cheats/").arg(game_id), false});
        }
        // (The memory editor says what it found, and how to use it, in a line of its own)
        const bool status_line = page == Page::Memory;
        const QString status = mem_status.isEmpty()
                                   ? QObject::tr("Back up your save before changing values")
                                   : mem_status;
        const int rows_top = HeaderHeight + Gap + (status_line ? HintHeight + Gap : 0);
        const int hint_top = rows_top + static_cast<int>(rows.size()) * (RowHeight + Gap);
        // (With the focus: the notice that the game gets no input, then the buttons' hints)
        const int hint_rows = focused ? 2 : combo.isEmpty() ? 0 : 1;
        const int height =
            (hint_rows > 0 ? hint_top + hint_rows * HintHeight + (hint_rows - 1) * Gap
                           : hint_top - Gap) +
            2;
        QImage image{PanelWidth + 2, height, QImage::Format_RGBA8888};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QRectF header(1, 1, PanelWidth, HeaderHeight);
        Style::Pill(painter, header, Style::Fill);
        const QRectF menu(header.left() + 4, header.top() + 4, HeaderHeight - 8 + 6,
                          HeaderHeight - 8);
        Style::Pill(painter, menu, hover == MenuButton ? hover_fill : QColor(255, 255, 255, 30));
        ArrowIcon(painter, menu, Qt::RightArrow, QColor(245, 245, 245));
        if (focused && on_header && header_pos == 0) {
            painter.setPen(QPen(QColor(245, 245, 245, 230), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(menu.adjusted(-1.5, -1.5, 1.5, 1.5), 8, 8);
            painter.setPen(Qt::NoPen);
        }
        menu_rect = menu.toRect();
        painter.setFont(title_font);
        painter.setPen(Style::Text);
        painter.drawText(QRectF(menu.right() + 8, header.top(), 160, header.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, PageTitle(page));
        pages_button(painter, header, header.right() - 4);
        if (status_line) {
            const QRectF line(1, HeaderHeight + Gap, PanelWidth, HintHeight);
            // (Lit while a scan runs)
            Style::Pill(painter, line, mem_busy ? QColor(45, 95, 200, 200) : Style::Fill);
            painter.setFont(small_font);
            painter.setPen(mem_busy ? Style::Text : Style::DimText);
            painter.drawText(line.adjusted(Inner + 4, 0, -(Inner + 4), 0),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             QFontMetrics{small_font}.elidedText(status, Qt::ElideRight,
                                                                 PanelWidth - 2 * (Inner + 4)));
        }
        const int highlight = page == Page::Menu ? menu_pos : page == Page::Memory ? mem_row : -1;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row_info = rows[i];
            const QRectF row(1, rows_top + static_cast<int>(i) * (RowHeight + Gap), PanelWidth,
                             RowHeight);
            const int target =
                page == Page::Memory ? MemRow - static_cast<int>(i) : PageRow - static_cast<int>(i);
            const bool highlighted =
                row_info.selectable && focused && !on_header && static_cast<int>(i) == highlight;
            Style::Pill(painter, row,
                        row_info.selectable && (hover == target || highlighted) ? hover_fill
                                                                                : Style::Fill);
            if (highlighted) {
                painter.setPen(QPen(QColor(245, 245, 245, 210), 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(row.adjusted(0.75, 0.75, -0.75, -0.75),
                                        (RowHeight - 1.5) / 2.0, (RowHeight - 1.5) / 2.0);
                painter.setPen(Qt::NoPen);
            }
            const QRectF text_rect = row.adjusted(Inner + 4, 0, -(Inner + 4), 0);
            painter.setFont(row_info.selectable ? text_font : small_font);
            painter.setPen(row_info.selectable ? Style::Text : Style::DimText);
            painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, row_info.label);
            painter.setFont(small_font);
            painter.setPen(QColor(225, 225, 230));
            const int label_w = QFontMetrics{painter.font()}.horizontalAdvance(row_info.label) + 24;
            painter.drawText(
                text_rect, Qt::AlignVCenter | Qt::AlignRight,
                QFontMetrics{small_font}.elidedText(row_info.value, Qt::ElideMiddle,
                                                    static_cast<int>(text_rect.width()) - label_w));
            if (row_info.selectable && page == Page::Menu) {
                page_rects.emplace_back(MenuPages()[i], row.toRect());
            } else if (row_info.selectable && page == Page::Memory) {
                mem_rects.emplace_back(static_cast<int>(i), row.toRect());
            }
        }
        if (focused) {
            const QRectF notice(1, hint_top, PanelWidth, HintHeight);
            Style::Pill(painter, notice, QColor(45, 95, 200, 200));
            painter.setFont(small_font);
            painter.setPen(Style::Text);
            painter.drawText(notice, Qt::AlignCenter,
                             QObject::tr("Controllers are on the overlay: the game gets no input"));
        }
        if (hint_rows > 0) {
            const QRectF hints(1, focused ? hint_top + HintHeight + Gap : hint_top, PanelWidth,
                               HintHeight);
            Style::Pill(painter, hints, Style::Fill);
            qreal x = hints.left() + 12;
            const qreal y = hints.center().y();
            if (focused && page == Page::Memory && mem_editing) {
                x = ButtonHint(painter, x, y, QStringLiteral("←→"), QObject::tr("Digit"));
                x = ButtonHint(painter, x, y, QStringLiteral("↑↓"), QObject::tr("Change"));
                ButtonHint(painter, x, y, QStringLiteral("A"), QObject::tr("Done"));
            } else if (focused) {
                if (page == Page::Menu) {
                    x = ButtonHint(painter, x, y, QStringLiteral("A"), QObject::tr("Open"));
                } else if (page == Page::Memory) {
                    x = ButtonHint(painter, x, y, QStringLiteral("A"),
                                   mem_row >= MemFixedRows ? QObject::tr("Set")
                                                           : QObject::tr("Use"));
                }
                if (page == Page::Memory && !on_header && (mem_row == 0 || mem_row == 3)) {
                    x = ButtonHint(painter, x, y, QStringLiteral("\u2190\u2192"),
                                   mem_row == 0 ? QObject::tr("Type") : QObject::tr("Mode"));
                } else {
                    x = ButtonHint(painter, x, y, QStringLiteral("X"),
                                   page == Page::Menu ? QObject::tr("Close menu")
                                                      : QObject::tr("Menu"));
                }
                ButtonHint(painter, x, y, QStringLiteral("B"), QObject::tr("Back"));
            } else {
                ButtonHint(painter, x, y, combo, QObject::tr("Use with a controller"));
            }
        }
        painter.end();
        GameOverlay::Publish(panel, &image);
        return;
    }

    const int count = static_cast<int>(cheats.size());
    const std::vector<int> listed = Listed();
    const int listed_count = static_cast<int>(listed.size());
    const int first = scroll;
    const int last = (std::min)(listed_count, first + VisibleRows);
    const bool more = listed_count > VisibleRows;
    // (Every cheat hidden: a line that says so)
    const int rows = (std::max)(1, last - first);
    const int rows_top = HeaderHeight + Gap + InfoHeight + Gap;
    const int footer_top = rows_top + rows * (RowHeight + Gap);
    const int hint_top = more ? footer_top + FooterHeight + Gap : footer_top;
    // (Without the focus: how to give the panel to the controllers, when there is a combo)
    const int hint_rows = focused ? 2 : combo.isEmpty() ? 0 : 1;
    const int height = (hint_rows > 0 ? hint_top + hint_rows * HintHeight + (hint_rows - 1) * Gap
                                      : hint_top - Gap) +
                       2;
    const int width = PanelWidth + 2;

    QImage image{width, height, QImage::Format_RGBA8888};
    image.fill(Qt::transparent);
    QPainter painter{&image};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // The header: the button that tucks it away, the title and how many are on
    const QRectF header(1, 1, PanelWidth, HeaderHeight);
    Style::Pill(painter, header, Style::Fill);
    const QRectF menu(header.left() + 4, header.top() + 4, HeaderHeight - 8 + 6, HeaderHeight - 8);
    Style::Pill(painter, menu, hover == MenuButton ? hover_fill : QColor(255, 255, 255, 30));
    ArrowIcon(painter, menu, Qt::RightArrow, QColor(245, 245, 245));
    if (focused && on_header && header_pos == 0) {
        painter.setPen(QPen(QColor(245, 245, 245, 230), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(menu.adjusted(-1.5, -1.5, 1.5, 1.5), 8, 8);
        painter.setPen(Qt::NoPen);
    }
    menu_rect = menu.toRect();
    painter.setFont(title_font);
    painter.setPen(Style::Text);
    painter.drawText(QRectF(menu.right() + 8, header.top(), 120, header.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, QObject::tr("Cheats"));
    // The eye: choose which cheats show (lit while choosing)
    // (The menu button right-most, as on every page; the eye left of it)
    const QRectF pages = pages_button(painter, header, header.right() - 4);
    const QRectF eye(pages.left() - 4 - 30, header.top() + 4, 30, HeaderHeight - 8);
    Style::Pill(painter, eye,
                choosing || hover == EyeButton ? hover_fill : QColor(255, 255, 255, 30));
    EyeIcon(painter, eye, true, QColor(245, 245, 245));
    if (focused && on_header && header_pos == 1) {
        painter.setPen(QPen(QColor(245, 245, 245, 230), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(eye.adjusted(-1.5, -1.5, 1.5, 1.5), 8, 8);
        painter.setPen(Qt::NoPen);
    }
    eye_rect = eye.toRect();
    painter.setFont(small_font);
    const QRectF count_rect = header.adjusted(0, 0, -(header.right() - eye.left() + 8), 0);
    if (choosing) {
        painter.setPen(Style::DimText);
        painter.drawText(count_rect, Qt::AlignVCenter | Qt::AlignRight,
                         QObject::tr("Tap to hide or show"));
    } else {
        painter.setPen(on_count > 0 ? QColor(120, 220, 140) : Style::DimText);
        painter.drawText(count_rect, Qt::AlignVCenter | Qt::AlignRight,
                         QObject::tr("%1 of %2 on").arg(on_count).arg(count));
    }

    // The game: its title id and the build id its cheat files are named after
    const QRectF info(1, header.bottom() + Gap, PanelWidth, InfoHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Style::Fill);
    painter.drawRoundedRect(info, 12, 12);
    const auto info_line = [&](int line, const QString& label, const QString& value) {
        const QRectF at(info.left() + Inner + 4, info.top() + 3 + line * 16,
                        info.width() - (Inner + 4) * 2, 16);
        painter.setFont(small_font);
        painter.setPen(Style::DimText);
        painter.drawText(at, Qt::AlignVCenter | Qt::AlignLeft, label);
        painter.setPen(QColor(225, 225, 230));
        painter.drawText(at, Qt::AlignVCenter | Qt::AlignRight, value);
    };
    info_line(0, QObject::tr("Game ID"), game_id);
    info_line(1, QObject::tr("Build ID"), build_id);

    // The cheats: a pill each, with its switch (or, while choosing, whether it shows)
    constexpr int switch_w = 34;
    constexpr int switch_h = 18;
    if (listed.empty()) {
        const QRectF row(1, rows_top, PanelWidth, RowHeight);
        Style::Pill(painter, row, Style::Fill);
        painter.setFont(text_font);
        painter.setPen(Style::DimText);
        painter.drawText(row, Qt::AlignCenter,
                         cheats.empty() ? QObject::tr("No cheats for this game (see Game info)")
                                        : QObject::tr("All cheats are hidden"));
    }
    for (int k = first; k < last; ++k) {
        const int i = listed[static_cast<std::size_t>(k)];
        const auto& cheat = cheats[static_cast<std::size_t>(i)];
        const bool is_hidden = hidden.contains(cheat.name);
        const int y = rows_top + (k - first) * (RowHeight + Gap);
        const QRectF row(1, y, PanelWidth, RowHeight);
        const bool highlighted = focused && !on_header && k == focus_pos;
        Style::Pill(painter, row,
                    (hover == i || highlighted) && (choosing || !cheat.locked) ? hover_fill
                                                                               : Style::Fill);
        // The controllers' highlight: an outline, also on a cheat that can't be switched
        if (highlighted) {
            painter.setPen(QPen(QColor(245, 245, 245, 210), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(row.adjusted(0.75, 0.75, -0.75, -0.75), (RowHeight - 1.5) / 2.0,
                                    (RowHeight - 1.5) / 2.0);
            painter.setPen(Qt::NoPen);
        }
        const QRectF toggle(row.right() - Inner - switch_w, row.center().y() - switch_h / 2.0,
                            switch_w, switch_h);
        if (choosing) {
            EyeIcon(painter, toggle, !is_hidden,
                    is_hidden ? Style::DimText : QColor(245, 245, 245));
        } else {
            Style::Switch(painter, toggle, cheat.enabled || cheat.locked, cheat.locked);
        }
        painter.setFont(text_font);
        painter.setPen(cheat.locked || (choosing && is_hidden) ? Style::DimText : Style::Text);
        const int name_w = static_cast<int>(toggle.left() - row.left()) - Inner * 2 - 6;
        const QString name =
            cheat.locked ? QObject::tr("%1 (master code)").arg(cheat.name) : cheat.name;
        painter.drawText(QRectF(row.left() + Inner + 4, row.top(), name_w, row.height()),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         text_fm.elidedText(name, Qt::ElideRight, name_w));
        cheat_rects.emplace_back(i, row.toRect());
    }

    // More than fit: which ones are in view, between buttons that page up and down (the
    // wheel scrolls too)
    if (more) {
        const QRectF footer(PanelWidth / 2.0 - 80, footer_top, 160, FooterHeight);
        Style::Pill(painter, footer, Style::Fill);
        const auto scroll_button = [&](const QRectF& rect, int which, Qt::ArrowType arrow,
                                       bool usable) {
            Style::Pill(painter, rect,
                        hover == which && usable ? hover_fill : QColor(255, 255, 255, 30));
            ArrowIcon(painter, rect, arrow, usable ? QColor(245, 245, 245) : Style::DimText);
            return usable ? rect.toRect() : QRect{};
        };
        up_rect = scroll_button(QRectF(footer.left() + 3, footer.top() + 3, 34, FooterHeight - 6),
                                ScrollUp, Qt::UpArrow, first > 0);
        down_rect =
            scroll_button(QRectF(footer.right() - 3 - 34, footer.top() + 3, 34, FooterHeight - 6),
                          ScrollDown, Qt::DownArrow, last < listed_count);
        painter.setFont(small_font);
        painter.setPen(Style::DimText);
        painter.drawText(footer, Qt::AlignCenter,
                         QStringLiteral("%1–%2 / %3").arg(first + 1).arg(last).arg(listed_count));
    }

    if (!focused && !combo.isEmpty()) {
        const QRectF hint(1, hint_top, PanelWidth, HintHeight);
        Style::Pill(painter, hint, Style::Fill);
        ButtonHint(painter, hint.left() + 12, hint.center().y(), combo,
                   QObject::tr("Use the cheats with a controller"));
    }

    // With the controllers' focus: what their buttons do
    if (focused) {
        // (Said plainly: the game is not broken, its controllers are here)
        const QRectF notice(1, hint_top, PanelWidth, HintHeight);
        Style::Pill(painter, notice, QColor(45, 95, 200, 200));
        painter.setFont(small_font);
        painter.setPen(Style::Text);
        painter.drawText(notice, Qt::AlignCenter,
                         QObject::tr("Controllers are on the cheats: the game gets no input"));
        const QRectF hints(1, hint_top + HintHeight + Gap, PanelWidth, HintHeight);
        Style::Pill(painter, hints, Style::Fill);
        qreal x = hints.left() + 12;
        const qreal y = hints.center().y();
        x = ButtonHint(painter, x, y, QStringLiteral("A"),
                       choosing ? QObject::tr("Hide/show") : QObject::tr("Switch"));
        x = ButtonHint(painter, x, y, QStringLiteral("Y"),
                       choosing ? QObject::tr("Done") : QObject::tr("Choose"));
        // (L/R pages through a long list too; left out, so B always fits)
        x = ButtonHint(painter, x, y, QStringLiteral("X"), QObject::tr("Menu"));
        ButtonHint(painter, x, y, QStringLiteral("B"), QObject::tr("Back"));
    }
    painter.end();
    GameOverlay::Publish(panel, &image);
}
