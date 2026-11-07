// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <tuple>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "common/controller_console_state.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "qt_common/config/uisettings.h"
#include "yuzu/configuration/configure_player_assignment.h"
#include "yuzu/configuration/input_profiles.h"

namespace {

/// A dark look: one of Eden's dark themes, or the system's dark mode under the default theme
bool DarkUi() {
    return UISettings::IsDarkTheme() ||
           QApplication::palette().color(QPalette::Window).lightness() < 128;
}

bool IsAutomatic(Settings::AppletMode mode) {
    return mode == Settings::AppletMode::ConsoleMode ||
           mode == Settings::AppletMode::HandheldConsoleMode;
}

constexpr int MaxPlayers = 8;

/// The players' colours, as the overlay draws their badges
constexpr std::array<QColor, 8> PlayerColors{
    QColor(79, 142, 247),  QColor(239, 91, 91),  QColor(47, 191, 113), QColor(245, 184, 46),
    QColor(155, 108, 242), QColor(242, 140, 56), QColor(46, 196, 201), QColor(227, 107, 174),
};

/// An on/off switch: a rounded track with a knob, like a phone's settings
class ToggleSwitch : public QAbstractButton {
public:
    explicit ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override {
        return {42, 24};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
        QColor fill = isChecked() ? ConfigurePlayerAssignment::Accent(this)
                                  : (DarkUi() ? QColor(255, 255, 255, 70)
                                              : palette().mid().color());
        if (!isEnabled()) {
            fill.setAlpha(110);
        }
        painter.setBrush(fill);
        painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        const qreal knob = track.height() - 6;
        const qreal x = isChecked() ? track.right() - 3 - knob : track.left() + 3;
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(x, track.top() + 3, knob, knob));
    }
};
constexpr char PlayerMime[] = "application/x-eden-player";

/// Keeps a drop-down's ✕ inside it, left of its arrow
class ClearButtonPlacer : public QObject {
public:
    ClearButtonPlacer(QComboBox* combo_, QToolButton* button_)
        : QObject(combo_), combo{combo_}, button{button_} {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == combo &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            button->move(combo->width() - 22 - button->width(),
                         (combo->height() - button->height()) / 2);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QComboBox* combo;
    QToolButton* button;
};

/// The power symbol on Turn Off: white, grey while it waits
QIcon PowerIcon() {
    const auto draw = [](const QColor& color) {
        QPixmap pixmap(QSize(14, 14) * 2);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter{&pixmap};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(QRectF(2, 2.5, 10, 10), 125 * 16, 290 * 16);
        painter.drawLine(QPointF(7, 1), QPointF(7, 7));
        painter.end();
        return pixmap;
    };
    QIcon icon;
    icon.addPixmap(draw(Qt::white), QIcon::Normal);
    icon.addPixmap(draw(QColor(138, 143, 150)), QIcon::Disabled);
    return icon;
}

/// The edit field of a player's name: Enter and Esc don't reach the dialog
class PlayerNameEdit : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

    std::function<void()> on_apply;
    std::function<void()> on_cancel;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            event->accept();
            if (on_apply) {
                on_apply();
            }
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            event->accept();
            if (on_cancel) {
                on_cancel();
            }
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

/// A setting's row: its title in bold, a line about it, and its control on the right
void AddOptionRow(QBoxLayout* to, QWidget* parent, const QString& title, const QString& note,
                  QWidget* control) {
    auto* line = new QHBoxLayout();
    auto* text = new QVBoxLayout();
    text->setSpacing(1);
    auto* title_label = new QLabel(title, parent);
    QFont font = title_label->font();
    font.setBold(true);
    title_label->setFont(font);
    auto* note_label = new QLabel(note, parent);
    note_label->setWordWrap(true);
    note_label->setObjectName(QStringLiteral("note"));
    text->addWidget(title_label);
    text->addWidget(note_label);
    line->addLayout(text, 1);
    line->addWidget(control, 0, Qt::AlignVCenter);
    to->addLayout(line);
}

/// A controller's battery, as the overlay draws it
QPixmap BatteryIcon(int percent, bool charging) {
    QPixmap pixmap(QSize(20, 12) * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF cell(1, 2, 15, 8);
    painter.setPen(QPen(QColor(150, 155, 162), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(cell, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(150, 155, 162));
    painter.drawRect(QRectF(cell.right() + 0.6, cell.center().y() - 1.5, 1.8, 3));
    painter.setBrush(percent <= 15 && !charging ? QColor(220, 60, 60) : QColor(70, 180, 100));
    painter.drawRect(QRectF(cell.left() + 1.5, cell.top() + 1.5,
                            (cell.width() - 3) * (std::clamp)(percent, 0, 100) / 100.0,
                            cell.height() - 3));
    if (charging) {
        painter.setPen(QPen(Qt::white, 1.2));
        const QPointF c = cell.center();
        const QPointF bolt[3] = {QPointF(c.x() + 1, c.y() - 3), QPointF(c.x() - 1.5, c.y() + 0.5),
                                 QPointF(c.x() + 1.5, c.y() - 0.5)};
        painter.drawPolyline(bolt, 3);
        painter.drawLine(bolt[2], QPointF(c.x() - 1, c.y() + 3));
    }
    painter.end();
    return pixmap;
}

/// The name's icons (edit, apply, remove), drawn so they look the same everywhere
QIcon NameIcon(int kind, const QColor& color) {
    QPixmap pixmap(QSize(14, 14) * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (kind == 0) { // pencil
        painter.drawLine(QPointF(3, 11), QPointF(10.5, 3.5));
        painter.drawLine(QPointF(9, 2), QPointF(12, 5));
        painter.drawLine(QPointF(2.5, 11.5), QPointF(2, 12));
    } else if (kind == 1) { // check
        const QPointF check[3] = {QPointF(2.5, 7.5), QPointF(5.5, 10.5), QPointF(11.5, 3.5)};
        painter.drawPolyline(check, 3);
    } else { // bin
        painter.drawLine(QPointF(2, 3.5), QPointF(12, 3.5));
        painter.drawLine(QPointF(5.5, 3.5), QPointF(5.5, 2));
        painter.drawLine(QPointF(5.5, 2), QPointF(8.5, 2));
        painter.drawLine(QPointF(8.5, 2), QPointF(8.5, 3.5));
        painter.drawRoundedRect(QRectF(3.5, 3.5, 7, 9), 1, 1);
        painter.drawLine(QPointF(6, 6), QPointF(6, 10));
        painter.drawLine(QPointF(8, 6), QPointF(8, 10));
    }
    painter.end();
    return QIcon{pixmap};
}

/// One player in the list: a click rumbles its controller, a drag onto another player swaps them
class PlayerRow : public QFrame {
public:
    PlayerRow(int player_, QWidget* parent) : QFrame(parent), player{player_} {
        setObjectName(QStringLiteral("playerRow"));
        setAcceptDrops(true);
        setCursor(Qt::OpenHandCursor);
        long_press.setSingleShot(true);
        long_press.setInterval(400);
        QObject::connect(&long_press, &QTimer::timeout, this, [this] { StartDrag(); });
    }

    std::function<void(int)> on_click;
    std::function<void(int, int)> on_drop;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            press_pos = event->position().toPoint();
            pressed = true;
            long_press.start();
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed && (event->position().toPoint() - press_pos).manhattanLength() >=
                           QApplication::startDragDistance()) {
            StartDrag();
        }
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (pressed && long_press.isActive() && on_click) {
            on_click(player);
        }
        pressed = false;
        long_press.stop();
        QFrame::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(QString::fromLatin1(PlayerMime))) {
            event->acceptProposedAction();
            drop_hover = true;
            update();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        drop_hover = false;
        update();
        QFrame::dragLeaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        if (drop_hover) {
            // Where the dragged player lands
            QPainter painter{this};
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(ConfigurePlayerAssignment::Accent(this), 2, Qt::DashLine));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        }
    }

    void dropEvent(QDropEvent* event) override {
        drop_hover = false;
        update();
        const int from = event->mimeData()->data(QString::fromLatin1(PlayerMime)).toInt();
        if (from != player && on_drop) {
            on_drop(from, player);
        }
        event->acceptProposedAction();
    }

private:
    void StartDrag() {
        if (!pressed) {
            return;
        }
        pressed = false;
        long_press.stop();
        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(PlayerMime), QByteArray::number(player));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->setHotSpot(press_pos);
        setCursor(Qt::ClosedHandCursor);
        drag->exec(Qt::MoveAction);
        setCursor(Qt::OpenHandCursor);
    }

    int player;
    bool pressed = false;
    bool drop_hover = false;
    QPoint press_pos;
    QTimer long_press;
};

} // Anonymous namespace

QToolButton* ConfigurePlayerAssignment::AddClearButton(QComboBox* combo) {
    auto* button = new QToolButton(combo);
    button->setText(QStringLiteral("\u2715"));
    button->setCursor(Qt::ArrowCursor);
    button->setFixedSize(18, 18);
    button->setStyleSheet(Themed(
        QStringLiteral("QToolButton { border: none; border-radius: 9px; background: transparent; "
                       "color: muted-text; padding: 0px; }"
                       "QToolButton:hover { background: palette(highlight); "
                       "color: palette(highlighted-text); }")));
    // (Room for it between the text and the arrow)
    combo->setStyleSheet(QStringLiteral("QComboBox { padding-right: 40px; }"));
    combo->installEventFilter(new ClearButtonPlacer(combo, button));
    return button;
}

QString ConfigurePlayerAssignment::Themed(QString style) {
    if (!DarkUi()) {
        return style.replace(QStringLiteral("muted-text"), QStringLiteral("#7d838b"))
            .replace(QStringLiteral("popup-base"), QStringLiteral("palette(base)"));
    }
    // An open drop-down list is a window of its own: a solid background (a tint shows white)
    style.replace(
        QStringLiteral("popup-base"),
        !UISettings::IsDarkTheme()
            ? QApplication::palette().color(QPalette::Window).name()
        : QString::fromStdString(UISettings::values.theme).contains(QStringLiteral("midnight"))
            ? QStringLiteral("#19232D")
            : QStringLiteral("#31363B"));
    // Cards show the theme's background, fields and buttons a light tint
    static const std::array<std::pair<const char*, const char*>, 8> dark{{
        {"palette(alternate-base)", "transparent"},
        {"palette(base)", "rgba(255, 255, 255, 14)"},
        {"palette(button)", "rgba(255, 255, 255, 22)"},
        {"palette(mid)", "rgba(255, 255, 255, 70)"},
        {"palette(highlighted-text)", "#ffffff"},
        {"palette(highlight)", "#3daee9"},
        {"palette(text)", "#eff0f1"},
        {"muted-text", "#9da3aa"},
    }};
    for (const auto& [from, to] : dark) {
        style.replace(QString::fromLatin1(from), QString::fromLatin1(to));
    }
    return style;
}

QColor ConfigurePlayerAssignment::Accent(const QWidget* widget) {
    return DarkUi() ? QColor(0x3d, 0xae, 0xe9)
                                     : widget->palette().highlight().color();
}

QPixmap ConfigurePlayerAssignment::Battery(int percent, bool charging) {
    return BatteryIcon(percent, charging);
}

QColor ConfigurePlayerAssignment::PlayerColor(int player) {
    return PlayerColors[static_cast<std::size_t>(player) % PlayerColors.size()];
}

QString ConfigurePlayerAssignment::ModernComboStyle() {
    return Themed(QStringLiteral(
        "QComboBox { border: 1px solid palette(mid); border-radius: 6px; padding: 3px 10px; "
        "background: palette(base); }"
        "QComboBox:hover, QComboBox:focus { border-color: palette(highlight); }"
        "QComboBox:disabled { color: palette(mid); }"
        // The open list: readable rows, the hovered one in the highlight colour
        "QComboBox QAbstractItemView { background: popup-base; color: palette(text); "
        "border: 1px solid palette(mid); outline: 0; padding: 2px; }"
        "QComboBox QAbstractItemView::item { min-height: 24px; padding: 0 8px; }"
        "QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { "
        "background: palette(highlight); color: palette(highlighted-text); }"));
}

void ConfigurePlayerAssignment::ModernizeCombos(QWidget* root) {
    // A combo box's own list painter ignores the style sheet: the standard one follows it
    for (auto* combo : root->findChildren<QComboBox*>()) {
        if (!combo->property("modern_list").toBool()) {
            combo->setItemDelegate(new QStyledItemDelegate(combo));
            // The list's frame takes its solid background (its padding showed white)
            if (QWidget* frame = combo->view()->window(); frame && frame != combo->window()) {
                frame->setStyleSheet(Themed(QStringLiteral(
                    "QFrame { background: popup-base; border: 1px solid palette(mid); }")));
            }
            combo->setProperty("modern_list", true);
        }
    }
}

QString ConfigurePlayerAssignment::OutlinedButtonStyle() {
    return Themed(QStringLiteral(
        "QPushButton, QToolButton { border: 1px solid palette(highlight); border-radius: 6px; "
        "padding: 3px 12px; color: palette(highlight); font-weight: bold; "
        "background: palette(base); }"
        "QPushButton:hover, QToolButton:hover { background: palette(highlight); "
        "color: palette(highlighted-text); }"
        "QPushButton:disabled, QToolButton:disabled { border-color: palette(mid); "
        "color: palette(mid); }"));
}

QWidget* ConfigurePlayerAssignment::MakeSegmented(QWidget* parent, const QStringList& labels,
                                                  const std::vector<int>& ids,
                                                  QButtonGroup* group) {
    auto* bar = new QWidget(parent);
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(0, 0, 0, 0);
    bar_layout->setSpacing(0);
    for (int k = 0; k < labels.size(); ++k) {
        auto* button = new QPushButton(labels[k], bar);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        QString style =
            QStringLiteral("QPushButton { padding: 5px 14px; border: 1px solid palette(mid); "
                           "background: palette(button); }"
                           "QPushButton:checked { background: palette(highlight); "
                           "color: palette(highlighted-text); border-color: palette(highlight); }"
                           "QPushButton:disabled { color: palette(mid); }");
        if (k == 0) {
            style += QStringLiteral("QPushButton { border-top-left-radius: 6px; "
                                    "border-bottom-left-radius: 6px; }");
        } else {
            style += QStringLiteral("QPushButton { border-left-width: 0px; }");
        }
        if (k == labels.size() - 1) {
            style += QStringLiteral("QPushButton { border-top-right-radius: 6px; "
                                    "border-bottom-right-radius: 6px; }");
        }
        button->setStyleSheet(Themed(style));
        group->addButton(button, ids[static_cast<std::size_t>(k)]);
        bar_layout->addWidget(button);
    }
    return bar;
}

ConfigurePlayerAssignment::ConfigurePlayerAssignment(QWidget* parent) : QWidget(parent) {
    // The cards keep their height and the page scrolls when they don't fit
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    auto* content = new QWidget(scroll);
    content->setAutoFillBackground(false);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    content_layout = layout;
    layout->setSpacing(8);
    // Each section is a rounded card (kept tight: the Deck's screen is 800 pixels tall)
    setStyleSheet(
        Themed(QStringLiteral("QGroupBox { border: 1px solid palette(mid); border-radius: 10px; "
                              "margin-top: 10px; padding: 8px 8px 4px 8px; "
                              "background: palette(alternate-base); }"
                              "QGroupBox::title { subcontrol-origin: margin; left: 14px; "
                              "padding: 0 4px; font-weight: bold; }"
                              // Labels show the card behind them (a dark theme paints every widget)
                              "QLabel { background: transparent; }"
                              "QLabel#note { color: muted-text; }")) +
        ModernComboStyle());

    // The handler, as a segmented control
    auto* mode_box = new QGroupBox(tr("Controller Assignment Handler"), this);
    auto* mode_layout = new QVBoxLayout(mode_box);
    handler_group = new QButtonGroup(this);
    auto* handler_row = new QHBoxLayout();
    handler_row->addWidget(MakeSegmented(mode_box, {tr("Modern Console"), tr("Manual")},
                                         {HandlerModern, HandlerManual}, handler_group));
    handler_row->addStretch(1);
    mode_layout->addLayout(handler_row);

    // Under Manual: the controller applet a game sees when it asks for its players
    manual_panel = new QFrame(mode_box);
    manual_panel->setObjectName(QStringLiteral("manualPanel"));
    manual_panel->setStyleSheet(
        Themed(QStringLiteral("#manualPanel { border: 1px solid palette(mid); "
                              "border-radius: 8px; background: palette(base); }")));
    auto* manual_layout = new QVBoxLayout(manual_panel);
    manual_layout->setContentsMargins(12, 8, 12, 8);
    auto* manual_title = new QLabel(tr("Controller Applet"), manual_panel);
    QFont manual_font = manual_title->font();
    manual_font.setBold(true);
    manual_title->setFont(manual_font);
    auto* manual_note = new QLabel(tr("What a game sees when it asks for its players. These are "
                                      "the legacy ways of handling controllers: they do not have "
                                      "Modern Console's features, such as players assigned "
                                      "automatically, the overlay and the hints."),
                                   manual_panel);
    manual_note->setWordWrap(true);
    manual_note->setObjectName(QStringLiteral("note"));
    manual_group = new QButtonGroup(this);
    auto* manual_row = new QHBoxLayout();
    manual_row->addWidget(MakeSegmented(
        manual_panel, {tr("Disabled"), tr("Custom Frontend"), tr("Real Hardware")},
        {static_cast<int>(Settings::AppletMode::Disabled),
         static_cast<int>(Settings::AppletMode::HLE), static_cast<int>(Settings::AppletMode::LLE)},
        manual_group));
    manual_row->addStretch(1);
    // What Manual itself does, under the choices
    auto* manual_about = new QLabel(tr("In Manual, each player's controller is set by hand in "
                                       "its player tab (P1 to P8) and stays until you change "
                                       "it. The same choices are in System > Applets, also per "
                                       "game."),
                                    manual_panel);
    manual_about->setWordWrap(true);
    manual_about->setObjectName(QStringLiteral("note"));
    manual_layout->addWidget(manual_title);
    manual_layout->addWidget(manual_note);
    manual_layout->addLayout(manual_row);
    manual_layout->addWidget(manual_about);
    auto* manual_indent = new QHBoxLayout();
    manual_indent->setContentsMargins(16, 0, 0, 0);
    manual_indent->addWidget(manual_panel);
    mode_layout->addLayout(manual_indent);

    // Under Modern Console: whether this is a handheld with its own controls
    handheld_panel = new QFrame(mode_box);
    handheld_panel->setObjectName(QStringLiteral("handheldPanel"));
    handheld_panel->setStyleSheet(
        Themed(QStringLiteral("#handheldPanel { border: 1px solid palette(mid); "
                              "border-radius: 8px; background: palette(base); }")));
    auto* panel_layout = new QHBoxLayout(handheld_panel);
    panel_layout->setContentsMargins(12, 8, 12, 8);
    auto* panel_text = new QVBoxLayout();
    auto* panel_title =
        new QLabel(tr("Portable Handheld Gaming Device with Built-in Controller"), handheld_panel);
    QFont title_font = panel_title->font();
    title_font.setBold(true);
    panel_title->setFont(title_font);
    panel_title->setWordWrap(true);
    auto* panel_note = new QLabel(tr("For a handheld with its own controls, like a Steam Deck, "
                                     "Legion Go or ROG Ally: connected controllers take the "
                                     "players first, and the built-in controls play only when "
                                     "no other controller is connected."),
                                  handheld_panel);
    panel_note->setWordWrap(true);
    panel_note->setObjectName(QStringLiteral("note"));
    panel_text->addWidget(panel_title);
    panel_text->addWidget(panel_note);
    panel_layout->addLayout(panel_text, 1);
    handheld_switch = new ToggleSwitch(handheld_panel);
    handheld_switch->setToolTip(tr("On: this is a handheld with its own controls"));
    panel_layout->addWidget(handheld_switch, 0, Qt::AlignVCenter);
    auto* indent = new QHBoxLayout();
    indent->setContentsMargins(16, 0, 0, 0);
    indent->addWidget(handheld_panel);
    mode_layout->addLayout(indent);

    mode_info = new QLabel(mode_box);
    mode_info->setWordWrap(true);
    mode_layout->addWidget(mode_info);
    layout->addWidget(mode_box);

    // Under Steam: read the controllers directly instead of through Steam Input
    auto* steam_box = new QGroupBox(tr("Steam Input"), this);
    steam_panel = steam_box;
    auto* steam_column = new QVBoxLayout(steam_box);
    auto* steam_layout = new QHBoxLayout();
    steam_column->addLayout(steam_layout);
    auto* steam_text = new QVBoxLayout();
    steam_text->setSpacing(1);
    auto* steam_title = new QLabel(tr("Bypass Steam Input"), steam_box);
    steam_title->setFont(title_font);
    auto* steam_note = new QLabel(
        tr("Eden reads your controllers directly instead of through Steam's virtual "
           "controllers, so Steam Input layouts don't apply in Eden. Try this if a controller "
           "doesn't work under Steam, such as the Steam Controller or a newer controller. Takes "
           "effect after restarting Eden; connected controllers may be assigned to different "
           "players."),
        steam_box);
    steam_note->setWordWrap(true);
    steam_note->setObjectName(QStringLiteral("note"));
    steam_restart = new QLabel(tr("Restart Eden to apply this change."), steam_box);
    steam_text->addWidget(steam_title);
    steam_text->addWidget(steam_note);
    steam_layout->addLayout(steam_text, 1);
    steam_switch = new ToggleSwitch(steam_box);
    steam_switch->setToolTip(tr("On: Eden reads the controllers directly"));
    steam_layout->addWidget(steam_switch, 0, Qt::AlignVCenter);
    original_bypass = Settings::values.console_bypass_steam.GetValue();
    steam_switch->setChecked(original_bypass);
    connect(steam_switch, &QAbstractButton::toggled, this, [this](bool on) {
        Settings::values.console_bypass_steam.SetValue(on);
        UpdateModeUi();
    });

    steam_column->addWidget(steam_restart);
    layout->addWidget(steam_box);

    // Assign Handheld Controller: declare the handheld's own controller (that mode only)
    builtin_row = new QGroupBox(tr("Assign Handheld Controller"), this);
    auto* builtin_layout = new QVBoxLayout(builtin_row);
    auto* picker_row = new QHBoxLayout();
    picker_row->addWidget(new QLabel(tr("Handheld's controller:"), builtin_row));
    builtin_picker = new QComboBox(builtin_row);
    builtin_picker->addItem(tr("Auto-detect"), QString{});
    picker_row->addWidget(builtin_picker, 1);
    builtin_identify = new QPushButton(tr("Identify"), builtin_row);
    builtin_identify->setCursor(Qt::PointingHandCursor);
    builtin_identify->setStyleSheet(OutlinedButtonStyle());
    picker_row->addWidget(builtin_identify);
    builtin_layout->addLayout(picker_row);
    // The ✕ inside the picker removes the handheld's controller (auto-detect then skips it)
    builtin_remove = AddClearButton(builtin_picker);
    builtin_remove->setToolTip(tr("Not the handheld's controller: remove it. Auto-detect will "
                                  "not pick it again until you allow it."));
    builtin_remove->hide();
    // A removed controller: said plainly, with the way to allow it again
    blocked_row = new QWidget(builtin_row);
    auto* blocked_line = new QHBoxLayout(blocked_row);
    blocked_line->setContentsMargins(0, 0, 0, 0);
    blocked_label = new QLabel(blocked_row);
    blocked_label->setWordWrap(true);
    blocked_label->setObjectName(QStringLiteral("note"));
    blocked_line->addWidget(blocked_label, 1);
    blocked_allow = new QPushButton(tr("Allow Again"), blocked_row);
    blocked_allow->setCursor(Qt::PointingHandCursor);
    blocked_allow->setStyleSheet(OutlinedButtonStyle());
    blocked_line->addWidget(blocked_allow);
    builtin_layout->addWidget(blocked_row);
    builtin_status = new QLabel(builtin_row);
    builtin_status->setWordWrap(true);
    builtin_layout->addWidget(builtin_status);
    connect(builtin_remove, &QToolButton::clicked, this, [] {
        auto& state = Common::GetControllerConsoleState();
        std::scoped_lock lock{state.mutex};
        state.builtin_remove_request = true;
    });
    connect(blocked_allow, &QPushButton::clicked, this,
            [] { Settings::values.builtin_pad_blocked.SetValue(std::string{}); });
    layout->addWidget(builtin_row);

    // The players right now, as the engine assigned them
    now_box = new QGroupBox(tr("Connected Players"), this);
    auto* now_layout = new QVBoxLayout(now_box);
    players_box = new QWidget(now_box);
    players_list = new QVBoxLayout(players_box);
    players_list->setContentsMargins(0, 0, 0, 0);
    players_list->setSpacing(6);
    now_layout->addWidget(players_box);
    players_note = new QLabel(now_box);
    players_note->setWordWrap(true);
    now_layout->addWidget(players_note);
    layout->addWidget(now_box);

    // Gameplay: how players' controllers behave in games
    auto* gameplay = new QGroupBox(tr("Gameplay"), this);
    gameplay_box = gameplay;
    auto* gameplay_layout = new QVBoxLayout(gameplay);
    const auto add_option = [gameplay, gameplay_layout](const QString& title, const QString& note,
                                                        QWidget* control) {
        AddOptionRow(gameplay_layout, gameplay, title, note, control);
    };
    joycon_switch = new ToggleSwitch(gameplay);
    add_option(tr("Use Any Controller as Joy-Cons"),
               tr("When a game only takes Joy-Cons, such as one Joy-Con per player, your "
                  "controllers act as Joy-Cons automatically, with the buttons arranged to fit. A "
                  "controller type picked in a player's tab is left as it is."),
               joycon_switch);
    original_joycon = Settings::values.console_joycon_auto.GetValue();
    joycon_switch->setChecked(original_joycon);
    connect(joycon_switch, &QAbstractButton::toggled, this,
            [](bool on) { Settings::values.console_joycon_auto.SetValue(on); });

    layout_group = new QButtonGroup(this);
    auto* layout_choice =
        MakeSegmented(gameplay, {tr("By Position"), tr("By Label")}, {0, 1}, layout_group);
    add_option(tr("Button Layout"),
               tr("For Xbox and PlayStation style controllers. By Position: the button where the "
                  "Switch has A acts as A. By Label: the button marked A acts as A. Nintendo "
                  "controllers already match both ways."),
               layout_choice);
    original_labels = Settings::values.console_button_labels.GetValue();
    layout_group->button(original_labels ? 1 : 0)->setChecked(true);
    connect(layout_group, &QButtonGroup::idClicked, this,
            [](int id) { Settings::values.console_button_labels.SetValue(id == 1); });

    pause_switch = new ToggleSwitch(gameplay);
    add_option(tr("Pause When a Controller Disconnects"),
               tr("Pauses the game when a player's controller is turned off, or has been gone "
                  "for 3 seconds, and resumes when a controller takes that player again. Press + "
                  "on another controller to go on without that player."),
               pause_switch);
    original_pause = Settings::values.console_pause_disconnect.GetValue();
    pause_switch->setChecked(original_pause);
    connect(pause_switch, &QAbstractButton::toggled, this,
            [](bool on) { Settings::values.console_pause_disconnect.SetValue(on); });

    lights_switch = new ToggleSwitch(gameplay);
    add_option(tr("Player Lights"),
               tr("Controllers' player lights and light bars show their player, in its colour. "
                  "Under Steam, Steam sets the lights of the controllers it handles."),
               lights_switch);
    original_lights = Settings::values.console_player_lights.GetValue();
    lights_switch->setChecked(original_lights);
    connect(lights_switch, &QAbstractButton::toggled, this,
            [](bool on) { Settings::values.console_player_lights.SetValue(on); });
    layout->addWidget(gameplay);

    // What shows over the game: a title, a line about it and a switch per item
    auto* display = new QGroupBox(tr("On-Screen Display"), this);
    display_box = display;
    auto* display_layout = new QVBoxLayout(display);
    const auto add_switch_to = [display](QVBoxLayout* to, const QString& title,
                                         const QString& note) {
        auto* toggle = new ToggleSwitch(display);
        AddOptionRow(to, display, title, note, toggle);
        return toggle;
    };
    const auto add_switch = [&](const QString& title, const QString& note) {
        return add_switch_to(display_layout, title, note);
    };
    // The same switch as View > Show Controller Overlay
    overlay_toggle = add_switch(tr("Controller Overlay"),
                                tr("The players, their controllers and their last buttons in "
                                   "the top-left corner."));
    original_overlay = Settings::values.console_debug_overlay.GetValue();
    overlay_toggle->setChecked(original_overlay);
    connect(overlay_toggle, &QAbstractButton::toggled, this,
            [](bool shown) { Settings::values.console_debug_overlay.SetValue(shown); });

    // The overlay's own options, indented under it: the button history and how it shows
    history_options = new QWidget(display);
    auto* history_layout = new QVBoxLayout(history_options);
    history_layout->setContentsMargins(20, 0, 0, 4);
    history_toggle = add_switch_to(history_layout, tr("Button History"),
                                   tr("The last buttons each player pressed, beside their row. "
                                      "Buttons pressed together show as one input."));
    // A slider with its value beside it
    const auto add_slider = [this, display, history_layout](const QString& title, int min, int max,
                                                            int value, const QString& unit) {
        auto* line = new QHBoxLayout();
        auto* label = new QLabel(title, display);
        line->addWidget(label);
        auto* slider = new QSlider(Qt::Horizontal, display);
        slider->setRange(min, max);
        slider->setValue(value);
        slider->setPageStep(1);
        line->addWidget(slider, 1);
        auto* shown = new QLabel(display);
        shown->setMinimumWidth(shown->fontMetrics().horizontalAdvance(unit.arg(max)) + 4);
        shown->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        shown->setText(unit.arg(value));
        line->addWidget(shown);
        connect(slider, &QSlider::valueChanged, this,
                [shown, unit](int v) { shown->setText(unit.arg(v)); });
        auto* row = new QWidget(display);
        row->setLayout(line);
        line->setContentsMargins(0, 0, 0, 0);
        history_layout->addWidget(row);
        return std::pair{slider, row};
    };
    original_history = Settings::values.console_history.GetValue();
    original_history_size = Settings::values.console_history_size.GetValue();
    original_history_seconds = Settings::values.console_history_seconds.GetValue();
    original_history_gaps = Settings::values.console_history_gaps.GetValue();
    QWidget* size_row = nullptr;
    QWidget* seconds_row = nullptr;
    std::tie(history_size, size_row) =
        add_slider(tr("Inputs kept"), 5, 20, original_history_size, tr("%1"));
    std::tie(history_seconds, seconds_row) =
        add_slider(tr("Expire after"), 1, 30, original_history_seconds, tr("%1 s"));
    gaps_toggle = add_switch_to(history_layout, tr("Time Between Presses"),
                                tr("The milliseconds between inputs, never more than the expire "
                                   "time."));
    display_layout->addWidget(history_options);
    history_toggle->setChecked(original_history);
    gaps_toggle->setChecked(original_history_gaps);
    connect(history_toggle, &QAbstractButton::toggled, this,
            [](bool shown) { Settings::values.console_history.SetValue(shown); });
    connect(history_size, &QSlider::valueChanged, this,
            [](int v) { Settings::values.console_history_size.SetValue(v); });
    connect(history_seconds, &QSlider::valueChanged, this,
            [](int v) { Settings::values.console_history_seconds.SetValue(v); });
    connect(gaps_toggle, &QAbstractButton::toggled, this,
            [](bool shown) { Settings::values.console_history_gaps.SetValue(shown); });
    // Only what applies can be changed
    const auto update_history_options = [this, size_row, seconds_row] {
        const bool history_on = history_toggle->isChecked();
        history_options->setEnabled(overlay_toggle->isChecked());
        size_row->setEnabled(history_on);
        seconds_row->setEnabled(history_on);
        gaps_toggle->setEnabled(history_on);
    };
    connect(overlay_toggle, &QAbstractButton::toggled, this, update_history_options);
    connect(history_toggle, &QAbstractButton::toggled, this, update_history_options);
    update_history_options();
    hints_toggle = add_switch(tr("Controller Hints"),
                              tr("Names a controller in the bottom-left corner for a moment "
                                 "when it joins as a player or leaves."));
    original_hints = Settings::values.console_hints.GetValue();
    hints_toggle->setChecked(original_hints);
    connect(hints_toggle, &QAbstractButton::toggled, this,
            [](bool shown) { Settings::values.console_hints.SetValue(shown); });
    layout->addWidget(display);

    // Logging for bug reports
    auto* debug = new QGroupBox(tr("Troubleshooting"), this);
    debug_box = debug;
    auto* debug_layout = new QVBoxLayout(debug);
    probes_toggle = add_switch_to(debug_layout, tr("Debug Probes"),
                                  tr("Writes detailed controller traces to the log several times "
                                     "a second, for bug reports. Connected controllers are "
                                     "always described in the log."));
    original_probes = Common::ControllerProbes();
    probes_toggle->setChecked(original_probes);
    connect(probes_toggle, &QAbstractButton::toggled, this,
            [](bool on) { Settings::values.console_debug_probes.SetValue(on); });
    layout->addWidget(debug);

    // A short word on the two automatic modes, last on the page
    auto* about = new QGroupBox(tr("How It Works"), this);
    about_box = about;
    auto* about_layout = new QVBoxLayout(about);
    const auto add_about = [about, about_layout](const QString& title, const QString& text) {
        auto* title_label = new QLabel(title, about);
        QFont font = title_label->font();
        font.setBold(true);
        title_label->setFont(font);
        auto* text_label = new QLabel(text, about);
        text_label->setWordWrap(true);
        text_label->setObjectName(QStringLiteral("note"));
        about_layout->addWidget(title_label);
        about_layout->addWidget(text_label);
    };
    add_about(tr("Modern Console"),
              tr("Controllers are assigned like on the console: each one that connects becomes "
                 "the next player, with nothing to set up. Players keep their own controls when "
                 "they swap, and a profile picked for a player is used again next time. Its "
                 "mappings are its own: each controller starts with its standard mapping, and "
                 "the Manual modes' players are never changed. Keyboard and mouse: use Manual."));
    add_about(tr("Handheld Console"),
              tr("For a handheld PC with built-in controls. The same as Modern Console, but the "
                 "built-in controls always come last: connect a controller and it becomes "
                 "Player 1, so a docked handheld plays like a console."));
    add_about(tr("Steam Controller"),
              tr("Under Steam, turn on Bypass Steam Input (Steam Input card) so Eden reads the "
                 "Steam Controller itself. Steam Input layouts then don't apply in Eden."));
    layout->addWidget(about);
    layout->addStretch(1);

    // The mode applies at once; closing without applying puts the previous one back
    original_mode = static_cast<int>(Settings::values.controller_applet_mode.GetValue());
    connect(handler_group, &QButtonGroup::idClicked, this,
            &ConfigurePlayerAssignment::OnModeChanged);
    connect(manual_group, &QButtonGroup::idClicked, this,
            &ConfigurePlayerAssignment::OnModeChanged);
    connect(handheld_switch, &QAbstractButton::clicked, this,
            &ConfigurePlayerAssignment::OnModeChanged);
    connect(builtin_picker, &QComboBox::currentIndexChanged, this,
            &ConfigurePlayerAssignment::OnBuiltinPicked);
    connect(builtin_identify, &QPushButton::clicked, this,
            &ConfigurePlayerAssignment::OnIdentifyPressed);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ConfigurePlayerAssignment::Refresh);
    // Fast enough for the highlight to follow a button press
    timer->start(50);

    LoadConfiguration();
    Refresh();
}

ConfigurePlayerAssignment::~ConfigurePlayerAssignment() {
    if (!applied) {
        Settings::values.controller_applet_mode.SetValue(
            static_cast<Settings::AppletMode>(original_mode));
        Settings::values.console_debug_overlay.SetValue(original_overlay);
        Settings::values.console_hints.SetValue(original_hints);
        Settings::values.console_history.SetValue(original_history);
        Settings::values.console_history_size.SetValue(original_history_size);
        Settings::values.console_history_seconds.SetValue(original_history_seconds);
        Settings::values.console_history_gaps.SetValue(original_history_gaps);
        Settings::values.console_debug_probes.SetValue(original_probes);
        Settings::values.console_bypass_steam.SetValue(original_bypass);
        Settings::values.console_joycon_auto.SetValue(original_joycon);
        Settings::values.console_button_labels.SetValue(original_labels);
        Settings::values.console_pause_disconnect.SetValue(original_pause);
        Settings::values.console_player_lights.SetValue(original_lights);
    }
}

void ConfigurePlayerAssignment::LoadConfiguration() {
    const auto current = Settings::values.controller_applet_mode.GetValue();
    const bool modern = IsAutomatic(current);
    handler_group->button(modern ? HandlerModern : HandlerManual)->setChecked(true);
    // Under Manual the applet as set; the default there is Custom Frontend
    QAbstractButton* applet = manual_group->button(static_cast<int>(current));
    (applet && !modern ? applet : manual_group->button(static_cast<int>(Settings::AppletMode::HLE)))
        ->setChecked(true);
    handheld_switch->setChecked(current == Settings::AppletMode::HandheldConsoleMode);
    UpdateModeUi();
}

int ConfigurePlayerAssignment::SelectedMode() const {
    switch (handler_group->checkedId()) {
    case HandlerModern:
        return static_cast<int>(handheld_switch->isChecked()
                                    ? Settings::AppletMode::HandheldConsoleMode
                                    : Settings::AppletMode::ConsoleMode);
    default:
        return manual_group->checkedId();
    }
}

void ConfigurePlayerAssignment::OnModeChanged() {
    Settings::values.controller_applet_mode.SetValue(
        static_cast<Settings::AppletMode>(SelectedMode()));
    UpdateModeUi();
}

void ConfigurePlayerAssignment::ApplyConfiguration() {
    Settings::values.controller_applet_mode.SetValue(
        static_cast<Settings::AppletMode>(SelectedMode()));
    applied = true;
}

void ConfigurePlayerAssignment::UpdateModeUi() {
    const auto selected = static_cast<Settings::AppletMode>(SelectedMode());
    handheld_panel->setVisible(handler_group->checkedId() == HandlerModern);
    // (Bypass Steam Input: only under Steam; applies to a Modern Console start)
    steam_panel->setVisible(handler_group->checkedId() == HandlerModern &&
                            Common::RunningUnderSteam());
    steam_restart->setVisible(steam_switch->isChecked() !=
                              Common::GetControllerConsoleState().steam_bypassed);
    manual_panel->setVisible(handler_group->checkedId() == HandlerManual);
    // Only the automatic modes have live players to show
    const bool automatic = IsAutomatic(selected);
    switch (selected) {
    case Settings::AppletMode::ConsoleMode:
        mode_info->setText(tr("Controller assignment is automatic. Each controller is assigned "
                              "to the next available player when it connects."));
        break;
    case Settings::AppletMode::HandheldConsoleMode:
        mode_info->setText(tr("Controller assignment is automatic, with the handheld's own "
                              "controls last."));
        break;
    case Settings::AppletMode::Disabled:
        mode_info->setText(tr("Controllers are assigned in each player's tab. Games use the "
                              "configured players without showing a controller applet."));
        break;
    case Settings::AppletMode::LLE:
        mode_info->setText(tr("Controllers are assigned in each player's tab. When a game "
                              "requests controller setup, the console's own controller applet "
                              "is shown. This requires the system firmware."));
        break;
    default:
        mode_info->setText(tr("Controllers are assigned in each player's tab. When a game "
                              "requests controller setup, Eden's controller applet is shown."));
        break;
    }
    now_box->setVisible(automatic);
    display_box->setVisible(automatic);
    gameplay_box->setVisible(automatic);
    about_box->setVisible(automatic);
    debug_box->setVisible(automatic);
    builtin_row->setVisible(selected == Settings::AppletMode::HandheldConsoleMode);
    Refresh();
}

void ConfigurePlayerAssignment::Refresh() {
    RefreshBuiltin();
    RefreshPlayers();
}

void ConfigurePlayerAssignment::RefreshBuiltin() {
    auto& state = Common::GetControllerConsoleState();
    std::vector<std::pair<std::string, std::string>> pads;
    std::size_t builtin_pads = 0;
    bool armed = false;
    std::chrono::steady_clock::time_point armed_at{};
    std::string detected_name;
    {
        std::scoped_lock lock{state.mutex};
        pads = state.pads;
        builtin_pads = state.builtin_pads;
        armed = state.identify_armed;
        armed_at = state.identify_armed_at;
        detected_name = state.handheld_name;
    }

    // The entries' data: the Steam handle under Steam (the virtual pads share one GUID), else
    // the pad's identity
    QString signature = QString::number(builtin_pads) + QLatin1Char('|');
    for (const auto& [display, ident] : pads) {
        signature += QString::fromStdString(ident) + QLatin1Char('|');
    }
    if (signature != builtin_signature) {
        builtin_signature = signature;
        const QString current = builtin_picker->currentData().toString();
        const bool blocked = builtin_picker->blockSignals(true);
        builtin_picker->clear();
        builtin_picker->addItem(tr("Auto-detect"), QString{});
        for (std::size_t i = 0; i < pads.size(); ++i) {
            if (i == builtin_pads) {
                // A heading (not pickable) before the standalone controllers
                builtin_picker->insertSeparator(builtin_picker->count());
                builtin_picker->addItem(tr("Other controllers"));
                if (auto* model = qobject_cast<QStandardItemModel*>(builtin_picker->model())) {
                    model->item(builtin_picker->count() - 1)->setEnabled(false);
                }
            }
            builtin_picker->addItem(QString::fromStdString(pads[i].first),
                                    QString::fromStdString(pads[i].second));
        }
        const int keep = builtin_picker->findData(current);
        builtin_picker->setCurrentIndex(keep > 0 ? keep : 0);
        builtin_picker->blockSignals(blocked);
    }

    if (armed) {
        const auto left =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::seconds{15} - (std::chrono::steady_clock::now() - armed_at))
                .count();
        builtin_status->setText(
            tr("Waiting for a button press (%1 seconds)...").arg(std::max<qint64>(0, left)));
        // (The line below counts down)
        builtin_identify->setText(tr("Press a button..."));
        return;
    }
    builtin_identify->setText(tr("Identify"));

    // Stored per launch kind: the Steam handle under Steam, else the pad's identity
    const bool steam = Common::SteamPadsInUse();
    const QString identity =
        QString::fromStdString(Settings::values.builtin_pad_identity.GetValue());
    const QString handle =
        QString::fromStdString(Settings::values.builtin_pad_steam_handle.GetValue());
    const QString stored =
        steam ? (handle.isEmpty() ? QString{} : QStringLiteral("steamhandle:") + handle) : identity;
    const bool declared = steam ? Settings::values.builtin_pad_steam_manual.GetValue()
                                : Settings::values.builtin_pad_manual.GetValue();

    // Keep the picker on a declared pad, else on automatic detection
    const int stored_index = stored.isEmpty() ? -1 : builtin_picker->findData(stored);
    const int want_index = declared && stored_index > 0 ? stored_index : 0;
    if (builtin_picker->currentIndex() != want_index) {
        const bool blocked = builtin_picker->blockSignals(true);
        builtin_picker->setCurrentIndex(want_index);
        builtin_picker->blockSignals(blocked);
    }

    bool engine_active = false;
    {
        std::scoped_lock lock{state.mutex};
        engine_active = state.engine_active;
    }
    if (engine_active && detected_name.empty() &&
        Settings::values.controller_applet_mode.GetValue() ==
            Settings::AppletMode::HandheldConsoleMode) {
        builtin_status->setText(tr("Handheld controller not detected \u2014 working like Modern "
                                   "until it is. Pick it above, or use Identify."));
    } else if (stored.isEmpty() && detected_name.empty()) {
        builtin_status->setText(
            tr("Not set. Pick the handheld's controller above, or use Identify."));
    } else {
        const QString name = stored_index > 0         ? builtin_picker->itemText(stored_index)
                             : !detected_name.empty() ? QString::fromStdString(detected_name)
                                                      : tr("not set");
        builtin_status->setText(
            tr("Handheld: %1 (%2). %3")
                .arg(name)
                .arg(want_index > 0 ? tr("declared") : tr("auto-detected"))
                .arg(!detected_name.empty()
                         ? tr("Takes no player while external controllers are connected.")
                         : tr("Not currently connected.")));
    }

    // The picker shows the handheld's controller, with the ✕ inside it to remove it
    const bool shown = engine_active && !detected_name.empty();
    const QString first = shown && want_index == 0
                              ? tr("%1 (auto-detected)").arg(QString::fromStdString(detected_name))
                              : tr("Auto-detect");
    if (builtin_picker->itemText(0) != first) {
        builtin_picker->setItemText(0, first);
    }
    builtin_remove->setVisible(shown);
    builtin_status->setVisible(true);
    // A removed controller, kept from auto-detect
    std::string blocked_name;
    {
        std::scoped_lock lock{state.mutex};
        blocked_name = state.handheld_blocked_name;
    }
    const bool blocked = !Settings::values.builtin_pad_blocked.GetValue().empty();
    blocked_row->setVisible(blocked);
    if (blocked) {
        blocked_label->setText(
            tr("Auto-detect is off for %1: it will not be picked as the handheld's controller.")
                .arg(blocked_name.empty() ? tr("the removed controller")
                                          : QString::fromStdString(blocked_name)));
    }
}

void ConfigurePlayerAssignment::RefreshPlayers() {
    auto& state = Common::GetControllerConsoleState();
    std::vector<Common::ControllerConsoleState::PlayerRow> players;
    std::string handheld_name;
    bool handheld_has_player = false;
    bool engine_active = false;
    {
        std::scoped_lock lock{state.mutex};
        players = state.players;
        handheld_name = state.handheld_name;
        handheld_has_player = state.handheld_has_player;
        engine_active = state.engine_active;
    }
    const auto selected = static_cast<Settings::AppletMode>(SelectedMode());
    const bool in_effect = selected == Settings::values.controller_applet_mode.GetValue();

    // The engine decides the flashes, as in the overlay
    constexpr qint64 flash_ms = 1500;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int last_player = -1;
    for (const auto& entry : players) {
        if (!entry.leaving) {
            last_player = (std::max)(last_player, entry.player);
        }
    }

    // The saved input profiles, by their files (cheap enough to look at every refresh)
    QStringList profiles;
    {
        const QDir dir{QString::fromStdString(Common::FS::PathToUTF8String(
            Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "input"))};
        for (const QString& file :
             dir.entryList({QStringLiteral("*.ini")}, QDir::Files, QDir::Name)) {
            const QString name = file.chopped(4);
            if (InputProfiles::IsProfileNameValid(name.toStdString())) {
                profiles.push_back(name);
            }
        }
    }

    // Rebuild the rows when they change; the colours only restyle them
    QString signature = QString::number(swap_from) + QLatin1Char(';') +
                        profiles.join(QLatin1Char(',')) + QLatin1Char(';');
    for (const auto& entry : players) {
        signature +=
            QString::number(entry.player) + QLatin1Char(':') + QString::fromStdString(entry.name) +
            QLatin1Char('/') + QString::fromStdString(entry.profile) +
            (entry.leaving ? QStringLiteral("x") : QString{}) + QString::number(entry.turn_off) +
            QLatin1Char('/') + QString::number(entry.origin) +
            (entry.guessed ? QStringLiteral("?") : QString{}) + QLatin1Char('/') +
            QString::number(entry.battery / 5) +
            (entry.charging ? QStringLiteral("c") : QString{}) + QLatin1Char('|');
    }
    if (!handheld_has_player) {
        signature += QStringLiteral("h:") + QString::fromStdString(handheld_name);
    }
    // (Not while a name is being typed: the rows would be rebuilt under it)
    if (signature != players_signature && !editing_name) {
        players_signature = signature;
        const bool any_turn_off = std::any_of(players.begin(), players.end(),
                                              [](const auto& e) { return e.turn_off != 0; });
        for (QFrame* row : player_rows) {
            players_list->removeWidget(row);
            row->hide();
            row->deleteLater();
        }
        player_rows.clear();
        row_players.clear();
        for (const auto& entry : players) {
            const int player = entry.player;
            auto* row = new PlayerRow(player, players_box);
            row->setToolTip(tr("Click to rumble this controller. Hold, then drop on another "
                               "player to swap them."));
            row->on_click = [this](int p) { RequestRumble(p); };
            row->on_drop = [this](int from, int to) { RequestSwap(from, to); };
            auto* line = new QHBoxLayout(row);
            line->setContentsMargins(8, 5, 6, 5);
            line->setSpacing(6);
            auto* grip = new QLabel(QStringLiteral("\u283F"), row);
            grip->setToolTip(tr("Drag onto another player to swap"));
            grip->setObjectName(QStringLiteral("note"));
            line->addWidget(grip);
            // The player's badge, marked with ? for a reconnect's guess, else a dot in the
            // colour of the player the controller started as
            auto* badge_box = new QWidget(row);
            badge_box->setFixedSize(40, 24);
            // (Only the badge and its mark show: the dark themes paint a plain widget)
            badge_box->setAttribute(Qt::WA_TranslucentBackground);
            badge_box->setStyleSheet(QStringLiteral("background: transparent;"));
            auto* badge = new QLabel(QStringLiteral("P%1").arg(player + 1), badge_box);
            badge->setAlignment(Qt::AlignCenter);
            badge->setGeometry(0, 3, 34, 20);
            badge->setStyleSheet(
                QStringLiteral("QLabel { background: %1; color: white; "
                               "border-radius: 10px; font-weight: bold; }")
                    .arg(PlayerColors[static_cast<std::size_t>(player) % 8].name()));
            if (!entry.leaving && (entry.guessed || entry.origin >= 0)) {
                auto* mark = new QLabel(badge_box);
                if (entry.guessed) {
                    mark->setText(QStringLiteral("?"));
                    mark->setAlignment(Qt::AlignCenter);
                    mark->setGeometry(27, 0, 13, 13);
                    mark->setStyleSheet(QStringLiteral(
                        "QLabel { background: white; color: #0e0e12; border: 1px solid #0e0e12; "
                        "border-radius: 6px; font-weight: bold; font-size: 9px; }"));
                    badge_box->setToolTip(tr("This controller came back without a way to tell "
                                             "which one it is. Swap it if it's in the wrong "
                                             "place."));
                } else {
                    mark->setGeometry(29, 1, 10, 10);
                    mark->setStyleSheet(
                        QStringLiteral("QLabel { background: %1; border: 1px solid #0e0e12; "
                                       "border-radius: 5px; }")
                            .arg(PlayerColors[static_cast<std::size_t>(entry.origin) % 8].name()));
                    badge_box->setToolTip(tr("Started as P%1").arg(entry.origin + 1));
                }
            }
            line->addWidget(badge_box);
            // The name, with a pencil to name the controller ("John" shows as "John (Xbox)")
            const QString given = QString::fromStdString(entry.custom_name);
            auto* name_label = new QLabel(QString::fromStdString(entry.name), row);
            auto* name_edit = new PlayerNameEdit(given, row);
            name_edit->setObjectName(QStringLiteral("playerName"));
            name_edit->setMaxLength(16);
            name_edit->setPlaceholderText(tr("Name this controller"));
            name_edit->setStyleSheet(ModernComboStyle().replace(QStringLiteral("QComboBox"),
                                                                QStringLiteral("QLineEdit")));
            name_edit->hide();
            const QColor icon_color = Accent(this);
            const auto name_button = [row, icon_color](int kind, const QString& tip) {
                auto* button = new QToolButton(row);
                button->setIcon(NameIcon(kind, icon_color));
                button->setIconSize(QSize(14, 14));
                button->setAutoRaise(true);
                button->setCursor(Qt::PointingHandCursor);
                button->setToolTip(tip);
                // (Clicking it keeps the field focused: leaving the field cancels)
                button->setFocusPolicy(Qt::NoFocus);
                return button;
            };
            auto* name_edit_button = name_button(0, tr("Name P%1's controller").arg(player + 1));
            auto* name_apply = name_button(1, tr("Apply the name"));
            auto* name_remove = name_button(2, tr("Remove the name"));
            name_apply->hide();
            // (The bin removes a given name)
            name_remove->setVisible(!given.isEmpty());
            name_edit_button->setEnabled(!entry.leaving);
            name_remove->setEnabled(!entry.leaving);
            const auto request = [player](const QString& value) {
                auto& console = Common::GetControllerConsoleState();
                std::scoped_lock lock{console.mutex};
                console.rename_requests.emplace_back(player, value.trimmed().toStdString());
            };
            const auto editing = [=, this](bool on) {
                // (While a name is typed, the rows aren't rebuilt under it)
                editing_name = on;
                name_label->setVisible(!on);
                name_edit_button->setVisible(!on);
                name_edit->setVisible(on);
                name_apply->setVisible(on);
                name_remove->setVisible(!given.isEmpty());
                if (on) {
                    name_edit->setText(given);
                    name_edit->setFocus();
                    name_edit->selectAll();
                }
            };
            name_edit->on_apply = [=] {
                editing(false);
                if (name_edit->text().trimmed() != given) {
                    request(name_edit->text());
                }
            };
            name_edit->on_cancel = [=] { editing(false); };
            connect(name_edit_button, &QToolButton::clicked, this, [=] { editing(true); });
            connect(name_apply, &QToolButton::clicked, this, [=] { name_edit->on_apply(); });
            connect(name_remove, &QToolButton::clicked, this, [=] {
                editing(false);
                request(QString{});
            });
            // The controller's battery, when it tells its charge
            QLabel* battery = nullptr;
            if (entry.battery >= 0 && !entry.leaving) {
                battery = new QLabel(row);
                battery->setPixmap(BatteryIcon(entry.battery, entry.charging));
                battery->setText(QString{});
                battery->setToolTip(entry.charging ? tr("Battery %1% (charging)").arg(entry.battery)
                                                   : tr("Battery %1%").arg(entry.battery));
            }
            auto* battery_text =
                battery ? new QLabel(QStringLiteral("%1%").arg(entry.battery), row) : nullptr;
            if (battery_text) {
                battery_text->setObjectName(QStringLiteral("note"));
                battery_text->setToolTip(battery->toolTip());
            }
            auto* name_line = new QHBoxLayout();
            name_line->setSpacing(2);
            name_line->addWidget(name_label);
            if (battery) {
                name_line->addSpacing(6);
                name_line->addWidget(battery_text);
                name_line->addWidget(battery);
            }
            name_line->addWidget(name_edit, 1);
            name_line->addWidget(name_edit_button);
            name_line->addWidget(name_apply);
            name_line->addWidget(name_remove);
            name_line->addStretch(1);
            line->addLayout(name_line, 1);
            const bool live = !entry.leaving;

            // The controller's profile; "No profile" (or the ✕) takes it off
            auto* profile = new QComboBox(row);
            auto* clear = AddClearButton(profile);
            profile->addItem(tr("No profile"));
            profile->addItems(profiles);
            // (Not found is -1: "No profile")
            profile->setCurrentIndex(
                static_cast<int>(profiles.indexOf(QString::fromStdString(entry.profile)) + 1));
            profile->setToolTip(tr("Mapping profile for P%1's controller").arg(player + 1));
            // One width for every row, whatever the profile's name
            profile->setFixedWidth(170);
            // (A profile name with no file behind it shows as none: there is nothing to pick)
            profile->setEnabled(live && !profiles.isEmpty());
            const auto take_off = [player] {
                auto& console = Common::GetControllerConsoleState();
                std::scoped_lock lock{console.mutex};
                console.profile_requests.push_back({player, {}, true});
            };
            connect(profile, qOverload<int>(&QComboBox::activated), this,
                    [player, profile, take_off](int index) {
                        if (index == 0) {
                            take_off();
                            return;
                        }
                        auto& console = Common::GetControllerConsoleState();
                        std::scoped_lock lock{console.mutex};
                        console.profile_requests.push_back(
                            {player, profile->itemText(index).toStdString()});
                    });
            clear->setToolTip(tr("Take the profile off P%1").arg(player + 1));
            // (A profile renamed or deleted elsewhere shows as none: no ✕ for it)
            clear->setVisible(live && profiles.contains(QString::fromStdString(entry.profile)));
            connect(clear, &QToolButton::clicked, this, take_off);
            line->addWidget(profile);

            // (A disabled arrow is drawn plain, not as a dark block)
            const QString plain_disabled =
                QStringLiteral("QToolButton:disabled { background: transparent; border: none; }");
            auto* up = new QToolButton(row);
            up->setArrowType(Qt::UpArrow);
            up->setAutoRaise(true);
            up->setStyleSheet(plain_disabled);
            up->setToolTip(tr("Swap with P%1").arg(player));
            up->setEnabled(live && player > 0);
            connect(up, &QToolButton::clicked, this,
                    [this, player] { RequestSwap(player, player - 1); });
            line->addWidget(up);
            auto* down = new QToolButton(row);
            down->setArrowType(Qt::DownArrow);
            down->setAutoRaise(true);
            down->setStyleSheet(plain_disabled);
            down->setToolTip(tr("Swap with P%1").arg(player + 2));
            down->setEnabled(live && player < last_player);
            connect(down, &QToolButton::clicked, this,
                    [this, player] { RequestSwap(player, player + 1); });
            line->addWidget(down);

            // Swap and Config outlined in the highlight colour (Swap filled while it waits)
            const QString outlined = OutlinedButtonStyle();
            const QString filled = Themed(QStringLiteral(
                "QToolButton { border: 1px solid palette(highlight); border-radius: 6px; "
                "padding: 3px 12px; background: palette(highlight); "
                "color: palette(highlighted-text); font-weight: bold; }"));
            auto* swap = new QToolButton(row);
            swap->setCursor(Qt::PointingHandCursor);
            swap->setStyleSheet(swap_from >= 0 ? filled : outlined);
            if (swap_from < 0) {
                swap->setText(tr("Swap"));
            } else if (swap_from == player) {
                swap->setText(tr("Cancel"));
            } else {
                swap->setText(tr("Swap with P%1").arg(swap_from + 1));
            }
            swap->setEnabled(live);
            connect(swap, &QToolButton::clicked, this, [this, player] { OnSwapPressed(player); });
            line->addWidget(swap);

            auto* settings = new QToolButton(row);
            settings->setText(tr("Config"));
            settings->setCursor(Qt::PointingHandCursor);
            settings->setStyleSheet(outlined);
            settings->setToolTip(tr("Open P%1's tab").arg(player + 1));
            settings->setEnabled(static_cast<bool>(open_player_tab));
            connect(settings, &QToolButton::clicked, this, [this, player] {
                if (open_player_tab) {
                    open_player_tab(player);
                }
            });
            line->addWidget(settings);

            // Turn Off, for a Bluetooth controller: Eden disconnects it, which turns most
            // controllers off. (Room kept on the other rows so the buttons line up.)
            if (any_turn_off) {
                line->addSpacing(6);
                auto* turn_off = new QToolButton(row);
                turn_off->setText(tr("Turn Off"));
                turn_off->setIcon(PowerIcon());
                turn_off->setIconSize(QSize(14, 14));
                turn_off->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                turn_off->setCursor(Qt::PointingHandCursor);
                turn_off->setStyleSheet(Themed(QStringLiteral(
                    "QToolButton { border: 1px solid #c83c3c; border-radius: 6px; "
                    "padding: 3px 10px; background: #c83c3c; color: white; font-weight: bold; }"
                    "QToolButton:hover { background: #a82c2c; border-color: #a82c2c; }"
                    "QToolButton:disabled { background: transparent; border-color: palette(mid); "
                    "color: palette(mid); }")));
                turn_off->setToolTip(
                    entry.turn_off == 2
                        ? tr("Turns off P%1's controller and frees the player. Turn the "
                             "controller on to join again.")
                              .arg(player + 1)
                        : tr("Press a button on this controller first, so Eden can tell it "
                             "from the others like it."));
                turn_off->setEnabled(live && entry.turn_off == 2);
                QSizePolicy keep = turn_off->sizePolicy();
                keep.setRetainSizeWhenHidden(true);
                turn_off->setSizePolicy(keep);
                turn_off->setVisible(entry.turn_off != 0);
                connect(turn_off, &QToolButton::clicked, this, [player] {
                    auto& console = Common::GetControllerConsoleState();
                    std::scoped_lock lock{console.mutex};
                    console.remove_requests.push_back(player);
                });
                line->addWidget(turn_off);
            }

            players_list->addWidget(row);
            player_rows.push_back(row);
            row_players.push_back(player);
        }
        if (!handheld_name.empty() && !handheld_has_player) {
            auto* row = new QFrame(players_box);
            auto* line = new QHBoxLayout(row);
            line->setContentsMargins(4, 2, 4, 2);
            line->addWidget(new QLabel(QStringLiteral("\u2014"), row));
            line->addWidget(new QLabel(tr("%1 (built-in, not a player while other controllers "
                                          "are connected)")
                                           .arg(QString::fromStdString(handheld_name)),
                                       row),
                            1);
            players_list->addWidget(row);
            player_rows.push_back(row);
            row_players.push_back(-1);
        }
    }

    // Rows are cards; a fill marks what happens to them: red leaving, blue input held or
    // rumbling, green new
    const QString card = Themed(QStringLiteral("QFrame#playerRow { border: 1px solid "
                                               "palette(mid); border-radius: 8px; "
                                               "background: palette(base); }"));
    const auto fill = [](const char* rgba) {
        return Themed(
            QStringLiteral("QFrame#playerRow { border: 1px solid palette(mid); border-radius: "
                           "8px; background-color: rgba(%1); } "
                           "QFrame#playerRow QLabel { color: white; }")
                .arg(QString::fromLatin1(rgba)));
    };
    const auto blink_on = [now](qint64 since) {
        const qint64 elapsed = now - since;
        return elapsed < flash_ms && (elapsed / 125) % 2 == 0;
    };
    for (size_t k = 0; k < player_rows.size(); ++k) {
        const int player = row_players[k];
        const auto entry = std::find_if(players.begin(), players.end(),
                                        [&](const auto& row) { return row.player == player; });
        if (player < 0 || entry == players.end()) {
            continue;
        }
        const auto rumbled = rumbled_at.find(player);
        const bool active =
            entry->active || (rumbled != rumbled_at.end() && now - rumbled->second < 500);
        const bool blink = entry->flash != 0 && blink_on(entry->flash_since);
        QString style = card;
        if (entry->flash == 2) {
            if (blink) {
                style = fill("210, 60, 60, 190");
            }
        } else if (active && !entry->leaving) {
            style = fill("60, 120, 230, 190");
        } else if (blink) {
            style = fill("60, 180, 90, 190");
        }
        if (player_rows[k]->styleSheet() != style) {
            player_rows[k]->setStyleSheet(style);
        }
    }

    if (!in_effect || (IsAutomatic(selected) && !engine_active)) {
        players_note->setText(tr("Switching..."));
    } else if (last_player < 0) {
        players_note->setText(tr("No controller is a player right now."));
    } else {
        players_note->setText(tr("Press a button on a controller to see its player. Swaps last "
                                 "for this session and do not change your saved controls."));
    }
    players_box->setVisible(in_effect && engine_active);
    ModernizeCombos(this);
}

void ConfigurePlayerAssignment::OnBuiltinPicked(int index) {
    // Picking is the user's say: a removed controller is no longer kept from auto-detect
    Settings::values.builtin_pad_blocked.SetValue(std::string{});
    const QString ident = builtin_picker->itemData(index).toString();
    if (ident.isEmpty()) {
        // Detect automatically: forget the stored pad, so the engine detects it afresh
        Settings::values.builtin_pad_identity.SetValue(std::string{});
        Settings::values.builtin_pad_steam_handle.SetValue(std::string{});
        Settings::values.builtin_pad_steam_manual.SetValue(false);
        Settings::values.builtin_pad_manual.SetValue(false);
        return;
    }
    // Under Steam the entries are Steam handles, set for Steam launches only
    const QString prefix = QStringLiteral("steamhandle:");
    if (ident.startsWith(prefix)) {
        Settings::values.builtin_pad_steam_handle.SetValue(ident.mid(prefix.size()).toStdString());
        Settings::values.builtin_pad_steam_manual.SetValue(true);
    } else {
        Settings::values.builtin_pad_identity.SetValue(ident.toStdString());
        Settings::values.builtin_pad_manual.SetValue(true);
    }
}

void ConfigurePlayerAssignment::OnIdentifyPressed() {
    Settings::values.builtin_pad_blocked.SetValue(std::string{});
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.identify_armed = true;
    state.identify_armed_at = std::chrono::steady_clock::now();
}

void ConfigurePlayerAssignment::OnSwapPressed(int player) {
    if (swap_from < 0) {
        swap_from = player;
    } else {
        if (swap_from != player) {
            RequestSwap(swap_from, player);
        }
        swap_from = -1;
    }
    RefreshPlayers();
}

void ConfigurePlayerAssignment::RequestSwap(int a, int b) {
    if (a == b || a < 0 || b < 0 || a >= MaxPlayers || b >= MaxPlayers) {
        return;
    }
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.swap_requests.emplace_back(a, b);
}

void ConfigurePlayerAssignment::RequestRumble(int player) {
    rumbled_at[player] = QDateTime::currentMSecsSinceEpoch();
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.rumble_requests.push_back(player);
}

void ConfigurePlayerAssignment::AddTopWidget(QWidget* widget) {
    content_layout->insertWidget(0, widget);
}

void ConfigurePlayerAssignment::SetOpenPlayerTab(std::function<void(int)> open) {
    open_player_tab = std::move(open);
    players_signature.clear();
    RefreshPlayers();
}
