// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Static Qt on macOS doesn't use Vulkan
// Other platforms do, and thus have conflicting VulkanMemoryAllocator symbols
#include <cmath>
#include <sstream>
#include "core/launch_params.h"
#if defined(QT_STATICPLUGIN) && !defined(__APPLE__)
#undef VMA_IMPLEMENTATION
#endif

#include <boost/algorithm/string/split.hpp>
#include "common/controller_console_state.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "frontend_common/settings_generator.h"
#include "render/performance_overlay.h"
#ifdef HAS_RESHADE
#include "configuration/configure_post_processing.h"
#include "video_core/post_processing/fx_preset.h"
#endif
#include "updater/update_dialog.h"

#include "common/fs/ryujinx_compat.h"
#include "main_window.h"
#include "network/network.h"
#include "qt_common/discord/discord.h"
#include "ui_main.h"

// Other Yuzu stuff //
#include "debugger/console.h"
#include "debugger/controller.h"

#include "about_dialog.h"
#include "data_dialog.h"
#include "deps_dialog.h"
#include "install_dialog.h"

#include "bootmanager.h"
#include "loading_screen.h"
#include "qt_common/util/vk.h"
#include "ryujinx_dialog.h"
#include "set_play_time_dialog.h"
#include "util/util.h"
#include "yuzu/game/game_list.h"
#include "yuzu/overlay_panel.h"

#include "applets/qt_amiibo_settings.h"
#include "applets/qt_controller.h"
#include "applets/qt_error.h"
#include "applets/qt_profile_select.h"
#include "applets/qt_software_keyboard.h"
#include "applets/qt_web_browser.h"

#include "configuration/configure_dialog.h"
#include "configuration/configure_input.h"
#include "configuration/configure_per_game.h"
#include "configuration/configure_player_assignment.h"
#include "configuration/configure_tas.h"
#include "configuration/input_profiles.h"

#include "util/clickable_label.h"
#include "util/controller_navigation.h"
#include "util/gamescope.h"
#include "util/overlay_dialog.h"

#include "multiplayer/state.h"

// Qt Stuff //
#define QT_NO_OPENGL

#include <QHelpEvent>
#include <QMenuBar>
#include <QStyleHints>
#include <QToolTip>

#include <atomic>
#include <thread>
#include <QActionGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#ifdef __linux__
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif
#include <QGuiApplication>
#include <QInputDialog>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QProgressDialog>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QtConcurrentRun>

// Qt Common //
#include "qt_common/config/shared_translation.h"
#include "qt_common/config/uisettings.h"

#include "qt_common/abstract/frontend.h"

#include "qt_common/qt_common.h"
#include "qt_common/qt_string_lookup.h"

#include "qt_common/util/content.h"
#include "qt_common/util/fs.h"
#include "qt_common/util/meta.h"
#include "qt_common/util/mod.h"
#include "qt_common/util/path.h"

#include "qt_common/render/emu_thread.h"

// These are wrappers to avoid the calls to CreateDirectory and CreateFile because of the Windows
// defines.
static FileSys::VirtualDir VfsFilesystemCreateDirectoryWrapper(const std::string& path,
                                                               FileSys::OpenMode mode) {
    return QtCommon::vfs->CreateDirectory(path, mode);
}

static FileSys::VirtualFile VfsDirectoryCreateFileWrapper(const FileSys::VirtualDir& dir,
                                                          const std::string& path) {
    return dir->CreateFile(path);
}

// Frontend //
#include "frontend_common/play_time_manager.h"

#ifdef ENABLE_UPDATE_CHECKER
#include "frontend_common/update_checker.h"
#endif

// Common //
#include "common/fs/fs.h"
#include "common/logging.h"
#include "common/memory_detect.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/string_util.h"

#ifdef ARCHITECTURE_x86_64
#include "common/cpu_features.h"
#endif

// Core //
#include "core/core.h"
#include "core/frontend/applets/general.h"
#include "core/frontend/applets/mii_edit.h"
#include "core/frontend/applets/software_keyboard.h"

#include "core/hle/kernel/k_process.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applet_web_browser_types.h"

#include "core/file_sys/card_image.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/savedata_factory.h"
#include "core/memory/dmnt_cheat_types.h"

#include "core/tools/renderdoc.h"

#include "core/perf_stats.h"

#include "core/crypto/key_manager.h"

// Input //
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/virtual_amiibo.h"

// Video Core //
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/shader_notify.h"

#include <SDL3/SDL.h>
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

#include <boost/container/flat_set.hpp>

// Platform stuff //
#include <boost/container/flat_set.hpp>

#ifdef __APPLE__
#include <unistd.h> // for chdir
#endif

#ifdef __unix__

#include <csignal>
#include <QSocketNotifier>
#include <sys/socket.h>

#endif

#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include "common/overlay_panels.h"
#include "common/param_package.h"
#include "qt_common/gamemode.h"

#ifdef _WIN32
#include "common/windows/timer_resolution.h"
#include "core/core_timing.h"

#include <QPlatformSurfaceEvent>
#include <dwmapi.h>
#include <format>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "Dwmapi.lib")
#endif

static inline void ApplyWindowsTitleBarDarkMode(HWND hwnd, bool enabled) {
    if (!hwnd)
        return;
    BOOL val = enabled ? TRUE : FALSE;
    // 20 = Win11/21H2+
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &val, sizeof(val))))
        return;
    // 19 = pre-21H2
    DwmSetWindowAttribute(hwnd, 19, &val, sizeof(val));
}

static inline void ApplyDarkToTopLevel(QWidget* w, bool on) {
    if (!w || !w->isWindow())
        return;
    ApplyWindowsTitleBarDarkMode(reinterpret_cast<HWND>(w->winId()), on);
}

namespace {
struct TitlebarFilter final : QObject {
    bool dark;
    explicit TitlebarFilter(bool is_dark) : QObject(qApp), dark(is_dark) {}

    void setDark(bool is_dark) {
        dark = is_dark;
    }

    void onFocusChanged(QWidget*, QWidget* now) {
        if (now)
            ApplyDarkToTopLevel(now->window(), dark);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            switch (ev->type()) {
            case QEvent::WinIdChange:
            case QEvent::Show:
            case QEvent::ShowToParent:
            case QEvent::WindowStateChange:
            case QEvent::ZOrderChange:
                ApplyDarkToTopLevel(w, dark);
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(obj, ev);
    }
};

static TitlebarFilter* g_filter = nullptr;
static QMetaObject::Connection g_focusConn;

} // namespace

static void ApplyGlobalDarkTitlebar(bool is_dark) {
    if (!g_filter) {
        g_filter = new TitlebarFilter(is_dark);
        qApp->installEventFilter(g_filter);
        g_focusConn = QObject::connect(qApp, &QApplication::focusChanged, g_filter,
                                       &TitlebarFilter::onFocusChanged);
    } else {
        g_filter->setDark(is_dark);
    }
    for (QWidget* w : QApplication::topLevelWidgets())
        ApplyDarkToTopLevel(w, is_dark);
}

static void RemoveTitlebarFilter() {
    if (!g_filter)
        return;
    qApp->removeEventFilter(g_filter);
    QObject::disconnect(g_focusConn);
    g_filter->deleteLater();
    g_filter = nullptr;
}

#endif

#ifdef YUZU_CRASH_DUMPS
#include "yuzu/breakpad.h"
#endif

using namespace Common::Literals;

#include "qt_common/discord/discord.h"

#ifdef USE_DISCORD_PRESENCE
#include "qt_common/discord/discord_impl.h"
#endif

#ifdef QT_STATICPLUGIN
#include <QtPlugin>

#if defined(_WIN32)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(__APPLE__)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif

#endif

#ifdef _WIN32
#include <windows.h>
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

constexpr int default_mouse_hide_timeout = 2500;
constexpr int default_input_update_timeout = 1;

constexpr size_t CopyBufferSize = 1_MiB;

/**
 * "Callouts" are one-time instructional messages shown to the user. In the config settings, there
 * is a bitfield "callout_flags" options, used to track if a message has already been shown to the
 * user. This is 32-bits - if we have more than 32 callouts, we should retire and recycle old ones.
 */
enum class CalloutFlag : uint32_t {
    DRDDeprecation = 0x2,
};

const int MainWindow::max_recent_files_item;

namespace {

constexpr std::array<std::pair<u32, const char*>, 5> default_game_icon_sizes{
    std::make_pair(0, QT_TRANSLATE_NOOP("MainWindow", "None")),
    std::make_pair(32, QT_TRANSLATE_NOOP("MainWindow", "Small (32x32)")),
    std::make_pair(64, QT_TRANSLATE_NOOP("MainWindow", "Standard (64x64)")),
    std::make_pair(128, QT_TRANSLATE_NOOP("MainWindow", "Large (128x128)")),
    std::make_pair(256, QT_TRANSLATE_NOOP("MainWindow", "Full Size (256x256)")),
};

QString GetTranslatedGameIconSize(size_t index) {
    return QCoreApplication::translate("MainWindow", default_game_icon_sizes[index].second);
}

} // namespace

#ifndef _WIN32
inline static bool isDarkMode() {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
}
#endif // _WIN32

namespace {

/// Asks the engine to put the players in this order: place k takes the controller player
/// order[k] has now, one swap at a time
void RequestPlayerOrder(const std::vector<int>& order) {
    std::vector<int> places = order;
    std::sort(places.begin(), places.end());
    if (order == places) {
        return;
    }
    std::map<int, int> holder;
    for (const int place : places) {
        holder[place] = place;
    }
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    for (std::size_t k = 0; k < places.size(); ++k) {
        int from = places[k];
        for (const auto& [place, original] : holder) {
            if (original == order[k]) {
                from = place;
            }
        }
        if (from != places[k]) {
            state.swap_requests.emplace_back(places[k], from);
            std::swap(holder[places[k]], holder[from]);
        }
    }
}

/// The players as chips after the last menu: a click rumbles the controller and opens its tab;
/// a chip drags left or right to change the order
class PlayerChips : public QWidget {
public:
    PlayerChips(QMenuBar* bar_, std::function<void(int)> open_settings_)
        : QWidget(bar_), bar{bar_}, open_settings{std::move(open_settings_)} {
        setAttribute(Qt::WA_NoSystemBackground);
        // (A dark theme gives a plain widget a solid background.) By name, or the chips'
        // tooltip takes the rule too and shows as a black box under gamescope
        setObjectName(QStringLiteral("playerChips"));
        setStyleSheet(QStringLiteral("#playerChips { background: transparent; }"));
        setCursor(Qt::PointingHandCursor);
        long_press.setSingleShot(true);
        long_press.setInterval(400);
        QObject::connect(&long_press, &QTimer::timeout, this, [this] {
            if (pressed >= 0) {
                dragging = true;
                setCursor(Qt::ClosedHandCursor);
                update();
            }
        });
        QObject::connect(&poll, &QTimer::timeout, this, [this] { Refresh(); });
        poll.start(100);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = bar->font();
        font.setBold(true);
        painter.setFont(font);
        const bool several = rows.size() > 1;
        const auto chip = [&](const Common::ControllerConsoleState::PlayerRow& row, QRectF rect,
                              int k) {
            const QColor color = ConfigurePlayerAssignment::PlayerColor(row.player);
            // Filled while an input is held, else outlined; blinking green after a swap
            const qint64 since = QDateTime::currentMSecsSinceEpoch() - row.flash_since;
            const bool blink = row.flash == 1 && since < 1500 && (since / 125) % 2 == 0;
            const bool lit = row.active || blink;
            if (lit) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(blink ? QColor(45, 160, 80) : color);
            } else {
                painter.setPen(QPen(color, 1.5));
                painter.setBrush(Qt::NoBrush);
            }
            painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
            const QColor ink = lit ? QColor(Qt::white) : color;
            // The battery's percent, then its icon, at the right end
            QRectF label = rect;
            if (row.battery >= 0 && batteries) {
                const QRectF cell(rect.right() - (several ? 16 : 6) - battery_w,
                                  rect.center().y() - 6, battery_w, 12);
                painter.drawPixmap(cell.topLeft(),
                                   ConfigurePlayerAssignment::Battery(row.battery, row.charging));
                const QRectF percent(cell.left() - 2 - percent_w, rect.top(), percent_w,
                                     rect.height());
                painter.save();
                QFont percent_font = painter.font();
                percent_font.setPointSizeF(percent_font.pointSizeF() * 0.85);
                painter.setFont(percent_font);
                painter.setPen(ink);
                painter.drawText(percent, Qt::AlignVCenter | Qt::AlignRight,
                                 QStringLiteral("%1%").arg(row.battery));
                painter.restore();
                label.setRight(percent.left() + (several ? 16 : 6));
            }
            // With other players around: the grip, and the arrows swapping with a neighbour
            painter.setPen(ink);
            painter.drawText(label, Qt::AlignCenter,
                             several ? QStringLiteral("\u283F P%1").arg(row.player + 1)
                                     : QStringLiteral("P%1").arg(row.player + 1));
            if (several) {
                const auto arrow = [&](QRectF area, bool left, bool enabled) {
                    QColor fill = ink;
                    fill.setAlpha(enabled ? 255 : 70);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(fill);
                    const QPointF c = area.center();
                    const qreal s = 3.5;
                    const QPointF tri[3] = {left ? QPointF(c.x() - s, c.y())
                                                 : QPointF(c.x() + s, c.y()),
                                            left ? QPointF(c.x() + s * 0.7, c.y() - s)
                                                 : QPointF(c.x() - s * 0.7, c.y() - s),
                                            left ? QPointF(c.x() + s * 0.7, c.y() + s)
                                                 : QPointF(c.x() - s * 0.7, c.y() + s)};
                    painter.drawPolygon(tri, 3);
                };
                arrow(LeftArrow(rect), true, k > 0);
                arrow(RightArrow(rect), false, k + 1 < static_cast<int>(rows.size()));
            }
        };
        for (std::size_t k = 0; k < rows.size(); ++k) {
            const QRectF rect = ChipRect(static_cast<int>(k));
            if (dragging && static_cast<int>(k) == pressed) {
                // Where the dragged chip came from
                QColor edge = ConfigurePlayerAssignment::PlayerColor(rows[k].player);
                edge.setAlpha(100);
                painter.setPen(QPen(edge, 1, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
                continue;
            }
            chip(rows[k], rect, static_cast<int>(k));
        }
        if (dragging && pressed >= 0 && pressed < static_cast<int>(rows.size())) {
            chip(rows[static_cast<std::size_t>(pressed)], ChipRect(pressed).translated(drag_dx, 0),
                 pressed);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        pressed = IndexAt(event->position().x());
        press_x = event->position().x();
        drag_dx = 0;
        dragging = false;
        // (Nothing to reorder with one player)
        if (pressed >= 0 && rows.size() > 1) {
            long_press.start();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging) {
            drag_dx = event->position().x() - press_x;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        long_press.stop();
        const int index = pressed;
        pressed = -1;
        setCursor(Qt::PointingHandCursor);
        if (index < 0 || index >= static_cast<int>(rows.size())) {
            dragging = false;
            return;
        }
        if (dragging) {
            dragging = false;
            // The players in the order of their chips, the dragged one where it was let go
            std::vector<std::pair<double, int>> centres;
            for (std::size_t k = 0; k < rows.size(); ++k) {
                const double x = ChipRect(static_cast<int>(k)).center().x() +
                                 (static_cast<int>(k) == index ? drag_dx : 0.0);
                centres.emplace_back(x, rows[k].player);
            }
            std::stable_sort(centres.begin(), centres.end());
            std::vector<int> order;
            for (const auto& [x, player] : centres) {
                order.push_back(player);
            }
            RequestPlayerOrder(order);
            update();
            return;
        }
        if (IndexAt(event->position().x()) != index) {
            return;
        }
        // An arrow: swap with the neighbouring chip's player
        if (rows.size() > 1) {
            const QRectF rect = ChipRect(index);
            const QPointF at = event->position();
            const int step = LeftArrow(rect).contains(at)    ? -1
                             : RightArrow(rect).contains(at) ? 1
                                                             : 0;
            if (step != 0) {
                const int other = index + step;
                if (other >= 0 && other < static_cast<int>(rows.size())) {
                    const int a = rows[static_cast<std::size_t>(index)].player;
                    const int b = rows[static_cast<std::size_t>(other)].player;
                    LOG_INFO(Frontend, "console-mode: menu chip swap P{} <-> P{}", a + 1, b + 1);
                    auto& state = Common::GetControllerConsoleState();
                    std::scoped_lock lock{state.mutex};
                    state.swap_requests.emplace_back(a, b);
                }
                return;
            }
        }
        // A click: the controller rumbles (vibration on), and its settings open
        const int player = rows[static_cast<std::size_t>(index)].player;
        if (Settings::values.vibration_enabled.GetValue() &&
            Settings::values.players.GetValue()[player].vibration_enabled) {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            state.rumble_requests.push_back(player);
        }
        QTimer::singleShot(0, this, [this, player] { open_settings(player); });
    }

    bool event(QEvent* event) override {
        if (event->type() == QEvent::ToolTip) {
            const auto* help = static_cast<QHelpEvent*>(event);
            const int index = IndexAt(help->pos().x());
            if (index >= 0) {
                const auto& row = rows[static_cast<std::size_t>(index)];
                const QString battery =
                    row.battery < 0 ? QString{}
                    : row.charging  ? QObject::tr("\nBattery %1% (charging)").arg(row.battery)
                                    : QObject::tr("\nBattery %1%").arg(row.battery);
                QToolTip::showText(
                    help->globalPos(),
                    (rows.size() > 1
                         ? QObject::tr("P%1: %2\nClick: rumble and open its settings\nArrows, "
                                       "or hold then drag: change the player order")
                               .arg(row.player + 1)
                               .arg(QString::fromStdString(row.name))
                         : QObject::tr("P%1: %2\nClick: rumble and open its settings")
                               .arg(row.player + 1)
                               .arg(QString::fromStdString(row.name))) +
                        battery,
                    this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QWidget::event(event);
    }

private:
    static constexpr int chip_w = 34;
    /// With arrows and the grip, when there are several players
    static constexpr int chip_w_full = 74;
    static constexpr int chip_gap = 4;
    /// A chip's battery, when its controller tells its charge (and the room it takes)
    static constexpr int battery_w = 20;
    /// Its percent after it ("100%" fits)
    static constexpr int percent_w = 28;
    static constexpr int battery_room = battery_w + percent_w + 4;

    /// A chip's width: wider with arrows and the grip (several players), and with a battery
    static int ChipWidth(const std::vector<Common::ControllerConsoleState::PlayerRow>& among,
                         std::size_t k, bool with_batteries) {
        return (among.size() > 1 ? chip_w_full : chip_w) +
               (with_batteries && among[k].battery >= 0 ? battery_room : 0);
    }

    QRectF ChipRect(int k) const {
        double x = 4;
        for (int j = 0; j < k; ++j) {
            x += ChipWidth(rows, static_cast<std::size_t>(j), batteries) + chip_gap;
        }
        return QRectF(x, 3, ChipWidth(rows, static_cast<std::size_t>(k), batteries),
                      height() - 6);
    }

    static QRectF LeftArrow(const QRectF& chip) {
        return QRectF(chip.left() + 2, chip.top(), 14, chip.height());
    }

    static QRectF RightArrow(const QRectF& chip) {
        return QRectF(chip.right() - 16, chip.top(), 14, chip.height());
    }

    int IndexAt(double x) const {
        for (int k = 0; k < static_cast<int>(rows.size()); ++k) {
            if (ChipRect(k)
                    .adjusted(-chip_gap / 2.0, 0, chip_gap / 2.0, 0)
                    .contains(x, height() / 2.0)) {
                return k;
            }
        }
        return -1;
    }

    void Refresh() {
        std::vector<Common::ControllerConsoleState::PlayerRow> now;
        bool engine_active = false;
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            engine_active = state.engine_active;
            for (const auto& row : state.players) {
                if (!row.leaving) {
                    now.push_back(row);
                }
            }
        }
        const auto mode = Settings::values.controller_applet_mode.GetValue();
        const bool automatic = mode == Settings::AppletMode::ConsoleMode ||
                               mode == Settings::AppletMode::HandheldConsoleMode;
        // Right after the last menu
        int x = 0;
        for (QAction* action : bar->actions()) {
            if (action->isVisible()) {
                const QRect place = bar->actionGeometry(action);
                if (place.isValid()) {
                    x = (std::max)(x, place.right());
                }
            }
        }
        // (Batteries go when the chips would run past the menu bar; they stay in the tooltips)
        const auto chips_width = [&](bool with_batteries) {
            int w = 8;
            for (std::size_t k = 0; k < now.size(); ++k) {
                w += ChipWidth(now, k, with_batteries) + chip_gap;
            }
            return w;
        };
        const bool fit = x + 10 + chips_width(true) <= bar->width();
        if (fit != batteries) {
            batteries = fit;
            update();
        }
        const int chips_w = chips_width(batteries);
        const QRect place(x + 10, 0, chips_w, bar->height());
        if (geometry() != place) {
            setGeometry(place);
        }
        const bool shown = automatic && engine_active && !now.empty();
        if (isVisible() != shown) {
            setVisible(shown);
        }
        // Repainted when a player changes and while one blinks (not while dragging)
        const auto same = [](const auto& a, const auto& b) {
            return a.player == b.player && a.active == b.active && a.name == b.name &&
                   a.flash == b.flash && a.flash_since == b.flash_since &&
                   a.battery == b.battery && a.charging == b.charging;
        };
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        const bool blinking = std::any_of(now.begin(), now.end(), [now_ms](const auto& r) {
            return r.flash != 0 && now_ms - r.flash_since < 1600;
        });
        if (!dragging && (blinking || now.size() != rows.size() ||
                          !std::equal(now.begin(), now.end(), rows.begin(), same))) {
            rows = std::move(now);
            update();
        }
    }

    QMenuBar* bar;
    std::function<void(int)> open_settings;
    std::vector<Common::ControllerConsoleState::PlayerRow> rows;
    /// The chips show their batteries (they fit in the menu bar with them)
    bool batteries = true;
    QTimer poll;
    QTimer long_press;
    int pressed = -1;
    double press_x = 0;
    double drag_dx = 0;
    bool dragging = false;
};

} // Anonymous namespace

MainWindow::MainWindow(bool has_broken_vulkan)
    : ui{std::make_unique<Ui::MainWindow>()},
      input_subsystem{std::make_shared<InputCommon::InputSubsystem>()}, user_data_migrator{this} {
    QtCommon::Init(this);

    Common::FS::CreateEdenPaths();
    this->config = std::make_unique<QtConfig>();

    Common::Log::Filter filter;
    filter.ParseFilterString(Settings::values.log_filter.GetValue());
    Common::Log::SetGlobalFilter(filter);

    if (user_data_migrator.migrated) {
        // Sort-of hack whereby we only move the old dir if it's a subfolder of the user dir

        using namespace Common::FS;

        static constexpr const std::array<const EdenPath, 4> paths = {
            EdenPath::NANDDir, EdenPath::SDMCDir, EdenPath::DumpDir, EdenPath::LoadDir};

        for (const EdenPath& path : paths) {
            std::string str_path = Common::FS::GetEdenPathString(path);
            if (str_path.starts_with(user_data_migrator.selected_emu.get_user_dir())) {
                boost::replace_all(
                    str_path, user_data_migrator.selected_emu.lower_name().toStdString(), "eden");
                Common::FS::SetEdenPath(path, str_path);
            }
        }
    }

#ifdef __unix__
    SetupSigInterrupts();
#endif

    SetGamemodeEnabled(UISettings::values.enable_gamemode.GetValue());

    UISettings::RestoreWindowState(config);

    LoadTranslation();
    FrontendCommon::GenerateSettings();

    setAcceptDrops(true);
    ui->setupUi(this);
    statusBar()->hide();

    startup_icon_theme = QIcon::themeName();
    // fallback can only be set once, colorful theme icons are okay on both light/dark
    QIcon::setFallbackThemeName(QStringLiteral("colorful"));
    QIcon::setFallbackSearchPaths(QStringList(QStringLiteral(":/icons")));

    default_theme_paths = QIcon::themeSearchPaths();
    UpdateUITheme();

    SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
    discord_rpc->Update();

    play_time_manager = std::make_unique<PlayTime::PlayTimeManager>();

    InitializeWidgets();
    InitializeDebugWidgets();
    InitializeRecentFileMenuActions();
    InitializeHotkeys();

    SetDefaultUIGeometry();
    RestoreUIState();
    FitToScreenUnderGamescope(this);
    Gamescope::FitDialogsAndMenus(this);

    ConnectMenuEvents();
    ConnectWidgetEvents();

    QtCommon::SetupHID();
    controller_dialog->refreshConfiguration();

    UpdateWindowTitle();

    show();

#ifdef ENABLE_UPDATE_CHECKER
    if (UISettings::values.check_for_updates) {
        update_future = QtConcurrent::run(
            []() -> std::optional<Common::Net::Release> { return UpdateChecker::GetUpdate(); });
        update_watcher.connect(&update_watcher, &QFutureWatcher<QString>::finished, this,
                               &MainWindow::OnEmulatorUpdateAvailable);
        update_watcher.setFuture(update_future);
    }
#endif

    // Setup content providers.
    QtCommon::SetupContentProviders();

    // Gen keys if necessary
    OnCheckFirmwareDecryption();

    // Check for orphaned profiles and reset profile data if necessary
    QtCommon::Content::FixProfiles();

    if (Settings::values.use_dev_keys.GetValue()) {
        Core::Crypto::KeyManager::Instance().ReloadKeys();
    }
    game_list->PopulateAsync(UISettings::values.game_dirs);

    // Set up game list mode checkboxes.
    SetGameListMode(UISettings::values.game_list_mode.GetValue());

    // make sure menubar has the arrow cursor instead of inheriting from this
    ui->menubar->setCursor(QCursor());
    statusBar()->setCursor(QCursor());

    mouse_hide_timer.setInterval(default_mouse_hide_timeout);
    connect(&mouse_hide_timer, &QTimer::timeout, this, &MainWindow::HideMouseCursor);
    connect(ui->menubar, &QMenuBar::hovered, this, &MainWindow::ShowMouseCursor);
    // The players, after the last menu, in the automatic modes
    new PlayerChips(ui->menubar, [this](int player) {
        open_player_tab = player;
        OnConfigure();
    });

    update_input_timer.setInterval(default_input_update_timeout);
    connect(&update_input_timer, &QTimer::timeout, this, &MainWindow::UpdateInputDrivers);
    update_input_timer.start();

    if (has_broken_vulkan) {
        UISettings::values.has_broken_vulkan = true;

        QMessageBox::warning(this, tr("Broken Vulkan Installation Detected"),
                             tr("Vulkan initialization failed during boot."));
#ifdef HAS_OPENGL
        Settings::values.renderer_backend = Settings::RendererBackend::OpenGL_GLSL;
#else
        Settings::values.renderer_backend = Settings::RendererBackend::Null;
#endif

        UpdateAPIText();
        renderer_status_button->setDisabled(true);
        renderer_status_button->setChecked(false);
    } else {
        VkDeviceInfo::PopulateRecords(vk_device_records, this->window()->windowHandle());
    }

#if !defined(_WIN32)
    SDL_InitSubSystem(SDL_INIT_VIDEO);

    // Set a screensaver inhibition reason string. Currently passed to DBus by SDL and visible to
    // the user through their desktop environment.
    //: TRANSLATORS: This string is shown to the user to explain why yuzu needs to prevent the
    //: computer from sleeping
    QByteArray wakelock_reason = tr("Running a game").toUtf8();
    SDL_SetHint(SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, wakelock_reason.data());

    // SDL disables the screen saver by default, and setting the hint
    // SDL_HINT_VIDEO_ALLOW_SCREENSAVER doesn't seem to work, so we just enable the screen saver
    // for now.
    SDL_EnableScreenSaver();
#endif

    SetupPrepareForSleep();

    // Some moron added a race condition to the status bar
    // so now we have to make this completely unnecessary call
    // to prevent the UI from blowing up.
    UpdateUITheme();

    if (QStringList args = QApplication::arguments(); args.size() >= 2) {
        QList<QByteArray> qba{};
        for (auto const& e : args)
            qba.push_back(e.toUtf8());
        std::vector<char*> args_cstr{};
        for (auto& e : qba)
            args_cstr.push_back(e.data());

        auto const lp = Core::ParseLaunchParams(*QtCommon::system, args_cstr.size(), args_cstr.data(), nullptr);

        // Override fullscreen setting if gamepath or argument is provided
        if (!lp.filepath.empty() || lp.fullscreen) {
            ui->action_Fullscreen->setChecked(lp.fullscreen);
        }

        if (!lp.filepath.empty()) {
            BootGame(QString::fromStdString(lp.filepath), ApplicationAppletParameters());
        } else if (lp.launch_setup) {
            LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Starter), std::nullopt);
        } else if (lp.launch_qlaunch) {
            LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::QLaunch), std::nullopt);
        } else if (lp.launch_hlaunch) {
            std::filesystem::path const sd_dir =
                Common::FS::GetEdenPathString(Common::FS::EdenPath::SDMCDir);
            auto const hbl_path = (sd_dir / "atmosphere" / "hbl.nsp").string();
            BootGame(
                QString::fromStdString(hbl_path),
                LibraryAppletParameters(0x010000000000100Dull, Service::AM::AppletId::QLaunch));
        }
    }
}

MainWindow::~MainWindow() {
    // The config saves itself on destruction: it must write the user's own players
    RestoreConsoleProfile();

    // will get automatically deleted otherwise
    if (render_window->parent() == nullptr) {
        delete render_window;
    }

#ifdef __unix__
    ::close(sig_interrupt_fds[0]);
    ::close(sig_interrupt_fds[1]);
#endif
}

void MainWindow::AmiiboSettingsShowDialog(const Core::Frontend::CabinetParameters& parameters,
                                          std::shared_ptr<Service::NFC::NfcDevice> nfp_device) {
    cabinet_applet =
        new QtAmiiboSettingsDialog(this, parameters, input_subsystem.get(), nfp_device);
    SCOPE_EXIT {
        cabinet_applet->deleteLater();
        cabinet_applet = nullptr;
    };

    cabinet_applet->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint |
                                   Qt::WindowTitleHint | Qt::WindowSystemMenuHint);
    cabinet_applet->setWindowModality(Qt::WindowModal);

    if (cabinet_applet->exec() == QDialog::Rejected) {
        emit AmiiboSettingsFinished(false, {});
        return;
    }

    emit AmiiboSettingsFinished(true, cabinet_applet->GetName());
}

void MainWindow::AmiiboSettingsRequestExit() {
    if (cabinet_applet) {
        cabinet_applet->reject();
    }
}

void MainWindow::ControllerSelectorReconfigureControllers(
    const Core::Frontend::ControllerParameters& parameters) {
    controller_applet =
        new QtControllerSelectorDialog(this, parameters, input_subsystem.get(), *QtCommon::system);
    SCOPE_EXIT {
        controller_applet->deleteLater();
        controller_applet = nullptr;
    };

    controller_applet->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                                      Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                                      Qt::WindowSystemMenuHint);
    controller_applet->setWindowModality(Qt::WindowModal);
    bool is_success = controller_applet->exec() != QDialog::Rejected;

    // Don't forget to apply settings.
    QtCommon::system->HIDCore().DisableAllControllerConfiguration();
    QtCommon::system->ApplySettings();
    SaveAllConfigValues();

    UpdateStatusButtons();

    emit ControllerSelectorReconfigureFinished(is_success);
}

void MainWindow::ControllerSelectorRequestExit() {
    if (controller_applet) {
        controller_applet->reject();
    }
}

void MainWindow::ProfileSelectorSelectProfile(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    profile_select_applet = new QtProfileSelectionDialog(*QtCommon::system, this, parameters);
    SCOPE_EXIT {
        profile_select_applet->deleteLater();
        profile_select_applet = nullptr;
    };

    profile_select_applet->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                                          Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                                          Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    profile_select_applet->setWindowModality(Qt::WindowModal);
    if (profile_select_applet->exec() == QDialog::Rejected) {
        emit ProfileSelectorFinishedSelection(std::nullopt);
        return;
    }

    const auto uuid = QtCommon::system->GetProfileManager().GetUser(
        static_cast<std::size_t>(profile_select_applet->GetIndex()));
    if (!uuid.has_value()) {
        emit ProfileSelectorFinishedSelection(std::nullopt);
        return;
    }

    emit ProfileSelectorFinishedSelection(uuid);
}

void MainWindow::ProfileSelectorRequestExit() {
    if (profile_select_applet) {
        profile_select_applet->reject();
    }
}

void MainWindow::SoftwareKeyboardInitialize(
    bool is_inline, Core::Frontend::KeyboardInitializeParameters initialize_parameters) {
    if (software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is already initialized!");
        return;
    }

    software_keyboard = new QtSoftwareKeyboardDialog(render_window, *QtCommon::system, is_inline,
                                                     std::move(initialize_parameters));

    if (is_inline) {
        connect(
            software_keyboard, &QtSoftwareKeyboardDialog::SubmitInlineText, this,
            [this](Service::AM::Frontend::SwkbdReplyType reply_type, std::u16string submitted_text,
                   s32 cursor_position) {
                emit SoftwareKeyboardSubmitInlineText(reply_type, submitted_text, cursor_position);
            },
            Qt::QueuedConnection);
    } else {
        connect(
            software_keyboard, &QtSoftwareKeyboardDialog::SubmitNormalText, this,
            [this](Service::AM::Frontend::SwkbdResult result, std::u16string submitted_text,
                   bool confirmed) {
                emit SoftwareKeyboardSubmitNormalText(result, submitted_text, confirmed);
            },
            Qt::QueuedConnection);
    }
}

void MainWindow::SoftwareKeyboardShowNormal() {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    const auto& layout = render_window->GetFramebufferLayout();

    const auto x = layout.screen.left;
    const auto y = layout.screen.top;
    const auto w = layout.screen.GetWidth();
    const auto h = layout.screen.GetHeight();
    const auto scale_ratio = devicePixelRatioF();

    software_keyboard->ShowNormalKeyboard(render_window->mapToGlobal(QPoint(x, y) / scale_ratio),
                                          QSize(w, h) / scale_ratio);
}

void MainWindow::SoftwareKeyboardShowTextCheck(
    Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
    std::u16string text_check_message) {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    software_keyboard->ShowTextCheckDialog(text_check_result, text_check_message);
}

void MainWindow::SoftwareKeyboardShowInline(
    Core::Frontend::InlineAppearParameters appear_parameters) {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    const auto& layout = render_window->GetFramebufferLayout();

    const auto x =
        static_cast<int>(layout.screen.left + (0.5f * layout.screen.GetWidth() *
                                               ((2.0f * appear_parameters.key_top_translate_x) +
                                                (1.0f - appear_parameters.key_top_scale_x))));
    const auto y =
        static_cast<int>(layout.screen.top + (layout.screen.GetHeight() *
                                              ((2.0f * appear_parameters.key_top_translate_y) +
                                               (1.0f - appear_parameters.key_top_scale_y))));
    const auto w = static_cast<int>(layout.screen.GetWidth() * appear_parameters.key_top_scale_x);
    const auto h = static_cast<int>(layout.screen.GetHeight() * appear_parameters.key_top_scale_y);
    const auto scale_ratio = devicePixelRatioF();

    software_keyboard->ShowInlineKeyboard(std::move(appear_parameters),
                                          render_window->mapToGlobal(QPoint(x, y) / scale_ratio),
                                          QSize(w, h) / scale_ratio);
}

void MainWindow::SoftwareKeyboardHideInline() {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    software_keyboard->HideInlineKeyboard();
}

void MainWindow::SoftwareKeyboardInlineTextChanged(
    Core::Frontend::InlineTextParameters text_parameters) {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    software_keyboard->InlineTextChanged(std::move(text_parameters));
}

void MainWindow::SoftwareKeyboardExit() {
    if (!software_keyboard) {
        return;
    }

    software_keyboard->ExitKeyboard();

    software_keyboard = nullptr;
}

void MainWindow::WebBrowserOpenWebPage(const std::string& main_url,
                                       const std::string& additional_args, bool is_local) {
#ifdef YUZU_USE_QT_WEB_ENGINE

    // Raw input breaks with the web applet, Disable web applets if enabled
    if (Settings::values.disable_web_applet || Settings::values.enable_raw_input) {
        emit WebBrowserClosed(Service::AM::Frontend::WebExitReason::WindowClosed,
                              "http://localhost/");
        return;
    }

    web_applet = new QtNXWebEngineView(this, *QtCommon::system, input_subsystem.get());

    ui->action_Pause->setEnabled(false);
    ui->action_Restart->setEnabled(false);
    ui->action_Stop->setEnabled(false);

    {
        QProgressDialog loading_progress(this);
        loading_progress.setLabelText(tr("Loading Web Applet..."));
        loading_progress.setRange(0, 3);
        loading_progress.setValue(0);

        if (is_local && !Common::FS::Exists(main_url)) {
            loading_progress.show();

            auto future = QtConcurrent::run([this] { emit WebBrowserExtractOfflineRomFS(); });

            while (!future.isFinished()) {
                QCoreApplication::processEvents();

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        loading_progress.setValue(1);

        if (is_local) {
            web_applet->LoadLocalWebPage(main_url, additional_args);
        } else {
            web_applet->LoadExternalWebPage(main_url, additional_args);
        }

        if (render_window->IsLoadingComplete()) {
            render_window->hide();
        }

        const auto& layout = render_window->GetFramebufferLayout();
        const auto scale_ratio = devicePixelRatioF();
        web_applet->resize(layout.screen.GetWidth() / scale_ratio,
                           layout.screen.GetHeight() / scale_ratio);
        web_applet->move(layout.screen.left / scale_ratio,
                         (layout.screen.top / scale_ratio) + menuBar()->height());
        web_applet->setZoomFactor(static_cast<qreal>(layout.screen.GetWidth() / scale_ratio) /
                                  static_cast<qreal>(Layout::ScreenUndocked::Width));

        web_applet->setFocus();
        web_applet->show();

        loading_progress.setValue(2);

        QCoreApplication::processEvents();

        loading_progress.setValue(3);
    }

    bool exit_check = false;

    // TODO (Morph): Remove this
    QAction* exit_action = new QAction(tr("Disable Web Applet"), this);
    connect(exit_action, &QAction::triggered, this, [this] {
        const auto result = QMessageBox::warning(
            this, tr("Disable Web Applet"),
            tr("Disabling the web applet can lead to undefined behavior and should only be used "
               "with Super Mario 3D All-Stars. Are you sure you want to disable the web "
               "applet?\n(This can be re-enabled in the Debug settings.)"),
            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes) {
            Settings::values.disable_web_applet = true;
            web_applet->SetFinished(true);
        }
    });
    ui->menubar->addAction(exit_action);

    while (!web_applet->IsFinished()) {
        QCoreApplication::processEvents();

        if (!exit_check) {
            web_applet->page()->runJavaScript(
                QStringLiteral("end_applet;"), [&](const QVariant& variant) {
                    exit_check = false;
                    if (variant.toBool()) {
                        web_applet->SetFinished(true);
                        web_applet->SetExitReason(
                            Service::AM::Frontend::WebExitReason::EndButtonPressed);
                    }
                });

            exit_check = true;
        }

        if (web_applet->GetCurrentURL().contains(QStringLiteral("localhost"))) {
            if (!web_applet->IsFinished()) {
                web_applet->SetFinished(true);
                web_applet->SetExitReason(Service::AM::Frontend::WebExitReason::CallbackURL);
            }

            web_applet->SetLastURL(web_applet->GetCurrentURL().toStdString());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto exit_reason = web_applet->GetExitReason();
    const auto last_url = web_applet->GetLastURL();

    web_applet->hide();

    render_window->setFocus();

    if (render_window->IsLoadingComplete()) {
        render_window->show();
    }

    ui->action_Pause->setEnabled(true);
    ui->action_Restart->setEnabled(true);
    ui->action_Stop->setEnabled(true);

    ui->menubar->removeAction(exit_action);

    QCoreApplication::processEvents();

    emit WebBrowserClosed(exit_reason, last_url);

#else

    // Utilize the same fallback as the default web browser applet.
    emit WebBrowserClosed(Service::AM::Frontend::WebExitReason::WindowClosed, "http://localhost/");

#endif
}

void MainWindow::WebBrowserRequestExit() {
#ifdef YUZU_USE_QT_WEB_ENGINE
    if (web_applet) {
        web_applet->SetExitReason(Service::AM::Frontend::WebExitReason::ExitRequested);
        web_applet->SetFinished(true);
    }
#endif
}

void MainWindow::InitializeWidgets() {
    render_window = new GRenderWindow(this, input_subsystem);
    render_window->installEventFilter(this);
    render_window->hide();

    game_list = new GameList(QtCommon::vfs, QtCommon::provider.get(), *play_time_manager,
                             *QtCommon::system, this);
    ui->horizontalLayout->addWidget(game_list);

    game_list_placeholder = new GameListPlaceholder(this);
    ui->horizontalLayout->addWidget(game_list_placeholder);
    game_list_placeholder->setVisible(false);

    loading_screen = new LoadingScreen(this);
    loading_screen->hide();
    ui->horizontalLayout->addWidget(loading_screen);
    connect(loading_screen, &LoadingScreen::Hidden, this, [&] {
        loading_screen->Clear();
        if (emulation_running) {
            render_window->show();
            render_window->setFocus();
        }
    });

    multiplayer_state = new MultiplayerState(this, game_list->GetModel(), ui->action_Leave_Room,
                                             ui->action_Show_Room, *QtCommon::system);
    multiplayer_state->setVisible(false);

    // Create status bar
    message_label = new QLabel();
    // Configured separately for left alignment
    message_label->setFrameStyle(QFrame::NoFrame);
    message_label->setContentsMargins(4, 0, 4, 0);
    message_label->setAlignment(Qt::AlignLeft);
    statusBar()->addPermanentWidget(message_label, 1);

    shader_building_label = new QLabel();
    shader_building_label->setToolTip(tr("The amount of shaders currently being built"));
    res_scale_label = new QLabel();
    res_scale_label->setToolTip(tr("The current selected resolution scaling multiplier."));
    emu_speed_label = new QLabel();
    emu_speed_label->setToolTip(
        tr("Current emulation speed. Values higher or lower than 100% "
           "indicate emulation is running faster or slower than a Switch."));
    game_fps_label = new QLabel();
    game_fps_label->setToolTip(tr("How many frames per second the game is currently displaying. "
                                  "This will vary from game to game and scene to scene."));
    emu_frametime_label = new QLabel();
    emu_frametime_label->setToolTip(
        tr("Time taken to emulate a Switch frame, not counting framelimiting or v-sync. For "
           "full-speed emulation this should be at most 16.67 ms."));

    for (auto& label : {shader_building_label, res_scale_label, emu_speed_label, game_fps_label,
                        emu_frametime_label}) {
        label->setVisible(false);
        label->setFrameStyle(QFrame::NoFrame);
        label->setContentsMargins(4, 0, 4, 0);
        statusBar()->addPermanentWidget(label);
    }

    firmware_label = new QLabel();
    firmware_label->setObjectName(QStringLiteral("FirmwareLabel"));
    firmware_label->setVisible(false);
    firmware_label->setContentsMargins(4, 0, 4, 0);
    firmware_label->setFocusPolicy(Qt::NoFocus);
    statusBar()->addPermanentWidget(firmware_label);

    statusBar()->addPermanentWidget(multiplayer_state->GetStatusText(), 0);
    statusBar()->addPermanentWidget(multiplayer_state->GetStatusIcon(), 0);

    tas_label = new QLabel();
    tas_label->setObjectName(QStringLiteral("TASlabel"));
    tas_label->setFocusPolicy(Qt::NoFocus);
    statusBar()->insertPermanentWidget(0, tas_label);

    volume_popup = new QWidget(this);
    volume_popup->setWindowFlags(Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Popup);
    volume_popup->setLayout(new QVBoxLayout());
    volume_popup->setMinimumWidth(200);

    volume_slider = new QSlider(Qt::Horizontal);
    volume_slider->setObjectName(QStringLiteral("volume_slider"));
    volume_slider->setMaximum(200);
    volume_slider->setPageStep(5);
    volume_popup->layout()->addWidget(volume_slider);

    volume_button = new VolumeButton();
    volume_button->setObjectName(QStringLiteral("TogglableStatusBarButton"));
    volume_button->setFocusPolicy(Qt::NoFocus);
    volume_button->setCheckable(true);
    UpdateVolumeUI();
    connect(volume_slider, &QSlider::valueChanged, this, [this](int percentage) {
        Settings::values.audio_muted = false;
        const auto volume = static_cast<u8>(percentage);
        Settings::values.volume.SetValue(volume);
        UpdateVolumeUI();
    });
    connect(volume_button, &QPushButton::clicked, this, [&] {
        UpdateVolumeUI();
        volume_popup->setVisible(!volume_popup->isVisible());
        QRect rect = volume_button->geometry();
        QPoint bottomLeft = statusBar()->mapToGlobal(rect.topLeft());
        bottomLeft.setY(bottomLeft.y() - volume_popup->geometry().height());
        volume_popup->setGeometry(QRect(bottomLeft, QSize(rect.width(), rect.height())));
    });
    volume_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(volume_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;
                context_menu.addAction(
                    Settings::values.audio_muted ? tr("Unmute") : tr("Mute"), [this] {
                        Settings::values.audio_muted = !Settings::values.audio_muted;
                        UpdateVolumeUI();
                    });

                context_menu.addAction(tr("Reset Volume"), [this] {
                    Settings::values.volume.SetValue(100);
                    UpdateVolumeUI();
                });

                context_menu.exec(volume_button->mapToGlobal(menu_location));
                volume_button->repaint();
            });
    connect(volume_button, &VolumeButton::VolumeChanged, this, &MainWindow::UpdateVolumeUI);

    statusBar()->insertPermanentWidget(0, volume_button);

#ifdef HAS_RESHADE
    post_shader_status_button = new QPushButton();
    post_shader_status_button->setObjectName(QStringLiteral("TogglableStatusBarButton"));
    post_shader_status_button->setFocusPolicy(Qt::NoFocus);
    post_shader_status_button->setCheckable(true);
    connect(post_shader_status_button, &QPushButton::clicked, this, [this] {
        const bool enabled = Settings::values.post_shader_enabled.GetValue();
        Settings::values.post_shader_enabled.SetValue(!enabled);
        UpdatePostShaderText();
    });
    UpdatePostShaderText();
    post_shader_status_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(post_shader_status_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;

                for (auto const& preset : VideoCore::GetFxPresetCatalog()) {
                    context_menu.addAction(QString::fromStdString(preset.name),
                                           [this, name = preset.name] {
                                               VideoCore::ApplyFxPreset(name);
                                               Settings::values.post_shader_enabled.SetValue(true);
                                               UpdatePostShaderText();
                                           });
                }

                context_menu.addSeparator();
                context_menu.addAction(tr("Configure Effects..."), this,
                                       &MainWindow::OnPostProcessingShaders);

                context_menu.exec(post_shader_status_button->mapToGlobal(menu_location));
                post_shader_status_button->repaint();
            });
    statusBar()->insertPermanentWidget(0, post_shader_status_button);
#endif

    // setup AA button
    aa_status_button = new QPushButton();
    aa_status_button->setObjectName(QStringLiteral("TogglableStatusBarButton"));
    aa_status_button->setFocusPolicy(Qt::NoFocus);
    connect(aa_status_button, &QPushButton::clicked, [&] {
        auto aa_mode = Settings::values.anti_aliasing.GetValue();
        aa_mode = Settings::AntiAliasing(u32(aa_mode) + 1);
        if (u32(aa_mode) > u32(Settings::EnumMetadata<Settings::AntiAliasing>::GetLast()))
            aa_mode = Settings::EnumMetadata<Settings::AntiAliasing>::GetFirst();
        Settings::values.anti_aliasing.SetValue(aa_mode);
        aa_status_button->setChecked(true);
        UpdateAAText();
    });
    UpdateAAText();
    aa_status_button->setCheckable(true);
    aa_status_button->setChecked(true);
    aa_status_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(aa_status_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;
                for (auto const& aa_text_pair : ConfigurationShared::anti_aliasing_texts_map) {
                    context_menu.addAction(aa_text_pair.second, [this, aa_text_pair] {
                        Settings::values.anti_aliasing.SetValue(aa_text_pair.first);
                        UpdateAAText();
                    });
                }
                context_menu.exec(aa_status_button->mapToGlobal(menu_location));
                aa_status_button->repaint();
            });
    statusBar()->insertPermanentWidget(0, aa_status_button);

    // Setup Filter button
    filter_status_button = new QPushButton();
    filter_status_button->setObjectName(QStringLiteral("TogglableStatusBarButton"));
    filter_status_button->setFocusPolicy(Qt::NoFocus);
    connect(filter_status_button, &QPushButton::clicked, this, &MainWindow::OnToggleAdaptingFilter);
    UpdateFilterText();
    filter_status_button->setCheckable(true);
    filter_status_button->setChecked(true);
    filter_status_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filter_status_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;
                for (auto const& filter_text_pair : ConfigurationShared::scaling_filter_texts_map) {
                    context_menu.addAction(filter_text_pair.second, [this, filter_text_pair] {
                        Settings::values.scaling_filter.SetValue(filter_text_pair.first);
                        UpdateFilterText();
                    });
                }
                context_menu.exec(filter_status_button->mapToGlobal(menu_location));
                filter_status_button->repaint();
            });
    statusBar()->insertPermanentWidget(0, filter_status_button);

    // Setup Dock button
    dock_status_button = new QPushButton();
    dock_status_button->setObjectName(QStringLiteral("DockingStatusBarButton"));
    dock_status_button->setFocusPolicy(Qt::NoFocus);
    connect(dock_status_button, &QPushButton::clicked, this, &MainWindow::OnToggleDockedMode);
    dock_status_button->setCheckable(true);
    UpdateDockedButton();
    dock_status_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dock_status_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;

                for (auto const& pair : ConfigurationShared::use_docked_mode_texts_map) {
                    context_menu.addAction(pair.second, [this, &pair] {
                        if (pair.first != Settings::values.use_docked_mode.GetValue()) {
                            OnToggleDockedMode();
                        }
                    });
                }
                context_menu.exec(dock_status_button->mapToGlobal(menu_location));
                dock_status_button->repaint();
            });
    statusBar()->insertPermanentWidget(0, dock_status_button);

    // Setup GPU Accuracy button
    gpu_accuracy_button = new QPushButton();
    gpu_accuracy_button->setObjectName(QStringLiteral("GPUStatusBarButton"));
    gpu_accuracy_button->setCheckable(true);
    gpu_accuracy_button->setFocusPolicy(Qt::NoFocus);
    connect(gpu_accuracy_button, &QPushButton::clicked, this, &MainWindow::OnToggleGpuAccuracy);
    UpdateGPUAccuracyButton();
    gpu_accuracy_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(gpu_accuracy_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;

                for (auto const& gpu_accuracy_pair : ConfigurationShared::gpu_accuracy_texts_map) {
                    context_menu.addAction(gpu_accuracy_pair.second, [this, gpu_accuracy_pair] {
                        Settings::values.gpu_accuracy.SetValue(gpu_accuracy_pair.first);
                        UpdateGPUAccuracyButton();
                    });
                }
                context_menu.exec(gpu_accuracy_button->mapToGlobal(menu_location));
                gpu_accuracy_button->repaint();
            });
    statusBar()->insertPermanentWidget(0, gpu_accuracy_button);

    // Setup Renderer API button
    renderer_status_button = new QPushButton();
    renderer_status_button->setObjectName(QStringLiteral("RendererStatusBarButton"));
    renderer_status_button->setCheckable(true);
    renderer_status_button->setFocusPolicy(Qt::NoFocus);
    connect(renderer_status_button, &QPushButton::clicked, this, &MainWindow::OnToggleGraphicsAPI);
    UpdateAPIText();
    renderer_status_button->setCheckable(true);
    renderer_status_button->setChecked(Settings::values.renderer_backend.GetValue() ==
                                       Settings::RendererBackend::Vulkan);
    renderer_status_button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(renderer_status_button, &QPushButton::customContextMenuRequested,
            [this](const QPoint& menu_location) {
                QMenu context_menu;

                for (auto const& renderer_backend_pair :
                     ConfigurationShared::renderer_backend_texts_map) {
                    if (renderer_backend_pair.first == Settings::RendererBackend::Null) {
                        continue;
                    }
                    context_menu.addAction(
                        renderer_backend_pair.second, [this, renderer_backend_pair] {
                            Settings::values.renderer_backend.SetValue(renderer_backend_pair.first);
                            UpdateAPIText();
                        });
                }
                context_menu.exec(renderer_status_button->mapToGlobal(menu_location));
                renderer_status_button->repaint();
            });
    statusBar()->insertPermanentWidget(0, renderer_status_button);

    // Setup Refresh Button
    refresh_button = new QPushButton();
    refresh_button->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    refresh_button->setObjectName(QStringLiteral("RefreshButton"));
    refresh_button->setFocusPolicy(Qt::NoFocus);
    connect(refresh_button, &QPushButton::clicked, this, &MainWindow::OnGameListRefresh);

    statusBar()->insertPermanentWidget(0, refresh_button);

    statusBar()->setVisible(true);
    setStyleSheet(QStringLiteral("QStatusBar::item{border: none;}"));
}

void MainWindow::InitializeDebugWidgets() {
    QMenu* debug_menu = ui->menu_View_Debugging;

    controller_dialog = new ControllerDialog(QtCommon::system->HIDCore(), input_subsystem, this);
    controller_dialog->hide();
    debug_menu->addAction(controller_dialog->toggleViewAction());
}

void MainWindow::InitializeRecentFileMenuActions() {
    for (int i = 0; i < max_recent_files_item; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &MainWindow::OnMenuRecentFile);

        ui->menu_recent_files->addAction(actions_recent_files[i]);
    }
    ui->menu_recent_files->addSeparator();
    QAction* action_clear_recent_files = new QAction(this);
    action_clear_recent_files->setText(tr("&Clear Recent Files"));
    connect(action_clear_recent_files, &QAction::triggered, this, [this] {
        UISettings::values.recent_files.clear();
        UpdateRecentFiles();
    });
    ui->menu_recent_files->addAction(action_clear_recent_files);

    UpdateRecentFiles();
}

void MainWindow::LinkActionShortcut(QAction* action, const QString& action_name,
                                    const bool tas_allowed) {
    static const auto main_window = std::string("Main Window");
    action->setShortcut(hotkey_registry.GetKeySequence(main_window, action_name.toStdString()));
    action->setShortcutContext(
        hotkey_registry.GetShortcutContext(main_window, action_name.toStdString()));
    action->setAutoRepeat(false);

    this->addAction(action);

    auto* controller =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    const auto* controller_hotkey =
        hotkey_registry.GetControllerHotkey(main_window, action_name.toStdString(), controller);
    connect(
        controller_hotkey, &ControllerShortcut::Activated, this,
        [action, tas_allowed, this] {
            auto [tas_status, current_tas_frame, total_tas_frames] =
                input_subsystem->GetTas()->GetStatus();
            if (tas_allowed || tas_status == InputCommon::TasInput::TasState::Stopped) {
                action->trigger();
            }
        },
        Qt::QueuedConnection);
}

void MainWindow::InitializeHotkeys() {
    hotkey_registry.LoadHotkeys();

    LinkActionShortcut(ui->action_Load_File, QStringLiteral("Load File"));
    LinkActionShortcut(ui->action_Load_Amiibo, QStringLiteral("Load/Remove Amiibo"));
    LinkActionShortcut(ui->action_Exit, QStringLiteral("Exit Eden"));
    LinkActionShortcut(ui->action_Restart, QStringLiteral("Restart Emulation"));
    LinkActionShortcut(ui->action_Pause, QStringLiteral("Continue/Pause Emulation"));
    LinkActionShortcut(ui->action_Stop, QStringLiteral("Stop Emulation"));
    LinkActionShortcut(ui->action_Show_Filter_Bar, QStringLiteral("Toggle Filter Bar"));
    LinkActionShortcut(ui->action_Show_Status_Bar, QStringLiteral("Toggle Status Bar"));
    LinkActionShortcut(ui->action_Show_Performance_Overlay,
                       QStringLiteral("Toggle Performance Overlay"));
    LinkActionShortcut(ui->action_Show_Controller_Overlay,
                       QStringLiteral("Toggle Controller Overlay"));
    LinkActionShortcut(ui->action_Fullscreen, QStringLiteral("Fullscreen"));
    LinkActionShortcut(ui->action_Capture_Screenshot, QStringLiteral("Capture Screenshot"));
    LinkActionShortcut(ui->action_TAS_Start, QStringLiteral("TAS Start/Stop"), true);
    LinkActionShortcut(ui->action_TAS_Record, QStringLiteral("TAS Record"), true);
    LinkActionShortcut(ui->action_TAS_Reset, QStringLiteral("TAS Reset"), true);
    LinkActionShortcut(ui->action_View_Lobby, QStringLiteral("Browse Public Game Lobby"));
    LinkActionShortcut(ui->action_Start_Room, QStringLiteral("Create Room"));
    LinkActionShortcut(ui->action_Connect_To_Room, QStringLiteral("Direct Connect to Room"));
    LinkActionShortcut(ui->action_Show_Room, QStringLiteral("Show Current Room"));
    LinkActionShortcut(ui->action_Leave_Room, QStringLiteral("Leave Room"));
    LinkActionShortcut(ui->action_Configure, QStringLiteral("Configure"));
    LinkActionShortcut(ui->action_Configure_Current_Game, QStringLiteral("Configure Current Game"));

    static const QString main_window = QStringLiteral("Main Window");
    const auto connect_shortcut = [&]<typename Fn>(const QString& action_name, const Fn& function) {
        const auto* hotkey =
            hotkey_registry.GetHotkey(main_window.toStdString(), action_name.toStdString(), this);
        auto* controller =
            QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
        const auto* controller_hotkey = hotkey_registry.GetControllerHotkey(
            main_window.toStdString(), action_name.toStdString(), controller);
        connect(hotkey, &QShortcut::activated, this, function);
        connect(controller_hotkey, &ControllerShortcut::Activated, this, function,
                Qt::QueuedConnection);
    };

    connect_shortcut(QStringLiteral("Exit Fullscreen"), [&] {
        if (emulation_running && ui->action_Fullscreen->isChecked()) {
            ui->action_Fullscreen->setChecked(false);
            ToggleFullscreen();
        }
    });
    connect_shortcut(QStringLiteral("Change Adapting Filter"), &MainWindow::OnToggleAdaptingFilter);
    connect_shortcut(QStringLiteral("Change Docked Mode"), &MainWindow::OnToggleDockedMode);
    connect_shortcut(QStringLiteral("Change GPU Mode"), &MainWindow::OnToggleGpuAccuracy);
    connect_shortcut(QStringLiteral("Audio Mute/Unmute"), &MainWindow::OnMute);
    connect_shortcut(QStringLiteral("Audio Volume Down"), &MainWindow::OnDecreaseVolume);
    connect_shortcut(QStringLiteral("Audio Volume Up"), &MainWindow::OnIncreaseVolume);

    connect_shortcut(QStringLiteral("Toggle Framerate Limit"), [this] {
        Settings::ToggleStandardMode();
        SetFPSSuffix();
    });

    connect_shortcut(QStringLiteral("Toggle Turbo Speed"), [this] {
        Settings::ToggleTurboMode();
        SetFPSSuffix();
    });

    connect_shortcut(QStringLiteral("Toggle Slow Speed"), [this] {
        Settings::ToggleSlowMode();
        SetFPSSuffix();
    });

    connect_shortcut(QStringLiteral("Toggle Renderdoc Capture"), [] {
        if (Settings::values.enable_renderdoc_hotkey) {
            QtCommon::system->GetRenderdocAPI().ToggleCapture();
        }
    });
    connect_shortcut(QStringLiteral("Toggle Mouse Panning"), [&] {
        Settings::values.mouse_panning = !Settings::values.mouse_panning;
        if (Settings::values.mouse_panning) {
            render_window->installEventFilter(render_window);
            render_window->setAttribute(Qt::WA_Hover, true);
        }
    });
}

void MainWindow::SetDefaultUIGeometry() {
    // geometry: 53% of the window contents are in the upper screen half, 47% in the lower half
    const QRect screenRect = QGuiApplication::primaryScreen()->geometry();

    const int w = screenRect.width() * 2 / 3;
    const int h = screenRect.height() * 2 / 3;
    const int x = (screenRect.x() + screenRect.width()) / 2 - w / 2;
    const int y = (screenRect.y() + screenRect.height()) / 2 - h * 53 / 100;

    setGeometry(x, y, w, h);
}

void MainWindow::RestoreUIState() {
    setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
    restoreGeometry(UISettings::values.geometry);
    // Work-around because the games list isn't supposed to be full screen
    if (isFullScreen()) {
        showNormal();
    }
    restoreState(UISettings::values.state);
    render_window->setWindowFlags(render_window->windowFlags() & ~Qt::FramelessWindowHint);
    render_window->restoreGeometry(UISettings::values.renderwindow_geometry);

    game_list->LoadInterfaceLayout();

    ui->action_Single_Window_Mode->setChecked(UISettings::values.single_window_mode.GetValue());
    ToggleWindowMode();

    ui->action_Fullscreen->setChecked(UISettings::values.fullscreen.GetValue());

    ui->action_Enable_Overlay_Applet->setChecked(Settings::values.enable_overlay.GetValue());

    ui->action_Show_Filter_Bar->setChecked(UISettings::values.show_filter_bar.GetValue());
    game_list->SetFilterVisible(ui->action_Show_Filter_Bar->isChecked());

    ui->action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar.GetValue());
    statusBar()->setVisible(ui->action_Show_Status_Bar->isChecked());

    ui->action_Show_Performance_Overlay->setChecked(
        UISettings::values.show_perf_overlay.GetValue());
    ui->action_Show_Controller_Overlay->setChecked(
        Settings::values.console_debug_overlay.GetValue());
    ui->action_Show_Controller_Hints->setChecked(Settings::values.console_hints.GetValue());
    if (perf_overlay)
        perf_overlay->setVisible(ui->action_Show_Performance_Overlay->isChecked());
    Debugger::ToggleConsole();
}

void MainWindow::OnAppFocusStateChanged(Qt::ApplicationState state) {
    if (state != Qt::ApplicationHidden && state != Qt::ApplicationInactive &&
        state != Qt::ApplicationActive) {
        LOG_DEBUG(Frontend, "ApplicationState unusual flag: {} ", state);
    }
    if (!emulation_running) {
        return;
    }
    if (UISettings::values.pause_when_in_background) {
        if (QtCommon::emu_thread->IsRunning() &&
            (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
            auto_paused = true;
            OnPauseGame();
        } else if (!QtCommon::emu_thread->IsRunning() && auto_paused &&
                   (state & Qt::ApplicationActive)) {
            auto_paused = false;
            OnStartGame();
        }
    }
    if (UISettings::values.mute_when_in_background) {
        if (!Settings::values.audio_muted &&
            (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
            Settings::values.audio_muted = true;
            auto_muted = true;
        } else if (auto_muted && (state & Qt::ApplicationActive)) {
            Settings::values.audio_muted = false;
            auto_muted = false;
        }
        UpdateVolumeUI();
    }
}

void MainWindow::ConnectWidgetEvents() {
    ui->menu_Cheats->installEventFilter(this);
    ui->menu_Cheats->menuAction()->setVisible(false);

    connect(game_list, &GameList::BootGame, this, &MainWindow::BootGameFromList);
    connect(game_list, &GameList::GameChosen, this, &MainWindow::OnGameListLoadFile);
    connect(game_list, &GameList::OpenDirectory, this, &MainWindow::OnGameListOpenDirectory);
    connect(game_list, &GameList::OpenFolderRequested, this, &MainWindow::OnGameListOpenFolder);
    connect(game_list, &GameList::OpenTransferableShaderCacheRequested, this,
            [this](u64 program_id) { QtCommon::Path::OpenShaderCache(program_id, this); });
    connect(game_list, &GameList::RemoveInstalledEntryRequested, this,
            &MainWindow::OnGameListRemoveInstalledEntry);
    connect(game_list, &GameList::RemoveFileRequested, this, &MainWindow::OnGameListRemoveFile);
    connect(game_list, &GameList::RemovePlayTimeRequested, this,
            &MainWindow::OnGameListRemovePlayTimeData);
    connect(game_list, &GameList::SetPlayTimeRequested, this, &MainWindow::OnGameListSetPlayTime);
    connect(game_list, &GameList::DumpRomFSRequested, this, &MainWindow::OnGameListDumpRomFS);
    connect(game_list, &GameList::VerifyIntegrityRequested, this,
            &MainWindow::OnGameListVerifyIntegrity);
    connect(game_list, &GameList::CopyTIDRequested, this, &MainWindow::OnGameListCopyTID);
    connect(game_list, &GameList::CreateShortcut, this, &MainWindow::OnGameListCreateShortcut);
    connect(game_list, &GameList::AddDirectory, this, &MainWindow::OnGameListAddDirectory);
    connect(game_list_placeholder, &GameListPlaceholder::AddDirectory, this,
            &MainWindow::OnGameListAddDirectory);
    connect(game_list, &GameList::ShowList, this, &MainWindow::OnGameListShowList);
    connect(game_list, &GameList::PopulatingCompleted,
            [this] { multiplayer_state->UpdateGameList(game_list->GetModel()); });
    connect(game_list, &GameList::SaveConfig, this, &MainWindow::OnSaveConfig);

    connect(game_list, &GameList::OpenPerGameGeneralRequested, this,
            &MainWindow::OnGameListOpenPerGameProperties);
    connect(game_list, &GameList::LinkToRyujinxRequested, this, &MainWindow::OnLinkToRyujinx);

    connect(this, &MainWindow::UpdateInstallProgress, this, &MainWindow::IncrementInstallProgress);

    // Software Keyboard Applet
    connect(this, &MainWindow::EmulationStarting, this, &MainWindow::SoftwareKeyboardExit);
    connect(this, &MainWindow::EmulationStopping, this, &MainWindow::SoftwareKeyboardExit);
    connect(this, &MainWindow::EmulationStarting, this, &MainWindow::RefreshRuntimeCheats);
    connect(this, &MainWindow::EmulationStopping, this, &MainWindow::ClearRuntimeCheats);
    connect(&console_mode_timer, &QTimer::timeout, this, &MainWindow::ApplyConsoleControllerMode);
    connect(&console_activity_timer, &QTimer::timeout, this, &MainWindow::PollControllerActivity);
    console_activity_timer.start(std::chrono::milliseconds{40});
    // 250ms: instant to the user, negligible cost (the input reload only runs on changes)
    console_mode_timer.start(std::chrono::milliseconds{250});

    connect(&status_bar_update_timer, &QTimer::timeout, this, &MainWindow::UpdateStatusBar);

    connect(this, &MainWindow::UpdateThemedIcons, multiplayer_state,
            &MultiplayerState::UpdateThemedIcons);
}

void MainWindow::RefreshRuntimeCheats() {
    ClearRuntimeCheats();

    const auto cheats = QtCommon::system->GetRuntimeCheats();
    auto* disable_all = ui->menu_Cheats->addAction(tr("Disable All Cheats"));
    connect(disable_all, &QAction::triggered, this, [this] {
        for (auto* action : runtime_cheat_actions) {
            if (action->isEnabled()) {
                action->setChecked(false);
            }
        }
    });

    auto* enable_all = ui->menu_Cheats->addAction(tr("Enable All Cheats"));
    connect(enable_all, &QAction::triggered, this, [this] {
        for (auto* action : runtime_cheat_actions) {
            if (action->isEnabled()) {
                action->setChecked(true);
            }
        }
    });

    ui->menu_Cheats->addSeparator();
    runtime_cheat_actions.reserve(cheats.size());
    for (const auto& cheat : cheats) {
        const QString name = cheat.name.empty() ? tr("Unnamed Cheat")
                                                : QString::fromUtf8(cheat.name);
        auto* action = ui->menu_Cheats->addAction(name);
        action->setCheckable(true);
        action->setChecked(cheat.enabled || cheat.is_master);
        action->setEnabled(!cheat.is_master);
        connect(action, &QAction::toggled, this, [action, id = cheat.id](bool enabled) {
            if (!QtCommon::system->SetCheatEnabled(id, enabled)) {
                const QSignalBlocker blocker{action};
                action->setChecked(!enabled);
            }
        });
        runtime_cheat_actions.push_back(action);
    }

    ui->menu_Cheats->menuAction()->setVisible(!runtime_cheat_actions.empty());
}

void MainWindow::ClearRuntimeCheats() {
    ui->menu_Cheats->clear();
    runtime_cheat_actions.clear();
    ui->menu_Cheats->menuAction()->setVisible(false);
}

namespace {

/// The joystick API is shared with the input driver's threads (the rumble worker);
/// SDL_LockJoysticks is the official guard for calling into it elsewhere.
struct JoystickApiLock {
    JoystickApiLock() {
        SDL_LockJoysticks();
    }
    ~JoystickApiLock() {
        SDL_UnlockJoysticks();
    }
};

/// Calls `use` with the pad opened as a joystick for the call (under the lock); `fallback` when it
/// can't be opened
template <typename T, typename Use>
T WithJoystick(SDL_JoystickID id, T fallback, Use&& use) {
    const JoystickApiLock lock;
    SDL_Joystick* js = SDL_OpenJoystick(id);
    if (!js) {
        return fallback;
    }
    T result = use(js);
    SDL_CloseJoystick(js);
    return result;
}

/// Calls `use` with the pad opened as a gamepad for the call (under the lock); `fallback` when it
/// can't be opened
template <typename T, typename Use>
T WithGamepad(SDL_JoystickID id, T fallback, Use&& use) {
    const JoystickApiLock lock;
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    if (!pad) {
        return fallback;
    }
    T result = use(pad);
    SDL_CloseGamepad(pad);
    return result;
}

/// A pad's stable identity: its hardware serial when it reports one, else its model GUID (which
/// two pads can share: slots are keyed by connection, this only matches returning pads)
std::string PadIdentity(SDL_JoystickID id) {
    const JoystickApiLock lock;
    SDL_Joystick* js = SDL_OpenJoystick(id);
    if (!js) {
        return {};
    }
    const char* serial = SDL_GetJoystickSerial(js);
    if (serial && serial[0]) {
        std::string out{"serial:"};
        out += serial;
        SDL_CloseJoystick(js);
        return out;
    }
    const SDL_GUID guid = SDL_GetJoystickGUID(js);
    SDL_CloseJoystick(js);
    char buf[33]{};
    for (int i = 0; i < 16; ++i) {
        unsigned byte = guid.data[i];
        // Bytes 2-3 are a CRC of the name, which Steam changes on the same pad: zeroed, as
        // SDLDriver::GetGUID does
        if (i == 2 || i == 3) {
            byte = 0;
        }
        std::snprintf(buf + i * 2, 3, "%02x", byte);
    }
    return std::string{"guid:"} + buf;
}

/// What Steam says is in one of its virtual pads, from the file it writes for the game. SDL
/// reads it at most every 3 seconds, so a controller Steam just moved showed as the old one.
struct SteamSlot {
    Uint64 handle = 0;
    Uint16 vendor = 0;
    Uint16 product = 0;
    SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN;
    QString name;
};

std::optional<SteamSlot> SteamSlotOf(SDL_JoystickID id);

/// A pad's display name
QString PadName(SDL_JoystickID id) {
    // (Steam names the Deck's controls with a translation key: not a name to show)
    const auto slot = SteamSlotOf(id);
    if (slot && !slot->name.isEmpty() && !slot->name.startsWith(QLatin1Char('#'))) {
        return slot->name;
    }
    const Uint16 vendor = slot ? slot->vendor : SDL_GetJoystickVendorForID(id);
    const Uint16 product = slot ? slot->product : SDL_GetJoystickProductForID(id);
    if (vendor == 0x28DE && product == 0x1205) {
        return QStringLiteral("Steam Deck");
    }
    return WithJoystick(id, QStringLiteral("?"), [](SDL_Joystick* js) {
        const char* name = SDL_GetJoystickName(js);
        return name ? QString::fromUtf8(name) : QStringLiteral("?");
    });
}

/// True for Steam Input's virtual pads, whose GUID and serial aren't stable (Steam re-binds them)
bool IsSteamVirtualPad(SDL_JoystickID id) {
    if (!Common::RunningUnderSteam()) {
        return false;
    }
    // Every virtual pad's GUID carries Valve's vendor (0x28DE, bytes 4-5); the name isn't
    // usable (one pad shows as "SteamDeck" and "Xbox" in a session)
    // Valve makes real controllers too: the virtual gamepad product (0x11FF, bytes 8-9) marks them
    return WithJoystick(id, false, [](SDL_Joystick* js) {
        const SDL_GUID guid = SDL_GetJoystickGUID(js);
        return guid.data[4] == 0xDE && guid.data[5] == 0x28 && guid.data[8] == 0xFF &&
               guid.data[9] == 0x11;
    });
}

/// The OS device node of a pad (e.g. /dev/input/event5); empty where unsupported
QString PadPath(SDL_JoystickID id) {
    return WithJoystick(id, QString{}, [](SDL_Joystick* js) {
        const char* path = SDL_GetJoystickPath(js);
        return path ? QString::fromUtf8(path) : QString{};
    });
}

/// A pad's slot as Steam says now (nothing outside Steam's pads); reread when Steam writes it
std::optional<SteamSlot> SteamSlotOf(SDL_JoystickID id) {
#ifdef __linux__
    static const QByteArray file = qgetenv("SteamVirtualGamepadInfo");
    if (file.isEmpty()) {
        return std::nullopt;
    }
    // The pad's slot: Steam names its device "Microsoft X-Box 360 pad <slot>" (read once a pad)
    static std::map<SDL_JoystickID, int> slot_of_pad;
    auto known = slot_of_pad.find(id);
    if (known == slot_of_pad.end()) {
        const QString node = QFileInfo{PadPath(id)}.fileName();
        if (node.isEmpty()) {
            return std::nullopt;
        }
        int slot = -1;
        QFile name_file{QStringLiteral("/sys/class/input/%1/device/name").arg(node)};
        if (name_file.open(QIODevice::ReadOnly)) {
            const QString name = QString::fromUtf8(name_file.readAll()).trimmed();
            const QString prefix = QStringLiteral("Microsoft X-Box 360 pad ");
            bool ok = false;
            const int number = name.startsWith(prefix) ? name.mid(prefix.size()).toInt(&ok) : -1;
            slot = ok ? number : -1;
        }
        known = slot_of_pad.emplace(id, slot).first;
    }
    if (known->second < 0) {
        return std::nullopt;
    }
    // The file, read again when Steam writes it
    static std::map<int, SteamSlot> said;
    static timespec said_at{};
    struct ::stat st {};
    if (::stat(file.constData(), &st) != 0) {
        return std::nullopt;
    }
    if (st.st_mtim.tv_sec != said_at.tv_sec || st.st_mtim.tv_nsec != said_at.tv_nsec) {
        QFile in{QString::fromLocal8Bit(file)};
        if (!in.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        said_at = st.st_mtim;
        said.clear();
        SteamSlot* current = nullptr;
        for (const QByteArray& raw : in.readAll().split('\n')) {
            const QByteArray line = raw.trimmed();
            if (line.startsWith("[slot ") && line.endsWith(']')) {
                current = &said[line.mid(6, line.size() - 7).toInt()];
                continue;
            }
            const auto at = line.indexOf('=');
            if (!current || at < 0) {
                continue;
            }
            const QByteArray key = line.left(at);
            const QByteArray value = line.mid(at + 1);
            if (key == "handle") {
                current->handle = value.toULongLong(nullptr, 16);
            } else if (key == "VID") {
                current->vendor = static_cast<Uint16>(value.toUInt(nullptr, 16));
            } else if (key == "PID") {
                current->product = static_cast<Uint16>(value.toUInt(nullptr, 16));
            } else if (key == "type") {
                current->type = SDL_GetGamepadTypeFromString(value.constData());
            } else if (key == "name") {
                current->name = QString::fromUtf8(value);
            }
        }
    }
    if (const auto slot = said.find(known->second); slot != said.end()) {
        return slot->second;
    }
#endif
    return std::nullopt;
}

/// A pad's USB vendor id (under Steam, its controller's, as Steam says now)
Uint16 PadVendor(SDL_JoystickID id) {
    if (const auto slot = SteamSlotOf(id)) {
        return slot->vendor;
    }
    return SDL_GetJoystickVendorForID(id);
}

/// A pad's USB product id (under Steam, its controller's, as Steam says now)
Uint16 PadProduct(SDL_JoystickID id) {
    if (const auto slot = SteamSlotOf(id)) {
        return slot->product;
    }
    return SDL_GetJoystickProductForID(id);
}

/// A pad's gamepad type (under Steam, its controller's, as Steam says now)
SDL_GamepadType PadType(SDL_JoystickID id) {
    if (const auto slot = SteamSlotOf(id)) {
        return slot->type;
    }
    return SDL_GetGamepadTypeForID(id);
}

#ifdef __linux__
/// The kernel's HID entry for a controller (bus, vendor, product, address), from its uevent
struct HidEntry {
    unsigned bus = 0;
    unsigned vendor = 0;
    unsigned product = 0;
    std::string address;
};

std::optional<HidEntry> ReadHidEntry(const QString& dir) {
    QFile file{dir + QStringLiteral("/uevent")};
    if (dir.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    HidEntry entry;
    bool has_id = false;
    for (const QByteArray& line : file.readAll().split('\n')) {
        if (line.startsWith("HID_ID=")) {
            const QList<QByteArray> parts = line.mid(7).split(':');
            if (parts.size() == 3) {
                entry.bus = parts[0].toUInt(nullptr, 16);
                entry.vendor = parts[1].toUInt(nullptr, 16);
                entry.product = parts[2].toUInt(nullptr, 16);
                has_id = true;
            }
        } else if (line.startsWith("HID_UNIQ=")) {
            entry.address = line.mid(9).trimmed().toLower().toStdString();
        }
    }
    return has_id ? std::optional{entry} : std::nullopt;
}

/// A controller as the kernel has it: address (or HID entry), Bluetooth or not, input nodes
struct RealController {
    std::string key;
    std::string address;
    bool bluetooth = false;
    std::vector<QString> events;
};

constexpr unsigned BusBluetooth = 0x5;

/// The controllers of a model (one controller's several HID entries share its address)
std::vector<RealController> ControllersOfModel(unsigned vendor, unsigned product) {
    std::map<std::string, RealController> found;
    const QDir devices{QStringLiteral("/sys/bus/hid/devices")};
    for (const QString& name : devices.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = devices.filePath(name);
        const auto entry = ReadHidEntry(dir);
        if (!entry || entry->vendor != vendor || entry->product != product) {
            continue;
        }
        const std::string key = entry->address.empty() ? name.toStdString() : entry->address;
        auto& controller = found[key];
        controller.key = key;
        controller.address = entry->address;
        controller.bluetooth = entry->bus == BusBluetooth && entry->address.size() == 17;
        const QDir inputs{dir + QStringLiteral("/input")};
        for (const QString& input : inputs.entryList({QStringLiteral("input*")}, QDir::Dirs)) {
            const QDir nodes{inputs.filePath(input)};
            for (const QString& event : nodes.entryList({QStringLiteral("event*")}, QDir::Dirs)) {
                controller.events.push_back(QStringLiteral("/dev/input/") + event);
            }
        }
    }
    std::vector<RealController> out;
    for (auto& [key, controller] : found) {
        out.push_back(std::move(controller));
    }
    return out;
}

/// Whether a pad's controller is on Bluetooth, and its address: from the HID device behind the
/// pad, else (under Steam) the only controller of its model. Unsure with several of the model
/// until a button press tells them apart, so Turn Off never hits the wrong one.
struct PadBluetooth {
    std::string address;
    bool unsure = false;
};

PadBluetooth FindPadBluetooth(SDL_JoystickID id) {
    const QString node = QFileInfo{PadPath(id)}.fileName();
    if (!node.isEmpty()) {
        const QString dir =
            node.startsWith(QStringLiteral("hidraw"))
                ? QFileInfo{QStringLiteral("/sys/class/hidraw/%1/device").arg(node)}
                      .canonicalFilePath()
                : QFileInfo{QStringLiteral("/sys/class/input/%1/device/device").arg(node)}
                      .canonicalFilePath();
        if (const auto entry = ReadHidEntry(dir)) {
            const bool bluetooth = entry->bus == BusBluetooth && entry->address.size() == 17;
            return {bluetooth ? entry->address : std::string{}};
        }
    }
    const auto controllers =
        ControllersOfModel(PadVendor(id), PadProduct(id));
    const bool any_bluetooth = std::any_of(controllers.begin(), controllers.end(),
                                           [](const auto& c) { return c.bluetooth; });
    if (controllers.size() == 1) {
        return {controllers[0].bluetooth ? controllers[0].address : std::string{}};
    }
    return {{}, any_bluetooth};
}

/// How long a press read from a controller's input node counts (checks come every 100ms)
constexpr s64 RealPressWindowUs = 300'000;

/// True while a controller has an input held, or had one in the last 300ms (a quick tap falls
/// between checks), read from its input nodes
bool RealButtonHeld(const RealController& controller) {
    static std::map<QString, int> open_nodes;
    // (Each node's axes, read when it is opened: their ranges, and where they rest then)
    static std::map<std::pair<QString, u16>, input_absinfo> ranges;
    // (Each node's last press, by the kernel's timestamp)
    static std::map<QString, s64> pressed_at_us;
    timespec real{};
    clock_gettime(CLOCK_REALTIME, &real);
    const s64 now_us = static_cast<s64>(real.tv_sec) * 1'000'000 + real.tv_nsec / 1000;
    for (const QString& event : controller.events) {
        auto it = open_nodes.find(event);
        if (it == open_nodes.end()) {
            const int fd =
                ::open(event.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            // (A node that can't be opened yet, as it connects, is tried again next time)
            if (fd < 0) {
                continue;
            }
            it = open_nodes.emplace(event, fd).first;
            for (u16 code = 0; code <= ABS_MAX; ++code) {
                input_absinfo info{};
                if (ioctl(fd, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum) {
                    ranges[{event, code}] = info;
                }
            }
        }
        if (it->second < 0) {
            continue;
        }
        // Its queue: the presses since the last check (and drained, so the kernel keeps nothing)
        std::array<input_event, 64> events;
        ssize_t got = 0;
        while ((got = ::read(it->second, events.data(), sizeof(events))) > 0) {
            for (std::size_t k = 0; k < static_cast<std::size_t>(got) / sizeof(input_event); ++k) {
                const input_event& e = events[k];
                bool pressed = e.type == EV_KEY && e.value == 1;
                if (const auto range = ranges.find({event, e.code});
                    e.type == EV_ABS && range != ranges.end()) {
                    // A quarter of its range away from rest
                    const auto& info = range->second;
                    const double moved = std::abs(static_cast<double>(e.value - info.value)) /
                                         static_cast<double>(info.maximum - info.minimum);
                    pressed = moved > 0.25;
                }
                if (pressed) {
                    pressed_at_us[event] = static_cast<s64>(e.input_event_sec) * 1'000'000 +
                                           static_cast<s64>(e.input_event_usec);
                }
            }
        }
        if (const auto pressed = pressed_at_us.find(event);
            pressed != pressed_at_us.end() && now_us - pressed->second < RealPressWindowUs) {
            return true;
        }
        std::array<unsigned char, KEY_MAX / 8 + 1> keys{};
        if (ioctl(it->second, EVIOCGKEY(keys.size()), keys.data()) < 0) {
            ::close(it->second);
            open_nodes.erase(it);
            std::erase_if(ranges, [&](const auto& e) { return e.first.first == event; });
            continue;
        }
        if (std::any_of(keys.begin(), keys.end(), [](unsigned char b) { return b != 0; })) {
            return true;
        }
    }
    return false;
}

/// A controller's BlueZ object paths, one per Bluetooth adapter
QStringList BluezDevicePaths(const std::string& address) {
    QString device = QString::fromStdString(address).toUpper();
    device.replace(QLatin1Char(':'), QLatin1Char('_'));
    QStringList paths;
    const QDir adapters{QStringLiteral("/sys/class/bluetooth")};
    for (const QString& adapter : adapters.entryList({QStringLiteral("hci*")}, QDir::AllEntries)) {
        if (!adapter.contains(QLatin1Char(':'))) { // (hci0:12 is a connection, not an adapter)
            paths.push_back(QStringLiteral("/org/bluez/%1/dev_%2").arg(adapter, device));
        }
    }
    return paths;
}

/// Asks BlueZ for a property of a controller without waiting: `answer` gets it on the GUI
/// thread (while `context` lives), or never
void AskBluezProperty(const std::string& address, const QString& interface,
                      const QString& property, QObject* context,
                      std::function<void(const QVariant&)> answer) {
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return;
    }
    for (const QString& path : BluezDevicePaths(address)) {
        QDBusMessage get = QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), path, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        get << interface << property;
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(get, 1000));
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                         [answer](QDBusPendingCallWatcher* w) {
                             const QDBusPendingReply<QDBusVariant> reply = *w;
                             if (!reply.isError()) {
                                 answer(reply.value().variant());
                             }
                             w->deleteLater();
                         });
    }
}

/// Asks BlueZ whether a controller is connected, without waiting (see AskBluezProperty)
void AskBluezConnected(const std::string& address, QObject* context,
                       std::function<void(bool)> answer) {
    AskBluezProperty(address, QStringLiteral("org.bluez.Device1"), QStringLiteral("Connected"),
                     context, [answer](const QVariant& value) { answer(value.toBool()); });
}

/// Asks BlueZ to disconnect a controller, which turns most controllers off
void DisconnectBluetooth(const std::string& address) {
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        LOG_WARNING(Frontend, "console-mode: no system bus to disconnect {}", address);
        return;
    }
    for (const QString& path : BluezDevicePaths(address)) {
        const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), path, QStringLiteral("org.bluez.Device1"),
            QStringLiteral("Disconnect"));
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call));
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [address, path](auto* w) {
            const QDBusPendingReply<> reply = *w;
            if (reply.isError()) {
                if (reply.error().type() != QDBusError::UnknownObject) {
                    LOG_WARNING(Frontend, "console-mode: disconnect {} failed: {}", address,
                                reply.error().message().toStdString());
                }
            } else {
                LOG_INFO(Frontend, "console-mode: disconnected {} ({})", address,
                         path.toStdString());
            }
            w->deleteLater();
        });
    }
}
#endif

/// True while any button of a pad is held: the Identify handshake reads this.
bool PadAnyButtonPressed(SDL_JoystickID id) {
    return WithJoystick(id, false, [](SDL_Joystick* js) {
        const int count = SDL_GetNumJoystickButtons(js);
        for (int i = 0; i < count; ++i) {
            if (SDL_GetJoystickButton(js, i)) {
                return true;
            }
        }
        return false;
    });
}

/// True while a pad's + (Start, Menu, Options) is held
bool PadStartHeld(SDL_JoystickID id) {
    return WithGamepad(id, false, [](SDL_Gamepad* pad) {
        return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
    });
}

/// Steam's own player index for a pad (-1 when not assigned by Steam Input).
int PadPlayerIndex(SDL_JoystickID id) {
    return WithJoystick(id, -1, [](SDL_Joystick* js) { return SDL_GetJoystickPlayerIndex(js); });
}

/// True when SDL reports a gyro on the pad
bool PadHasGyro(SDL_JoystickID id) {
    return WithGamepad(id, false,
                       [](SDL_Gamepad* pad) { return SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO); });
}

/// True while a pad's accelerometer reads nothing (gravity always registers when on)
bool PadMotionSilent(SDL_JoystickID id) {
    return WithGamepad(id, false, [](SDL_Gamepad* pad) {
        std::array<float, 3> accel{};
        return SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, accel.data(), 3) &&
               accel[0] == 0.0f && accel[1] == 0.0f && accel[2] == 0.0f;
    });
}

/// Asks the Steam Deck's controller to turn its motion sensors on (Steam keeps them off unless
/// its layout uses the gyro): IMU mode 0x30 = 0x18 in feature report 0x87, as SDL sends it, on
/// a thread of its own
void EnableDeckMotion(const QString& path) {
#ifdef __linux__
    static std::atomic<bool> busy{false};
    if (!path.startsWith(QStringLiteral("/dev/hidraw")) || busy.exchange(true)) {
        return;
    }
    std::thread([node = path.toStdString()] {
        const int fd = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            std::array<unsigned char, 65> report{0x00, 0x87, 0x03, 0x30, 0x18, 0x00};
            if (ioctl(fd, HIDIOCSFEATURE(report.size()), report.data()) < 0 &&
                Common::ControllerProbes()) {
                LOG_WARNING(Frontend, "console-mode: deck motion request failed on {}", node);
            }
            ::close(fd);
        }
        busy = false;
    }).detach();
#endif
}

/// The controller applet modes, as the log names them
const char* AppletModeName(Settings::AppletMode mode) {
    switch (mode) {
    case Settings::AppletMode::HLE:
        return "Custom Frontend";
    case Settings::AppletMode::LLE:
        return "Real Hardware";
    case Settings::AppletMode::Disabled:
        return "Disabled";
    case Settings::AppletMode::ConsoleMode:
        return "Modern Console";
    case Settings::AppletMode::HandheldConsoleMode:
        return "Handheld Console";
    default:
        return "unknown";
    }
}

/// A controller type, as the log names it
const char* StyleName(Core::HID::NpadStyleIndex style) {
    using Core::HID::NpadStyleIndex;
    switch (style) {
    case NpadStyleIndex::Fullkey:
        return "Pro Controller";
    case NpadStyleIndex::Handheld:
        return "Handheld";
    case NpadStyleIndex::JoyconDual:
        return "Dual Joycons";
    case NpadStyleIndex::JoyconLeft:
        return "Left Joycon";
    case NpadStyleIndex::JoyconRight:
        return "Right Joycon";
    case NpadStyleIndex::GameCube:
        return "GameCube";
    default:
        return "other";
    }
}

/// Whether a game's supported controller types include one (as hid_core decides it)
bool StyleSupported(Core::HID::NpadStyleIndex style, const Core::HID::NpadStyleTag& tag) {
    using Core::HID::NpadStyleIndex;
    switch (style) {
    case NpadStyleIndex::Fullkey:
        return tag.fullkey;
    case NpadStyleIndex::Handheld:
        return tag.handheld;
    case NpadStyleIndex::JoyconDual:
        return tag.joycon_dual;
    case NpadStyleIndex::JoyconLeft:
        return tag.joycon_left;
    case NpadStyleIndex::JoyconRight:
        return tag.joycon_right;
    case NpadStyleIndex::GameCube:
        return tag.gamecube;
    case NpadStyleIndex::Pokeball:
        return tag.palma;
    case NpadStyleIndex::NES:
        return tag.lark;
    case NpadStyleIndex::SNES:
        return tag.lucia;
    case NpadStyleIndex::N64:
        return tag.lagoon;
    case NpadStyleIndex::SegaGenesis:
        return tag.lager;
    default:
        return false;
    }
}

/// A controller's short name, for a named player ("John (Xbox)")
QString ShortControllerName(SDL_JoystickID id, const QString& full) {
    const Uint16 vendor = PadVendor(id);
    const Uint16 product = PadProduct(id);
    if (vendor == 0x28DE && product == 0x1205) {
        return QStringLiteral("Steam Deck");
    }
    // (The Steam Controller and the Steam Controller (2026), wired or through a dongle)
    if (vendor == 0x28DE && (product == 0x1102 || product == 0x1142 || product == 0x1302 ||
                             product == 0x1304 || product == 0x1305)) {
        return QStringLiteral("Steam Controller");
    }
    switch (PadType(id)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return QStringLiteral("Xbox");
    case SDL_GAMEPAD_TYPE_PS3:
        return QStringLiteral("DualShock 3");
    case SDL_GAMEPAD_TYPE_PS4:
        return QStringLiteral("DualShock 4");
    case SDL_GAMEPAD_TYPE_PS5:
        return QStringLiteral("DualSense");
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        return QStringLiteral("Pro Controller");
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return QStringLiteral("Joy-Con");
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return QStringLiteral("GameCube");
    default:
        return full;
    }
}

/// A stick binding turned for a single Joy-Con held sideways (the right one a quarter clockwise,
/// the left one the other way); only SDL bindings turn
std::string TurnStickSideways(const std::string& raw, bool right) {
    Common::ParamPackage param{raw};
    if (param.Get("engine", "") != "sdl" || !param.Has("axis_x") || !param.Has("axis_y")) {
        return raw;
    }
    const int axis_x = param.Get("axis_x", 0);
    const int axis_y = param.Get("axis_y", 1);
    const std::string invert_x = param.Get("invert_x", "+");
    const std::string invert_y = param.Get("invert_y", "+");
    const auto flip = [](const std::string& sign) {
        return sign == "-" ? std::string{"+"} : std::string{"-"};
    };
    param.Set("axis_x", axis_y);
    param.Set("axis_y", axis_x);
    // Right: its x is the push's -y, its y the push's x. Left: its x is y, its y is -x.
    // (An SDL stick's y is read negated, after the axes are picked: SDL's y points down. So
    // the new x, SDL's y as read, is already the push's -y, and the new y needs a flip of x;
    // the left one the other way round.)
    param.Set("invert_x", right ? invert_y : flip(invert_y));
    param.Set("invert_y", right ? flip(invert_x) : invert_x);
    // (The centre offsets follow their axes; each is minus the axis's resting value as read,
    // and the swap reads each axis with the other's sign)
    if (param.Has("offset_x") || param.Has("offset_y")) {
        const float offset_x = param.Get("offset_x", 0.0f);
        const float offset_y = param.Get("offset_y", 0.0f);
        param.Set("offset_x", -offset_y);
        param.Set("offset_y", -offset_x);
    }
    return param.Serialize();
}

/// A stick binding the D-pad also moves (up, down, left, right: the D-pad buttons that push
/// the stick that way); an empty D-pad leaves the stick as it is
std::string StickWithDpad(const std::string& stick, const std::string& up,
                          const std::string& down, const std::string& left,
                          const std::string& right) {
    if (up.empty() && down.empty() && left.empty() && right.empty()) {
        return stick;
    }
    Common::ParamPackage param{{"engine", "analog_from_button"}};
    param.Set("up", up);
    param.Set("down", down);
    param.Set("left", left);
    param.Set("right", right);
    if (!stick.empty()) {
        param.Set("stick", stick);
    }
    return param.Serialize();
}

/// A player's bindings for a single Joy-Con held upright: the right one's stick is the left
/// stick (the D-pad moves it too), L / ZL its SL / SR; the left one's SL / SR are R / ZR
void BindUprightJoyCon(Settings::PlayerInput& set, bool right) {
    using namespace Settings::NativeButton;
    const auto pad = set.buttons;
    auto& out = set.buttons;
    if (right) {
        out[RStick] = pad[LStick];
        out[SLRight] = pad[L];
        out[SRRight] = pad[ZL];
        set.analogs[Settings::NativeAnalog::RStick] =
            StickWithDpad(set.analogs[Settings::NativeAnalog::LStick], pad[DUp], pad[DDown],
                          pad[DLeft], pad[DRight]);
    } else {
        out[SLLeft] = pad[R];
        out[SRLeft] = pad[ZR];
    }
}

/// A player's bindings for a single Joy-Con held sideways: face buttons by position, the
/// shoulders to SL / SR, the left stick to its stick (the D-pad moves it too), the motion
/// turned to the Joy-Con's own axes (a game turns a sideways Joy-Con's stick and motion itself)
void BindSidewaysJoyCon(Settings::PlayerInput& set, bool right) {
    for (std::string& motion : set.motions) {
        Common::ParamPackage param{motion};
        if (param.Has("motion")) {
            param.Set("turn", right ? 1 : -1);
            motion = param.Serialize();
        }
    }
    using namespace Settings::NativeButton;
    const auto pad = set.buttons;
    auto& out = set.buttons;
    const std::string bottom = pad[B], east = pad[A], west = pad[Y], top = pad[X];
    if (right) {
        // Turned a quarter clockwise: A below, X to the right, B to the left, Y on top
        out[A] = bottom;
        out[X] = east;
        out[B] = west;
        out[Y] = top;
        out[SLRight] = pad[L];
        out[SRRight] = pad[R];
        out[RStick] = pad[LStick];
        // (The D-pad as the player sees it: up pushes the turned stick as the stick's up does)
        set.analogs[Settings::NativeAnalog::RStick] =
            StickWithDpad(TurnStickSideways(set.analogs[Settings::NativeAnalog::LStick], true),
                          pad[DRight], pad[DLeft], pad[DUp], pad[DDown]);
    } else {
        // Turned a quarter the other way
        out[DLeft] = bottom;
        out[DDown] = east;
        out[DUp] = west;
        out[DRight] = top;
        out[SLLeft] = pad[L];
        out[SRLeft] = pad[R];
        set.analogs[Settings::NativeAnalog::LStick] =
            StickWithDpad(TurnStickSideways(set.analogs[Settings::NativeAnalog::LStick], false),
                          pad[DLeft], pad[DRight], pad[DDown], pad[DUp]);
    }
}

/// A battery as Linux lists it (/sys/class/power_supply): charge and whether it charges
#ifdef __linux__
std::optional<std::pair<int, bool>> ReadPowerSupply(const QString& dir) {
    const auto read = [&](const char* name) {
        QFile file{dir + QLatin1Char('/') + QLatin1String(name)};
        return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).trimmed()
                                              : QString{};
    };
    int percent = -1;
    bool ok = false;
    if (const int capacity = read("capacity").toInt(&ok); ok) {
        percent = capacity;
    } else {
        const QString level = read("capacity_level");
        percent = level == QStringLiteral("Full")       ? 100
                  : level == QStringLiteral("High")     ? 75
                  : level == QStringLiteral("Normal")   ? 50
                  : level == QStringLiteral("Low")      ? 15
                  : level == QStringLiteral("Critical") ? 5
                                                        : -1;
    }
    if (percent < 0) {
        return std::nullopt;
    }
    const QString status = read("status");
    return std::pair{percent,
                     status == QStringLiteral("Charging") || status == QStringLiteral("Full")};
}

/// A Bluetooth controller's battery, by its address (hid-<address>-battery)
std::optional<std::pair<int, bool>> BluetoothBattery(const std::string& address) {
    const QDir supplies{QStringLiteral("/sys/class/power_supply")};
    const QString wanted = QString::fromStdString(address).toLower();
    for (const QString& name : supplies.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        const QString dir = supplies.filePath(name);
        bool match = name.toLower().contains(wanted);
        if (!match) {
            if (const auto entry =
                    ReadHidEntry(QFileInfo{dir + QStringLiteral("/device")}.canonicalFilePath())) {
                match = entry->address == address;
            }
        }
        if (match) {
            return ReadPowerSupply(dir);
        }
    }
    return std::nullopt;
}

/// Asks BlueZ for a Bluetooth LE controller's battery (Xbox Series controllers have no power
/// supply in Linux), without waiting: `answer` gets it on the GUI thread
void AskBluezBattery(const std::string& address, QObject* context,
                     std::function<void(int)> answer) {
    AskBluezProperty(address, QStringLiteral("org.bluez.Battery1"), QStringLiteral("Percentage"),
                     context, [answer](const QVariant& value) {
                         bool ok = false;
                         const int percent = value.toInt(&ok);
                         if (ok && percent >= 0 && percent <= 100) {
                             answer(percent);
                         }
                     });
}

/// The system's own battery (the handheld's own controls run on it)
std::optional<std::pair<int, bool>> SystemBattery() {
    const QDir supplies{QStringLiteral("/sys/class/power_supply")};
    for (const QString& name : supplies.entryList({QStringLiteral("BAT*")}, QDir::AllEntries)) {
        if (const auto battery = ReadPowerSupply(supplies.filePath(name))) {
            return battery;
        }
    }
    return std::nullopt;
}
#endif

/// What SDL knows of a pad's battery: percent (-1 unknown), charging, wired
std::tuple<int, bool, bool> SdlBattery(SDL_JoystickID id) {
    const JoystickApiLock lock;
    SDL_Joystick* js = SDL_OpenJoystick(id);
    if (!js) {
        return {-1, false, false};
    }
    int percent = -1;
    const SDL_PowerState state = SDL_GetJoystickPowerInfo(js, &percent);
    SDL_CloseJoystick(js);
    switch (state) {
    case SDL_POWERSTATE_ON_BATTERY:
        return {percent, false, false};
    case SDL_POWERSTATE_CHARGING:
    case SDL_POWERSTATE_CHARGED:
        return {percent, true, false};
    case SDL_POWERSTATE_NO_BATTERY:
        return {-1, false, true};
    default:
        return {-1, false, false};
    }
}

/// True for Nintendo's controllers and look-alikes: their buttons already match the Switch's
bool IsNintendoPad(SDL_JoystickID id) {
    switch (PadType(id)) {
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return true;
    default:
        return PadVendor(id) == 0x057E;
    }
}

/// The players controllers take in the console modes: a Switch's eight (a ninth controller
/// waits). The settings' players past them are the handheld's own and one more.
constexpr int ConsolePlayers = 8;

/// Makers of standalone controllers: never a handheld's built-in controls, even when alone
bool IsControllerMaker(Uint16 vendor) {
    switch (vendor) {
    case 0x045E: // Microsoft
    case 0x054C: // Sony
    case 0x057E: // Nintendo
    case 0x2DC8: // 8BitDo
    case 0x046D: // Logitech
    case 0x20D6: // PowerA
    case 0x0E6F: // PDP
    case 0x0F0D: // Hori
    case 0x1532: // Razer
    case 0x0738: // Mad Catz
    case 0x146B: // Nacon
    case 0x3537: // GameSir
        return true;
    default:
        return false;
    }
}

/// Makers of handhelds with built-in controls: a pad from one is the handheld's own on a
/// direct launch
bool IsHandheldMaker(Uint16 vendor) {
    return vendor == 0x28DE || vendor == 0x17EF || vendor == 0x0B05 || vendor == 0x0DB0;
}

/// Whether a pad can be a handheld's own controls (of Valve's, only the Deck's)
bool CouldBeHandheld(Uint16 vendor, Uint16 product) {
    if (vendor == 0x28DE) {
        return product == 0x1205;
    }
    return !IsControllerMaker(vendor);
}

/// Whether a pad is plainly a standalone controller, never a handheld's controls: Valve's other
/// controllers, controller-only makers, and wireless Xbox controllers (a wired Microsoft id
/// stays: some handhelds' controls report one, such as the ROG Xbox Ally)
bool IsPlainlyStandalone(SDL_JoystickID id) {
    const Uint16 vendor = PadVendor(id);
    const Uint16 product = PadProduct(id);
    if (vendor == 0x28DE) {
        return product != 0x1205;
    }
    if (vendor == 0x045E) {
        switch (product) {
        case 0x02E0: // Xbox One S, Bluetooth
        case 0x02E6: // Xbox Wireless Adapter
        case 0x02FD: // Xbox One S, Bluetooth
        case 0x0719: // Xbox 360 wireless receiver
        case 0x0B05: // Xbox Elite Series 2, Bluetooth
        case 0x0B13: // Xbox Series X|S, Bluetooth
        case 0x0B20: // Xbox Series X|S, Bluetooth
        case 0x0B22: // Xbox Elite Series 2, Bluetooth
            return true;
        default:
            break;
        }
    } else if (IsControllerMaker(vendor)) {
        return true;
    }
    const JoystickApiLock lock;
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    if (!pad) {
        return false;
    }
    const bool wireless = SDL_GetJoystickConnectionState(SDL_GetGamepadJoystick(pad)) ==
                          SDL_JOYSTICK_CONNECTION_WIRELESS;
    SDL_CloseGamepad(pad);
    return wireless;
}

/// True for the controller types Steam reports for standalone controllers
bool IsStandaloneControllerType(SDL_GamepadType type) {
    return type != SDL_GAMEPAD_TYPE_UNKNOWN && type != SDL_GAMEPAD_TYPE_STANDARD;
}

/// Steam Input's handle for the physical controller behind a pad (0 none): Steam refills its
/// virtual pads in its own order, so only the handle tells which controller came back
Uint64 PadSteamHandle(SDL_JoystickID id) {
    // (As Steam says now: SDL's copy is up to 3 seconds old)
    if (const auto slot = SteamSlotOf(id)) {
        return slot->handle;
    }
    return WithGamepad(id, Uint64{0},
                       [](SDL_Gamepad* pad) { return SDL_GetGamepadSteamHandle(pad); });
}

/// The key a pad's Bluetooth state is kept under: its Steam handle under Steam (it follows the
/// controller between virtual pads), else the connection instance
std::string BluetoothKey(SDL_JoystickID id) {
    if (const Uint64 handle = PadSteamHandle(id)) {
        return std::format("steamhandle:{:#x}", handle);
    }
    return std::format("inst:{}", id);
}

/// How a pad is stored as the handheld's: Steam handle under Steam, else its identity
std::string PadKey(SDL_JoystickID id, const std::string& ident) {
    if (Common::SteamPadsInUse()) {
        if (const Uint64 handle = PadSteamHandle(id)) {
            return std::format("steamhandle:{:#x}", handle);
        }
    }
    return ident;
}

/// True while a pad has an input held: any button, or a stick or trigger past half travel.
bool PadInputHeld(SDL_JoystickID id) {
    return WithGamepad(id, false, [](SDL_Gamepad* pad) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b))) {
                return true;
            }
        }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            const Sint16 value = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(a));
            if (value > 16384 || value < -16384) {
                return true;
            }
        }
        return false;
    });
}

/// A short rumble pulse on a pad, if it supports rumble.
void PadRumble(SDL_JoystickID id, Uint16 strength, Uint32 duration_ms) {
    const JoystickApiLock lock;
    if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
        SDL_RumbleGamepad(pad, strength, strength, duration_ms);
        SDL_CloseGamepad(pad);
    }
}

/// An SDL motion binding for a device, as InputCommon's auto-mapping builds it
std::string SdlMotionParam(const std::string& guid, int port) {
    Common::ParamPackage param;
    param.Set("engine", "sdl");
    param.Set("motion", 0);
    param.Set("port", port);
    param.Set("guid", guid);
    return param.Serialize();
}

/// Points every SDL binding of a player at one device (guid + port), leaving other engines
/// alone. True when it changed.
bool RouteSdlBindings(Settings::PlayerInput& player, const std::string& guid, int port) {
    bool routed = false;
    const auto route = [&](auto& params) {
        for (std::string& raw : params) {
            Common::ParamPackage param{raw};
            if (param.Get("engine", "") != "sdl") {
                continue;
            }
            const std::string old_guid = param.Get("guid", "");
            if (old_guid == guid && param.Get("port", -1) == port) {
                continue;
            }
            param.Set("guid", guid);
            param.Set("port", port);
            if (param.Has("guid2") && param.Get("guid2", "") == old_guid) {
                param.Set("guid2", guid);
            }
            raw = param.Serialize();
            routed = true;
        }
    };
    route(player.buttons);
    route(player.analogs);
    route(player.motions);
    return routed;
}

/// The binding sets the console modes start from: the user's players without mappings or
/// profiles (each controller gets its defaults or a picked profile). Only a DSU server's motion
/// stays: the console modes don't expose motion.
std::array<Settings::PlayerInput, 10> ModernStartSets(
    const std::array<Settings::PlayerInput, 10>& user_players) {
    auto sets = user_players;
    for (auto& set : sets) {
        set.buttons.fill({});
        set.analogs.fill({});
        for (std::string& motion : set.motions) {
            if (Common::ParamPackage{motion}.Get("engine", "") != "cemuhookudp") {
                motion.clear();
            }
        }
        set.profile_name.clear();
    }
    return sets;
}

} // Anonymous namespace

void MainWindow::SaveAllConfigValues() {
    if (!console_profile) {
        config->SaveAllValues();
        return;
    }
    // A console mode drives the players at runtime; the file keeps the user's own
    auto& players = Settings::values.players.GetValue(true);
    std::swap(players, *console_profile);
    config->SaveAllValues();
    std::swap(players, *console_profile);
}

namespace {
/// The handheld set's motion bindings, to trace what clears the DSU gyro (Debug Probes)
std::string MotionTrace(const Settings::PlayerInput& player) {
    std::string out;
    for (const auto& motion : player.motions) {
        out += '[' + motion + ']';
    }
    return out;
}
} // Anonymous namespace

void MainWindow::RestoreConsoleProfile() {
    if (!console_profile) {
        return;
    }
    if (Common::ControllerProbes()) {
        LOG_INFO(Frontend, "console-mode: restore, profile P1 motions {}",
                 MotionTrace((*console_profile)[0]));
    }
    // A running game has its own copy of the players, which the engine drove: both copies get
    // the user's players back (the game's as read at boot)
    Settings::values.players.GetValue(true) = *console_profile;
    if (!Settings::values.players.UsingGlobal()) {
        Settings::values.players.GetValue() = game_profiles ? *game_profiles : *console_profile;
    }
    console_profile.reset();
    game_profiles.reset();
    session_sets.reset();
}

Settings::PlayerInput& MainWindow::SessionSet(int set) {
    if (game_profiles && !(*game_profiles)[set].profile_name.empty()) {
        return (*game_profiles)[set];
    }
    if (!session_sets) {
        session_sets = ModernStartSets(*console_profile);
    }
    return (*session_sets)[set];
}

std::array<Settings::PlayerInput, 10> MainWindow::ConsoleBindingSets() const {
    auto sets = session_sets ? *session_sets : ModernStartSets(*console_profile);
    if (game_profiles) {
        for (size_t i = 0; i < sets.size(); ++i) {
            if (!(*game_profiles)[i].profile_name.empty()) {
                sets[i] = (*game_profiles)[i];
            }
        }
    }
    return sets;
}

namespace {

/// A binding's mapping without its device (guid, port): what a Controls tab edit changes.
/// Order- and float-format-insensitive ("0.5" and "0.500000" are the same).
std::string BindingMapping(const std::string& raw) {
    if (raw.empty()) {
        return {};
    }
    Common::ParamPackage param{raw};
    // Only an SDL binding's guid/port is routing (a DSU server's address is the mapping)
    if (param.Get("engine", "") == "sdl") {
        param.Erase("guid");
        param.Erase("guid2");
        param.Erase("port");
    }
    std::vector<std::string> fields;
    std::string field;
    for (const char c : param.Serialize() + ",") {
        if (c != ',') {
            field += c;
            continue;
        }
        const auto colon = field.find(':');
        if (colon != std::string::npos) {
            const std::string value = field.substr(colon + 1);
            char* end = nullptr;
            const float number = std::strtof(value.c_str(), &end);
            if (!value.empty() && end && *end == '\0') {
                field = field.substr(0, colon + 1) + std::format("{}", number);
            }
        }
        fields.push_back(field);
        field.clear();
    }
    std::sort(fields.begin(), fields.end());
    std::string out;
    for (const auto& f : fields) {
        out += f + ',';
    }
    return out;
}

/// Puts a profile's mappings on a binding set, keeping the set's controller on SDL bindings
void ApplyProfileMappings(Settings::PlayerInput& set, const Settings::PlayerInput& profile) {
    const auto apply = [](auto& to, const auto& from) {
        for (size_t i = 0; i < to.size(); ++i) {
            Common::ParamPackage taken{from[i]};
            const Common::ParamPackage kept{to[i]};
            if (!from[i].empty() && !to[i].empty() && taken.Get("engine", "") == "sdl" &&
                kept.Get("engine", "") == "sdl") {
                taken.Set("guid", kept.Get("guid", ""));
                taken.Set("port", kept.Get("port", 0));
                if (kept.Has("guid2")) {
                    taken.Set("guid2", kept.Get("guid2", ""));
                } else {
                    taken.Erase("guid2");
                }
                to[i] = taken.Serialize();
            } else {
                to[i] = from[i];
            }
        }
    };
    apply(set.buttons, profile.buttons);
    apply(set.analogs, profile.analogs);
    apply(set.motions, profile.motions);
}

/// Brings edited mappings into the profile's, keeping the profile's own device on SDL bindings.
/// True when anything was taken.
template <typename Array>
bool MergeMappings(Array& profile, const Array& before, const Array& after) {
    bool taken = false;
    for (size_t i = 0; i < after.size(); ++i) {
        if (BindingMapping(after[i]) == BindingMapping(before[i])) {
            continue;
        }
        Common::ParamPackage edited{after[i]};
        const Common::ParamPackage original{profile[i]};
        if (!after[i].empty() && !profile[i].empty() && edited.Get("engine", "") == "sdl" &&
            original.Get("engine", "") == "sdl") {
            edited.Set("guid", original.Get("guid", ""));
            edited.Set("port", original.Get("port", 0));
            if (original.Has("guid2")) {
                edited.Set("guid2", original.Get("guid2", ""));
            } else {
                edited.Erase("guid2");
            }
            profile[i] = edited.Serialize();
        } else {
            profile[i] = after[i];
        }
        taken = true;
    }
    return taken;
}

} // Anonymous namespace

void MainWindow::ApplySeatProfiles() {
    const std::string stored = Settings::values.modern_seat_profiles.GetValue();
    std::vector<std::string> seats;
    std::stringstream entries{stored};
    for (std::string entry; std::getline(entries, entry, ',');) {
        seats.push_back(entry);
    }
    std::optional<InputProfiles> input_profiles;
    for (size_t i = 0; i < seats.size() && i < 8; ++i) {
        if (seats[i].empty()) {
            continue;
        }
        if (!input_profiles) {
            input_profiles.emplace();
        }
        const auto read = input_profiles->PeekProfile(seats[i], i);
        if (!read) {
            LOG_INFO(Frontend, "console-mode: P{} seat profile \"{}\" is gone", i + 1, seats[i]);
            SetSeatProfile(static_cast<int>(i), {});
            continue;
        }
        ApplyProfileMappings((*session_sets)[i], *read);
        (*session_sets)[i].profile_name = seats[i];
        LOG_INFO(Frontend, "console-mode: P{} starts with its seat profile \"{}\"", i + 1,
                 seats[i]);
    }
}

void MainWindow::SetSeatProfile(int player, const std::string& name) {
    if (player < 0 || player >= 8) {
        return;
    }
    std::vector<std::string> seats;
    std::stringstream entries{Settings::values.modern_seat_profiles.GetValue()};
    for (std::string entry; std::getline(entries, entry, ',');) {
        seats.push_back(entry);
    }
    seats.resize(8);
    seats[static_cast<size_t>(player)] = name;
    std::string joined;
    for (size_t i = 0; i < seats.size(); ++i) {
        joined += (i ? "," : "") + seats[i];
    }
    // (Nothing stored when no seat has a profile)
    if (joined.find_first_not_of(',') == std::string::npos) {
        joined.clear();
    }
    Settings::values.modern_seat_profiles.SetValue(joined);
}

void MainWindow::MergeConsoleProfileEdits(const std::array<Settings::PlayerInput, 10>& before) {
    if (!console_profile) {
        return;
    }
    // The Controls tab edits the global copy, whatever copy a running game uses
    const auto& after = Settings::values.players.GetValue(true);
    // The handheld's own non-SDL motion (a DSU server) must survive edits on players it doesn't
    // hold: kept here and put back at the end
    const auto handheld_motions_kept = SessionSet(0).motions;
    const auto own_slot = pad_slots.find(builtin_pad_id);
    // (Only a handheld without a player can't have edited its set)
    const bool handheld_has_player = builtin_pad_id && own_slot != pad_slots.end();
    if (Common::ControllerProbes()) {
        LOG_INFO(Frontend,
                 "console-mode: merge start, profile P1 motions {} before {} after {} "
                 "handheld_has_player={}",
                 MotionTrace((*console_profile)[0]), MotionTrace(before[0]), MotionTrace(after[0]),
                 handheld_has_player);
    }
    // The SDL device (guid, port) a player's bindings point at
    const auto sdl_device = [](const Settings::PlayerInput& player) {
        for (const std::string& raw : player.buttons) {
            const Common::ParamPackage param{raw};
            if (!raw.empty() && param.Get("engine", "") == "sdl") {
                return std::optional<std::pair<std::string, int>>{
                    {param.Get("guid", ""), param.Get("port", 0)}};
            }
        }
        return std::optional<std::pair<std::string, int>>{};
    };
    for (size_t i = 0; i < after.size(); ++i) {
        const auto& a = after[i];
        const auto& b = before[i];
        // A device change is a controller swap (below), not an edit
        if (const auto device = sdl_device(a); device && device != sdl_device(b)) {
            continue;
        }
        // A single Joy-Con's mapping is arranged from the set, not the set itself (its player
        // tab is read-only meanwhile): nothing there is an edit
        if (i < joycon_auto_set.size() && joycon_auto_set[i] &&
            (*joycon_auto_set[i] == Settings::ControllerType::RightJoycon ||
             *joycon_auto_set[i] == Settings::ControllerType::LeftJoycon)) {
            continue;
        }
        // Only mapping changes count as edits (connection and routing are the engine's). The
        // handheld's own pad's edits go to player one's set.
        const auto own = pad_slots.find(builtin_pad_id);
        size_t target = static_cast<size_t>(slot_set[i]);
        // For this session only: the user's own setup is the manual modes'
        auto& p = SessionSet(static_cast<int>(target));
        bool remapped = MergeMappings(p.buttons, b.buttons, a.buttons);
        remapped = MergeMappings(p.analogs, b.analogs, a.analogs) || remapped;
        bool edited = remapped;
        // An external pad never gets the handheld's non-SDL motion: its absence on another
        // player is the engine's doing, not an edit, and must not wipe it from the set
        auto edited_motions = a.motions;
        if (target != 0 ||
            !(builtin_pad_id && own != pad_slots.end() && own->second == static_cast<int>(i))) {
            const auto& handheld_motions = SessionSet(0).motions;
            for (size_t k = 0; k < edited_motions.size(); ++k) {
                const std::string& kept = p.motions[k];
                const bool handheld_motion =
                    !kept.empty() && Common::ParamPackage{kept}.Get("engine", "") != "sdl" &&
                    std::find(handheld_motions.begin(), handheld_motions.end(), kept) !=
                        handheld_motions.end();
                if (handheld_motion && edited_motions[k].empty()) {
                    edited_motions[k] = b.motions[k];
                }
            }
        }
        edited = MergeMappings(p.motions, b.motions, edited_motions) || edited;
        // Remapped and not saved: the controls are no longer the profile's
        if (remapped && !p.profile_name.empty()) {
            InputProfiles input_profiles;
            const auto saved = input_profiles.PeekProfile(p.profile_name, i);
            bool same = saved.has_value();
            for (size_t k = 0; same && k < p.buttons.size(); ++k) {
                same = BindingMapping(p.buttons[k]) == BindingMapping(saved->buttons[k]);
            }
            for (size_t k = 0; same && k < p.analogs.size(); ++k) {
                same = BindingMapping(p.analogs[k]) == BindingMapping(saved->analogs[k]);
            }
            if (!same) {
                LOG_INFO(Frontend, "console-mode: P{} remapped: profile \"{}\" let go", i + 1,
                         p.profile_name);
                p.profile_name.clear();
                SetSeatProfile(static_cast<int>(i), {});
            }
        }
        if (!edited) {
            continue;
        }
        set_default_of[target] = 0; // (the user's own mapping now)
        LOG_INFO(Frontend, "console-mode: Controls edit on P{} kept for the session in set P{}",
                 i + 1, target + 1);
    }

    if (!handheld_has_player) {
        auto& motions = SessionSet(0).motions;
        for (size_t k = 0; k < motions.size(); ++k) {
            const std::string& kept = handheld_motions_kept[k];
            if (motions[k].empty() && !kept.empty() &&
                Common::ParamPackage{kept}.Get("engine", "") != "sdl") {
                motions[k] = kept;
                LOG_INFO(Frontend, "console-mode: kept the handheld's motion {} on P1", kept);
            }
        }
    }
    if (Common::ControllerProbes()) {
        LOG_INFO(Frontend, "console-mode: merge end, profile P1 motions {}",
                 MotionTrace((*console_profile)[0]));
    }

    // A swap from the Controls tab: a player whose device was changed to another player's pad
    // trades slots with it; the routing pass re-points them next tick
    if (!input_subsystem) {
        return;
    }
    // The pads and their (guid, port); listed first, as GetSDLBinding can't run under the lock
    std::vector<u32> instances;
    {
        const JoystickApiLock lock;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int k = 0; k < count; ++k) {
            instances.push_back(ids[k]);
        }
        SDL_free(ids);
    }
    std::map<u32, std::pair<std::string, int>> bound;
    for (const u32 inst : instances) {
        if (const auto binding = input_subsystem->GetSDLBinding(inst)) {
            bound[inst] = *binding;
        }
    }
    // (The eight players only)
    for (size_t i = 0; i < after.size() && i < static_cast<size_t>(ConsolePlayers); ++i) {
        const auto chosen = sdl_device(after[i]);
        if (!chosen || chosen == sdl_device(before[i])) {
            continue;
        }
        u32 pad = 0;
        for (const auto& [inst, binding] : bound) {
            if (binding == *chosen) {
                pad = inst;
                break;
            }
        }
        if (!pad) {
            continue;
        }
        if (pad == builtin_pad_id && Settings::values.controller_applet_mode.GetValue() ==
                                         Settings::AppletMode::HandheldConsoleMode) {
            LOG_INFO(Frontend, "console-mode: Controls swap to P{} ignored: the handheld's own pad",
                     i + 1);
            continue;
        }
        const auto pad_slot = pad_slots.find(pad);
        if (pad_slot != pad_slots.end() && pad_slot->second == static_cast<int>(i)) {
            continue;
        }
        u32 holder = 0;
        for (const auto& [inst, slot] : pad_slots) {
            if (slot == static_cast<int>(i) && bound.contains(inst)) {
                holder = inst;
                break;
            }
        }
        const int from = pad_slot != pad_slots.end() ? pad_slot->second : -1;
        if (holder) {
            if (from >= 0) {
                pad_slots[holder] = from;
            } else {
                pad_slots.erase(holder);
                pad_identity.erase(holder);
            }
        }
        pad_slots[pad] = static_cast<int>(i);
        LOG_INFO(Frontend, "console-mode: Controls swap: inst={} P{} -> P{}{}", pad,
                 from >= 0 ? from + 1 : 0, i + 1,
                 holder
                     ? std::format(", inst={} P{} -> P{}", holder, i + 1, from >= 0 ? from + 1 : 0)
                     : std::string{});
    }
}

namespace {
/// The green/red flashes blink four times a second for their 1.5 seconds
bool BlinkOn(qint64 elapsed_ms) {
    return elapsed_ms < 1500 && (elapsed_ms / 125) % 2 == 0;
}
} // Anonymous namespace

void MainWindow::RenderControllerOverlay() {
    // A row lights while its controller has an input held, or a moment after a tap. Never for
    // motion: a resting gyro is never still.
    const auto now = std::chrono::steady_clock::now();
    std::set<u32> active;
    for (const auto& row : overlay_rows) {
        if (row.pad == 0) {
            continue;
        }
        const auto last =
            input_subsystem ? input_subsystem->GetSDLLastActivity(row.pad) : std::nullopt;
        if (PadInputHeld(row.pad) || (last && now - *last < std::chrono::milliseconds{250})) {
            active.insert(row.pad);
        }
    }
    overlay_active_pads = active;
    {
        auto& state = Common::GetControllerConsoleState();
        std::scoped_lock lock{state.mutex};
        for (auto& player : state.players) {
            const auto row = std::find_if(overlay_rows.begin(), overlay_rows.end(),
                                          [&](const auto& r) { return r.player == player.player; });
            player.active = row != overlay_rows.end() && active.contains(row->pad);
        }
    }

    const auto mode = Settings::values.controller_applet_mode.GetValue();
    const bool automatic = mode == Settings::AppletMode::ConsoleMode ||
                           mode == Settings::AppletMode::HandheldConsoleMode;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const auto publish = [](Common::OverlayPanel& panel, const QImage* image) {
        GameOverlay::Publish(panel, image);
    };

    const auto player_color = [](int player) {
        return ConfigurePlayerAssignment::PlayerColor(player);
    };
    const QFont badge_font = GameOverlay::Style::LabelFont();

    // Hints: a controller that took or left a player, named for a few seconds at the bottom left
    constexpr qint64 hint_ms = 3000;
    for (const auto& row : overlay_rows) {
        const int i = row.player;
        if (row.flash == 1 && row.pad != 0 && row.flash_since > hinted_join[i]) {
            hinted_join[i] = row.flash_since;
            controller_hints.push_back({overlay_row_name[i], i, true, row.flash_since});
            if (Common::ControllerProbes()) {
                LOG_INFO(Frontend, "console-mode: hint \"{} joined\" P{}",
                         overlay_row_name[i].toStdString(), i + 1);
            }
        } else if (row.flash == 2 && row.flash_since > hinted_leave[i]) {
            hinted_leave[i] = row.flash_since;
            // (Removed on the tick it left)
            const bool turned_off = std::abs(row.flash_since - turned_off_at[i]) < 1000;
            // (Named after the controller that left: another may have the player already)
            const QString& left_name =
                overlay_left_name[i].isEmpty() ? overlay_row_name[i] : overlay_left_name[i];
            controller_hints.push_back({left_name, i, false, row.flash_since, turned_off});
            if (Common::ControllerProbes()) {
                LOG_INFO(Frontend, "console-mode: hint \"{} {}\" P{}", left_name.toStdString(),
                         turned_off ? "turned off" : "left", i + 1);
            }
        }
    }
    // (A pause's notice stays while the game is paused for it, and shows even with the hints off)
    const bool hints_on = Settings::values.console_hints.GetValue();
    std::erase_if(controller_hints, [&](const auto& hint) {
        if (hint.pause_notice && !disconnect_paused.empty()) {
            return false;
        }
        return now_ms - hint.at >= hint_ms || (!hints_on && !hint.pause_notice);
    });
    if (!automatic || controller_hints.empty()) {
        publish(Common::GetOverlayPanel(Common::OverlayPanelId::Hints), nullptr);
    } else {
        QFont hint_font;
        hint_font.setPixelSize(15);
        hint_font.setBold(true);
        const QFontMetrics hint_fm{hint_font};
        constexpr int hint_h = 30;
        constexpr int hint_gap = 6;
        constexpr int badge_w = 36;
        const auto text_of = [](const ControllerHint& hint) {
            if (!hint.note.isEmpty()) {
                return hint.note;
            }
            if (hint.joined) {
                return tr("%1 joined").arg(hint.name);
            }
            return hint.turned_off ? tr("%1 turned off").arg(hint.name)
                                   : tr("%1 left").arg(hint.name);
        };
        int width = 0;
        for (const auto& hint : controller_hints) {
            width =
                (std::max)(width, 14 + hint_fm.horizontalAdvance(text_of(hint)) + 10 + badge_w + 6);
        }
        // The widest a hint can be, so the image keeps its size while hints come and go
        width = (std::max)(width, 260) + 2;
        const int count = static_cast<int>(controller_hints.size());
        QImage image{width, count * (hint_h + hint_gap), QImage::Format_RGBA8888};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        // Newest at the bottom, nearest the corner
        int y = 0;
        for (const auto& hint : controller_hints) {
            const QString text = text_of(hint);
            const int w = 14 + hint_fm.horizontalAdvance(text) + 10 + badge_w + 6;
            // (A pause's notice doesn't fade while the game is paused for it)
            const qint64 left = hint.pause_notice && !disconnect_paused.empty()
                                    ? hint_ms
                                    : hint_ms - (now_ms - hint.at);
            const int alpha = left < 500 ? static_cast<int>(255 * (std::max)(left, qint64{0}) / 500)
                                         : 255;
            painter.setPen(Qt::NoPen);
            painter.setBrush(hint.joined ? QColor(18, 18, 22, 215 * alpha / 255)
                                         : QColor(96, 22, 28, 220 * alpha / 255));
            painter.drawRoundedRect(QRectF(1, y, w, hint_h), hint_h / 2.0, hint_h / 2.0);
            painter.setFont(hint_font);
            painter.setPen(QColor(255, 255, 255, alpha));
            painter.drawText(QRect(15, y, w, hint_h), Qt::AlignVCenter | Qt::AlignLeft, text);
            QColor badge = player_color(hint.player);
            badge.setAlpha(hint.joined ? alpha : alpha / 2);
            painter.setPen(Qt::NoPen);
            painter.setBrush(badge);
            const QRectF badge_rect(1 + w - 6 - badge_w, y + 5, badge_w, hint_h - 10);
            painter.drawRoundedRect(badge_rect, badge_rect.height() / 2, badge_rect.height() / 2);
            painter.setFont(badge_font);
            painter.setPen(QColor(255, 255, 255, alpha));
            painter.drawText(badge_rect, Qt::AlignCenter,
                             QStringLiteral("P%1").arg(hint.player + 1));
            y += hint_h + hint_gap;
        }
        painter.end();
        publish(Common::GetOverlayPanel(Common::OverlayPanelId::Hints), &image);
    }

    overlay_blink.clear();
    for (const auto& row : overlay_rows) {
        overlay_blink.push_back(row.flash != 0 && BlinkOn(now_ms - row.flash_since));
    }
    if (!Settings::values.console_debug_overlay.GetValue() || !automatic || overlay_rows.empty()) {
        publish(Common::GetOverlayPanel(Common::OverlayPanelId::Players), nullptr);
        return;
    }

    // The Switch buttons as chips: a label and a colour (stick moves: the stick and an arrow)
    const auto chip = [](int button) -> std::pair<QString, QColor> {
        using namespace Settings::NativeButton;
        if (button >= StickMoveButton) {
            static constexpr std::array<const char16_t*, 4> arrows{u"\u2190", u"\u2191", u"\u2192",
                                                                   u"\u2193"};
            const int stick = (button - StickMoveButton) / 4;
            const int direction = (button - StickMoveButton) % 4;
            return {(stick == 0 ? QStringLiteral("LS") : QStringLiteral("RS")) +
                        QString::fromUtf16(arrows[static_cast<std::size_t>(direction)]),
                    QColor(52, 73, 94)};
        }
        switch (button) {
        case A:
            return {QStringLiteral("A"), QColor(231, 76, 60)};
        case B:
            return {QStringLiteral("B"), QColor(230, 180, 20)};
        case X:
            return {QStringLiteral("X"), QColor(52, 152, 219)};
        case Y:
            return {QStringLiteral("Y"), QColor(46, 184, 104)};
        case LStick:
            return {QStringLiteral("L3"), QColor(70, 90, 112)};
        case RStick:
            return {QStringLiteral("R3"), QColor(70, 90, 112)};
        case L:
            return {QStringLiteral("L"), QColor(120, 132, 140)};
        case R:
            return {QStringLiteral("R"), QColor(120, 132, 140)};
        case ZL:
            return {QStringLiteral("ZL"), QColor(98, 108, 116)};
        case ZR:
            return {QStringLiteral("ZR"), QColor(98, 108, 116)};
        case Plus:
            return {QStringLiteral("+"), QColor(142, 88, 182)};
        case Minus:
            return {QStringLiteral("−"), QColor(142, 88, 182)};
        case DLeft:
            return {QStringLiteral("←"), QColor(84, 99, 116)};
        case DUp:
            return {QStringLiteral("↑"), QColor(84, 99, 116)};
        case DRight:
            return {QStringLiteral("→"), QColor(84, 99, 116)};
        case DDown:
            return {QStringLiteral("↓"), QColor(84, 99, 116)};
        case Home:
            return {QStringLiteral("⌂"), QColor(60, 60, 66)};
        case Screenshot:
            return {QStringLiteral("◉"), QColor(60, 60, 66)};
        case SLLeft:
        case SLRight:
            return {QStringLiteral("SL"), QColor(120, 132, 140)};
        default:
            return {QStringLiteral("SR"), QColor(120, 132, 140)};
        }
    };
    const QFont name_font = GameOverlay::Style::TextFont();
    const QFont chip_font = GameOverlay::Style::SmallFont();
    const QFontMetrics name_fm{name_font};
    const QFontMetrics chip_fm{chip_font};

    constexpr int row_h = 28;
    constexpr int row_gap = 4;
    constexpr int badge_w = 30;
    constexpr int inner = 6;
    constexpr int bubble_gap = 6;
    constexpr int chip_h = 18;
    constexpr int chip_gap = 3;
    const auto chip_width = [&](const QString& text) {
        return (std::max)(chip_h, chip_fm.horizontalAdvance(text) + 10);
    };
    // The widest chip there can be (a stick move, "LS→", is wider than a button)
    int widest_chip = 26;
    for (int k = 0; k < 8; ++k) {
        widest_chip = (std::max)(widest_chip, chip_width(chip(StickMoveButton + k).first));
    }
    // An entry lives as long as set, then shrinks away
    const bool show_history = Settings::values.console_history.GetValue();
    const bool show_gaps = Settings::values.console_history_gaps.GetValue();
    const int history_size = Settings::values.console_history_size.GetValue();
    const qint64 press_life_ms = Settings::values.console_history_seconds.GetValue() * 1000LL;
    constexpr qint64 press_shrink_ms = 400;
    // The time between inputs: a tag between their chips, in ms, or seconds from one ("1.2")
    QFont gap_font;
    gap_font.setPixelSize(8);
    gap_font.setBold(true);
    const QFontMetrics gap_fm{gap_font};
    const auto gap_text = [press_life_ms](qint64 ms) {
        ms = (std::min)(ms, press_life_ms);
        return ms < 1000 ? QString::number(ms)
                         : QStringLiteral("%1s").arg(static_cast<double>(ms) / 1000.0, 0, 'f',
                                                     ms < 10000 ? 1 : 0);
    };
    constexpr int gap_pad = 3;
    const int widest_gap = gap_fm.horizontalAdvance(QStringLiteral("999")) + gap_pad * 2;
    const auto press_scale = [now_ms, press_life_ms](const ButtonPress& press) {
        const qint64 over = now_ms - press.at - press_life_ms;
        if (over <= 0) {
            return 1.0;
        }
        return (std::max)(0.0, 1.0 - static_cast<double>(over) / press_shrink_ms);
    };

    int name_w = 0;
    for (const auto& row : overlay_rows) {
        name_w = (std::max)(name_w, name_fm.horizontalAdvance(overlay_row_name[row.player]));
    }
    // (A name being typed has room for its 16 characters)
    if (overlay_edit_player >= 0) {
        name_w = (std::max)(name_w, name_fm.horizontalAdvance(QString(16, QLatin1Char('M'))));
    }
    // Room for a battery after the name, when any controller tells its charge
    const bool batteries = std::any_of(overlay_rows.begin(), overlay_rows.end(),
                                       [](const auto& row) { return row.battery >= 0; });
    const int battery_w =
        batteries ? 8 + chip_fm.horizontalAdvance(QStringLiteral("100%")) + 5 + 20 : 0;
    const int controller_w = inner + badge_w + 8 + name_w + battery_w + inner + 4;
    // Room for as many of the widest chips as the history keeps, so the image keeps its size
    int history_room = show_history ? inner * 2 + history_size * (widest_chip + chip_gap) +
                                          (show_gaps ? (history_size - 1) * widest_gap : 0)
                                    : 200;
    // Short of the panels at the top right: the oldest inputs drop off sooner
    if (render_window) {
        const auto& layout = render_window->GetFramebufferLayout();
        const int room = static_cast<int>(Common::OverlayRoomTopLeft(
                             static_cast<float>(layout.screen.right - layout.screen.left))) -
                         (controller_w + bubble_gap + 2);
        history_room = (std::clamp)(room, inner * 2 + widest_chip, history_room);
    }
    const int width = controller_w + bubble_gap + history_room + 2;
    const int count = static_cast<int>(overlay_rows.size());
    const int height = count * row_h + (count - 1) * row_gap + 2;

    QImage image{width, height, QImage::Format_RGBA8888};
    image.fill(Qt::transparent);
    QPainter painter{&image};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::NoPen);
    // The controller bubble: its state lights it (blue held, green joined, red left)
    const auto controller_bubble = [&](const OverlayRow& row, bool blink, int at_y) {
        const bool gone = row.pad == 0;
        QColor fill = GameOverlay::Style::Fill;
        if (row.flash == 2) {
            if (blink) {
                fill = GameOverlay::Style::Bad;
            }
        } else if (!gone && active.contains(row.pad)) {
            fill = GameOverlay::Style::Active;
        } else if (blink) {
            fill = GameOverlay::Style::Good;
        }
        GameOverlay::Style::Pill(painter, QRectF(1, at_y, controller_w, row_h), fill);
        QColor badge = player_color(row.player);
        if (gone) {
            badge.setAlpha(110);
        }
        painter.setBrush(badge);
        const QRectF badge_rect(1 + inner, at_y + 5, badge_w, row_h - 10);
        painter.drawRoundedRect(badge_rect, badge_rect.height() / 2, badge_rect.height() / 2);
        painter.setFont(badge_font);
        painter.setPen(Qt::white);
        painter.drawText(badge_rect, Qt::AlignCenter, QStringLiteral("P%1").arg(row.player + 1));
        // On the badge's corner: ? for a reconnect's guess, else the starting player's colour
        if (!gone && (row.guessed || row.origin >= 0)) {
            const QPointF corner(badge_rect.right() - 1, badge_rect.top() + 1);
            painter.setPen(QPen(QColor(14, 14, 18), 1.5));
            if (row.guessed) {
                painter.setBrush(Qt::white);
                painter.drawEllipse(corner, 6, 6);
                QFont mark_font = badge_font;
                mark_font.setPixelSize(9);
                painter.setFont(mark_font);
                painter.setPen(QColor(14, 14, 18));
                painter.drawText(QRectF(corner.x() - 6, corner.y() - 6, 12, 12), Qt::AlignCenter,
                                 QStringLiteral("?"));
            } else {
                painter.setBrush(player_color(row.origin));
                painter.drawEllipse(corner, 4.5, 4.5);
            }
            painter.setPen(Qt::NoPen);
        }
        painter.setFont(name_font);
        if (row.player == overlay_edit_player && !gone) {
            // The name being typed: a field with its caret (blinking), or what to type
            const QRectF field(1 + inner + badge_w + 4, at_y + 4, name_w + 10, row_h - 8);
            painter.setPen(QPen(QColor(120, 170, 255, 230), 1.2));
            painter.setBrush(QColor(255, 255, 255, 30));
            painter.drawRoundedRect(field, 5, 5);
            const QRectF text_rect = field.adjusted(5, 0, -5, 0);
            painter.setPen(overlay_edit_text.isEmpty() ? QColor(255, 255, 255, 110)
                                                       : QColor(245, 245, 245));
            painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                             overlay_edit_text.isEmpty() ? tr("Name") : overlay_edit_text);
            if ((QDateTime::currentMSecsSinceEpoch() / 500) % 2 == 0) {
                const double caret = text_rect.left() +
                                     (overlay_edit_text.isEmpty()
                                          ? 0
                                          : name_fm.horizontalAdvance(overlay_edit_text)) +
                                     1;
                painter.setPen(QPen(Qt::white, 1.2));
                painter.drawLine(QPointF(caret, field.top() + 4), QPointF(caret, field.bottom() - 4));
            }
            painter.setPen(Qt::NoPen);
            return;
        }
        painter.setPen(gone ? GameOverlay::Style::DimText : GameOverlay::Style::Text);
        painter.drawText(QRect(1 + inner + badge_w + 8, at_y, name_w + 4, row_h),
                         Qt::AlignVCenter | Qt::AlignLeft, overlay_row_name[row.player]);
        // The battery's percent, then its icon (the same as the tabs' and the menu bar's)
        if (!gone && row.battery >= 0) {
            const int percent_w = chip_fm.horizontalAdvance(QStringLiteral("100%"));
            const double bx = 1 + inner + badge_w + 8 + name_w + 8 + percent_w + 5;
            painter.drawPixmap(QPointF(bx, at_y + row_h / 2.0 - 6),
                               ConfigurePlayerAssignment::Battery(row.battery, row.charging));
            painter.setFont(chip_font);
            painter.setPen(QColor(220, 220, 220));
            painter.drawText(QRectF(bx - 5 - percent_w, at_y, percent_w, row_h),
                             Qt::AlignVCenter | Qt::AlignRight,
                             QStringLiteral("%1%").arg(row.battery));
        }
        painter.setPen(Qt::NoPen);
    };
    // A name being typed: apply, remove and cancel, where the history goes
    const auto edit_buttons = [&](std::size_t r, int at_y) {
        const auto& row = overlay_rows[r];
        constexpr int round_w = 24;
        const bool named = !overlay_custom_name[static_cast<std::size_t>(row.player)].empty();
        const int buttons = named ? 3 : 2;
        const double x0 = 1 + controller_w + bubble_gap;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(14, 14, 18, 200));
        painter.drawRoundedRect(QRectF(x0, at_y, 8 + buttons * (round_w + chip_gap) - chip_gap,
                                       row_h),
                                row_h / 2.0, row_h / 2.0);
        double x = x0 + 4;
        const double h = row_h - 6;
        const auto round = [&](int action, const QColor& fill) {
            const QRectF rect(x, at_y + 3, round_w, h);
            overlay_buttons.push_back({rect.toAlignedRect(), row.player, action, -1});
            x += round_w + chip_gap;
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawEllipse(rect.center(), h / 2.0, h / 2.0);
            painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            return rect.center();
        };
        // Apply: a check
        {
            const QPointF c = round(4, QColor(45, 160, 80, 230));
            const QPointF check[3] = {QPointF(c.x() - 4.5, c.y()), QPointF(c.x() - 1.5, c.y() + 3),
                                      QPointF(c.x() + 4.5, c.y() - 3.5)};
            painter.drawPolyline(check, 3);
        }
        // Remove the name: a bin
        if (named) {
            const QPointF c = round(5, QColor(200, 50, 50, 230));
            painter.drawLine(QPointF(c.x() - 4.5, c.y() - 3.5), QPointF(c.x() + 4.5, c.y() - 3.5));
            painter.drawLine(QPointF(c.x() - 1.5, c.y() - 5), QPointF(c.x() + 1.5, c.y() - 5));
            painter.drawRoundedRect(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 8.5), 1, 1);
        }
        // Cancel: a cross
        {
            const QPointF c = round(6, QColor(255, 255, 255, 40));
            painter.drawLine(QPointF(c.x() - 3.5, c.y() - 3.5), QPointF(c.x() + 3.5, c.y() + 3.5));
            painter.drawLine(QPointF(c.x() + 3.5, c.y() - 3.5), QPointF(c.x() - 3.5, c.y() + 3.5));
        }
        painter.setPen(Qt::NoPen);
    };
    // The row under the mouse shows its buttons where its history goes
    overlay_buttons.clear();
    const auto neighbour = [this](std::size_t r, int step) {
        for (int k = static_cast<int>(r) + step;
             k >= 0 && k < static_cast<int>(overlay_rows.size()); k += step) {
            if (overlay_rows[static_cast<std::size_t>(k)].pad != 0) {
                return overlay_rows[static_cast<std::size_t>(k)].player;
            }
        }
        return -1;
    };
    const auto row_buttons = [&](std::size_t r, int at_y) {
        const auto& row = overlay_rows[r];
        const auto address = pad_bluetooth.find(BluetoothKey(row.pad));
        const bool turns_off = address != pad_bluetooth.end() && !address->second.empty();
        const QString label = tr("Turn Off");
        const QString config_label = tr("Config");
        constexpr int round_w = 24;
        const int remove_w = turns_off ? 22 + chip_fm.horizontalAdvance(label) + 10 : 0;
        const int config_w = chip_fm.horizontalAdvance(config_label) + 20;
        const int above = neighbour(r, -1);
        const int below = neighbour(r, 1);
        const int count_round = (above >= 0 ? 1 : 0) + (below >= 0 ? 1 : 0);
        const double x0 = 1 + controller_w + bubble_gap;
        const double pill_w = 4 + (count_round + 1) * (round_w + chip_gap) + config_w +
                              (turns_off ? chip_gap + remove_w : 0) + 4;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(14, 14, 18, 200));
        painter.drawRoundedRect(QRectF(x0, at_y, pill_w, row_h), row_h / 2.0, row_h / 2.0);
        double x = x0 + 4;
        const double top = at_y + 3;
        const double h = row_h - 6;
        const auto add = [&](double w, int action, int target) {
            const QRectF rect(x, top, w, h);
            overlay_buttons.push_back({rect.toAlignedRect(), row.player, action, target});
            x += w + chip_gap;
            return rect;
        };
        const auto arrow = [&](int step, int target) {
            const QRectF rect = add(round_w, step, target);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 40));
            painter.drawEllipse(rect.center(), h / 2.0, h / 2.0);
            const QPointF c = rect.center();
            const double d = step < 0 ? -1.0 : 1.0;
            const QPointF tri[3] = {QPointF(c.x() - 5, c.y() - 2.5 * d),
                                    QPointF(c.x() + 5, c.y() - 2.5 * d),
                                    QPointF(c.x(), c.y() + 3.5 * d)};
            painter.setBrush(Qt::white);
            painter.drawPolygon(tri, 3);
        };
        if (above >= 0) {
            arrow(-1, above);
        }
        if (below >= 0) {
            arrow(1, below);
        }
        // Rename: a pencil, opening a small dialog for the name
        {
            const QRectF rect = add(round_w, 3, -1);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 40));
            painter.drawEllipse(rect.center(), h / 2.0, h / 2.0);
            const QPointF c = rect.center();
            painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(c.x() - 4, c.y() + 4), QPointF(c.x() + 3, c.y() - 3));
            painter.drawLine(QPointF(c.x() + 1.5, c.y() - 4.5), QPointF(c.x() + 4.5, c.y() - 1.5));
            painter.setPen(Qt::NoPen);
        }
        // Config: the player's tab in the controls settings
        {
            const QRectF config_rect = add(config_w, 2, -1);
            painter.setPen(QPen(QColor(120, 170, 255, 230), 1.5));
            painter.setBrush(QColor(45, 95, 200, 120));
            painter.drawRoundedRect(config_rect.adjusted(0.75, 0.75, -0.75, -0.75), h / 2.0,
                                    h / 2.0);
            painter.setFont(chip_font);
            painter.setPen(Qt::white);
            painter.drawText(config_rect, Qt::AlignCenter, config_label);
            painter.setPen(Qt::NoPen);
        }
        if (!turns_off) {
            return true;
        }
        const QRectF rect = add(remove_w, 0, -1);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 50, 50, 230));
        painter.drawRoundedRect(rect, h / 2.0, h / 2.0);
        // A power symbol
        const QPointF c(rect.left() + 12, rect.center().y());
        painter.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(c.x() - 5, c.y() - 4.5, 10, 10), 125 * 16, 290 * 16);
        painter.drawLine(QPointF(c.x(), c.y() - 6), QPointF(c.x(), c.y()));
        painter.setFont(chip_font);
        painter.setPen(Qt::white);
        painter.drawText(rect.adjusted(22, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
        painter.setPen(Qt::NoPen);
        return true;
    };
    // Where each row is, for the mouse (a row drags onto another to swap them)
    overlay_hits.clear();
    int drag_row = -1;
    int drag_y = 0;
    int y = 1;
    for (std::size_t r = 0; r < overlay_rows.size(); ++r) {
        const auto& row = overlay_rows[r];
        const bool blink = overlay_blink[r];
        overlay_hits.emplace_back(row.player, QRect(1, y, width - 2, row_h));
        if (overlay_dragging && row.player == overlay_drag_player) {
            // Where the dragged row came from: an empty slot (the row follows the mouse)
            QColor edge = player_color(row.player);
            edge.setAlpha(150);
            painter.setPen(QPen(edge, 1.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(1.5, y + 0.5, controller_w - 1, row_h - 1), row_h / 2.0,
                                    row_h / 2.0);
            painter.setPen(Qt::NoPen);
            drag_row = static_cast<int>(r);
            drag_y = y;
        } else {
            controller_bubble(row, blink, y);
        }
        if (row.player == overlay_edit_player && row.pad != 0) {
            edit_buttons(r, y);
            y += row_h + row_gap;
            continue;
        }
        if (row.player == overlay_hover_player && row.pad != 0 && !overlay_dragging &&
            row_buttons(r, y)) {
            y += row_h + row_gap;
            continue;
        }

        // The history bubble: the last inputs, newest first, shrinking away as they expire.
        // Buttons pressed together share a capsule, joined by "+".
        static const std::deque<ButtonPress> none;
        const auto found = press_history.find(overlay_row_history[row.player]);
        const auto& presses = show_history && found != press_history.end() ? found->second : none;
        struct Input {
            std::size_t first;
            std::size_t count;
            double scale;
            double width;
            double gap_w; ///< the time to the newer input, drawn before this one (0: none)
        };
        constexpr double plus_w = 10;
        constexpr double capsule_pad = 3;
        std::vector<Input> inputs; // newest first
        double chips_w = 0;
        for (std::size_t end = presses.size(); end > 0;) {
            std::size_t first = end - 1;
            while (first > 0 && presses[first - 1].at == presses[end - 1].at) {
                --first;
            }
            const double scale = press_scale(presses[first]);
            double input_w = 0;
            for (std::size_t k = first; k < end; ++k) {
                input_w += chip_width(chip(presses[k].button).first);
            }
            if (end - first > 1) {
                input_w += capsule_pad * 2 + plus_w * static_cast<double>(end - first - 1);
            }
            input_w *= scale;
            if (scale > 0.0) {
                // The time from this input to the newer one before it
                double gap_w = 0;
                if (show_gaps && !inputs.empty()) {
                    gap_w = gap_fm.horizontalAdvance(
                                gap_text(presses[inputs.back().first].at - presses[first].at)) +
                            gap_pad * 2;
                }
                if (chips_w + gap_w + input_w > history_room - inner * 2) {
                    break;
                }
                inputs.push_back({first, end - first, scale, input_w, gap_w});
                chips_w += gap_w + input_w + chip_gap * scale;
            }
            end = first;
        }
        if (chips_w > 0.5) {
            const double bubble_x = 1 + controller_w + bubble_gap;
            const double bubble_w = inner * 2 + chips_w - chip_gap;
            GameOverlay::Style::Pill(
                painter, QRectF(bubble_x, y, (std::max)(bubble_w, double{row_h}), row_h),
                GameOverlay::Style::Fill);
            double cx = bubble_x + inner;
            for (std::size_t g = 0; g < inputs.size(); ++g) {
                const auto& input = inputs[g];
                const double scale = input.scale;
                // (Older inputs fade a step each; past twenty they're transparent)
                const int alpha =
                    (std::clamp)(static_cast<int>((255 - static_cast<int>(g) * 12) * scale), 0, 255);
                const double h = chip_h * scale;
                const bool together = input.count > 1;
                if (input.gap_w > 0) {
                    // A tag sitting in the gap, overlapping the space around it
                    const QRectF tag(cx - chip_gap * scale, y + (row_h - 12) / 2.0, input.gap_w,
                                     12);
                    painter.setBrush(QColor(255, 255, 255, 22 * alpha / 255));
                    painter.drawRoundedRect(tag, 6, 6);
                    painter.setFont(gap_font);
                    painter.setPen(QColor(190, 195, 202, alpha));
                    painter.drawText(
                        tag, Qt::AlignCenter,
                        gap_text(presses[inputs[g - 1].first].at - presses[input.first].at));
                    painter.setPen(Qt::NoPen);
                    cx += input.gap_w - chip_gap * scale;
                }
                double x = cx;
                if (together) {
                    // The capsule: a light band with an outline around the buttons
                    const double ch = (chip_h + capsule_pad * 2) * scale;
                    const QRectF capsule(cx, y + (row_h - ch) / 2.0, input.width, ch);
                    painter.setBrush(QColor(255, 255, 255, 40 * alpha / 255));
                    painter.setPen(QPen(QColor(255, 255, 255, 120 * alpha / 255), 1));
                    painter.drawRoundedRect(capsule, ch / 2.0, ch / 2.0);
                    painter.setPen(Qt::NoPen);
                    x += capsule_pad * scale;
                }
                for (std::size_t k = input.first; k < input.first + input.count; ++k) {
                    const auto& [label, color] = chip(presses[k].button);
                    const double w = chip_width(label) * scale;
                    QColor chip_color = color;
                    chip_color.setAlpha(alpha);
                    const QRectF rect(x, y + (row_h - h) / 2.0, w, h);
                    painter.setBrush(chip_color);
                    painter.drawRoundedRect(rect, h / 2.0, h / 2.0);
                    if (scale > 0.6) {
                        painter.setFont(chip_font);
                        painter.setPen(QColor(255, 255, 255, alpha));
                        painter.drawText(rect, Qt::AlignCenter, label);
                        painter.setPen(Qt::NoPen);
                    }
                    x += w;
                    if (k + 1 < input.first + input.count) {
                        if (scale > 0.6) {
                            painter.setFont(chip_font);
                            painter.setPen(QColor(255, 255, 255, alpha));
                            painter.drawText(QRectF(x, y, plus_w * scale, row_h), Qt::AlignCenter,
                                             QStringLiteral("+"));
                            painter.setPen(Qt::NoPen);
                        }
                        x += plus_w * scale;
                    }
                }
                cx += input.width + chip_gap * scale;
            }
        }
        y += row_h + row_gap;
    }
    // The dragged row, last, over the others
    if (drag_row >= 0) {
        const int at_y = std::clamp(drag_y + overlay_drag_dy, 0, height - row_h - 1);
        controller_bubble(overlay_rows[static_cast<std::size_t>(drag_row)],
                          overlay_blink[static_cast<std::size_t>(drag_row)], at_y);
    }
    painter.end();
    publish(Common::GetOverlayPanel(Common::OverlayPanelId::Players), &image);
}

bool MainWindow::HandleOverlayMouse(QEvent* event) {
    const auto type = event->type();
    // A name being typed in the overlay takes the keys
    if (overlay_edit_player >= 0) {
        bool shown_now = false;
        {
            auto& frame = Common::GetOverlayPanel(Common::OverlayPanelId::Players);
            std::scoped_lock lock{frame.mutex};
            shown_now = frame.enabled;
        }
        const bool still_there =
            std::any_of(overlay_rows.begin(), overlay_rows.end(), [this](const auto& row) {
                return row.player == overlay_edit_player && row.pad != 0;
            });
        if (!shown_now || !still_there) {
            overlay_edit_player = -1;
        }
    }
    if (overlay_edit_player >= 0 &&
        (type == QEvent::KeyPress || type == QEvent::KeyRelease ||
         type == QEvent::ShortcutOverride)) {
        event->accept();
        if (type != QEvent::KeyPress) {
            return true;
        }
        const auto* key = static_cast<QKeyEvent*>(event);
        const int player = overlay_edit_player;
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
            key->key() == Qt::Key_Escape) {
            if (key->key() != Qt::Key_Escape) {
                auto& state = Common::GetControllerConsoleState();
                std::scoped_lock lock{state.mutex};
                state.rename_requests.emplace_back(player,
                                                   overlay_edit_text.trimmed().toStdString());
            }
            overlay_edit_player = -1;
        } else if (key->key() == Qt::Key_Backspace) {
            overlay_edit_text.chop(1);
        } else if (!key->text().isEmpty() && key->text().at(0).isPrint() &&
                   overlay_edit_text.size() < 16) {
            overlay_edit_text += key->text();
        }
        RenderControllerOverlay();
        return true;
    }
    if (type == QEvent::Leave && overlay_hover_player >= 0) {
        overlay_hover_player = -1;
        RenderControllerOverlay();
        return false;
    }
    // (With no button held, the pointer's moves come as hover moves)
    const bool hover_move = type == QEvent::HoverMove;
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove &&
        type != QEvent::MouseButtonRelease && !hover_move) {
        return false;
    }
    bool shown = false;
    {
        auto& frame = Common::GetOverlayPanel(Common::OverlayPanelId::Players);
        std::scoped_lock lock{frame.mutex};
        shown = frame.enabled;
    }
    if (!shown || overlay_hits.empty() || !render_window) {
        overlay_drag_player = -1;
        overlay_dragging = false;
        overlay_hover_player = -1;
        return false;
    }
    // The cursor in the overlay image
    const QPoint at =
        GameOverlay::CursorOn(Common::GetOverlayPanel(Common::OverlayPanelId::Players),
                              *render_window)
            .at;
    const auto row_at = [this](QPoint point) {
        for (const auto& [player, rect] : overlay_hits) {
            if (rect.contains(point)) {
                return player;
            }
        }
        return -1;
    };
    const auto* mouse = static_cast<QMouseEvent*>(event);
    // The row under the mouse shows its buttons (not while one is dragged)
    if ((type == QEvent::MouseMove || hover_move) && !overlay_dragging) {
        int hover = row_at(at);
        if (hover >= 0 &&
            std::none_of(overlay_rows.begin(), overlay_rows.end(),
                         [&](const auto& row) { return row.player == hover && row.pad != 0; })) {
            hover = -1;
        }
        if (hover != overlay_hover_player) {
            overlay_hover_player = hover;
            RenderControllerOverlay();
        }
    }
    if (hover_move) {
        return false; // (the rest is the press, drag and release's)
    }
    // A button acts on its press; its release is the overlay's too
    if (overlay_button_down && type == QEvent::MouseButtonRelease) {
        overlay_button_down = false;
        return true;
    }
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        const auto button = std::find_if(overlay_buttons.begin(), overlay_buttons.end(),
                                         [&](const auto& b) { return b.rect.contains(at); });
        if (button != overlay_buttons.end()) {
            overlay_button_down = true;
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            if (button->action == 3) {
                // Name it in place: typing goes to the field (the window takes the keys)
                overlay_edit_player = button->player;
                overlay_edit_text = QString::fromStdString(
                    overlay_custom_name[static_cast<std::size_t>(button->player)]);
                render_window->setFocus();
                QTimer::singleShot(0, this, [this] { RenderControllerOverlay(); });
            } else if (button->action >= 4 && button->action <= 6) {
                // Apply (4), remove the name (5), cancel (6)
                if (button->action != 6) {
                    state.rename_requests.emplace_back(
                        button->player, button->action == 4
                                            ? overlay_edit_text.trimmed().toStdString()
                                            : std::string{});
                }
                overlay_edit_player = -1;
                QTimer::singleShot(0, this, [this] { RenderControllerOverlay(); });
            } else if (button->action == 2) {
                // (After the event: the dialog runs its own loop)
                const int player = button->player;
                QTimer::singleShot(0, this, [this, player] {
                    open_player_tab = player;
                    OnConfigure();
                });
            } else if (button->action == 0) {
                LOG_INFO(Frontend, "console-mode: overlay turn off P{}", button->player + 1);
                state.remove_requests.push_back(button->player);
            } else {
                LOG_INFO(Frontend, "console-mode: overlay swap P{} <-> P{}", button->player + 1,
                         button->target + 1);
                state.swap_requests.emplace_back(button->player, button->target);
                // The buttons follow the controller to its new row
                overlay_hover_player = button->target;
            }
            return true;
        }
    }
    if (type == QEvent::MouseButtonPress) {
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const int player = row_at(at);
        if (player < 0) {
            return false; // the game's
        }
        overlay_drag_player = player;
        overlay_press = at;
        overlay_drag_dy = 0;
        overlay_dragging = false;
        return true;
    }
    if (overlay_drag_player < 0) {
        return false;
    }
    if (type == QEvent::MouseMove) {
        const int dy = at.y() - overlay_press.y();
        if (!overlay_dragging && std::abs(dy) < QApplication::startDragDistance()) {
            return true;
        }
        overlay_dragging = true;
        overlay_drag_dy = dy;
        RenderControllerOverlay();
        return true;
    }
    // Released: dropped on another row swaps the two players; a click rumbles the controller
    const int player = overlay_drag_player;
    const bool dragged = overlay_dragging;
    overlay_drag_player = -1;
    overlay_dragging = false;
    overlay_drag_dy = 0;
    if (dragged) {
        const int target = row_at(QPoint(10, at.y()));
        if (target >= 0 && target != player) {
            LOG_INFO(Frontend, "console-mode: overlay swap P{} <-> P{}", player + 1, target + 1);
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            state.swap_requests.emplace_back(player, target);
        }
        RenderControllerOverlay();
        return true;
    }
    if (Settings::values.vibration_enabled.GetValue() &&
        Settings::values.players.GetValue()[player].vibration_enabled) {
        auto& state = Common::GetControllerConsoleState();
        std::scoped_lock lock{state.mutex};
        state.rumble_requests.push_back(player);
    }
    return true;
}

bool MainWindow::OverlayAnimating() const {
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (!controller_hints.empty() || overlay_edit_player >= 0) {
        return true;
    }
    const qint64 life_ms = Settings::values.console_history_seconds.GetValue() * 1000LL;
    for (const auto& [ident, history] : press_history) {
        if (!history.empty() && now_ms - history.front().at > life_ms - 100) {
            return true;
        }
    }
    return false;
}

void MainWindow::LearnBluetoothPads() {
#ifdef __linux__
    // Pads with several controllers of their model: the one controller with a button held while
    // just one of those pads has one is that pad's
    if (pad_bt_pending.empty()) {
        return;
    }
    // The pads now, each with the controller behind it now (Steam may have just moved one)
    std::map<std::pair<unsigned, unsigned>, std::vector<std::pair<u32, std::string>>> by_model;
    {
        const JoystickApiLock lock;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i) {
            const u32 id = ids[i];
            // (Not a real device Steam hid: its virtual pad is the player)
            if (Common::SteamPadsInUse() && !IsSteamVirtualPad(id) &&
                Common::SteamHidModel(PadVendor(id),
                                      PadProduct(id))) {
                continue;
            }
            std::string key = BluetoothKey(id);
            if (pad_bt_pending.contains(key)) {
                by_model[{PadVendor(id), PadProduct(id)}]
                    .emplace_back(id, std::move(key));
            }
        }
        SDL_free(ids);
    }
    // Debug Probes: controllers waiting for a press whose pads can't be found (every 2 seconds)
    if (Common::ControllerProbes()) {
        std::size_t found = 0;
        for (const auto& [model, pads] : by_model) {
            found += pads.size();
        }
        static auto last_missing = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (found < pad_bt_pending.size() && now - last_missing > std::chrono::seconds{2}) {
            last_missing = now;
            std::string keys;
            for (const auto& key : pad_bt_pending) {
                keys += " " + key;
            }
            LOG_INFO(Frontend, "console-mode: waiting for a press:{}, {} of them on a pad", keys,
                     found);
        }
    }
    std::set<std::string> taken;
    for (const auto& [key, address] : pad_bluetooth) {
        if (!address.empty()) {
            taken.insert(address);
        }
    }
    for (const auto& [model, pads] : by_model) {
        std::vector<RealController> controllers = ControllersOfModel(model.first, model.second);
        std::erase_if(controllers, [&](const auto& c) { return taken.contains(c.address); });
        // The last of its model left to tell apart: that one is it
        if (pads.size() == 1 && controllers.size() == 1) {
            const auto& [id, key] = pads[0];
            pad_bluetooth[key] = controllers[0].bluetooth ? controllers[0].address : std::string{};
            pad_bt_pending.erase(key);
            if (Common::ControllerProbes()) {
                LOG_INFO(
                    Frontend, "console-mode: inst={} ({}) is the last of its model: bluetooth={}",
                    id, key, controllers[0].bluetooth ? controllers[0].address : std::string{"no"});
            }
            continue;
        }
        // A pad's button held now, or pressed in the last 200ms (a quick tap)
        const auto now_steady = std::chrono::steady_clock::now();
        std::vector<std::pair<u32, std::string>> pressed_pads;
        for (const auto& pad : pads) {
            const auto last =
                input_subsystem ? input_subsystem->GetSDLLastActivity(pad.first) : std::nullopt;
            if (PadAnyButtonPressed(pad.first) ||
                (last && now_steady - *last < std::chrono::milliseconds{200})) {
                pressed_pads.push_back(pad);
            }
        }
        // (Every check reads the controllers' queues, so a press counts when it happened)
        const RealController* pressed = nullptr;
        int count = 0;
        for (const auto& controller : controllers) {
            if (RealButtonHeld(controller)) {
                pressed = &controller;
                ++count;
            }
        }
        if (pressed_pads.size() != 1) {
            // Debug Probes: presses that can't tell them apart (several pads at once)
            if (pressed_pads.size() > 1 && Common::ControllerProbes()) {
                static auto last_many = std::chrono::steady_clock::time_point{};
                if (now_steady - last_many > std::chrono::seconds{1}) {
                    last_many = now_steady;
                    std::string which;
                    for (const auto& [id, key] : pressed_pads) {
                        which += std::format(" inst={}({})", id, key);
                    }
                    LOG_INFO(Frontend,
                             "console-mode: {} pads of model {:04x}:{:04x} pressed at once, "
                             "{} of {} controllers show one held:{}",
                             pressed_pads.size(), model.first, model.second, count,
                             controllers.size(), which);
                }
            }
            continue;
        }
        // Debug Probes: why a press didn't tell them apart (at most once a second)
        if (count != 1 && Common::ControllerProbes()) {
            static auto last = std::chrono::steady_clock::time_point{};
            if (const auto now = std::chrono::steady_clock::now();
                now - last > std::chrono::seconds{1}) {
                last = now;
                std::string nodes;
                for (const auto& controller : controllers) {
                    nodes += std::format(" {}[{}]", controller.address,
                                         controller.events.empty()
                                             ? std::string{"no input node"}
                                             : controller.events.front().toStdString());
                }
                LOG_INFO(Frontend,
                         "console-mode: {} pressed a button, {} of {} controllers of its model "
                         "show one held:{}",
                         pressed_pads.front().second, count, controllers.size(), nodes);
            }
        }
        if (count != 1) {
            continue;
        }
        const auto& [id, key] = pressed_pads[0];
        // (The controller behind the pad must not have changed while it was read)
        if (BluetoothKey(id) != key) {
            continue;
        }
        pad_bluetooth[key] = pressed->bluetooth ? pressed->address : std::string{};
        pad_bt_pending.erase(key);
        if (Common::ControllerProbes()) {
            LOG_INFO(Frontend,
                     "console-mode: inst={} ({}) told apart by a button press: bluetooth={}", id,
                     key, pressed->bluetooth ? pressed->address : std::string{"no"});
        }
    }
#endif
}

#ifdef __linux__
namespace {

/// The presses since the last look: the pads that just got input, and the real controllers
/// with input in the last 150ms, read from their input nodes
struct FreshPresses {
    std::set<SDL_JoystickID> pads;
    std::set<std::string> controllers;
};

/// Reads the pads' and the real controllers' presses (a pad's is SDL's record of its input)
FreshPresses WatchControllerPresses(InputCommon::InputSubsystem* input) {
    FreshPresses fresh;
    // The pads with a button down, and the models behind them
    std::vector<SDL_JoystickID> pads;
    std::set<std::pair<unsigned, unsigned>> models;
    {
        const JoystickApiLock lock;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i) {
            pads.push_back(ids[i]);
            models.emplace(PadVendor(ids[i]), PadProduct(ids[i]));
        }
        SDL_free(ids);
    }
    // (A pad's input newer than at the last look)
    static std::map<SDL_JoystickID, std::chrono::steady_clock::time_point> pads_seen;
    for (const SDL_JoystickID id : pads) {
        const auto last = input ? input->GetSDLLastActivity(id) : std::nullopt;
        if (!last) {
            continue;
        }
        auto& seen = pads_seen[id];
        if (*last != seen) {
            const bool first = seen == std::chrono::steady_clock::time_point{};
            seen = *last;
            if (!first) {
                fresh.pads.insert(id);
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: probe: pad inst={} ({}) input", id,
                             BluetoothKey(id));
                }
            }
        }
    }
    // The controllers' input nodes, listed again every 2 seconds
    struct Node {
        int fd;
        std::string controller;
        /// Its axes' ranges and where they rested when it was opened
        std::map<u16, input_absinfo> axes;
    };
    static std::map<QString, Node> nodes;
    static std::map<std::string, std::chrono::steady_clock::time_point> pressed_at;
    static auto listed = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - listed > std::chrono::seconds{2}) {
        listed = now;
        for (const auto& [vendor, product] : models) {
            for (const auto& controller : ControllersOfModel(vendor, product)) {
                for (const QString& event : controller.events) {
                    if (!nodes.contains(event)) {
                        const int fd = ::open(event.toLocal8Bit().constData(),
                                              O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                        // (Not a motion sensor's node: its axes move all the time)
                        std::array<unsigned char, INPUT_PROP_MAX / 8 + 1> props{};
                        if (fd >= 0 && ioctl(fd, EVIOCGPROP(props.size()), props.data()) >= 0 &&
                            (props[INPUT_PROP_ACCELEROMETER / 8] &
                             (1 << (INPUT_PROP_ACCELEROMETER % 8)))) {
                            ::close(fd);
                            continue;
                        }
                        std::map<u16, input_absinfo> axes;
                        for (u16 code = 0; fd >= 0 && code <= ABS_MAX; ++code) {
                            input_absinfo info{};
                            if (ioctl(fd, EVIOCGABS(code), &info) == 0 &&
                                info.maximum > info.minimum) {
                                axes[code] = info;
                            }
                        }
                        nodes[event] = {fd, controller.key, std::move(axes)};
                        if (Common::ControllerProbes()) {
                            LOG_INFO(Frontend,
                                     "console-mode: probe: watching {} of {} ({:04x}:{:04x}){}",
                                     event.toStdString(), controller.key, vendor, product,
                                     nodes[event].fd < 0 ? " - can't open" : "");
                        }
                    }
                }
            }
        }
    }
    for (auto it = nodes.begin(); it != nodes.end();) {
        std::array<input_event, 64> events;
        ssize_t got = 0;
        while (it->second.fd >= 0 &&
               (got = ::read(it->second.fd, events.data(), sizeof(events))) > 0) {
            for (std::size_t k = 0; k < static_cast<std::size_t>(got) / sizeof(input_event); ++k) {
                const input_event& e = events[k];
                bool pressed = e.type == EV_KEY && e.value == 1;
                if (const auto axis = it->second.axes.find(e.code);
                    e.type == EV_ABS && axis != it->second.axes.end()) {
                    const auto& info = axis->second;
                    pressed = std::abs(static_cast<double>(e.value - info.value)) /
                                  static_cast<double>(info.maximum - info.minimum) >
                              0.25;
                }
                if (!pressed) {
                    continue;
                }
                pressed_at[it->second.controller] = now;
                // (Logged once a burst: a stick held out sends events all the time)
                static std::map<std::string, std::chrono::steady_clock::time_point> logged_at;
                auto& logged = logged_at[it->second.controller];
                const bool burst = now - logged < std::chrono::milliseconds{500};
                logged = now;
                if (Common::ControllerProbes() && !burst) {
                    LOG_INFO(Frontend, "console-mode: probe: controller {} input ({} {:#x})",
                             it->second.controller, e.type == EV_KEY ? "button" : "axis",
                             e.code);
                }
            }
        }
        // (Gone: listed again if it comes back)
        if (it->second.fd < 0 || (got < 0 && errno == ENODEV)) {
            if (it->second.fd >= 0) {
                ::close(it->second.fd);
            }
            it = nodes.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = pressed_at.begin(); it != pressed_at.end();) {
        if (now - it->second < std::chrono::milliseconds{150}) {
            fresh.controllers.insert(it->first);
            ++it;
        } else {
            it = pressed_at.erase(it);
        }
    }
    return fresh;
}

} // Anonymous namespace
#endif

void MainWindow::WatchSteamPadInputs() {
#ifdef __linux__
    // (Read under Steam, and for Debug Probes)
    if (!Common::SteamPadsInUse() && !Common::ControllerProbes()) {
        return;
    }
    const FreshPresses fresh = WatchControllerPresses(input_subsystem.get());
    // A pad's input together with exactly one real controller's: when Steam then moves a
    // controller to that pad, that is the one
    if (Common::SteamPadsInUse() && fresh.pads.size() == 1 && fresh.controllers.size() == 1) {
        last_lone_input = LoneInput{*fresh.pads.begin(), *fresh.controllers.begin(),
                                    std::chrono::steady_clock::now()};
    }
#endif
}

void MainWindow::PollControllerActivity() {
    // (The automatic modes only)
    const auto mode = Settings::values.controller_applet_mode.GetValue();
    if (mode == Settings::AppletMode::ConsoleMode ||
        mode == Settings::AppletMode::HandheldConsoleMode) {
        WatchSteamPadInputs();
        LearnBluetoothPads();
    }
    // Redrawn faster than the 250ms tick, only when who is active changes or while animating
    if (overlay_rows.empty()) {
        if (!controller_hints.empty()) {
            RenderControllerOverlay();
        }
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::set<u32> active;
    for (const auto& row : overlay_rows) {
        if (row.pad == 0) {
            continue;
        }
        const auto last =
            input_subsystem ? input_subsystem->GetSDLLastActivity(row.pad) : std::nullopt;
        if (PadInputHeld(row.pad) || (last && now - *last < std::chrono::milliseconds{250})) {
            active.insert(row.pad);
        }
    }
    // ...and when a green/red flash blinks
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    std::vector<bool> blink;
    for (const auto& row : overlay_rows) {
        blink.push_back(row.flash != 0 && BlinkOn(now_ms - row.flash_since));
    }
    const bool pressed = PollButtonHistory();
    const auto& layout = render_window->GetFramebufferLayout();
    const float room = Common::OverlayRoomTopLeft(
        static_cast<float>(layout.screen.right - layout.screen.left));
    const bool room_changed = std::exchange(overlay_room, room) != room;
    if (pressed || OverlayAnimating() || active != overlay_active_pads || blink != overlay_blink ||
        room_changed) {
        RenderControllerOverlay();
    }
}

bool MainWindow::PollButtonHistory() {
    if (!QtCommon::system || !QtCommon::system->IsPoweredOn()) {
        return false;
    }
    auto& hid = QtCommon::system->HIDCore();
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    bool pressed = false;
    const qint64 life_ms = Settings::values.console_history_seconds.GetValue() * 1000LL;
    const std::size_t size =
        static_cast<std::size_t>(Settings::values.console_history_size.GetValue());
    // A controller that left takes its history with it (back, it starts over)
    std::erase_if(press_history, [this, &pressed](const auto& entry) {
        const bool kept =
            std::any_of(overlay_rows.begin(), overlay_rows.end(), [&](const auto& row) {
                return row.pad != 0 && overlay_row_history[row.player] == entry.first;
            });
        pressed |= !kept;
        return !kept;
    });
    for (const auto& row : overlay_rows) {
        if (row.pad == 0 || row.player < 0 || row.player >= 8 ||
            overlay_row_history[row.player].empty()) {
            continue;
        }
        const int i = row.player;
        const std::string& ident = overlay_row_history[i];
        const bool fresh = history_ident[i] != ident;
        history_ident[i] = ident;
        const auto& player = Settings::values.players.GetValue()[i];
        auto* controller = i == 0 && player.controller_type == Settings::ControllerType::Handheld
                               ? hid.GetEmulatedController(Core::HID::NpadIdType::Handheld)
                               : hid.GetEmulatedControllerByIndex(i);
        if (!controller) {
            continue;
        }
        // Entries gone through their life and shrink leave the history
        auto& history = press_history[ident];
        while (!history.empty() && now_ms - history.front().at > life_ms + 400) {
            history.pop_front();
            pressed = true;
        }
        // Presses within 50ms of an input's first (about three frames) are one input
        constexpr qint64 together_ms = 50;
        const auto record = [&](int button) {
            qint64 at = now_ms;
            if (!history.empty() && now_ms - history.back().at <= together_ms) {
                const qint64 group = history.back().at;
                // (A stick turning again is a new input, not a second press)
                const bool same_stick =
                    button >= StickMoveButton &&
                    std::any_of(history.begin(), history.end(), [&](const ButtonPress& p) {
                        return p.at == group && p.button >= StickMoveButton &&
                               (p.button - StickMoveButton) / 4 == (button - StickMoveButton) / 4;
                    });
                if (!same_stick) {
                    at = group;
                }
            }
            history.push_back({button, at});
            std::set<qint64> inputs;
            for (const auto& press : history) {
                inputs.insert(press.at);
            }
            while (inputs.size() > size) {
                const qint64 oldest = history.front().at;
                while (!history.empty() && history.front().at == oldest) {
                    history.pop_front();
                }
                inputs.erase(oldest);
            }
            pressed = true;
        };
        // A stick counts when it leaves the middle, and again when it turns another way
        const auto sticks = controller->GetSticksValues();
        for (int s = 0; s < 2; ++s) {
            const float x = sticks[static_cast<std::size_t>(s)].x.value;
            const float y = sticks[static_cast<std::size_t>(s)].y.value;
            const float reach = std::hypot(x, y);
            auto& pointing = stick_direction[i][static_cast<std::size_t>(s)];
            if (reach > 0.6f) {
                const int direction = std::abs(x) > std::abs(y) ? (x < 0 ? 0 : 2) : (y > 0 ? 1 : 3);
                if (direction + 1 != pointing) {
                    pointing = direction + 1;
                    if (!fresh) {
                        record(StickMoveButton + s * 4 + direction);
                    }
                }
            } else if (reach < 0.3f) {
                pointing = 0;
            }
        }
        const auto values = controller->GetButtonsValues();
        auto& down = buttons_down[i];
        for (int b = 0; b < Settings::NativeButton::NumButtons; ++b) {
            const bool held = values[b].value;
            if (held && !down[b] && !fresh) {
                record(b);
            }
            down[b] = held;
        }
    }
    return pressed;
}

void MainWindow::ApplyConsoleControllerMode() {
    // (Closing: the controllers' inputs are unloaded; a reload now would outlive the input
    // engines and crash at exit)
    if (closing) {
        return;
    }
    // A tick longer than 250ms means a controller or the system held Eden up: always logged
    struct SlowCheckWarning {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~SlowCheckWarning() {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
            if (ms > 250) {
                LOG_WARNING(Frontend, "console-mode: controller check took {} ms", ms);
            }
        }
    } slow_check_warning;
    const auto mode = Settings::values.controller_applet_mode.GetValue();
    const bool handheld = mode == Settings::AppletMode::HandheldConsoleMode;
    const bool console = mode == Settings::AppletMode::ConsoleMode;
    // The mode, when it changes (from the Controllers tab or System > Applets): always logged
    if (mode != logged_mode) {
        LOG_INFO(Frontend, "console-mode: mode {} -> {}",
                 logged_mode ? AppletModeName(*logged_mode) : "(start)", AppletModeName(mode));
        logged_mode = mode;
        // Debug Probes: what every report needs, once per mode
        if (Common::ControllerProbes()) {
            LOG_INFO(Frontend,
                     "console-mode: setup steam={} bypass={} overlay={} hints={} history={} "
                     "inputs={} expire={}s gaps={} seat_profiles=\"{}\"",
                     Common::RunningUnderSteam(),
                     Common::GetControllerConsoleState().steam_bypassed.load(),
                     Settings::values.console_debug_overlay.GetValue(),
                     Settings::values.console_hints.GetValue(),
                     Settings::values.console_history.GetValue(),
                     Settings::values.console_history_size.GetValue(),
                     Settings::values.console_history_seconds.GetValue(),
                     Settings::values.console_history_gaps.GetValue(),
                     Settings::values.modern_seat_profiles.GetValue());
        }
    }
    // The handheld's own pad is stored per way of launching: the Steam handle under Steam, the
    // raw pad's identity otherwise. A Steam virtual pad's GUID stored as the identity (older
    // builds) never matches a raw pad: dropped, so auto-detection works again.
    if (!Common::SteamPadsInUse()) {
        const std::string identity = Settings::values.builtin_pad_identity.GetValue();
        if (identity.size() >= 25 && identity.starts_with("guid:") &&
            identity.compare(5 + 8, 4, "de28") == 0 && identity.compare(5 + 16, 4, "ff11") == 0) {
            Settings::values.builtin_pad_identity.SetValue(std::string{});
            Settings::values.builtin_pad_manual.SetValue(false);
            LOG_INFO(Frontend,
                     "console-mode: stored handheld was a Steam virtual pad ({}), "
                     "back to auto-detect for direct launches",
                     identity);
        }
    }
    // A declaration with no identity (older builds) would pin "no handheld" forever: undone
    if (Settings::values.builtin_pad_manual.GetValue() &&
        Settings::values.builtin_pad_identity.GetValue().empty()) {
        Settings::values.builtin_pad_manual.SetValue(false);
        LOG_INFO(Frontend, "console-mode: declared handheld had no identity, back to auto");
    }

    // The live gamepads, keyed by connection instance (always unique); pad_identity keeps the
    // stable identity used to reclaim slots.
    //
    // Under Steam the virtual pads share one GUID and Steam moves controllers between them, so
    // the Steam handle (which follows the controller) is the identity. Steam fills it in a
    // moment after a pad appears: until then the pad isn't live (at most 3 seconds).
    std::map<u32, std::string> live;
    /// Under Steam: real devices unhidden for their motion (never players)
    std::vector<u32> motion_devices;
    {
        static std::map<u32, std::chrono::steady_clock::time_point> first_seen;
        const auto now = std::chrono::steady_clock::now();
        std::set<u32> present;
        const JoystickApiLock lock;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        // Steam's virtual pads around (none when Steam Input is off for Eden)
        Common::GetControllerConsoleState().steam_pads_present =
            std::any_of(ids, ids + count, [](SDL_JoystickID id) { return IsSteamVirtualPad(id); });
        for (int i = 0; i < count; ++i) {
            const u32 id = ids[i];
            present.insert(id);
            first_seen.try_emplace(id, now);
            // Bypassed: the real controllers play, Steam's virtual pads are left alone
            if (Common::GetControllerConsoleState().steam_bypassed && IsSteamVirtualPad(id)) {
                continue;
            }
            // A real device Steam hid, opened again for its motion: never a player
            if (Common::SteamPadsInUse() && !IsSteamVirtualPad(id) &&
                Common::SteamHidModel(PadVendor(id),
                                      PadProduct(id))) {
                motion_devices.push_back(id);
                // (Only marked once logged: Debug Probes turned on later still describes it)
                static std::set<u32> probed;
                if (Common::ControllerProbes() && probed.insert(id).second) {
                    std::string twins;
                    const Uint16 vendor = PadVendor(id);
                    const Uint16 product = PadProduct(id);
                    for (int k = 0; k < count; ++k) {
                        if (ids[k] != id && IsSteamVirtualPad(ids[k]) &&
                            PadVendor(ids[k]) == vendor &&
                            PadProduct(ids[k]) == product) {
                            twins += std::format("{} ", ids[k]);
                        }
                    }
                    if (Common::ControllerProbes()) {
                        LOG_INFO(Frontend,
                                 "console-mode: real device inst={} {:04x}:{:04x} name={} "
                                 "gyro={} path={} same model as virtual inst=[{}]",
                                 id, vendor, product, PadName(id).toStdString(), PadHasGyro(id),
                                 PadPath(id).toStdString(), twins);
                    }
                }
                continue;
            }
            std::string ident = PadIdentity(id);
            if (Common::SteamPadsInUse() && IsSteamVirtualPad(id)) {
                if (const Uint64 handle = PadSteamHandle(id)) {
                    ident = std::format("steamhandle:{:#x}", handle);
                } else if (now - first_seen[id] < std::chrono::seconds{3}) {
                    continue;
                }
            }
            // (For identifying a user's handheld from their log)
            static std::set<std::string> described;
            if (Common::ControllerProbes() && described.insert(ident).second) {
                const Uint16 vendor = PadVendor(id);
                // (SDL has no name for an unknown type: null, which fmt refuses)
                const char* type = SDL_GetGamepadStringForType(PadType(id));
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend,
                             "console-mode: device {:04x}:{:04x} name=\"{}\" type={} steam={} "
                             "gyro={} handheld_maker={} controller_maker={} ident={}",
                             vendor, PadProduct(id), PadName(id).toStdString(),
                             type ? type : "unknown", IsSteamVirtualPad(id), PadHasGyro(id),
                             IsHandheldMaker(vendor), IsControllerMaker(vendor), ident);
                }
            }
            live[id] = std::move(ident);
        }
        SDL_free(ids);
        std::erase_if(first_seen, [&present](const auto& e) { return !present.contains(e.first); });
    }

    // The Deck's controller only reports motion while its sensors are on, which Steam leaves off
    // unless its layout uses the gyro (in Desktop Mode too): when it goes silent, ask again (at
    // most every 2 seconds). Under Steam it is a motion device, else one of the pads.
    {
        const auto is_deck = [](u32 id) {
            return PadVendor(id) == 0x28DE && PadProduct(id) == 0x1205;
        };
        std::optional<u32> deck;
        if (const auto it = std::find_if(motion_devices.begin(), motion_devices.end(), is_deck);
            it != motion_devices.end()) {
            deck = *it;
        } else if (!Common::SteamPadsInUse()) {
            for (const auto& [id, ident] : live) {
                if (is_deck(id) && PadPath(id).startsWith(QStringLiteral("/dev/hidraw"))) {
                    deck = id;
                    break;
                }
            }
        }
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        if (deck && now_ms - deck_motion_asked_at >= 2000 && PadMotionSilent(*deck)) {
            deck_motion_asked_at = now_ms;
            const QString path = PadPath(*deck);
            EnableDeckMotion(path);
            if (Common::ControllerProbes()) {
                LOG_INFO(Frontend, "console-mode: deck motion silent, asked for it via {}",
                         path.toStdString());
            }
        }
    }

    // A controller Steam moved to another virtual pad takes its slot along
    if (Common::SteamPadsInUse()) {
        static std::map<std::string, u32> handle_inst;
        std::map<u32, u32> moves; // to <- from
        for (const auto& [id, ident] : live) {
            if (!ident.starts_with("steamhandle:")) {
                continue;
            }
            const auto it = handle_inst.find(ident);
            if (it != handle_inst.end() && it->second != id) {
                moves[id] = it->second;
            }
        }
        if (!moves.empty()) {
            std::map<u32, int> old_slot;
            std::set<u32> involved;
            for (const auto& [to, from] : moves) {
                involved.insert(to);
                involved.insert(from);
            }
            for (const u32 inst : involved) {
                if (const auto it = pad_slots.find(inst); it != pad_slots.end()) {
                    old_slot[inst] = it->second;
                    pad_slots.erase(it);
                }
                pad_identity.erase(inst);
            }
            u32 new_builtin = builtin_pad_id;
            const std::set<u32> benched_before = benched_pads;
            const auto address_before = inst_address;
            const auto origin_before = pad_origin;
            const auto guessed_before = pad_guessed;
            for (const auto& [to, from] : moves) {
                const auto slot = old_slot.find(from);
                LOG_INFO(Frontend, "console-mode: Steam moved {} from inst={} to inst={} (P{})",
                         live[to], from, to, slot != old_slot.end() ? slot->second + 1 : 0);
                // Steam moves a controller on its first use and says so later, so its input
                // already came on the new pad: the one controller that had input with this pad
                // just before is the one moved (a match made meanwhile is put right)
                const std::string key = BluetoothKey(to);
                if (last_lone_input && last_lone_input->pad == to &&
                    std::chrono::steady_clock::now() - last_lone_input->at <
                        std::chrono::seconds{5} &&
                    last_lone_input->controller.size() == 17 && pad_bluetooth.contains(key)) {
                    const std::string& address = last_lone_input->controller;
                    const auto had = pad_bluetooth.find(key);
                    if (had->second != address) {
                        // (The controller it was taken for is unsure again)
                        for (auto& [other, other_address] : pad_bluetooth) {
                            if (other != key && other_address == address) {
                                other_address.clear();
                                pad_bt_pending.insert(other);
                            }
                        }
                        LOG_INFO(Frontend,
                                 "console-mode: inst={} ({}) told apart by the input Steam moved "
                                 "it for: bluetooth={}{}",
                                 to, key, address,
                                 had->second.empty() ? std::string{}
                                                     : " (was " + had->second + ")");
                        had->second = address;
                        pad_bt_pending.erase(key);
                    }
                }
                if (slot != old_slot.end()) {
                    pad_slots[to] = slot->second;
                    pad_identity[to] = live[to];
                }
                if (builtin_pad_id == from) {
                    new_builtin = to;
                }
                // (From the state before the moves: two pads can trade instances)
                if (benched_before.contains(from)) {
                    benched_pads.insert(to);
                } else {
                    benched_pads.erase(to);
                }
                const auto carry = [from, to](auto& now_map, const auto& before) {
                    if (const auto it = before.find(from); it != before.end()) {
                        now_map[to] = it->second;
                    } else {
                        now_map.erase(to);
                    }
                };
                carry(inst_address, address_before);
                carry(pad_origin, origin_before);
                if (guessed_before.contains(from)) {
                    pad_guessed.insert(to);
                } else {
                    pad_guessed.erase(to);
                }
            }
            builtin_pad_id = new_builtin;
        }
        handle_inst.clear();
        for (const auto& [id, ident] : live) {
            if (ident.starts_with("steamhandle:")) {
                handle_inst[ident] = id;
            }
        }
    }

    // Publish the live pads to the settings UI, and serve Identify: the next pad with a button
    // held becomes the handheld's own. Whatever the mode (the tab declares it before OK).
    {
        auto& state = Common::GetControllerConsoleState();
        std::scoped_lock lock{state.mutex};
        state.pads.clear();
        // Every pad is offered, possible handheld controls first (they can pose as a
        // standalone controller, such as an emulated DualSense)
        std::vector<std::pair<std::string, std::string>> standalone;
        for (const auto& [id, ident] : live) {
            // Under Steam the entries carry the Steam handle (every pad shares one GUID)
            const Uint64 handle = Common::SteamPadsInUse() ? PadSteamHandle(id) : 0;
            auto entry = std::pair{PadName(id).toStdString(),
                                   handle ? std::format("steamhandle:{:#x}", handle) : ident};
            if (IsPlainlyStandalone(id)) {
                standalone.push_back(std::move(entry));
            } else {
                state.pads.push_back(std::move(entry));
            }
        }
        state.builtin_pads = state.pads.size();
        state.pads.insert(state.pads.end(), standalone.begin(), standalone.end());
        // A tap is shorter than 250ms: poll faster while Identify waits, while twins wait for
        // a press, and while paused for a disconnect (for +)
        console_mode_timer.start(std::chrono::milliseconds{
            state.identify_armed || !disconnect_paused.empty() ? 50
            : pad_bt_pending.empty()                           ? 250
                                                               : 100});
        if (state.identify_armed) {
            const auto now = std::chrono::steady_clock::now();
            if (now - state.identify_armed_at > std::chrono::seconds{15}) {
                state.identify_armed = false;
            } else {
                for (const auto& [id, ident] : live) {
                    // (Any pad: the user presses on the handheld's own on purpose)
                    if (PadAnyButtonPressed(id)) {
                        state.identify_armed = false;
                        builtin_pad_id = id;
                        builtin_identity = ident;
                        builtin_seen_first = true;
                        builtin_warned = false;
                        // Each way of launching keeps its own: the other one's stays
                        const Uint64 handle = Common::SteamPadsInUse() ? PadSteamHandle(id) : 0;
                        if (Common::SteamPadsInUse()) {
                            if (handle) {
                                Settings::values.builtin_pad_steam_handle.SetValue(
                                    std::format("{:#x}", handle));
                                Settings::values.builtin_pad_steam_manual.SetValue(true);
                            }
                        } else {
                            Settings::values.builtin_pad_identity.SetValue(ident);
                            Settings::values.builtin_pad_manual.SetValue(true);
                        }
                        LOG_INFO(Frontend,
                                 "console-mode: identify captured inst={} ident={} "
                                 "steam_handle={:#x}",
                                 id, ident, handle);
                        break;
                    }
                }
            }
        }
    }

    // Under Steam the picker declares a pad by its Steam handle
    {
        const std::string declared = Settings::values.builtin_pad_identity.GetValue();
        constexpr std::string_view prefix = "steamhandle:";
        if (declared.starts_with(prefix)) {
            const std::string handle = declared.substr(prefix.size());
            for (const auto& [id, ident] : live) {
                if (std::format("{:#x}", PadSteamHandle(id)) == handle) {
                    builtin_pad_id = id;
                    break;
                }
            }
            // The identity is the direct launches' own: a Steam pick only sets the handle
            Settings::values.builtin_pad_steam_handle.SetValue(handle);
            Settings::values.builtin_pad_steam_manual.SetValue(true);
            Settings::values.builtin_pad_identity.SetValue(std::string{});
            Settings::values.builtin_pad_manual.SetValue(false);
            LOG_INFO(Frontend, "console-mode: picker declared Steam handle {} inst={}", handle,
                     builtin_pad_id);
        }
    }

    // The overlay and hints only exist in the automatic modes: so do their View menu toggles
    ui->action_Show_Controller_Overlay->setVisible(handheld || console);
    ui->action_Show_Controller_Hints->setVisible(handheld || console);
    if (!handheld && !console) {
        if (!overlay_rows.empty()) {
            overlay_rows.clear();
            RenderControllerOverlay();
        }
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            state.engine_active = false;
            state.players.clear();
            state.handheld_name.clear();
            state.handheld_has_player = false;
            state.swap_requests.clear();
            state.remove_requests.clear();
        }
        benched_pads.clear();
        if (console_profile) {
            // Leaving the console modes: the user's own players come back exactly as they were
            RestoreConsoleProfile();
            pad_slots.clear();
            pad_identity.clear();
            pad_sets.clear();
            address_origin.clear();
            address_claims.clear();
            slot_set = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
            set_default_of.fill(0);
            set_before_profile.clear();
            LOG_INFO(Frontend, "console-mode: left, the user's players are restored");
            if (QtCommon::system && QtCommon::system->IsPoweredOn()) {
                QtCommon::system->HIDCore().ReloadInputDevices();
            }
        }
        if (builtin_seen_first) {
            // Leaving forgets the auto-detected handheld; a declared one stays
            builtin_pad_id = 0;
            builtin_identity.clear();
            builtin_seen_first = false;
            builtin_warned = false;
            // (The Steam handle stays: it is the same controller in every session)
            if (!Settings::values.builtin_pad_manual.GetValue()) {
                Settings::values.builtin_pad_identity.SetValue(std::string{});
            }
        }
        return;
    }
    if (!console_profile) {
        // Entering a console mode: the user's players are kept aside; the engine's changes stay
        // runtime only (see SaveAllConfigValues)
        console_profile = Settings::values.players.GetValue(true);
        game_profiles.reset();
        session_sets = ModernStartSets(*console_profile);
        ApplySeatProfiles();
        LOG_INFO(Frontend, "console-mode: entered, the user's players are kept aside");
        if (Common::ControllerProbes()) {
            LOG_INFO(Frontend, "console-mode: entered with profile P1 motions {}",
                     MotionTrace((*console_profile)[0]));
        }
    }
    // A game with input profiles of its own: those players' sets are the profile's
    if (Settings::values.players.UsingGlobal()) {
        game_profiles.reset();
    } else if (!game_profiles) {
        game_profiles = Settings::values.players.GetValue();
        for (size_t i = 0; i < game_profiles->size(); ++i) {
            if (!(*game_profiles)[i].profile_name.empty()) {
                LOG_INFO(Frontend, "console-mode: P{} uses the game's input profile \"{}\"", i + 1,
                         (*game_profiles)[i].profile_name);
            }
        }
    }
    if (builtin_identity.empty()) {
        // Console Mode also needs to know the handheld's own pad: it carries player one's set
        builtin_identity = Settings::values.builtin_pad_identity.GetValue();
    }

    // Names as the Controls tab lists them, by the device the pad is bound as
    std::vector<Common::ParamPackage> input_devices;
    if (input_subsystem) {
        input_devices = input_subsystem->GetInputDevices();
    }
    const auto label_of = [&](u32 id, const std::string&) {
        QString display;
        if (input_subsystem) {
            if (const auto binding = input_subsystem->GetSDLBinding(id)) {
                for (const auto& device : input_devices) {
                    if (device.Get("engine", "") == "sdl" &&
                        device.Get("guid", "") == binding->first &&
                        device.Get("port", -1) == binding->second) {
                        display = QString::fromStdString(device.Get("display", ""));
                        break;
                    }
                }
            }
        }
        // Under Steam, SDL's names lag up to 3 seconds behind a move: until SDL has Steam's
        // controller type, the name is Steam's controller's
        if (const auto slot = SteamSlotOf(id);
            slot && SDL_GetGamepadTypeForID(id) != slot->type) {
            switch (slot->type) {
            case SDL_GAMEPAD_TYPE_XBOX360:
                display = QStringLiteral("Xbox 360 Controller");
                break;
            case SDL_GAMEPAD_TYPE_XBOXONE:
                display = QStringLiteral("Xbox One Controller");
                break;
            case SDL_GAMEPAD_TYPE_PS4:
                display = QStringLiteral("PS4 Controller");
                break;
            case SDL_GAMEPAD_TYPE_PS5:
                display = QStringLiteral("DualSense Wireless Controller");
                break;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
                display = QStringLiteral("Nintendo Switch Pro Controller");
                break;
            default:
                display = slot->name.startsWith(QLatin1Char('#')) ? PadName(id) : slot->name;
                break;
            }
        }
        // (Steam's names can be translation keys: not a name to show)
        if (display.startsWith(QLatin1Char('#'))) {
            display = PadName(id);
        }
        return std::pair<QString, QString>{display, PadName(id)};
    };

    // Debug Probes: the pad identities and the slot mapping, every tick
    if (Common::ControllerProbes()) {
        std::string trace;
        for (const auto& [id, ident] : live) {
            const auto it = pad_slots.find(id);
            trace += std::format("\n  pad inst={} slot={} idx={} name={} path={} ident={}", id,
                                 it == pad_slots.end() ? -1 : static_cast<int>(it->second),
                                 PadPlayerIndex(id), PadName(id).toStdString(),
                                 PadPath(id).toStdString(), ident);
        }
        LOG_INFO(Frontend, "console-mode: live={}{}, builtin={}{}", trace,
                 live.empty() ? " (none)" : "", builtin_pad_id,
                 builtin_seen_first ? "" : " (unlatched)");
    }
    {
        // Steam fills a virtual pad's handle, type and name a moment after it appears: traced
        static std::map<u32, std::string> traced_steam;
        for (const auto& [id, ident] : live) {
            const std::string desc = std::format(
                "steam_handle={:#x} type={} name={}", PadSteamHandle(id),
                static_cast<int>(PadType(id)), PadName(id).toStdString());
            auto& last = traced_steam[id];
            if (last != desc) {
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: pad inst={} slot={} {}", id,
                             pad_slots.contains(id) ? pad_slots.at(id) + 1 : 0, desc);
                }
                last = desc;
            }
        }

        static std::set<u32> traced_insts;
        for (const auto& [id, ident] : live) {
            if (traced_insts.insert(id).second) {
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend,
                             "console-mode: pad connected inst={} ident={} name={} idx={} gyro={} "
                             "steam_handle={:#x} type={} real_type={}",
                             id, ident, PadName(id).toStdString(), PadPlayerIndex(id),
                             PadHasGyro(id), PadSteamHandle(id),
                             static_cast<int>(PadType(id)),
                             static_cast<int>(SDL_GetRealGamepadTypeForID(id)));
                }
            }
        }
        for (auto it = traced_insts.begin(); it != traced_insts.end();) {
            if (live.find(*it) == live.end()) {
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: pad disconnected inst={}", *it);
                }
                it = traced_insts.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Turn Off: Eden disconnects a Bluetooth controller (most turn off); it sits out until it
    // reconnects, and the next controller to connect takes the player
    {
        // (By the controller, see BluetoothKey)
        std::map<u32, std::string> keys;
        std::set<std::string> live_keys;
        for (const auto& [id, ident] : live) {
            keys[id] = BluetoothKey(id);
            live_keys.insert(keys[id]);
        }
        std::erase_if(pad_bluetooth, [&](const auto& e) { return !live_keys.contains(e.first); });
        std::erase_if(pad_bt_pending, [&](const auto& key) { return !live_keys.contains(key); });
#ifdef __linux__
        for (const auto& [id, ident] : live) {
            if (!pad_bluetooth.contains(keys[id])) {
                const PadBluetooth found = FindPadBluetooth(id);
                pad_bluetooth[keys[id]] = found.address;
                if (found.unsure) {
                    pad_bt_pending.insert(keys[id]);
                }
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: inst={} bluetooth={}", id,
                             found.unsure            ? std::string{"unsure until a button press"}
                             : found.address.empty() ? std::string{"no"}
                                                     : found.address);
                }
            }
        }
#endif
        std::vector<int> removals;
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            removals.swap(state.remove_requests);
        }
        for (const int player : removals) {
            const auto held = std::find_if(pad_slots.begin(), pad_slots.end(), [&](const auto& e) {
                return e.second == player && live.contains(e.first);
            });
            if (player < 0 || player >= 10 || held == pad_slots.end()) {
                continue;
            }
            const u32 id = held->first;
            if (pad_bluetooth[keys[id]].empty()) {
                continue; // (Only a Bluetooth controller is turned off)
            }
            pad_slots.erase(held);
            pad_identity.erase(id);
            benched_pads.insert(id);
            turned_off_at[player] = QDateTime::currentMSecsSinceEpoch();
            const std::string address = pad_bluetooth[keys[id]];
#ifdef __linux__
            DisconnectBluetooth(address);
#endif
            LOG_INFO(Frontend, "console-mode: P{} turned off, inst={} {}", player + 1, id, address);
        }
        std::erase_if(benched_pads, [&](u32 id) { return !live.contains(id); });
        std::erase_if(live, [&](const auto& e) { return benched_pads.contains(e.first); });
    }

    // The handheld's controller removed with its ✕: auto-detect leaves it alone until it is
    // allowed again (or picked, or identified)
    {
        bool remove = false;
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            remove = std::exchange(state.builtin_remove_request, false);
        }
        if (remove && builtin_pad_id && live.contains(builtin_pad_id)) {
            const std::string key = PadKey(builtin_pad_id, live[builtin_pad_id]);
            Settings::values.builtin_pad_blocked.SetValue(key);
            Settings::values.builtin_pad_steam_handle.SetValue(std::string{});
            Settings::values.builtin_pad_steam_manual.SetValue(false);
            Settings::values.builtin_pad_identity.SetValue(std::string{});
            Settings::values.builtin_pad_manual.SetValue(false);
            builtin_pad_id = 0;
            builtin_identity.clear();
            LOG_INFO(Frontend, "console-mode: handheld controller {} removed: auto-detect skips it",
                     key);
        }
    }
    const std::string blocked_key = Settings::values.builtin_pad_blocked.GetValue();
    const auto is_blocked = [&](u32 id) {
        return !blocked_key.empty() && live.contains(id) && PadKey(id, live[id]) == blocked_key;
    };
    // The handheld's rules apply only to the pad proven to be its own: the latched instance
    // still alive, its Steam handle under Steam, or a unique identity match. Otherwise none.
    u32 builtin_match_id = 0;
    bool builtin_valid = false;
    const bool steam_handle_known =
        Common::SteamPadsInUse() && !Settings::values.builtin_pad_steam_handle.GetValue().empty();
    if (builtin_pad_id && live.find(builtin_pad_id) != live.end() && !steam_handle_known &&
        !is_blocked(builtin_pad_id)) {
        builtin_match_id = builtin_pad_id;
        builtin_valid = true;
    } else if (Common::SteamPadsInUse()) {
        // Under Steam neither the GUID nor Steam's player index tells the Deck's pad: its Steam
        // handle, stable across sessions, does. With none stored yet, a lone pad is latched.
        const std::string want = Settings::values.builtin_pad_steam_handle.GetValue();
        int count = 0;
        u32 holder = 0;
        if (!want.empty()) {
            for (const auto& [id, ident] : live) {
                if (std::format("{:#x}", PadSteamHandle(id)) != want || is_blocked(id)) {
                    continue;
                }
                if (!Settings::values.builtin_pad_steam_manual.GetValue() &&
                    !CouldBeHandheld(PadVendor(id), PadProduct(id))) {
                    // A standalone controller stored as the handheld's (older builds) is dropped,
                    // unless the user declared it
                    LOG_INFO(Frontend,
                             "console-mode: stored handheld {} is a {:04x}:{:04x} "
                             "controller, not a handheld's: forgotten",
                             want, PadVendor(id), PadProduct(id));
                    Settings::values.builtin_pad_steam_handle.SetValue(std::string{});
                    continue;
                }
                ++count;
                holder = id;
            }
        }
        bool adopted = false;
        if (count == 0) {
            // No pad holds the stored handle (it can change after a reboot or update): the
            // Deck's controller is the only one Steam reports with no gamepad type. Adopted when
            // exactly one pad fits.
            int fits = 0;
            u32 fit = 0;
            for (const auto& [id, ident] : live) {
                if (PadType(id) == SDL_GAMEPAD_TYPE_UNKNOWN &&
                    PadSteamHandle(id) != 0 && CouldBeHandheld(PadVendor(id), PadProduct(id)) &&
                    !is_blocked(id)) {
                    ++fits;
                    fit = id;
                }
            }
            if (fits == 1) {
                count = 1;
                holder = fit;
                adopted = true;
            } else if (want.empty() && live.size() == 1 &&
                       !IsStandaloneControllerType(PadType(live.begin()->first)) &&
                       CouldBeHandheld(PadVendor(live.begin()->first),
                                       PadProduct(live.begin()->first)) &&
                       !is_blocked(live.begin()->first)) {
                count = 1;
                holder = live.begin()->first;
            }
        }
        if (adopted) {
            const std::string handle = std::format("{:#x}", PadSteamHandle(holder));
            if (handle != want) {
                Settings::values.builtin_pad_steam_handle.SetValue(handle);
                Settings::values.builtin_pad_steam_manual.SetValue(false);
                LOG_INFO(Frontend,
                         "console-mode: builtin adopted inst={} as the deck's controller, "
                         "Steam handle {} -> {}",
                         holder, want.empty() ? std::string{"(none)"} : want, handle);
            }
        }
        static std::string last_resolved;
        const std::string resolved =
            count == 1 ? std::format("inst={}", holder) : std::format("{} matches", count);
        if (resolved != last_resolved) {
            last_resolved = resolved;
            if (Common::ControllerProbes()) {
                LOG_INFO(Frontend, "console-mode: builtin by Steam handle {}: {}",
                         want.empty() ? std::string{"(none stored)"} : want, resolved);
            }
        }
        if (count == 1) {
            builtin_match_id = holder;
            builtin_valid = true;
        }
    } else if (!builtin_identity.empty()) {
        int identity_count = 0;
        u32 identity_holder = 0;
        for (const auto& [id, ident] : live) {
            if (ident == builtin_identity && !is_blocked(id)) {
                ++identity_count;
                identity_holder = id;
            }
        }
        if (identity_count == 1) {
            builtin_match_id = identity_holder;
            builtin_valid = true;
        }
        if (!builtin_valid) {
            static int last_count = -1;
            if (identity_count != last_count) {
                last_count = identity_count;
                LOG_INFO(Frontend,
                         "console-mode: builtin unresolved ({} pads match identity {}), "
                         "built-in rules paused",
                         identity_count, builtin_identity);
            }
        }
    }
    if (builtin_valid) {
        builtin_pad_id = builtin_match_id;
        if (Common::SteamPadsInUse() &&
            Settings::values.builtin_pad_steam_handle.GetValue().empty()) {
            // Latch the handheld's Steam handle as soon as Steam has filled it in
            if (const Uint64 handle = PadSteamHandle(builtin_match_id)) {
                Settings::values.builtin_pad_steam_handle.SetValue(std::format("{:#x}", handle));
                LOG_INFO(Frontend, "console-mode: builtin Steam handle latched {:#x} inst={}",
                         handle, builtin_match_id);
            }
        }
    }

    if (handheld) {
        const std::string stored{Settings::values.builtin_pad_identity.GetValue()};
        const bool manual = Settings::values.builtin_pad_manual.GetValue();
        if (manual) {
            // A declared pad (picker or Identify): resolve its current instance
            builtin_identity = stored;
            builtin_seen_first = true;
            builtin_pad_id = 0;
            if (builtin_valid) {
                builtin_pad_id = builtin_match_id;
            }
        } else if (!builtin_pad_id && !stored.empty() && !Common::SteamPadsInUse()) {
            // (Under Steam the stored GUID is every pad's: the Steam handle resolves it)
            for (const auto& [id, ident] : live) {
                if (ident == stored) {
                    builtin_pad_id = id;
                    builtin_identity = ident;
                    builtin_seen_first = true;
                    builtin_match_id = id;
                    builtin_valid = true;
                    break;
                }
            }
        }
        // (Direct launches only: under Steam the handle resolves it above)
        if (!builtin_pad_id && !manual && !Common::SteamPadsInUse()) {
            auto handheld_candidate = [&]() -> u32 {
                std::vector<u32> makers;
                std::vector<u32> decks;
                for (const auto& [id, ident] : live) {
                    if (IsSteamVirtualPad(id) || is_blocked(id)) {
                        // (A virtual pad carries Valve's vendor: no evidence)
                        continue;
                    }
                    const Uint16 vendor = PadVendor(id);
                    if (IsHandheldMaker(vendor) && CouldBeHandheld(vendor, PadProduct(id))) {
                        makers.push_back(id);
                        // Valve also makes standalone controllers: the deck's own wins
                        if (vendor == 0x28DE && PadProduct(id) == 0x1205) {
                            decks.push_back(id);
                        }
                    }
                }
                if (makers.size() == 1) {
                    return makers.front();
                }
                return decks.size() == 1 ? decks.front() : 0;
            };
            const u32 candidate = handheld_candidate();
            // The handheld's own from this tick on: the claims below must already skip it, or
            // with externals on at launch it takes P1 and leaves it empty a tick later
            if (candidate) {
                LOG_INFO(Frontend, "console-mode: handheld {:04x}:{:04x} detected by its maker",
                         PadVendor(candidate), PadProduct(candidate));
                builtin_pad_id = candidate;
                builtin_identity = live[candidate];
                builtin_seen_first = true;
                Settings::values.builtin_pad_identity.SetValue(live[candidate]);
                builtin_warned = false;
                builtin_match_id = candidate;
                builtin_valid = true;
            } else if (live.size() == 1 && !IsControllerMaker(PadVendor(live.begin()->first)) &&
                       !is_blocked(live.begin()->first)) {
                LOG_INFO(Frontend, "console-mode: handheld {:04x}:{:04x} assumed: the only pad",
                         PadVendor(live.begin()->first), PadProduct(live.begin()->first));
                builtin_match_id = live.begin()->first;
                builtin_valid = true;
                builtin_pad_id = live.begin()->first;
                builtin_identity = live.begin()->second;
                builtin_seen_first = true;
                Settings::values.builtin_pad_identity.SetValue(live.begin()->second);
                // (A lone Steam virtual pad is the handheld's own: only unknown raw hardware is
                // noted)
                if (!builtin_warned && !IsSteamVirtualPad(live.begin()->first)) {
                    QMessageBox::information(
                        this, tr("Assign Handheld Controller"),
                        tr("Unknown handheld detected.\n\n"
                           "Careful: a less popular controller could be labeled as a handheld."));
                    builtin_warned = true;
                }
            }
            // Nothing identified: this tick works like Modern; the next looks again
        }
    }

    auto& players = Settings::values.players.GetValue();
    bool changed = false;

    // A disconnected pad keeps its reservation so a quick blip reclaims its player, for a grace
    // period only (a different controller must not leave a player dead)
    {
        static std::map<u32, std::chrono::steady_clock::time_point> pad_last_seen;
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [id, ident] : live) {
            pad_last_seen[id] = now;
        }
        for (auto it = pad_slots.begin(); it != pad_slots.end();) {
            const bool is_live = live.find(it->first) != live.end();
            const auto seen = pad_last_seen.find(it->first);
            const bool expired = !is_live && seen != pad_last_seen.end() &&
                                 now - seen->second > std::chrono::seconds{45};
            if (expired) {
                LOG_INFO(Frontend, "console-mode: reservation expired inst={} (slot {} free again)",
                         it->first, it->second);
                pad_identity.erase(it->first);
                pad_last_seen.erase(it->first);
                it = pad_slots.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    // The handheld's own pad takes no player while external controllers are around: released,
    // so they fill from player one. It claims player one again when they leave.
    if (handheld && builtin_valid && live.size() > 1) {
        for (auto it = pad_slots.begin(); it != pad_slots.end();) {
            if (it->first == builtin_match_id) {
                pad_identity.erase(it->first);
                it = pad_slots.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    // Alone, the handheld's own pad is player one: the departed externals' reservations are
    // dropped (they would push it down), and returning externals fill from player one
    if (handheld && builtin_valid && live.size() == 1) {
        for (auto it = pad_slots.begin(); it != pad_slots.end();) {
            const bool own = it->first == builtin_match_id;
            if (!own || it->second != 0) {
                if (!own) {
                    LOG_INFO(Frontend,
                             "console-mode: handheld alone, dropping reservation inst={} "
                             "(slot {})",
                             it->first, it->second);
                }
                pad_identity.erase(it->first);
                it = pad_slots.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    // The addresses of the Bluetooth controllers holding players; promises to away controllers
    // last as long as a reservation (45 seconds)
    const auto now_steady = std::chrono::steady_clock::now();
    for (const auto& [id, ident] : live) {
        if (const auto known = pad_bluetooth.find(BluetoothKey(id));
            known != pad_bluetooth.end() && !known->second.empty()) {
            // A controller just told apart: its battery is read now
            if (auto& address = inst_address[id]; address != known->second) {
                address = known->second;
                battery_read.erase(id);
            }
            if (const auto origin = pad_origin.find(id); origin != pad_origin.end()) {
                address_origin.try_emplace(known->second, origin->second);
            }
        }
    }
    std::erase_if(address_claims, [&](const auto& e) {
        return now_steady - e.second.at > std::chrono::seconds{45};
    });

    // New pads claim the lowest free slot (the handheld's own skipped while others are around),
    // in player index order where there is one
    std::set<int> used;
    for (const auto& [id, slot] : pad_slots) {
        used.insert(slot);
    }
    std::vector<u32> claim_order;
    claim_order.reserve(live.size());
    for (const auto& [id, ident] : live) {
        claim_order.push_back(id);
    }
    std::sort(claim_order.begin(), claim_order.end(), [](u32 a, u32 b) {
        const int ia = PadPlayerIndex(a);
        const int ib = PadPlayerIndex(b);
        if (ia != ib) {
            return ia >= 0 && (ib < 0 || ia < ib);
        }
        return a < b;
    });
    for (const u32 id : claim_order) {
        const std::string& ident = live.at(id);
        // The handheld's own is excluded by identity: its instance changes when it cycles
        if (handheld && builtin_valid && id == builtin_match_id && live.size() > 1) {
            continue;
        }
        // Steam's player index only orders the claims; slots fill lowest-free
        if (pad_slots.find(id) == pad_slots.end()) {
            // How many controllers are away (with several, an untold return is a guess)
            const auto away = std::count_if(pad_slots.begin(), pad_slots.end(),
                                            [&](const auto& e) { return !live.contains(e.first); });
            // A Bluetooth controller comes back to its player by address (Steam's handles don't
            // survive a reconnect)
            const std::string bt_key = BluetoothKey(id);
            const auto known = pad_bluetooth.find(bt_key);
            const std::string address =
                known != pad_bluetooth.end() ? known->second : std::string{};
            if (!address.empty()) {
                const auto own =
                    std::find_if(pad_slots.begin(), pad_slots.end(), [&](const auto& e) {
                        if (live.contains(e.first)) {
                            return false;
                        }
                        const auto held = inst_address.find(e.first);
                        return held != inst_address.end() && held->second == address;
                    });
                int slot = -1;
                int origin = -1;
                if (own != pad_slots.end()) {
                    slot = own->second;
                    const auto from = pad_origin.find(own->first);
                    origin = from != pad_origin.end() ? from->second : slot;
                    pad_identity.erase(own->first);
                    pad_slots.erase(own);
                } else if (const auto claim = address_claims.find(address);
                           claim != address_claims.end() && !used.contains(claim->second.slot)) {
                    // (Its player was taken by a guess, then given back)
                    slot = claim->second.slot;
                    origin = claim->second.origin;
                    address_claims.erase(claim);
                }
                if (slot >= 0) {
                    LOG_INFO(Frontend, "console-mode: {} back as P{} by its Bluetooth address",
                             address, slot + 1);
                    pad_slots[id] = slot;
                    pad_identity[id] = ident;
                    pad_origin[id] = origin;
                    used.insert(slot);
                    changed = true;
                    continue;
                }
            } else if (pad_bt_pending.contains(bt_key) && away > 1) {
                // Its address is about to be known: wait a moment rather than guess
                const auto since = claim_wait_since.try_emplace(id, now_steady).first->second;
                if (now_steady - since < std::chrono::seconds{3}) {
                    continue;
                }
            }
            // Reclaim a reservation with a matching identity. Identical pads share one, so an
            // ambiguous pad takes the lowest of them (one the game is paused for first), or a
            // lower free slot
            const auto before = [&](int slot, int other) {
                const bool waited = disconnect_paused.contains(slot);
                if (waited != disconnect_paused.contains(other)) {
                    return waited;
                }
                return slot < other;
            };
            auto match = pad_slots.end();
            int matches = 0;
            for (auto it = pad_slots.begin(); it != pad_slots.end(); ++it) {
                if (live.find(it->first) == live.end() && pad_identity[it->first] == ident) {
                    if (match == pad_slots.end() || before(it->second, match->second)) {
                        match = it;
                    }
                    ++matches;
                }
            }
            if (matches == 0 && Common::SteamPadsInUse()) {
                // A new Steam handle on return: no reservation is provably its own, the lowest
                // is taken
                for (auto it = pad_slots.begin(); it != pad_slots.end(); ++it) {
                    if (live.find(it->first) == live.end() &&
                        (match == pad_slots.end() || before(it->second, match->second))) {
                        match = it;
                    }
                }
                if (match != pad_slots.end()) {
                    matches = 2;
                }
            }
            if (matches > 1) {
                int lowest_free = -1;
                for (int slot = 0; slot < ConsolePlayers; ++slot) {
                    if (used.find(slot) == used.end()) {
                        lowest_free = slot;
                        break;
                    }
                }
                if (lowest_free < 0 || match->second < lowest_free ||
                    disconnect_paused.contains(match->second)) {
                    LOG_INFO(Frontend,
                             "console-mode: ambiguous reclaim inst={} ident={} ({} candidates) "
                             "takes the lowest, slot {} from dead inst={}",
                             id, ident, matches, match->second, match->first);
                    matches = 1;
                } else {
                    match = pad_slots.end();
                    matches = 0;
                }
            }
            if (matches == 1) {
                // A guess (several away): marked (?) until its address proves it. The player
                // stays promised to its controller's address
                const bool guessed = away > 1;
                LOG_INFO(Frontend,
                         "console-mode: reclaim inst={} ident={} takes slot {} from dead inst={}{}",
                         id, ident, match->second, match->first, guessed ? " (a guess)" : "");
                const auto from = pad_origin.find(match->first);
                pad_origin[id] = from != pad_origin.end() ? from->second : match->second;
                if (guessed) {
                    pad_guessed.insert(id);
                    if (const auto held = inst_address.find(match->first);
                        held != inst_address.end()) {
                        address_claims[held->second] = {match->second, pad_origin[id], now_steady};
                    }
                }
                // Its own set comes back at once (kept by the address, learned only later)
                if (const auto held = inst_address.find(match->first);
                    !guessed && held != inst_address.end() && !pad_sets.contains(ident)) {
                    if (const auto set = pad_sets.find("bt:" + held->second);
                        set != pad_sets.end()) {
                        pad_sets[ident] = set->second;
                        if (Common::ControllerProbes()) {
                            LOG_INFO(Frontend, "console-mode: inst={} carries its own P{} set back",
                                     id, set->second + 1);
                        }
                    }
                }
                pad_slots[id] = match->second;
                pad_identity[id] = ident;
                pad_identity.erase(match->first);
                pad_slots.erase(match);
                changed = true;
                continue;
            }
            // Claim the lowest free slot
            bool assigned = false;
            for (int slot = 0; slot < ConsolePlayers && !assigned; ++slot) {
                if (used.find(slot) == used.end()) {
                    LOG_INFO(Frontend, "console-mode: claim inst={} ident={} -> slot {}", id, ident,
                             slot);
                    pad_slots[id] = slot;
                    pad_identity[id] = ident;
                    // (A known Bluetooth controller keeps the player it started as)
                    const auto first = address_origin.find(address);
                    pad_origin[id] = first != address_origin.end() ? first->second : slot;
                    used.insert(slot);
                    assigned = true;
                    changed = true;
                }
            }
            if (!assigned) {
                // Retire reservations and retry, never a live pad's slot
                for (auto it = pad_slots.begin(); it != pad_slots.end();) {
                    const bool live_pad = live.find(it->first) != live.end();
                    if (!live_pad) {
                        pad_identity.erase(it->first);
                        it = pad_slots.erase(it);
                    } else {
                        ++it;
                    }
                }
                used.clear();
                for (const auto& [k, slot] : pad_slots) {
                    used.insert(slot);
                }
                for (int slot = 0; slot < ConsolePlayers; ++slot) {
                    if (used.find(slot) == used.end()) {
                        LOG_INFO(Frontend, "console-mode: claim inst={} ident={} -> slot {}", id,
                                 ident, slot);
                        pad_slots[id] = slot;
                        pad_identity[id] = ident;
                        pad_origin[id] = slot;
                        used.insert(slot);
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    // What is kept per connection lasts while the pad is live or its player is held for it
    {
        const auto kept = [&](u32 id) { return live.contains(id) || pad_slots.contains(id); };
        std::erase_if(inst_address, [&](const auto& e) { return !kept(e.first); });
        std::erase_if(pad_origin, [&](const auto& e) { return !kept(e.first); });
        std::erase_if(pad_guessed, [&](u32 id) { return !kept(id); });
        std::erase_if(claim_wait_since, [&](const auto& e) {
            return !live.contains(e.first) || pad_slots.contains(e.first);
        });
    }

    // A guessed controller whose address became known stays, or moves to its real player
    for (auto it = pad_guessed.begin(); it != pad_guessed.end();) {
        const u32 id = *it;
        const auto known = pad_bluetooth.find(BluetoothKey(id));
        const auto at = pad_slots.find(id);
        if (!live.contains(id) || at == pad_slots.end() || known == pad_bluetooth.end() ||
            known->second.empty()) {
            ++it;
            continue;
        }
        const std::string& address = known->second;
        const auto own = std::find_if(pad_slots.begin(), pad_slots.end(), [&](const auto& e) {
            const auto held = inst_address.find(e.first);
            return !live.contains(e.first) && held != inst_address.end() && held->second == address;
        });
        if (own != pad_slots.end()) {
            LOG_INFO(Frontend, "console-mode: {} was guessed as P{}: moved back to P{}", address,
                     at->second + 1, own->second + 1);
            const auto from = pad_origin.find(own->first);
            pad_origin[id] = from != pad_origin.end() ? from->second : own->second;
            at->second = own->second;
            pad_identity.erase(own->first);
            pad_slots.erase(own);
            changed = true;
        } else {
            address_claims.erase(address); // (the guess was right)
        }
        it = pad_guessed.erase(it);
    }

    // Swaps from the settings or the overlay: the pads trade slots, for this session
    {
        std::vector<std::pair<int, int>> requests;
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            requests.swap(state.swap_requests);
        }
        for (const auto& [a, b] : requests) {
            if (a < 0 || b < 0 || a >= ConsolePlayers || b >= ConsolePlayers || a == b) {
                continue;
            }
            u32 pad_a = 0;
            u32 pad_b = 0;
            for (const auto& [id, slot] : pad_slots) {
                if (live.find(id) == live.end()) {
                    continue;
                }
                if (slot == a) {
                    pad_a = id;
                }
                if (slot == b) {
                    pad_b = id;
                }
            }
            if (!pad_a && !pad_b) {
                continue;
            }
            // Everything on the two players changes places, held players too (else a returning
            // controller could find its player taken)
            for (auto& [id, slot] : pad_slots) {
                if (slot == a) {
                    slot = b;
                } else if (slot == b) {
                    slot = a;
                }
            }
            // (A swap is the user's say: a guess is settled)
            pad_guessed.erase(pad_a);
            pad_guessed.erase(pad_b);
            LOG_INFO(Frontend, "console-mode: swap P{} <-> P{} (inst={} inst={})", a + 1, b + 1,
                     pad_a, pad_b);
            changed = true;
        }
    }

    // Profiles picked in the Controls page go on the set the player's controller carries
    {
        std::vector<Common::ControllerConsoleState::ProfileRequest> picks;
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            picks.swap(state.profile_requests);
        }
        std::optional<InputProfiles> input_profiles;
        for (const auto& [player, name, restore] : picks) {
            if (!console_profile || player < 0 || player >= static_cast<int>(players.size())) {
                continue;
            }
            const int set = slot_set[player];
            const bool game_set = game_profiles && !(*game_profiles)[set].profile_name.empty();
            auto& target = SessionSet(set);
            if (name.empty() && restore) {
                if (const auto before = set_before_profile.find(set);
                    before != set_before_profile.end()) {
                    target.buttons = before->second.buttons;
                    target.analogs = before->second.analogs;
                    target.motions = before->second.motions;
                    set_before_profile.erase(before);
                }
            }
            if (!name.empty()) {
                set_before_profile.try_emplace(set, target);
                if (!input_profiles) {
                    input_profiles.emplace();
                }
                const auto read = input_profiles->PeekProfile(name, player);
                if (!read) {
                    LOG_WARNING(Frontend, "console-mode: input profile \"{}\" not found", name);
                    continue;
                }
                ApplyProfileMappings(target, *read);
            }
            target.profile_name = name;
            // The seat remembers it: the next session's P{player} starts with it
            SetSeatProfile(player, name);
            LOG_INFO(Frontend, "console-mode: P{} input profile \"{}\" on the P{} set{}",
                     player + 1, name, set + 1, game_set ? " (the game's)" : "");
            changed = true;
        }
    }

    // Rumble requested from the Controllers tab: a short pulse on the player's controller
    {
        std::vector<int> rumbles;
        {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            rumbles.swap(state.rumble_requests);
        }
        for (const int player : rumbles) {
            for (const auto& [id, slot] : pad_slots) {
                if (slot != player || live.find(id) == live.end()) {
                    continue;
                }
                // Sent twice: a single request through Steam's virtual pads is sometimes lost
                const u32 pad = id;
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: rumble P{} inst={}", player + 1, pad);
                }
                PadRumble(pad, 0xC000, 300);
                QTimer::singleShot(150, this, [pad] { PadRumble(pad, 0xC000, 200); });
                break;
            }
        }
    }

    // Each live pad's player gets the pad's binding set, pointed at the pad (bindings address
    // pads by guid and port, which follow attach order, so the slot map alone isn't the
    // routing). The handheld's own pad carries player one's set; an external pad its own set,
    // without the handheld's non-SDL motion, plus its SDL gyro when the set has no motion.
    slot_set = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    // Use Any Controller as Joy-Cons: a game that doesn't take Pro Controllers gets Dual
    // Joy-Cons, else a single one held sideways. Only a Pro Controller player is changed (and
    // put back), never a type set by hand.
    {
        std::optional<Core::HID::NpadStyleTag> supported;
        bool horizontal = true;
        if (QtCommon::system && QtCommon::system->IsPoweredOn() &&
            Settings::values.console_joycon_auto.GetValue()) {
            supported = QtCommon::system->HIDCore().GetSupportedStyleTag();
            horizontal = QtCommon::system->HIDCore().IsJoyHoldHorizontal();
        }
        for (int i = 0; i < static_cast<int>(players.size()) && i < 8; ++i) {
            auto& player = players[i];
            auto& from = joycon_auto_from[static_cast<std::size_t>(i)];
            auto& set = joycon_auto_set[static_cast<std::size_t>(i)];
            if (set && player.controller_type != *set) {
                from.reset(); // (changed by hand since)
                set.reset();
            }
            const auto base = from ? *from : player.controller_type;
            if (base != Settings::ControllerType::ProController) {
                continue;
            }
            auto want = base;
            if (supported && player.connected && supported->raw != Core::HID::NpadStyleSet::None &&
                !supported->fullkey) {
                if (supported->joycon_dual) {
                    want = Settings::ControllerType::DualJoyconDetached;
                } else if (supported->joycon_right) {
                    want = Settings::ControllerType::RightJoycon;
                } else if (supported->joycon_left) {
                    want = Settings::ControllerType::LeftJoycon;
                }
            }
            // A single Joy-Con is held as the game holds it: sideways or upright
            const bool single = want == Settings::ControllerType::RightJoycon ||
                                want == Settings::ControllerType::LeftJoycon;
            const bool sideways = single && horizontal;
            auto& was_sideways = joycon_auto_sideways[static_cast<std::size_t>(i)];
            const auto say = [&](const QString& note) {
                if (player.connected) {
                    controller_hints.push_back(
                        {QString{}, i, true, QDateTime::currentMSecsSinceEpoch(), false, note});
                }
            };
            if (want == player.controller_type) {
                // (The game turned its Joy-Cons: the bindings follow on the routing pass)
                if (set && single && was_sideways != sideways) {
                    was_sideways = sideways;
                    LOG_INFO(Frontend, "console-mode: P{} now holds its Joy-Con {}", i + 1,
                             sideways ? "sideways" : "upright");
                    say(sideways ? tr("P%1 plays as a sideways Joy-Con").arg(i + 1)
                                 : tr("P%1 plays as an upright Joy-Con").arg(i + 1));
                }
                continue;
            }
            player.controller_type = want;
            changed = true;
            if (want == base) {
                from.reset();
                set.reset();
                was_sideways = false;
                LOG_INFO(Frontend, "console-mode: P{} back to a Pro Controller", i + 1);
                say(tr("P%1 plays as a Pro Controller again").arg(i + 1));
                continue;
            }
            from = base;
            set = want;
            was_sideways = sideways;
            LOG_INFO(Frontend, "console-mode: P{} plays as {} (the game takes no Pro Controller)",
                     i + 1,
                     !single    ? "Dual Joy-Cons"
                     : sideways ? "a sideways Joy-Con"
                                : "an upright Joy-Con");
            say(!single    ? tr("P%1 plays as Dual Joy-Cons").arg(i + 1)
                : sideways ? tr("P%1 plays as a sideways Joy-Con").arg(i + 1)
                           : tr("P%1 plays as an upright Joy-Con").arg(i + 1));
        }
        // (For the player tabs: a single Joy-Con's mapping is the engine's)
        std::array<int, 10> held{};
        for (std::size_t i = 0; i < held.size(); ++i) {
            const auto& set = joycon_auto_set[i];
            if (set && (*set == Settings::ControllerType::RightJoycon ||
                        *set == Settings::ControllerType::LeftJoycon)) {
                held[i] = joycon_auto_sideways[i] ? 1 : 2;
            }
        }
        auto& state = Common::GetControllerConsoleState();
        std::scoped_lock lock{state.mutex};
        state.joycon_held = held;
    }

    if (input_subsystem && console_profile) {
        const auto profile = ConsoleBindingSets();
        const auto is_handheld_motion = [&profile](const std::string& raw) {
            if (raw.empty() || Common::ParamPackage{raw}.Get("engine", "") == "sdl") {
                return false;
            }
            return std::find(profile[0].motions.begin(), profile[0].motions.end(), raw) !=
                   profile[0].motions.end();
        };
        for (const auto& [id, ident] : live) {
            const auto it = pad_slots.find(id);
            if (it == pad_slots.end() || it->second < 0 ||
                it->second >= static_cast<int>(players.size())) {
                continue;
            }
            const auto binding = input_subsystem->GetSDLBinding(id);
            if (!binding) {
                continue;
            }
            const int slot = it->second;
            const bool is_builtin = builtin_valid && id == builtin_match_id;
            // The set's key: a Bluetooth controller's address (so its set follows it through
            // reconnects), else its identity; a set made before the address is known moves to it
            const auto address = inst_address.find(id);
            const std::string set_key =
                address != inst_address.end() ? "bt:" + address->second : ident;
            if (set_key != ident && !pad_sets.contains(set_key) && pad_sets.contains(ident)) {
                pad_sets[set_key] = pad_sets[ident];
                pad_sets.erase(ident);
            } else if (set_key != ident && pad_sets.contains(set_key) &&
                       pad_sets.contains(ident) && pad_sets[ident] == pad_sets[set_key]) {
                // (Lent to it on a reclaim, until its address was known)
                pad_sets.erase(ident);
            }
            // A controller keeps the set it first got: its slot's, else the first set nobody has
            if (!is_builtin && !pad_sets.contains(set_key)) {
                std::set<int> taken;
                for (const auto& [other, set] : pad_sets) {
                    taken.insert(set);
                }
                int set = slot;
                for (int k = 0; taken.contains(set) && k < static_cast<int>(players.size()); ++k) {
                    set = k;
                }
                if (taken.contains(set)) {
                    set = slot;
                }
                pad_sets[set_key] = set;
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: inst={} carries the P{} set", id, set + 1);
                }
            }
            // A set with no controller mappings gives the pad its default mapping, below
            const auto has_sdl = [](const Settings::PlayerInput& p) {
                return std::any_of(p.buttons.begin(), p.buttons.end(), [](const std::string& b) {
                    return !b.empty() && Common::ParamPackage{b}.Get("engine", "") == "sdl";
                });
            };
            const int set_index = is_builtin ? 0 : pad_sets[set_key];
            slot_set[slot] = set_index;
            Settings::PlayerInput want = players[slot];
            // A set holding only a gone controller's default mapping is rebuilt for this one (an
            // Xbox pad's has no gyro or back buttons). Not while that controller is connected:
            // identical ones without an address share a set.
            const u32 defaults_of = set_default_of[static_cast<std::size_t>(set_index)];
            const bool others_defaults = defaults_of != 0 && defaults_of != id &&
                                         !live.contains(defaults_of) &&
                                         profile[set_index].profile_name.empty();
            if (others_defaults && Common::ControllerProbes()) {
                LOG_INFO(Frontend,
                         "console-mode: P{} set holds inst={}'s defaults: rebuilt for inst={}",
                         set_index + 1, defaults_of, id);
            }
            if (has_sdl(profile[set_index]) && !others_defaults) {
                const auto& own = profile[set_index];
                want.buttons = own.buttons;
                want.analogs = own.analogs;
                want.motions = own.motions;
                want.profile_name = own.profile_name;
                RouteSdlBindings(want, binding->first, binding->second);
            } else {
                // No controller mappings: the controller's default mapping, for this session
                Common::ParamPackage device;
                device.Set("engine", "sdl");
                device.Set("guid", binding->first);
                device.Set("port", binding->second);
                // A DSU server's motion stays; only SDL motion is the device's to fill
                const auto kept_motions = profile[set_index].motions;
                const bool motion_kept =
                    std::any_of(kept_motions.begin(), kept_motions.end(), [](const auto& m) {
                        return !m.empty() &&
                               Common::ParamPackage{m}.Get("engine", "") == "cemuhookudp";
                    });
                want.buttons.fill({});
                want.analogs.fill({});
                want.motions.fill({});
                for (const auto& [index, param] :
                     input_subsystem->GetButtonMappingForDevice(device)) {
                    want.buttons[index] = param.Serialize();
                }
                for (const auto& [index, param] :
                     input_subsystem->GetAnalogMappingForDevice(device)) {
                    want.analogs[index] = param.Serialize();
                }
                if (motion_kept) {
                    want.motions = kept_motions;
                } else {
                    for (const auto& [index, param] :
                         input_subsystem->GetMotionMappingForDevice(device)) {
                        want.motions[index] = param.Serialize();
                    }
                }
                want.profile_name.clear();
                // Kept for the session: rebuilt every tick, it would differ once a game polls the
                // sensors and reload the input four times a second
                auto& own = SessionSet(set_index);
                own.buttons = want.buttons;
                own.analogs = want.analogs;
                own.motions = want.motions;
                set_default_of[static_cast<std::size_t>(set_index)] = id;
                static std::set<u32> defaulted;
                if (defaulted.insert(id).second) {
                    LOG_INFO(Frontend,
                             "console-mode: P{} set has no controller mappings: inst={} "
                             "gets its default mapping{}",
                             set_index + 1, id, motion_kept ? " (its set's motion kept)" : "");
                }
            }
            // Under Steam the virtual pad has no motion: take the paired real device's (the Deck,
            // or the only one of a model)
            u32 motion_source = 0;
            if (Common::SteamPadsInUse()) {
                const Uint16 vendor = is_builtin ? 0x28DE : PadVendor(id);
                const Uint16 product = is_builtin ? 0x1205 : PadProduct(id);
                const auto same_model = [vendor, product](u32 other) {
                    return PadVendor(other) == vendor &&
                           PadProduct(other) == product;
                };
                const auto real =
                    std::count_if(motion_devices.begin(), motion_devices.end(), same_model);
                const auto virtuals = std::count_if(
                    live.begin(), live.end(), [&](const auto& e) { return same_model(e.first); });
                if (real == 1 && (is_builtin || virtuals == 1)) {
                    const u32 device =
                        *std::find_if(motion_devices.begin(), motion_devices.end(), same_model);
                    if (PadHasGyro(device)) {
                        motion_source = device;
                    }
                }
            }
            if (motion_source != overlay_motion_source[slot]) {
                overlay_motion_source[slot] = motion_source;
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: P{} motion from real device inst={}",
                             slot + 1, motion_source);
                }
            }
            if (motion_source) {
                if (const auto source = input_subsystem->GetSDLBinding(motion_source)) {
                    want.motions.fill(SdlMotionParam(source->first, source->second));
                }
            } else {
                if (!is_builtin) {
                    for (std::string& motion : want.motions) {
                        if (is_handheld_motion(motion)) {
                            motion.clear();
                        }
                    }
                }
                // (The handheld's own pad too: its set may have been an external's)
                const bool no_motion = std::all_of(want.motions.begin(), want.motions.end(),
                                                   [](const std::string& m) { return m.empty(); });
                if (no_motion && PadHasGyro(id)) {
                    want.motions.fill(SdlMotionParam(binding->first, binding->second));
                }
            }
            auto& player = players[slot];
            // A single Joy-Con (set above): the buttons and stick arranged to fit, sideways or
            // upright as the game holds it
            const bool single =
                joycon_auto_set[static_cast<std::size_t>(slot)] &&
                (player.controller_type == Settings::ControllerType::RightJoycon ||
                 player.controller_type == Settings::ControllerType::LeftJoycon);
            const bool right_joycon =
                player.controller_type == Settings::ControllerType::RightJoycon;
            if (single && joycon_auto_sideways[static_cast<std::size_t>(slot)]) {
                BindSidewaysJoyCon(want, right_joycon);
            } else {
                if (Settings::values.console_button_labels.GetValue() && !IsNintendoPad(id)) {
                    // Button Layout by label: the controller's A is the Switch's A
                    using namespace Settings::NativeButton;
                    std::swap(want.buttons[A], want.buttons[B]);
                    std::swap(want.buttons[X], want.buttons[Y]);
                }
                if (single) {
                    BindUprightJoyCon(want, right_joycon);
                }
            }
            player.profile_name = want.profile_name;
            if (player.buttons != want.buttons || player.analogs != want.analogs ||
                player.motions != want.motions) {
                player.buttons = want.buttons;
                player.analogs = want.analogs;
                player.motions = want.motions;
                if (Common::ControllerProbes()) {
                    LOG_INFO(Frontend, "console-mode: P{} <- inst={} guid={} port={} set=P{}{}",
                             slot + 1, id, binding->first, binding->second, set_index + 1,
                             is_builtin ? " (handheld)" : "");
                }
                changed = true;
            }
        }
    }

    // Only the players with a live pad are connected
    for (int i = 0; i < static_cast<int>(players.size()); ++i) {
        bool want = false;
        for (const auto& [id, ident] : live) {
            const auto it = pad_slots.find(id);
            if (it != pad_slots.end() && it->second == i) {
                want = true;
                break;
            }
        }
        if (players[i].connected != want) {
            players[i].connected = want;
            changed = true;
        }
    }

    // A game running with no controller is told so once (keyboard and mouse are Manual's)
    {
        const bool running = QtCommon::system && QtCommon::system->IsPoweredOn();
        const bool any_player = std::any_of(players.begin(), players.end(),
                                            [](const auto& player) { return player.connected; });
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        if (!running) {
            no_controller_told = false;
            no_controller_since = 0;
        } else if (any_player) {
            no_controller_since = 0;
        } else if (!no_controller_told) {
            if (no_controller_since == 0) {
                no_controller_since = now_ms;
            } else if (now_ms - no_controller_since >= 3000) {
                no_controller_told = true;
                controller_hints.push_back(
                    {QString{}, 0, false, now_ms, false,
                     tr("No controller: connect one, or use Manual for keyboard and mouse")});
                LOG_INFO(Frontend, "console-mode: a game runs with no controller");
            }
        }
    }

    if (changed) {
        std::string out;
        for (int i = 0; i < static_cast<int>(players.size()); ++i) {
            out += players[i].connected ? "1" : "0";
        }

        static std::string last_written;
        if (out != last_written) {
            for (size_t i = 0; i < out.size(); ++i) {
                if (last_written.size() != out.size() || last_written[i] != out[i]) {
                    if (Common::ControllerProbes()) {
                        LOG_INFO(Frontend, "console-mode: player {} {}", i + 1,
                                 out[i] == '1' ? "activated" : "deactivated");
                    }
                }
            }
            last_written = out;
        }
        if (Common::ControllerProbes()) {
            LOG_INFO(Frontend, "console-mode: players written={}", out);
        }
    }

    // A live pad holds at most one slot, in range, never shared: if this logs, it's a bug
    {
        std::set<int> seen_slots;
        for (const auto& [id, slot] : pad_slots) {
            const bool in_range = slot >= 0 && slot < ConsolePlayers;
            const bool unique = in_range && seen_slots.insert(slot).second;
            if (live.find(id) != live.end() && (!in_range || !unique)) {
                LOG_ERROR(Frontend,
                          "console-mode: INVARIANT BROKEN inst={} slot={} in_range={} "
                          "unique={} - two pads fighting over one player or a corrupt slot",
                          id, slot, in_range, unique);
            }
        }
    }

    // Pause When a Controller Disconnects: a player whose controller left and didn't come back
    // within 3 seconds pauses the game; it resumes when they all have a controller again, or
    // with + on another controller (those players are then let go)
    {
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < static_cast<int>(players.size()) && i < 10; ++i) {
            const bool held = std::any_of(pad_slots.begin(), pad_slots.end(),
                                          [&](const auto& e) { return e.second == i; });
            const bool here = std::any_of(pad_slots.begin(), pad_slots.end(), [&](const auto& e) {
                return e.second == i && live.contains(e.first);
            });
            auto& since = waiting_since[static_cast<std::size_t>(i)];
            since = held && !here ? (since ? since : now_ms) : 0;
            if (since == 0) {
                pause_done.erase(i); // (a new wait may pause again)
            }
        }
        // Who holds each player now (0 nobody), and whether that is an external controller
        std::array<u32, 10> holder{};
        for (const auto& [id, slot] : pad_slots) {
            if (slot >= 0 && slot < 10 && live.contains(id)) {
                holder[static_cast<std::size_t>(slot)] = id;
            }
        }
        const auto on_handheld = [&](u32 id) {
            return id != 0 && builtin_valid && id == builtin_match_id;
        };
        const auto was_external = held_by_external;
        for (std::size_t i = 0; i < holder.size(); ++i) {
            held_by_external[i] = holder[i] != 0 && !on_handheld(holder[i]);
        }
        const bool handheld_took_over =
            !std::exchange(handheld_was_playing,
                           std::any_of(holder.begin(), holder.end(), on_handheld)) &&
            handheld_was_playing;
        const bool running =
            emulation_running && QtCommon::emu_thread && QtCommon::emu_thread->IsRunning();
        // (Pausing takes a moment: running again counts as a resume by hand only once seen paused)
        if (!disconnect_paused.empty() && !running && !disconnect_pause_pending) {
            disconnect_pause_seen = true;
        }
        if (!disconnect_paused.empty() && running && disconnect_pause_seen) {
            disconnect_paused.clear(); // (resumed by hand)
            disconnect_continue_held = false;
            disconnect_pause_seen = false;
            handover_paused = -1;
            handover_pressed = false;
        }
        // The overlay is drawn into frames: the notice first, the pause a moment later
        const auto pause_with_notice = [&](int player, const QString& notice) {
            controller_hints.push_back({QString{}, player, false, now_ms, false, notice, true});
            RenderControllerOverlay();
            disconnect_pause_pending = true;
            disconnect_pause_seen = false;
            QTimer::singleShot(150, this, [this] {
                disconnect_pause_pending = false;
                if (!disconnect_paused.empty() && emulation_running && QtCommon::emu_thread &&
                    QtCommon::emu_thread->IsRunning()) {
                    OnPauseGame();
                }
            });
        };
        if (Settings::values.console_pause_disconnect.GetValue() && running &&
            !disconnect_pause_pending) {
            // A disconnect that is surely real (Turn Off, or BlueZ says disconnected) pauses at
            // once; any other waits 3 seconds, as Steam briefly drops its pads now and then
            const auto surely_gone = [&](int player) {
                if (now_ms - turned_off_at[static_cast<std::size_t>(player)] < 5000) {
                    return true;
                }
#ifdef __linux__
                // (BlueZ is asked without waiting, at most every half second; an answer counts if
                // it came after the controller left)
                for (const auto& [id, slot] : pad_slots) {
                    if (slot != player || live.contains(id)) {
                        continue;
                    }
                    const auto address = inst_address.find(id);
                    if (address == inst_address.end()) {
                        continue;
                    }
                    auto& known = bluez_connected[address->second];
                    if (known.asked_ms == 0 || now_ms - known.asked_ms >= 500) {
                        known.asked_ms = now_ms;
                        AskBluezConnected(address->second, this,
                                          [this, key = address->second](bool on) {
                                              auto& entry = bluez_connected[key];
                                              entry.connected = on;
                                              entry.answered_ms =
                                                  QDateTime::currentMSecsSinceEpoch();
                                          });
                    }
                    if (!known.connected && known.answered_ms >=
                                                waiting_since[static_cast<std::size_t>(player)]) {
                        return true;
                    }
                }
#endif
                return false;
            };
            std::set<int> gone;
            for (int i = 0; i < 10; ++i) {
                // (Each wait pauses once: resumed by hand, the game keeps running)
                const qint64 since = waiting_since[static_cast<std::size_t>(i)];
                if (since && !pause_done.contains(i) &&
                    (now_ms - since >= 3000 || surely_gone(i))) {
                    gone.insert(i);
                }
            }
            // A handover: the last external controller left (whichever player it held) and the
            // handheld's controls took over. Paused until someone picks the handheld up or
            // reconnects.
            int handover = -1;
            const bool externals_left =
                std::any_of(was_external.begin(), was_external.end(), [](bool b) { return b; }) &&
                std::none_of(held_by_external.begin(), held_by_external.end(),
                             [](bool b) { return b; });
            if (gone.empty() && disconnect_paused.empty() && externals_left &&
                handheld_took_over) {
                for (std::size_t i = 0; i < holder.size() && handover < 0; ++i) {
                    if (on_handheld(holder[i])) {
                        handover = static_cast<int>(i);
                    }
                }
            }
            if (handover >= 0) {
                disconnect_paused = {handover};
                handover_paused = handover;
                handover_pressed = PadInputHeld(holder[static_cast<std::size_t>(handover)]);
                LOG_INFO(Frontend, "console-mode: paused, P{} handed over to the built-in controls",
                         handover + 1);
                pause_with_notice(handover,
                                  tr("P%1 is on the built-in controls now: press a button on "
                                     "them, or reconnect a controller, to go on")
                                      .arg(handover + 1));
            } else if (!gone.empty()) {
                disconnect_paused = gone;
                pause_done.insert(gone.begin(), gone.end());
                LOG_INFO(Frontend, "console-mode: paused, P{}'s controller disconnected",
                         *gone.begin() + 1);
                QStringList names;
                for (const int player : gone) {
                    names.push_back(QStringLiteral("P%1").arg(player + 1));
                }
                const QString who = names.join(QStringLiteral(", "));
                // (Said once: the pause's notice takes the place of their "left" hints)
                std::erase_if(controller_hints, [&](const auto& hint) {
                    return !hint.joined && !hint.pause_notice && gone.contains(hint.player);
                });
                // (The leave is stamped a moment later, when the overlay draws it: a margin)
                for (const int player : gone) {
                    auto& said = hinted_leave[static_cast<std::size_t>(player)];
                    said = (std::max)(said, now_ms + 2'000);
                }
                // (With no other controller left, reconnecting is the only way on)
                pause_with_notice(*gone.begin(),
                                  live.empty()
                                      ? tr("%1 disconnected: reconnect to go on").arg(who)
                                      : tr("%1 disconnected: reconnect, or press + on another "
                                           "controller to go on without %1")
                                            .arg(who));
            }
        } else if (!disconnect_paused.empty() && !running && emulation_running &&
                   !disconnect_pause_pending) {
            // Paused for a disconnect when the last external controller left too: the handheld's
            // controls took a player, and the pause becomes a handover (not a resume)
            if (handover_paused < 0 && handheld_took_over &&
                std::none_of(held_by_external.begin(), held_by_external.end(),
                             [](bool b) { return b; })) {
                for (std::size_t i = 0; i < holder.size(); ++i) {
                    if (on_handheld(holder[i])) {
                        handover_paused = static_cast<int>(i);
                        handover_pressed = PadInputHeld(holder[i]);
                        disconnect_paused = {handover_paused};
                        LOG_INFO(Frontend,
                                 "console-mode: pause handed over to the built-in controls (P{})",
                                 i + 1);
                        // (The disconnect's notice gives way; running a moment, so the new
                        // one is drawn into a frame)
                        std::erase_if(controller_hints,
                                      [](const auto& hint) { return hint.pause_notice; });
                        OnStartGame();
                        pause_with_notice(handover_paused,
                                          tr("P%1 is on the built-in controls now: press a button "
                                             "on them, or reconnect a controller, to go on")
                                              .arg(handover_paused + 1));
                        break;
                    }
                }
            }
            bool all_back =
                std::all_of(disconnect_paused.begin(), disconnect_paused.end(), [&](int player) {
                    return std::any_of(pad_slots.begin(), pad_slots.end(), [&](const auto& e) {
                        return e.second == player && live.contains(e.first);
                    });
                });
            // A handover goes on when an external controller takes the player back, or at a
            // press on the handheld (once let go, so the game never sees it)
            if (handover_paused >= 0) {
                const u32 at = holder[static_cast<std::size_t>(handover_paused)];
                all_back = at != 0 && !on_handheld(at);
                if (on_handheld(at)) {
                    if (PadInputHeld(at)) {
                        handover_pressed = true;
                    } else if (std::exchange(handover_pressed, false)) {
                        all_back = true;
                    }
                }
            }
            // (+ counts once let go, so the game never sees it on resuming)
            const bool plus = std::any_of(live.begin(), live.end(),
                                          [](const auto& e) { return PadStartHeld(e.first); });
            if (all_back) {
                LOG_INFO(Frontend, "console-mode: resumed, the controllers are back");
                disconnect_paused.clear();
                disconnect_continue_held = false;
                handover_paused = -1;
                OnStartGame();
            } else if (plus) {
                disconnect_continue_held = true;
            } else if (std::exchange(disconnect_continue_held, false)) {
                // On without them: their players are no longer held for their controllers
                for (auto it = pad_slots.begin(); it != pad_slots.end();) {
                    if (disconnect_paused.contains(it->second) && !live.contains(it->first)) {
                        pad_identity.erase(it->first);
                        it = pad_slots.erase(it);
                    } else {
                        ++it;
                    }
                }
                QStringList names;
                for (const int player : disconnect_paused) {
                    names.push_back(QStringLiteral("P%1").arg(player + 1));
                }
                LOG_INFO(Frontend, "console-mode: resumed without {}",
                         names.join(QStringLiteral(", ")).toStdString());
                controller_hints.push_back({QString{}, *disconnect_paused.begin(), false, now_ms,
                                            false,
                                            tr("Going on without %1")
                                                .arg(names.join(QStringLiteral(", ")))});
                disconnect_paused.clear();
                handover_paused = -1;
                handover_pressed = false;
                OnStartGame();
            }
        }
    }

    // Player Lights: each controller shows its player in the player's colour (not under Steam,
    // which sets them)
    if (Settings::values.console_player_lights.GetValue() &&
        !(Common::RunningUnderSteam() && Common::GetControllerConsoleState().steam_pads_present)) {
        std::erase_if(lights_player, [&](const auto& e) { return !live.contains(e.first); });
        for (const auto& [id, slot] : pad_slots) {
            if (!live.contains(id) || IsSteamVirtualPad(id)) {
                continue;
            }
            auto& shown = lights_player[id];
            if (shown == slot + 1) {
                continue;
            }
            shown = slot + 1;
            const JoystickApiLock lock;
            if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
                SDL_SetGamepadPlayerIndex(pad, slot);
                const QColor color = ConfigurePlayerAssignment::PlayerColor(slot);
                SDL_SetGamepadLED(pad, static_cast<Uint8>(color.red()),
                                  static_cast<Uint8>(color.green()),
                                  static_cast<Uint8>(color.blue()));
                SDL_CloseGamepad(pad);
            }
            if (Common::ControllerProbes()) {
                LOG_INFO(Frontend, "console-mode: inst={} lights show P{}", id, slot + 1);
            }
        }
    }

    // A player whose controller type the game doesn't support: logged once per change
    if (QtCommon::system && QtCommon::system->IsPoweredOn()) {
        auto& hid = QtCommon::system->HIDCore();
        const auto supported = hid.GetSupportedStyleTag();
        for (int i = 0; i < static_cast<int>(players.size()) && i < 8; ++i) {
            // (A handheld P1 is the handheld's own controller, as the game sees it)
            const auto* controller =
                i == 0 && players[0].controller_type == Settings::ControllerType::Handheld
                    ? hid.GetEmulatedController(Core::HID::NpadIdType::Handheld)
                    : hid.GetEmulatedControllerByIndex(static_cast<std::size_t>(i));
            if (!controller || !players[i].connected) {
                refused_logged[static_cast<std::size_t>(i)].clear();
                continue;
            }
            const auto style = controller->GetNpadStyleIndex();
            const bool taken = StyleSupported(style, supported);
            const std::string what = taken ? std::string{}
                                           : std::format("{}/{:#x}", static_cast<int>(style),
                                                         static_cast<u32>(supported.raw));
            auto& logged = refused_logged[static_cast<std::size_t>(i)];
            if (what != logged) {
                if (!taken) {
                    LOG_WARNING(Frontend,
                                "console-mode: P{} is a {} controller, which the game doesn't "
                                "support (supported styles {:#x})",
                                i + 1, StyleName(style), static_cast<u32>(supported.raw));
                }
                logged = what;
            }
        }
    }

    // The controller overlay is drawn into the presented frame (as an overlay panel): no Qt
    // window can composite over the game under gamescope
    {
        // One row per player: green when its pad connects, red when it leaves, then gone. The
        // name drops InputCommon's port number, which shifts as Steam moves controllers.
        constexpr qint64 flash_ms = 1500;
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        static const QRegularExpression port_number{QStringLiteral(" \\d+(?=( \\(Steam\\))?$)")};
        static const QRegularExpression steam_suffix{QStringLiteral(" \\(Steam\\)$")};
        // A controller's name, or the one given to it with its short name ("John (Xbox)")
        const auto name_key = [&](u32 pad) {
            if (builtin_valid && pad == builtin_match_id) {
                return std::string{"handheld"};
            }
            if (const auto address = inst_address.find(pad); address != inst_address.end()) {
                return "bt:" + address->second;
            }
            return "id:" + live[pad];
        };
        // Names given from the Controllers tab belong to the controller
        {
            std::vector<std::pair<int, std::string>> renames;
            {
                auto& state = Common::GetControllerConsoleState();
                std::scoped_lock lock{state.mutex};
                renames.swap(state.rename_requests);
            }
            for (const auto& [player, given] : renames) {
                const auto held =
                    std::find_if(pad_slots.begin(), pad_slots.end(), [&](const auto& e) {
                        return e.second == player && live.contains(e.first);
                    });
                if (held == pad_slots.end()) {
                    continue;
                }
                const std::string key = name_key(held->first);
                if (given.empty()) {
                    controller_names.erase(key);
                } else {
                    controller_names[key] = given;
                }
                LOG_INFO(Frontend, "console-mode: P{} named \"{}\"", player + 1, given);
            }
        }
        std::array<std::string, 10> custom_names{};
        overlay_rows.clear();
        std::erase_if(battery_read, [&](const auto& e) { return !live.contains(e.first); });
        // (A controller that leaves starts afresh when it connects again)
        const auto left = [&](const std::string& key) {
            return std::none_of(live.begin(), live.end(), [&](const auto& pad) {
                const auto address = inst_address.find(pad.first);
                return address != inst_address.end() && address->second == key;
            });
        };
        std::erase_if(bluez_first, [&](const auto& e) { return left(e.first); });
        std::erase_if(bluez_battery, [&](const auto& e) { return left(e.first); });
        std::erase_if(battery_whose, [&](const auto& e) { return !live.contains(e.first); });
        std::vector<Common::ControllerConsoleState::PlayerRow> tab_rows;
        // Flashes follow the controller behind a player (in handheld mode P1 stays connected
        // while it changes): green when one takes a player, red when one leaves
        for (int i = 0; i < static_cast<int>(players.size()); ++i) {
            u32 pad = 0;
            for (const auto& [id, slot] : pad_slots) {
                if (slot == i && live.find(id) != live.end()) {
                    pad = id;
                    break;
                }
            }
            auto& since = overlay_row_since[i];
            auto& gone = overlay_row_gone[i];
            auto& flash = overlay_flash[i];
            auto& flash_since = overlay_flash_since[i];
            auto& green_at = overlay_green_at[i];
            if (players[i].connected && pad) {
                const auto [display, raw] = label_of(pad, std::string{});
                QString name = display.isEmpty() ? raw : display;
                name.remove(port_number);
                name.remove(steam_suffix);
                if (name == QStringLiteral("SteamDeck")) {
                    name = QStringLiteral("Steam Deck"); // (Steam's name for the Deck's own)
                }
                // (The name of the controller that had the player, should it have just left)
                const QString previous = overlay_row_name[i];
                // (A name given before the address was known moves to it, like the bindings)
                if (const std::string key = name_key(pad); key.starts_with("bt:")) {
                    const auto earlier = controller_names.find("id:" + live[pad]);
                    if (earlier != controller_names.end()) {
                        controller_names[key] = earlier->second;
                        controller_names.erase(earlier);
                    }
                }
                const auto given = controller_names.find(name_key(pad));
                // Handheld Console: the controller that took P1 from the handheld's controls
                // carries the handheld's name when it has none (the same person, docked)
                const auto handed = controller_names.find("handheld");
                const bool inherits = given == controller_names.end() && i == 0 && handheld &&
                                      builtin_valid && pad != builtin_match_id &&
                                      !pad_slots.contains(builtin_match_id) &&
                                      handed != controller_names.end();
                if (given != controller_names.end()) {
                    custom_names[static_cast<std::size_t>(i)] = given->second;
                    name = QStringLiteral("%1 (%2)").arg(QString::fromStdString(given->second),
                                                         ShortControllerName(pad, name));
                } else if (inherits) {
                    name = QStringLiteral("%1 (%2)").arg(QString::fromStdString(handed->second),
                                                         ShortControllerName(pad, name));
                }
                overlay_row_name[i] = name;
                if (since == 0 || gone != 0) {
                    since = now_ms;
                    gone = 0;
                    flash = 1;
                    flash_since = now_ms;
                    green_at = 0;
                } else if (live[pad] != overlay_row_ident[i]) {
                    const bool old_left =
                        std::none_of(live.begin(), live.end(), [&](const auto& entry) {
                            return entry.second == overlay_row_ident[i];
                        });
                    if (old_left) {
                        flash = 2;
                        flash_since = now_ms;
                        green_at = now_ms + flash_ms / 2;
                        overlay_left_name[i] = previous;
                    } else {
                        flash = 1;
                        flash_since = now_ms;
                        green_at = 0;
                    }
                }
                overlay_row_ident[i] = live[pad];
                // The input history's key: the controller's own (its address, else its
                // connection too when identical controllers share an identity)
                std::string history_key = name_key(pad);
                if (history_key.starts_with("id:") &&
                    std::any_of(live.begin(), live.end(), [&](const auto& entry) {
                        return entry.first != pad && entry.second == live[pad];
                    })) {
                    history_key += std::format("#{}", pad);
                }
                overlay_row_history[i] = std::move(history_key);
            } else if (since != 0 && gone == 0) {
                gone = now_ms;
                flash = 2;
                flash_since = now_ms;
                green_at = 0;
                overlay_left_name[i] = overlay_row_name[i];
            }
            if (since == 0) {
                continue;
            }
            if (gone != 0 && now_ms - gone > flash_ms) {
                since = 0;
                gone = 0;
                flash = 0;
                overlay_row_ident[i].clear();
                overlay_row_history[i].clear();
                continue;
            }
            if (green_at != 0 && now_ms >= green_at) {
                flash = 1;
                flash_since = green_at;
                green_at = 0;
            }
            if (flash != 0 && gone == 0 && green_at == 0 && now_ms - flash_since >= flash_ms) {
                flash = 0;
            }
            // The controller's battery: by its Bluetooth address, the system's for the handheld's
            // own controls, else SDL's (never a Steam virtual pad's). Read every 10 seconds, more
            // often while a new controller's value settles.
            Battery battery;
            if (pad) {
                auto& read = battery_read[pad];
                // Whose reading it is: Steam reshuffles its pads, so another controller is read
                // afresh
                const bool own = builtin_valid && pad == builtin_match_id;
                const std::string whose = (own ? "handheld:" : "") + BluetoothKey(pad);
                if (auto& was = battery_whose[pad]; was != whose) {
                    was = whose;
                    read = {};
                }
                if (read.first == 0 || now_ms - read.first > 10'000) {
                    Battery fresh;
                    const char* source = "sdl";
                    if (!IsSteamVirtualPad(pad)) {
                        const auto [percent, charging, wired] = SdlBattery(pad);
                        fresh = {percent, charging, wired};
                    }
#ifdef __linux__
                    std::optional<std::pair<int, bool>> found;
                    bool by_address = false;
                    bool settling = false;
                    if (builtin_valid && pad == builtin_match_id) {
                        found = SystemBattery();
                        source = "system";
                    } else if (const auto address = inst_address.find(pad);
                               address != inst_address.end()) {
                        by_address = true;
                        found = BluetoothBattery(address->second);
                        source = "power supply";
                        if (!found) {
                            // (Asked without waiting: a new answer triggers a reread)
                            const std::string& key = address->second;
                            auto& bluez = bluez_battery[key];
                            if (bluez.asked_ms == 0 || now_ms - bluez.asked_ms >= 500) {
                                bluez.asked_ms = now_ms;
                                AskBluezBattery(key, this, [this, key](int percent) {
                                    auto& entry = bluez_battery[key];
                                    const bool moved = entry.percent != percent;
                                    entry.percent = percent;
                                    if (moved) {
                                        for (const auto& [inst, at] : inst_address) {
                                            if (at == key) {
                                                battery_read[inst].first = 0;
                                            }
                                        }
                                    }
                                });
                            }
                            if (bluez.percent >= 0) {
                                found = std::pair{bluez.percent, false};
                            }
                            source = "bluez";
                            // BlueZ first reads 50% for a new controller (the real charge
                            // comes about 10 seconds later): a first 50% counts once it changes,
                            // or after 12 seconds
                            if (found) {
                                // (By controller: Steam reshuffling its pads is no new connection)
                                const auto [first, added] =
                                    bluez_first.try_emplace(address->second, now_ms, found->first);
                                // (Only its placeholder: a first value of anything else is real)
                                if (first->second.second == 50 &&
                                    now_ms - first->second.first < 12'000 &&
                                    found->first == first->second.second) {
                                    found.reset();
                                    settling = true;
                                }
                            }
                        }
                    }
                    if (!found && !settling && fresh.percent < 0 && !fresh.wired &&
                        Common::ControllerProbes()) {
                        const auto address = inst_address.find(pad);
                        LOG_INFO(Frontend, "console-mode: P{} inst={} battery unknown (address {})",
                                 i + 1, pad,
                                 address != inst_address.end() ? address->second : "none");
                    }
                    if (found) {
                        fresh = {found->first, found->second, false};
                    }
                    // (No value yet, or BlueZ's first one settling: read again sooner)
                    const qint64 read_at = settling                ? now_ms - 9'000
                                           : by_address && !found ? now_ms - 8'000
                                                                   : now_ms;
#else
                    const qint64 read_at = now_ms;
#endif
                    if (Common::ControllerProbes() && fresh.percent >= 0 &&
                        (fresh.percent != read.second.percent ||
                         fresh.charging != read.second.charging)) {
                        LOG_INFO(Frontend, "console-mode: P{} inst={} battery {}%{} ({})", i + 1,
                                 pad, fresh.percent, fresh.charging ? " charging" : "", source);
                    }
                    read = {read_at, fresh};
                }
                battery = read.second;
                // A low battery, once a session per controller: named on screen
                const std::string key = name_key(pad);
                if (battery.percent >= 0 && battery.percent <= 15 && !battery.charging &&
                    battery_low_told.insert(key).second) {
                    controller_hints.push_back({QString{}, i, false, now_ms, false,
                                                tr("%1: battery low (%2%)")
                                                    .arg(overlay_row_name[i])
                                                    .arg(battery.percent)});
                    LOG_INFO(Frontend, "console-mode: P{} battery low ({}%)", i + 1,
                             battery.percent);
                }
            }
            const auto started = pad ? pad_origin.find(pad) : pad_origin.end();
            const int origin =
                started != pad_origin.end() && started->second != i ? started->second : -1;
            const bool guessed = pad && pad_guessed.contains(pad);
            overlay_rows.push_back({i,
                                    QStringLiteral("P%1: %2").arg(i + 1).arg(overlay_row_name[i]),
                                    gone != 0 ? 0u : pad, flash, flash_since, origin, guessed,
                                    battery.percent, battery.charging});
            const std::string key = pad ? BluetoothKey(pad) : std::string{};
            const auto address = pad_bluetooth.find(key);
            const int turn_off = address != pad_bluetooth.end() && !address->second.empty() ? 2
                                 : pad_bt_pending.contains(key)                             ? 1
                                                                                            : 0;
            overlay_custom_name[static_cast<std::size_t>(i)] =
                custom_names[static_cast<std::size_t>(i)];
            tab_rows.push_back({i, overlay_row_name[i].toStdString(), false, gone != 0, flash,
                                flash_since, ConsoleBindingSets()[slot_set[i]].profile_name,
                                turn_off, origin, guessed,
                                custom_names[static_cast<std::size_t>(i)], battery.percent,
                                battery.charging, battery.wired});
        }
        {
            std::string handheld_name;
            bool handheld_has_player = false;
            if (builtin_valid && live.find(builtin_match_id) != live.end()) {
                const auto [display, raw] = label_of(builtin_match_id, std::string{});
                QString name = display.isEmpty() ? raw : display;
                name.remove(port_number);
                handheld_name = name.toStdString();
                handheld_has_player = pad_slots.contains(builtin_match_id);
            }
            // The removed controller, by name while it is connected
            std::string blocked_name;
            for (const auto& [id, ident] : live) {
                if (is_blocked(id)) {
                    blocked_name = PadName(id).toStdString();
                }
            }
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            state.engine_active = true;
            state.players = std::move(tab_rows);
            state.handheld_name = std::move(handheld_name);
            state.handheld_has_player = handheld_has_player;
            state.handheld_blocked_name = std::move(blocked_name);
        }
        RenderControllerOverlay();
    }

    if (!changed) {
        return;
    }
    // Reloaded whether a game runs or not: the Controls tab reads the players back from the
    // emulated controllers, and stale ones would be taken for the user's edits
    if (QtCommon::system) {
        QtCommon::system->HIDCore().ReloadInputDevices();
    }
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    ++state.generation;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // The controller overlay takes the mouse over its rows (drag to swap, click to rumble)
    if (watched == render_window && HandleOverlayMouse(event)) {
        return true;
    }
    if (watched == ui->menu_Cheats && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse_event = static_cast<QMouseEvent*>(event);
        auto* action = ui->menu_Cheats->actionAt(mouse_event->pos());
        if (mouse_event->button() == Qt::LeftButton && action && action->isEnabled() &&
            action->isCheckable()) {
            action->trigger();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::ConnectMenuEvents() {
    const auto connect_menu = [&]<typename Fn>(QAction* action, const Fn& event_fn) {
        connect(action, &QAction::triggered, this, event_fn);
        // Add actions to this window so that hiding menus in fullscreen won't disable them
        addAction(action);
        // Add actions to the render window so that they work outside of single window mode
        render_window->addAction(action);
    };

    // File
    connect_menu(ui->action_Load_File, &MainWindow::OnMenuLoadFile);
    connect_menu(ui->action_Load_Folder, &MainWindow::OnMenuLoadFolder);
    connect_menu(ui->action_Install_File_NAND, &MainWindow::OnMenuInstallToNAND);
    connect_menu(ui->action_Exit, &QMainWindow::close);
    connect_menu(ui->action_Load_Amiibo, &MainWindow::OnLoadAmiibo);

    // Emulation
    connect_menu(ui->action_Pause, &MainWindow::OnPauseContinueGame);
    connect_menu(ui->action_Stop, &MainWindow::OnStopGame);
    connect_menu(ui->action_Open_Mods_Page, &MainWindow::OnOpenModsPage);
    connect_menu(ui->action_Open_UserHandbook, &MainWindow::OnOpenUserHandbook);
    connect_menu(ui->action_Restart, &MainWindow::OnRestartGame);
    connect_menu(ui->action_Configure, &MainWindow::OnConfigure);
    connect_menu(ui->action_Configure_Current_Game, &MainWindow::OnConfigurePerGame);

    // View
    connect_menu(ui->action_Fullscreen, &MainWindow::ToggleFullscreen);
    connect_menu(ui->action_Single_Window_Mode, &MainWindow::ToggleWindowMode);
    connect_menu(ui->action_Show_Filter_Bar, &MainWindow::OnToggleFilterBar);
    connect_menu(ui->action_Show_Status_Bar, &MainWindow::OnToggleStatusBar);
    connect_menu(ui->action_Show_Performance_Overlay, &MainWindow::OnTogglePerfOverlay);
    connect(ui->action_Show_Controller_Overlay, &QAction::toggled, this,
            [](bool shown) { Settings::values.console_debug_overlay.SetValue(shown); });
    connect(ui->action_Show_Controller_Hints, &QAction::toggled, this,
            [](bool shown) { Settings::values.console_hints.SetValue(shown); });
#ifdef HAS_RESHADE
    connect_menu(ui->action_Post_Processing_Shaders, &MainWindow::OnPostProcessingShaders);
#else
    ui->action_Post_Processing_Shaders->setVisible(false);
#endif

    connect_menu(ui->action_Reset_Window_Size_720, &MainWindow::ResetWindowSize720);
    connect_menu(ui->action_Reset_Window_Size_900, &MainWindow::ResetWindowSize900);
    connect_menu(ui->action_Reset_Window_Size_1080, &MainWindow::ResetWindowSize1080);
    ui->menu_Reset_Window_Size->addActions({ui->action_Reset_Window_Size_720,
                                            ui->action_Reset_Window_Size_900,
                                            ui->action_Reset_Window_Size_1080});

    connect_menu(ui->action_Grid_View, &MainWindow::SetGridView);
    connect_menu(ui->action_Tree_View, &MainWindow::SetTreeView);
    connect_menu(ui->action_Carousel_View, &MainWindow::SetCarouselView);

    game_size_actions = new QActionGroup(this);
    game_size_actions->setExclusive(true);

    for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
        const auto current_size = UISettings::values.game_icon_size.GetValue();
        const auto size = default_game_icon_sizes[i].first;
        QAction* action = ui->menuGame_Icon_Size->addAction(GetTranslatedGameIconSize(i));
        action->setCheckable(true);

        if (current_size == size)
            action->setChecked(true);

        game_size_actions->addAction(action);

        connect(action, &QAction::triggered, this, [this, size](bool checked) {
            if (checked) {
                UISettings::values.game_icon_size.SetValue(size);
                CheckIconSize();

                game_list->UpdateIconSizes();
                game_list->RefreshGameDirectory();
            }
        });
    }

    CheckIconSize();

    ui->action_Show_Game_Name->setChecked(UISettings::values.show_game_name.GetValue());
    connect(ui->action_Show_Game_Name, &QAction::triggered, this, &MainWindow::ToggleShowGameName);

    // Multiplayer
    connect(ui->action_View_Lobby, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnViewLobby);
    connect(ui->action_Start_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCreateRoom);
    connect(ui->action_Leave_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCloseRoom);
    connect(ui->action_Connect_To_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnDirectConnectToRoom);
    connect(ui->action_Show_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnOpenNetworkRoom);
    connect(multiplayer_state, &MultiplayerState::SaveConfig, this, &MainWindow::OnSaveConfig);

    // Tools
    connect_menu(ui->action_Launch_PhotoViewer, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::PhotoViewer), std::nullopt);
    });
    connect_menu(ui->action_Launch_MiiEdit, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::MiiEdit), std::nullopt);
    });
    connect_menu(ui->action_Launch_Controller, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Controller), std::nullopt);
    });
    connect_menu(ui->action_Launch_QLaunch, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::QLaunch), std::nullopt);
    });
    // Tools (cabinet)
    connect_menu(ui->action_Launch_Cabinet_Nickname_Owner, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartNicknameAndOwnerSettings});
    });
    connect_menu(ui->action_Launch_Cabinet_Eraser, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartGameDataEraser});
    });
    connect_menu(ui->action_Launch_Cabinet_Restorer, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartRestorer});
    });
    connect_menu(ui->action_Launch_Cabinet_Formatter, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartFormatter});
    });

    connect_menu(ui->action_Desktop, &MainWindow::OnCreateHomeMenuDesktopShortcut);
    connect_menu(ui->action_Application_Menu, &MainWindow::OnCreateHomeMenuApplicationMenuShortcut);
    connect_menu(ui->action_Capture_Screenshot, &MainWindow::OnCaptureScreenshot);

    // TAS
    connect_menu(ui->action_TAS_Start, &MainWindow::OnTasStartStop);
    connect_menu(ui->action_TAS_Record, &MainWindow::OnTasRecord);
    connect_menu(ui->action_TAS_Reset, &MainWindow::OnTasReset);
    connect_menu(ui->action_Configure_Tas, &MainWindow::OnConfigureTas);

    // Help
    connect_menu(ui->action_Root_Data_Folder, &MainWindow::OnOpenRootDataFolder);
    connect_menu(ui->action_NAND_Folder, &MainWindow::OnOpenNANDFolder);
    connect_menu(ui->action_SDMC_Folder, &MainWindow::OnOpenSDMCFolder);
    connect_menu(ui->action_Mod_Folder, &MainWindow::OnOpenModFolder);
    connect_menu(ui->action_Log_Folder, &MainWindow::OnOpenLogFolder);

    connect_menu(ui->action_Verify_installed_contents, &MainWindow::OnVerifyInstalledContents);
    connect_menu(ui->action_Firmware_From_Folder, &MainWindow::OnInstallFirmware);
    connect_menu(ui->action_Firmware_From_ZIP, &MainWindow::OnInstallFirmwareFromZIP);
    connect_menu(ui->action_Install_Keys, &MainWindow::OnInstallDecryptionKeys);
    connect_menu(ui->action_About, &MainWindow::OnAbout);
    connect_menu(ui->action_Eden_Dependencies, &MainWindow::OnEdenDependencies);
    connect_menu(ui->action_Data_Manager, &MainWindow::OnDataDialog);
}

void MainWindow::UpdateMenuState() {
    const bool is_paused = QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning();
    const bool is_firmware_available = CheckFirmwarePresence();

    const std::array running_actions{
        ui->action_Stop,
        ui->action_Restart,
        ui->action_Configure_Current_Game,
        ui->action_Load_Amiibo,
        ui->action_Pause,
    };

    const std::array applet_actions{
        ui->action_Launch_PhotoViewer,       ui->action_Launch_Cabinet_Nickname_Owner,
        ui->action_Launch_Cabinet_Eraser,    ui->action_Launch_Cabinet_Restorer,
        ui->action_Launch_Cabinet_Formatter, ui->action_Launch_MiiEdit,
        ui->action_Launch_QLaunch,           ui->action_Launch_Controller};

    for (QAction* action : running_actions) {
        action->setEnabled(emulation_running);
    }

    ui->action_Firmware_From_Folder->setEnabled(!emulation_running);
    ui->action_Firmware_From_ZIP->setEnabled(!emulation_running);
    ui->action_Install_Keys->setEnabled(!emulation_running);

    for (QAction* action : applet_actions) {
        action->setEnabled(is_firmware_available && !emulation_running);
    }

    ui->action_Capture_Screenshot->setEnabled(emulation_running && !is_paused);

    if (emulation_running && is_paused) {
        ui->action_Pause->setText(tr("&Continue"));
    } else {
        ui->action_Pause->setText(tr("&Pause"));
    }

    multiplayer_state->UpdateNotificationStatus();
}

void MainWindow::SetupPrepareForSleep() {
#ifdef __unix__
    if (auto bus = QDBusConnection::systemBus(); bus.isConnected()) {
        // See https://github.com/ConsoleKit2/ConsoleKit2/issues/150
#ifdef __linux__
        const auto dbus_logind_service = QStringLiteral("org.freedesktop.login1");
        const auto dbus_logind_path = QStringLiteral("/org/freedesktop/login1");
        const auto dbus_logind_manager_if = QStringLiteral("org.freedesktop.login1.Manager");
        // const auto dbus_logind_session_if = QStringLiteral("org.freedesktop.login1.Session");
#else
        const auto dbus_logind_service = QStringLiteral("org.freedesktop.ConsoleKit");
        const auto dbus_logind_path = QStringLiteral("/org/freedesktop/ConsoleKit/Manager");
        const auto dbus_logind_manager_if = QStringLiteral("org.freedesktop.ConsoleKit.Manager");
        // const auto dbus_logind_session_if = QStringLiteral("org.freedesktop.ConsoleKit.Session");
#endif
        const bool success = bus.connect(dbus_logind_service, dbus_logind_path,
                                         dbus_logind_manager_if, QStringLiteral("PrepareForSleep"),
                                         QStringLiteral("b"), this, SLOT(OnPrepareForSleep(bool)));
        if (!success)
            LOG_WARNING(Frontend, "Couldn't register PrepareForSleep signal");
    } else {
        LOG_WARNING(Frontend, "QDBusConnection system bus is not connected");
    }
#endif // __unix__
}

void MainWindow::OnPrepareForSleep(bool prepare_sleep) {
    if (QtCommon::emu_thread == nullptr)
        return;

    if (prepare_sleep) {
        if (QtCommon::emu_thread->IsRunning()) {
            auto_paused = true;
            OnPauseGame();
        }
    } else {
        if (!QtCommon::emu_thread->IsRunning() && auto_paused) {
            auto_paused = false;
            OnStartGame();
        }
    }
}

#ifdef __unix__
std::array<int, 3> MainWindow::sig_interrupt_fds{0, 0, 0};

void MainWindow::SetupSigInterrupts() {
    if (sig_interrupt_fds[2] == 1) {
        return;
    }
    socketpair(AF_UNIX, SOCK_STREAM, 0, sig_interrupt_fds.data());
    sig_interrupt_fds[2] = 1;

    struct sigaction sa;
    sa.sa_handler = &MainWindow::HandleSigInterrupt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sig_interrupt_notifier = new QSocketNotifier(sig_interrupt_fds[1], QSocketNotifier::Read, this);
    connect(sig_interrupt_notifier, &QSocketNotifier::activated, this,
            &MainWindow::OnSigInterruptNotifierActivated);
    connect(this, &MainWindow::SigInterrupt, this, &MainWindow::close);
}

void MainWindow::HandleSigInterrupt(int sig) {
    if (sig == SIGINT) {
        _exit(1);
    }

    // Calling into Qt directly from a signal handler is not safe,
    // so wake up a QSocketNotifier with this hacky write call instead.
    char a = 1;
    int ret = write(sig_interrupt_fds[0], &a, sizeof(a));
    (void)ret;
}

void MainWindow::OnSigInterruptNotifierActivated() {
    sig_interrupt_notifier->setEnabled(false);

    char a;
    int ret = read(sig_interrupt_fds[1], &a, sizeof(a));
    (void)ret;

    sig_interrupt_notifier->setEnabled(true);

    emit SigInterrupt();
}
#endif // __unix__

void MainWindow::PreventOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#else
    SDL_DisableScreenSaver();
#endif
}

void MainWindow::AllowOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#else
    SDL_EnableScreenSaver();
#endif
}

bool MainWindow::LoadROM(const QString& filename, Service::AM::FrontendAppletParameters params) {
    // Shutdown previous session if the emu thread is still active...
    if (QtCommon::emu_thread != nullptr)
        ShutdownGame();

    if (!render_window->InitRenderTarget())
        return false;

    QtCommon::system->SetFilesystem(QtCommon::vfs);

    if (params.launch_type == Service::AM::LaunchType::FrontendInitiated)
        QtCommon::system->GetUserChannel().clear();

    QtCommon::system->SetFrontendAppletSet({
        std::make_unique<QtAmiiboSettings>(*this), // Amiibo Settings
        // The console modes own the controllers and suppress the applet, like Disabled
        (Settings::values.controller_applet_mode.GetValue() == Settings::AppletMode::Disabled ||
         Settings::values.controller_applet_mode.GetValue() == Settings::AppletMode::ConsoleMode ||
         Settings::values.controller_applet_mode.GetValue() ==
             Settings::AppletMode::HandheldConsoleMode)
            ? nullptr
            : std::make_unique<QtControllerSelector>(*this), // Controller Selector
        std::make_unique<QtErrorDisplay>(*this),             // Error Display
        nullptr,                                             // Mii Editor
        nullptr,                                             // Parental Controls
        nullptr,                                             // Photo Viewer
        std::make_unique<QtProfileSelector>(*this),          // Profile Selector
        std::make_unique<QtSoftwareKeyboard>(*this),         // Software Keyboard
        std::make_unique<QtWebBrowser>(*this),               // Web Browser
        nullptr,                                             // Net Connect
    });

    /** firmware check */

    if (!QtCommon::Content::CheckGameFirmware(params.program_id))
        return false;

    /** Exec */
    const Core::SystemResultStatus result{
        QtCommon::system->Load(*render_window, filename.toStdString(), params)};

    const auto drd_callout = (UISettings::values.callout_flags.GetValue() &
                              static_cast<u32>(CalloutFlag::DRDDeprecation)) == 0;

    if (result == Core::SystemResultStatus::Success &&
        QtCommon::system->GetAppLoader().GetFileType() ==
            Loader::FileType::DeconstructedRomDirectory &&
        drd_callout) {
        UISettings::values.callout_flags = UISettings::values.callout_flags.GetValue() |
                                           static_cast<u32>(CalloutFlag::DRDDeprecation);
        QMessageBox::warning(
            this, tr("Warning: Outdated Game Format"),
            tr("You are using the deconstructed ROM directory format for this game, which is an "
               "outdated format that has been superseded by others such as NCA, NAX, XCI, or "
               "NSP. Deconstructed ROM directories lack icons, metadata, and update "
               "support.<br>For an explanation of the various Switch formats Eden supports, "
               "out our user handbook. This message will not be shown again."));
    }

    if (result != Core::SystemResultStatus::Success) {
        switch (result) {
        case Core::SystemResultStatus::ErrorGetLoader:
            LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", filename.toStdString());
            QMessageBox::critical(this, tr("Error while loading ROM!"),
                                  tr("The ROM format is not supported."));
            break;
        case Core::SystemResultStatus::ErrorVideoCore:
            QMessageBox::critical(
                this, tr("An error occurred initializing the video core."),
                tr("This is usually caused by outdated GPU drivers. "
                   "Please see the log for more details. See: "
                   "<a href='https://git.eden-emu.dev/eden-emu/eden/src/branch/master/docs/user/HowToAccessLogs.md'>"
                   "How to access log files</a>."));
            break;
        default:
            if (result > Core::SystemResultStatus::ErrorLoader) {
                const u16 loader_id = static_cast<u16>(Core::SystemResultStatus::ErrorLoader);
                const u16 error_id = static_cast<u16>(result) - loader_id;
                const std::string error_code = std::format("({:04X}-{:04X})", loader_id, error_id);
                LOG_CRITICAL(Frontend, "Failed to load ROM! {}", error_code);

                const auto title =
                    tr("Error while loading ROM! %1", "%1 signifies a numeric error code.")
                        .arg(QString::fromStdString(error_code));
                const auto description =
                    tr("%1<br>Please redump your files or ask on Discord/Stoat for help.",
                       "%1 signifies an error string.")
                        .arg(QString::fromStdString(
                            GetResultStatusString(static_cast<Loader::ResultStatus>(error_id))));

                QMessageBox::critical(this, title, description);
            } else {
                QMessageBox::critical(
                    this, tr("Error while loading ROM!"),
                    tr("An unknown error occurred. Please see the log for more details."));
            }
            break;
        }
        return false;
    }
    current_game_path = filename;

    return true;
}

bool MainWindow::SelectAndSetCurrentUser(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    QtProfileSelectionDialog dialog(*QtCommon::system, this, parameters);
    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::WindowModal);

    if (dialog.exec() == QDialog::Rejected) {
        return false;
    }

    Settings::values.current_user = dialog.GetIndex();
    return true;
}

void MainWindow::BootGame(const QString& filename, Service::AM::FrontendAppletParameters params,
                          StartGameType type) {
    LOG_INFO(Frontend, "Eden starting...");

    if (params.program_id == 0 ||
        params.program_id > static_cast<u64>(Service::AM::AppletProgramId::MaxProgramId)) {
        StoreRecentFile(filename); // Put the filename on top of the list
    }

    // Save configurations
    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    SaveAllConfigValues();

    u64 title_id{0};

    last_filename_booted = filename;

    QtCommon::Content::configureFilesystemProvider(filename.toStdString());
    const auto v_file = Core::GetGameFileFromPath(QtCommon::vfs, filename.toUtf8().constData());
    const auto loader =
        Loader::GetLoader(*QtCommon::system, v_file, params.program_id, params.program_index);

    if (loader != nullptr && loader->ReadProgramId(title_id) == Loader::ResultStatus::Success &&
        type == StartGameType::Normal) {
        // Load per game settings
        const auto file_path =
            std::filesystem::path{Common::U16StringFromBuffer(filename.utf16(), filename.size())};
        const auto config_file_name = title_id == 0
                                          ? Common::FS::PathToUTF8String(file_path.filename())
                                          : std::format("{:016X}", title_id);
        QtConfig per_game_config(config_file_name, Config::ConfigType::PerGameConfig);
        QtCommon::system->HIDCore().ReloadInputDevices();
        QtCommon::system->ApplySettings();
    }

    Settings::LogSettings();

    if (UISettings::values.select_user_on_boot && !user_flag_cmd_line) {
        const Core::Frontend::ProfileSelectParameters parameters{
            .mode = Service::AM::Frontend::UiMode::UserSelector,
            .invalid_uid_list = {},
            .display_options = {},
            .purpose = Service::AM::Frontend::UserSelectionPurpose::General,
        };
        if (SelectAndSetCurrentUser(parameters) == false) {
            return;
        }
    }

    // If the user specifies -u (successfully) on the cmd line, don't prompt for a user on first
    // game startup only. If the user stops emulation and starts a new one, go back to the expected
    // behavior of asking.
    user_flag_cmd_line = false;

    if (!LoadROM(filename, params)) {
        return;
    }

    QtCommon::system->SetShuttingDown(false);
    game_list->setDisabled(true);

    // Create and start the emulation thread
    QtCommon::emu_thread = std::make_unique<EmuThread>();
    emit EmulationStarting();
    QtCommon::emu_thread->start();

    // Register an ExecuteProgram callback such that Core can execute a sub-program
    QtCommon::system->RegisterExecuteProgramCallback(
        [this](std::size_t program_index_) { render_window->ExecuteProgram(program_index_); });

    QtCommon::system->RegisterExitCallback([this] {
        QtCommon::emu_thread->ForceStop();
        render_window->Exit();
    });

    QtCommon::system->RegisterApplicationChangedCallback([this](u64 changed_program_id) {
        if (QtCommon::emu_thread)
            QtCommon::emu_thread->RequestDiskShaderCacheReload(changed_program_id);

        QMetaObject::invokeMethod(
            this, [this, changed_program_id] { this->OnApplicationChanged(changed_program_id); },
            Qt::QueuedConnection);
    });

    connect(render_window, &GRenderWindow::Closed, this, &MainWindow::OnStopGame);
    connect(render_window, &GRenderWindow::MouseActivity, this, &MainWindow::OnMouseActivity);

    connect(QtCommon::emu_thread.get(), &EmuThread::LoadProgress, loading_screen,
            &LoadingScreen::OnLoadProgress, Qt::QueuedConnection);

    connect(
        QtCommon::emu_thread.get(), &EmuThread::ShaderCacheReloadStarted, this,
        [this] {
            loading_screen->Prepare(QtCommon::system->GetAppLoader());
            loading_screen->show();
            render_window->hide();
        },
        Qt::QueuedConnection);

    connect(
        QtCommon::emu_thread.get(), &EmuThread::ShaderCacheReloadFinished, this,
        [this] {
            loading_screen->OnLoadComplete();
        },
        Qt::QueuedConnection);

    // Update the GUI
    UpdateStatusButtons();
    if (ui->action_Single_Window_Mode->isChecked()) {
        game_list->hide();
        game_list_placeholder->hide();
    }
    status_bar_update_timer.start(500);
    renderer_status_button->setDisabled(true);
    refresh_button->setDisabled(true);
    SetFPSSuffix();

    if (UISettings::values.hide_mouse || Settings::values.mouse_panning) {
        render_window->installEventFilter(render_window);
        render_window->setAttribute(Qt::WA_Hover, true);
    }

    if (UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
    }

    render_window->InitializeCamera();

    std::string title_name;
    std::string title_version;
    const auto res = QtCommon::system->GetGameName(title_name);

    const auto metadata = [title_id] {
        const FileSys::PatchManager pm(title_id, QtCommon::system->GetFileSystemController(),
                                       QtCommon::system->GetContentProvider());
        return pm.GetControlMetadata();
    }();
    if (metadata.first != nullptr) {
        title_version = metadata.first->GetVersionString();
        title_name = metadata.first->GetApplicationName();
    }
    if (res != Loader::ResultStatus::Success || title_name.empty()) {
        title_name = Common::FS::PathToUTF8String(
            std::filesystem::path{Common::U16StringFromBuffer(filename.utf16(), filename.size())}
                .filename());
    }
    const bool is_64bit = QtCommon::system->Kernel().ApplicationProcess()->Is64Bit();
    const auto instruction_set_suffix = is_64bit ? tr("(64-bit)") : tr("(32-bit)");
    title_name = tr("%1 %2", "%1 is the title name. %2 indicates if the title is 64-bit or 32-bit")
                     .arg(QString::fromStdString(title_name), instruction_set_suffix)
                     .toStdString();
    LOG_INFO(Frontend, "Booting game: {:016X} | {} | {}", title_id, title_name, title_version);
    const auto gpu_vendor = QtCommon::system->GPU().Renderer().GetDeviceVendor();
    UpdateWindowTitle(title_name, title_version, gpu_vendor);

    loading_screen->Prepare(QtCommon::system->GetAppLoader());
    loading_screen->show();

    emulation_running = true;
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    }
    OnStartGame();
}

void MainWindow::BootGameFromList(const QString& filename, StartGameType with_config) {
    BootGame(filename, ApplicationAppletParameters(), with_config);
}

bool MainWindow::OnShutdownBegin() {
    if (!emulation_running) {
        return false;
    }

    if (ui->action_Fullscreen->isChecked()) {
        HideFullscreen();
    }

    AllowOSSleep();

    // Disable unlimited frame rate and turbo/slow modes
    Settings::values.use_speed_limit.SetValue(true);
    Settings::values.current_speed_mode = Settings::SpeedMode::Standard;

    if (QtCommon::system->IsShuttingDown()) {
        return false;
    }

    if (perf_overlay) {
        perf_overlay->hide();
        perf_overlay->deleteLater();
        perf_overlay = nullptr;
    }

    QtCommon::system->SetShuttingDown(true);
    discord_rpc->Pause();

    RequestGameExit();
    QtCommon::emu_thread->disconnect();
    QtCommon::emu_thread->SetRunning(true);

    connect(QtCommon::emu_thread.get(), &QThread::finished, this, &MainWindow::OnEmulationStopped);
    emit EmulationStopping();

    int shutdown_time = 1000;

    if (QtCommon::system->DebuggerEnabled()) {
        shutdown_time = 0;
    } else if (QtCommon::system->GetExitLocked()) {
        shutdown_time = 5000;
    }

    shutdown_timer.setSingleShot(true);
    shutdown_timer.start(shutdown_time);
    connect(&shutdown_timer, &QTimer::timeout, this, &MainWindow::OnEmulationStopTimeExpired);

    // Disable everything to prevent anything from being triggered here
    ui->action_Pause->setEnabled(false);
    ui->action_Restart->setEnabled(false);
    ui->action_Stop->setEnabled(false);

    return true;
}

void MainWindow::OnShutdownBeginDialog() {
    shutdown_dialog =
        new OverlayDialog(this, *QtCommon::system, QString{}, tr("Closing software..."), QString{},
                          QString{}, Qt::AlignHCenter | Qt::AlignVCenter);
    shutdown_dialog->open();
}

void MainWindow::OnEmulationStopTimeExpired() {
    if (QtCommon::emu_thread) {
        QtCommon::emu_thread->ForceStop();
    }
}

void MainWindow::OnEmulationStopped() {
    shutdown_timer.stop();
    if (QtCommon::emu_thread) {
        QtCommon::emu_thread->disconnect();
        QtCommon::emu_thread->wait();
        QtCommon::emu_thread.reset();
    }

    if (shutdown_dialog) {
        shutdown_dialog->deleteLater();
        shutdown_dialog = nullptr;
    }

    emulation_running = false;

    discord_rpc->Update();
    Common::FeralGamemode::Stop();

    // The emulation is stopped, so closing the window or not does not matter anymore
    disconnect(render_window, &GRenderWindow::Closed, this, &MainWindow::OnStopGame);

    // Update the GUI
    UpdateMenuState();

    render_window->hide();
    loading_screen->hide();
    loading_screen->Clear();
    if (game_list->IsEmpty()) {
        game_list_placeholder->show();
    } else {
        game_list->show();
    }
    game_list->SetFilterFocus();
    tas_label->clear();
    input_subsystem->GetTas()->Stop();
    OnTasStateChanged();
    render_window->FinalizeCamera();

    QtCommon::system->GetFrontendAppletHolder().SetCurrentAppletId(Service::AM::AppletId::None);

    // Enable all controllers
    QtCommon::system->HIDCore().SetSupportedStyleTag({Core::HID::NpadStyleSet::All});

    render_window->removeEventFilter(render_window);
    render_window->setAttribute(Qt::WA_Hover, false);

    UpdateWindowTitle();

    // Disable status bar updates
    status_bar_update_timer.stop();
    shader_building_label->setVisible(false);
    res_scale_label->setVisible(false);
    emu_speed_label->setVisible(false);
    game_fps_label->setVisible(false);
    emu_frametime_label->setVisible(false);
    renderer_status_button->setEnabled(!UISettings::values.has_broken_vulkan);
    refresh_button->setEnabled(true);

    if (!firmware_label->text().isEmpty()) {
        firmware_label->setVisible(true);
    }

    current_game_path.clear();

    // When closing the game, destroy the GLWindow to clear the context after the game is closed
    render_window->ReleaseRenderTarget();

    // Enable game list
    game_list->setEnabled(true);

    Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
    QtCommon::system->HIDCore().ReloadInputDevices();
    UpdateStatusButtons();
}

void MainWindow::ShutdownGame() {
    if (!emulation_running) {
        return;
    }

    // TODO(crueter): make this common as well (frontend_common?)
    play_time_manager->Stop();
    OnShutdownBegin();
    OnEmulationStopTimeExpired();
    OnEmulationStopped();
}

void MainWindow::StoreRecentFile(const QString& filename) {
    UISettings::values.recent_files.prepend(filename);
    UISettings::values.recent_files.removeDuplicates();
    while (UISettings::values.recent_files.size() > max_recent_files_item) {
        UISettings::values.recent_files.removeLast();
    }

    UpdateRecentFiles();
}

void MainWindow::UpdateRecentFiles() {
    const int num_recent_files =
        (std::min)(static_cast<int>(UISettings::values.recent_files.size()), max_recent_files_item);

    for (int i = 0; i < num_recent_files; i++) {
        const QString text = QStringLiteral("&%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName());
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }

    for (int j = num_recent_files; j < max_recent_files_item; ++j) {
        actions_recent_files[j]->setVisible(false);
    }

    // Enable the recent files menu if the list isn't empty
    ui->menu_recent_files->setEnabled(num_recent_files != 0);
}

void MainWindow::OnGameListLoadFile(QString game_path, u64 program_id) {
    auto params = ApplicationAppletParameters();
    params.program_id = program_id;

    BootGame(game_path, params);
}

// TODO(crueter): Common profile selector
void MainWindow::OnGameListOpenFolder(u64 program_id, GameListOpenTarget target,
                                      const std::string& game_path) {
    std::filesystem::path path;
    QString open_target;

    const auto [user_save_size, device_save_size] = [&game_path, &program_id] {
        const FileSys::PatchManager pm{program_id, QtCommon::system->GetFileSystemController(),
                                       QtCommon::system->GetContentProvider()};
        const auto control = pm.GetControlMetadata().first;
        if (control != nullptr) {
            return std::make_pair(control->GetDefaultNormalSaveSize(),
                                  control->GetDeviceSaveDataSize());
        } else {
            const auto file = Core::GetGameFileFromPath(QtCommon::vfs, game_path);
            const auto loader = Loader::GetLoader(*QtCommon::system, file);

            FileSys::NACP nacp{};
            loader->ReadControlData(nacp);
            return std::make_pair(nacp.GetDefaultNormalSaveSize(), nacp.GetDeviceSaveDataSize());
        }
    }();

    const bool has_user_save{user_save_size > 0};
    const bool has_device_save{device_save_size > 0};

    ASSERT_MSG(has_user_save != has_device_save, "Game uses both user and device savedata?");

    switch (target) {
    case GameListOpenTarget::SaveData: {
        open_target = tr("Save Data");
        const auto save_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::SaveDir);
        auto vfs_save_dir = QtCommon::vfs->OpenDirectory(Common::FS::PathToUTF8String(save_dir),
                                                         FileSys::OpenMode::Read);

        if (has_user_save) {
            // User save data
            const auto user_id = GetProfileID();
            assert(user_id);

            const auto user_save_data_path = FileSys::SaveDataFactory::GetFullPath(
                {}, vfs_save_dir, FileSys::SaveDataSpaceId::User, FileSys::SaveDataType::Account,
                program_id, user_id->AsU128(), 0);

            path = Common::FS::ConcatPathSafe(save_dir, user_save_data_path);
        } else {
            // Device save data
            const auto device_save_data_path = FileSys::SaveDataFactory::GetFullPath(
                {}, vfs_save_dir, FileSys::SaveDataSpaceId::User, FileSys::SaveDataType::Account,
                program_id, {}, 0);

            path = Common::FS::ConcatPathSafe(save_dir, device_save_data_path);
        }

        if (!Common::FS::CreateDirs(path)) {
            LOG_ERROR(Frontend, "Unable to create the directories for save data");
        }

        break;
    }
    case GameListOpenTarget::ModData: {
        open_target = tr("Mod Data");
        path = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
               std::format("{:016X}", program_id);
        break;
    }
    default:
        UNIMPLEMENTED();
        break;
    }

    const QString qpath = QString::fromStdString(Common::FS::PathToUTF8String(path));
    const QDir dir(qpath);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("Error Opening %1 Folder").arg(open_target),
                             tr("Folder does not exist!"));
        return;
    }
    LOG_INFO(Frontend, "Opening {} path for program_id={:016x}", open_target.toStdString(),
             program_id);
    QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
}

static bool RomFSRawCopy(size_t total_size, size_t& read_size, QProgressDialog& dialog,
                         const FileSys::VirtualDir& src, const FileSys::VirtualDir& dest,
                         bool full) {
    if (src == nullptr || dest == nullptr || !src->IsReadable() || !dest->IsWritable())
        return false;
    if (dialog.wasCanceled())
        return false;

    std::vector<u8> buffer(CopyBufferSize);
    auto last_timestamp = std::chrono::steady_clock::now();

    const auto QtRawCopy = [&](const FileSys::VirtualFile& src_file,
                               const FileSys::VirtualFile& dest_file) {
        if (src_file == nullptr || dest_file == nullptr) {
            return false;
        }
        if (!dest_file->Resize(src_file->GetSize())) {
            return false;
        }

        for (std::size_t i = 0; i < src_file->GetSize(); i += buffer.size()) {
            if (dialog.wasCanceled()) {
                dest_file->Resize(0);
                return false;
            }

            using namespace std::literals::chrono_literals;
            const auto new_timestamp = std::chrono::steady_clock::now();

            if ((new_timestamp - last_timestamp) > 33ms) {
                last_timestamp = new_timestamp;
                dialog.setValue(
                    static_cast<int>((std::min)(read_size, total_size) * 100 / total_size));
                QCoreApplication::processEvents();
            }

            const auto read = src_file->Read(buffer.data(), buffer.size(), i);
            dest_file->Write(buffer.data(), read, i);

            read_size += read;
        }

        return true;
    };

    if (full) {
        for (const auto& file : src->GetFiles()) {
            const auto out = VfsDirectoryCreateFileWrapper(dest, file->GetName());
            if (!QtRawCopy(file, out))
                return false;
        }
    }

    for (const auto& dir : src->GetSubdirectories()) {
        const auto out = dest->CreateSubdirectory(dir->GetName());
        if (!RomFSRawCopy(total_size, read_size, dialog, dir, out, full))
            return false;
    }

    return true;
}

// TODO(crueter): All this can be transfered to qt_common
// Aldoe I need to decide re: message boxes for QML
// translations_common? strings_common? qt_strings? who knows
void MainWindow::OnGameListRemoveInstalledEntry(u64 program_id,
                                                QtCommon::Game::InstalledEntryType type) {
    const QString entry_question = [type] {
        switch (type) {
        case QtCommon::Game::InstalledEntryType::Game:
            return tr("Remove Installed Game Contents?");
        case QtCommon::Game::InstalledEntryType::Update:
            return tr("Remove Installed Game Update?");
        case QtCommon::Game::InstalledEntryType::AddOnContent:
            return tr("Remove Installed Game DLC?");
        default:
            return QStringLiteral("Remove Installed Game <Invalid Type>?");
        }
    }();

    if (!question(this, tr("Remove Entry"), entry_question, QMessageBox::Yes | QMessageBox::No,
                  QMessageBox::No)) {
        return;
    }

    // TODO(crueter): move this to QtCommon (populate async?)
    switch (type) {
    case QtCommon::Game::InstalledEntryType::Game:
        QtCommon::Game::RemoveBaseContent(program_id, type);
        [[fallthrough]];
    case QtCommon::Game::InstalledEntryType::Update:
        QtCommon::Game::RemoveUpdateContent(program_id, type);
        if (type != QtCommon::Game::InstalledEntryType::Game) {
            break;
        }
        [[fallthrough]];
    case QtCommon::Game::InstalledEntryType::AddOnContent:
        QtCommon::Game::RemoveAddOnContent(program_id, type);
        break;
    }
    Common::FS::RemoveDirRecursively(Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) /
                                     "game_list");
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

void MainWindow::OnGameListRemoveFile(u64 program_id, QtCommon::Game::GameListRemoveTarget target,
                                      const std::string& game_path) {
    const QString question = [target] {
        switch (target) {
        case QtCommon::Game::GameListRemoveTarget::GlShaderCache:
            return tr("Delete OpenGL Transferable Shader Cache?");
        case QtCommon::Game::GameListRemoveTarget::VkShaderCache:
            return tr("Delete Vulkan Transferable Shader Cache?");
        case QtCommon::Game::GameListRemoveTarget::AllShaderCache:
            return tr("Delete All Transferable Shader Caches?");
        case QtCommon::Game::GameListRemoveTarget::CustomConfiguration:
            return tr("Remove Custom Game Configuration?");
        case QtCommon::Game::GameListRemoveTarget::CacheStorage:
            return tr("Remove Cache Storage?");
        default:
            return QString{};
        }
    }();

    if (!MainWindow::question(this, tr("Remove File"), question, QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)) {
        return;
    }

    switch (target) {
    case QtCommon::Game::GameListRemoveTarget::VkShaderCache:
        QtCommon::Game::RemoveVulkanDriverPipelineCache(program_id);
        [[fallthrough]];
    case QtCommon::Game::GameListRemoveTarget::GlShaderCache:
        QtCommon::Game::RemoveTransferableShaderCache(program_id, target);
        break;
    case QtCommon::Game::GameListRemoveTarget::AllShaderCache:
        QtCommon::Game::RemoveAllTransferableShaderCaches(program_id);
        break;
    case QtCommon::Game::GameListRemoveTarget::CustomConfiguration:
        QtCommon::Game::RemoveCustomConfiguration(program_id, game_path);
        break;
    case QtCommon::Game::GameListRemoveTarget::CacheStorage:
        QtCommon::Game::RemoveCacheStorage(program_id);
        break;
    }
}

void MainWindow::OnGameListSetPlayTime(u64 program_id) {
    const u64 current_play_time = play_time_manager->GetPlayTime(program_id);

    SetPlayTimeDialog dialog(this, current_play_time);

    if (dialog.exec() == QDialog::Accepted) {
        const u64 total_seconds = dialog.GetTotalSeconds();
        play_time_manager->SetPlayTime(program_id, total_seconds);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }
}

void MainWindow::OnGameListRemovePlayTimeData(u64 program_id) {
    if (QMessageBox::question(this, tr("Remove Play Time Data"), tr("Reset play time?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    play_time_manager->ResetProgramPlayTime(program_id);
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

void MainWindow::OnGameListDumpRomFS(u64 program_id, const std::string& game_path,
                                     DumpRomFSTarget target) {
    const auto failed = [this] {
        QMessageBox::warning(this, tr("RomFS Extraction Failed!"),
                             tr("There was an error copying the RomFS files or the user "
                                "cancelled the operation."));
    };

    const auto loader = Loader::GetLoader(
        *QtCommon::system, QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read));
    if (loader == nullptr) {
        failed();
        return;
    }

    FileSys::VirtualFile packed_update_raw{};
    loader->ReadUpdateRaw(packed_update_raw);

    const auto& installed = QtCommon::system->GetContentProvider();

    u64 title_id{};
    u8 raw_type{};
    if (!SelectRomFSDumpTarget(installed, program_id, &title_id, &raw_type)) {
        failed();
        return;
    }

    const auto type = static_cast<FileSys::ContentRecordType>(raw_type);
    const auto base_nca = installed.GetEntry(title_id, type);
    if (!base_nca) {
        failed();
        return;
    }

    const FileSys::NCA update_nca{packed_update_raw, nullptr};
    if (type != FileSys::ContentRecordType::Program ||
        update_nca.GetStatus() != Loader::ResultStatus::ErrorMissingBKTRBaseRomFS ||
        update_nca.GetTitleId() != FileSys::GetUpdateTitleID(title_id)) {
        packed_update_raw = {};
    }

    const auto base_romfs = base_nca->GetRomFS();
    const auto dump_dir =
        target == DumpRomFSTarget::Normal
            ? Common::FS::GetEdenPath(Common::FS::EdenPath::DumpDir)
            : Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "atmosphere" / "contents";
    const auto romfs_dir = std::format("{:016X}/romfs", title_id);

    const auto path = Common::FS::PathToUTF8String(dump_dir / romfs_dir);

    const FileSys::PatchManager pm{title_id, QtCommon::system->GetFileSystemController(),
                                   installed};
    auto romfs = pm.PatchRomFS(base_nca.get(), base_romfs, type, packed_update_raw, false);

    const auto out = VfsFilesystemCreateDirectoryWrapper(path, FileSys::OpenMode::ReadWrite);

    if (out == nullptr) {
        failed();
        QtCommon::vfs->DeleteDirectory(path);
        return;
    }

    bool ok = false;
    const QStringList selections{tr("Full"), tr("Skeleton")};
    const auto res = QInputDialog::getItem(
        this, tr("Select RomFS Dump Mode"),
        tr("Please select the how you would like the RomFS dumped.<br>Full will copy all of the "
           "files into the new directory while <br>skeleton will only create the directory "
           "structure."),
        selections, 0, false, &ok);
    if (!ok) {
        failed();
        QtCommon::vfs->DeleteDirectory(path);
        return;
    }

    const auto extracted = FileSys::ExtractRomFS(romfs);
    if (extracted == nullptr) {
        failed();
        return;
    }

    const auto full = res == selections.constFirst();

    // The expected required space is the size of the RomFS + 1 GiB
    const auto minimum_free_space = romfs->GetSize() + 0x40000000;

    if (full && Common::FS::GetFreeSpaceSize(path) < minimum_free_space) {
        QMessageBox::warning(this, tr("RomFS Extraction Failed!"),
                             tr("There is not enough free space at %1 to extract the RomFS. Please "
                                "free up space or select a different dump directory at "
                                "Emulation > Configure > System > Filesystem > Dump Root")
                                 .arg(QString::fromStdString(path)));
        return;
    }

    QProgressDialog progress(tr("Extracting RomFS..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setAutoClose(false);
    progress.setAutoReset(false);

    size_t read_size = 0;

    if (RomFSRawCopy(romfs->GetSize(), read_size, progress, extracted, out, full)) {
        progress.close();
        QMessageBox::information(this, tr("RomFS Extraction Succeeded!"),
                                 tr("The operation completed successfully."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(path)));
    } else {
        progress.close();
        failed();
        QtCommon::vfs->DeleteDirectory(path);
    }
}

// END
void MainWindow::OnGameListVerifyIntegrity(const std::string& game_path) {
    QtCommon::Content::VerifyGameContents(game_path);
}

void MainWindow::OnGameListCopyTID(u64 program_id) {
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(QString::fromStdString(std::format("{:016X}", program_id)));
}

void MainWindow::OnGameListCreateShortcut(u64 program_id, const std::string& game_path,
                                          const QtCommon::Game::ShortcutTarget target) {
    // Create shortcu
    std::string arguments = std::format("-g \"{:s}\"", game_path);

    QtCommon::Game::CreateShortcut(game_path, program_id, "", target, arguments, true);
}

void MainWindow::OnGameListOpenDirectory(const QString& directory) {
    // TODO(crueter): QtCommon
    std::filesystem::path fs_path;
    if (directory == QStringLiteral("SDMC")) {
        fs_path =
            Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "Nintendo/Contents/registered";
    } else if (directory == QStringLiteral("UserNAND")) {
        fs_path =
            Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user/Contents/registered";
    } else if (directory == QStringLiteral("SysNAND")) {
        fs_path =
            Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/Contents/registered";
    } else {
        fs_path = directory.toStdString();
    }

    const auto qt_path = QString::fromStdString(Common::FS::PathToUTF8String(fs_path));

    if (!Common::FS::IsDir(fs_path)) {
        QMessageBox::critical(this, tr("Error Opening %1").arg(qt_path),
                              tr("Folder does not exist!"));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(qt_path));
}

void MainWindow::OnGameListAddDirectory() {
    const QString dir_path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
    if (dir_path.isEmpty()) {
        return;
    }

    UISettings::GameDir game_dir{dir_path.toStdString(), false, true};
    if (!UISettings::values.game_dirs.contains(game_dir)) {
        UISettings::values.game_dirs.append(game_dir);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    } else {
        LOG_WARNING(Frontend, "Selected directory is already in the game list");
    }

    OnSaveConfig();
}

void MainWindow::OnGameListShowList(bool show) {
    if (emulation_running && ui->action_Single_Window_Mode->isChecked())
        return;
    game_list->setVisible(show);
    game_list_placeholder->setVisible(!show);
};

void MainWindow::OnGameListOpenPerGameProperties(const std::string& file) {
    u64 title_id{};
    const auto v_file = Core::GetGameFileFromPath(QtCommon::vfs, file);
    const auto loader = Loader::GetLoader(*QtCommon::system, v_file);

    if (loader == nullptr || loader->ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        QMessageBox::information(this, tr("Properties"),
                                 tr("The game properties could not be loaded."));
        return;
    }

    OpenPerGameConfiguration(title_id, file);
}

void MainWindow::OnLinkToRyujinx(const u64& program_id) {
    namespace fs = std::filesystem;

    fs::path ryu_dir;

    // find an existing Ryujinx linked path in config.ini; if it exists, use it as a "hint"
    // If it's not defined in config.ini, use default
    const fs::path existing_path =
        UISettings::values.ryujinx_link_paths
            .value(program_id, QDir(Common::FS::GetLegacyPath(Common::FS::RyujinxDir)))
            .filesystemAbsolutePath();

    // this function also prompts the user to manually specify a portable location
    ryu_dir = QtCommon::FS::GetRyujinxSavePath(existing_path, program_id);

    if (ryu_dir.empty())
        return;

    const std::string user_id = GetProfileIDString();
    if (user_id.empty())
        return;

    const std::string hex_program = std::format("{:016X}", program_id);

    const fs::path eden_dir = FrontendCommon::DataManager::GetDataDir(
                                  FrontendCommon::DataManager::DataDir::Saves, user_id) /
                              hex_program;

    // CheckUnlink basically just checks to see if one or both are linked, and prompts the user to
    // unlink if this is the case.
    // If it returns false, neither dir is linked so it's fine to continue
    if (!QtCommon::FS::CheckUnlink(eden_dir, ryu_dir)) {
        RyujinxDialog dialog(eden_dir, ryu_dir, this);
        if (dialog.exec() == QDialog::Accepted) {
            UISettings::values.ryujinx_link_paths.insert(
                program_id,
                QString::fromStdString(Common::FS::GetRyuPathFromSavePath(ryu_dir).string()));
        }
    } else {
        UISettings::values.ryujinx_link_paths.remove(program_id);
    }

    SaveAllConfigValues();
}

void MainWindow::OnMenuLoadFile() {
    if (is_load_file_select_active) {
        return;
    }

    is_load_file_select_active = true;
    const QString extensions =
        QStringLiteral("*.")
            .append(QtCommon::supported_file_extensions.join(QStringLiteral(" *.")))
            .append(QStringLiteral(" main"));
    const QString file_filter = tr("Switch Executable (%1);;All Files (*.*)",
                                   "%1 is an identifier for the Switch executable file extensions.")
                                    .arg(extensions);
    const QString filename = QFileDialog::getOpenFileName(
        this, tr("Load File"), QString::fromStdString(UISettings::values.roms_path), file_filter);
    is_load_file_select_active = false;

    if (filename.isEmpty()) {
        return;
    }

    UISettings::values.roms_path = QFileInfo(filename).path().toStdString();
    BootGame(filename, ApplicationAppletParameters());
}

void MainWindow::OnMenuLoadFolder() {
    const QString dir_path =
        QFileDialog::getExistingDirectory(this, tr("Open Extracted ROM Directory"));

    if (dir_path.isNull()) {
        return;
    }

    const QDir dir{dir_path};
    const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
    if (matching_main.size() == 1) {
        BootGame(dir.path() + QDir::separator() + matching_main[0], ApplicationAppletParameters());
    } else {
        QMessageBox::warning(this, tr("Invalid Directory Selected"),
                             tr("The directory you have selected does not contain a 'main' file."));
    }
}

void MainWindow::IncrementInstallProgress() {
    install_progress->setValue(install_progress->value() + 1);
}

void MainWindow::OnMenuInstallToNAND() {
    const QString file_filter =
        tr("Installable Switch File (*.nca *.nsp *.nsz *.xci *.xcz);;Nintendo Content Archive "
           "(*.nca);;Nintendo Submission Package (*.nsp *.nsz);;NX Cartridge "
           "Image (*.xci *.xcz)");

    QStringList filenames = QFileDialog::getOpenFileNames(
        this, tr("Install Files"), QString::fromStdString(UISettings::values.roms_path),
        file_filter);

    if (filenames.isEmpty()) {
        return;
    }

    InstallDialog installDialog(this, filenames);
    if (installDialog.exec() == QDialog::Rejected) {
        return;
    }

    const QStringList files = installDialog.GetFiles();

    if (files.isEmpty()) {
        return;
    }

    // Save folder location of the first selected file
    UISettings::values.roms_path = QFileInfo(filenames[0]).path().toStdString();

    int remaining = filenames.size();

    // This would only overflow above 2^51 bytes (2.252 PB)
    int total_size = 0;
    for (const QString& file : files) {
        total_size += static_cast<int>(QFile(file).size() / CopyBufferSize);
    }
    if (total_size < 0) {
        LOG_CRITICAL(Frontend, "Attempting to install too many files, aborting.");
        return;
    }

    QStringList new_files{};         // Newly installed files that do not yet exist in the NAND
    QStringList overwritten_files{}; // Files that overwrote those existing in the NAND
    QStringList failed_files{};      // Files that failed to install due to errors
    bool detected_base_install{};    // Whether a base game was attempted to be installed

    ui->action_Install_File_NAND->setEnabled(false);

    install_progress = new QProgressDialog(QString{}, tr("Cancel"), 0, total_size, this);
    install_progress->setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    install_progress->setAttribute(Qt::WA_DeleteOnClose, true);
    install_progress->setFixedWidth(installDialog.GetMinimumWidth() + 40);
    install_progress->show();

    for (const QString& file : files) {
        install_progress->setWindowTitle(tr("%n file(s) remaining", "", remaining));
        install_progress->setLabelText(
            tr("Installing file \"%1\"...").arg(QFileInfo(file).fileName()));

        QFuture<ContentManager::InstallResult> future;
        ContentManager::InstallResult result;

        if (file.endsWith(QStringLiteral("nsp"), Qt::CaseInsensitive) ||
            file.endsWith(QStringLiteral("nsz"), Qt::CaseInsensitive) ||
            file.endsWith(QStringLiteral("xci"), Qt::CaseInsensitive) ||
            file.endsWith(QStringLiteral("xcz"), Qt::CaseInsensitive)) {
            const auto progress_callback = [this](size_t size, size_t progress) {
                emit UpdateInstallProgress();
                if (install_progress->wasCanceled()) {
                    return true;
                }
                return false;
            };
            future = QtConcurrent::run([&file, progress_callback] {
                return ContentManager::InstallNSP(*QtCommon::system, *QtCommon::vfs,
                                                  file.toStdString(), progress_callback);
            });

            while (!future.isFinished()) {
                QCoreApplication::processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            result = future.result();

        } else {
            result = InstallNCA(file);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        switch (result) {
        case ContentManager::InstallResult::Success:
            new_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::Overwrite:
            overwritten_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::Failure:
            failed_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::BaseInstallAttempted:
            failed_files.append(QFileInfo(file).fileName());
            detected_base_install = true;
            break;
        }

        --remaining;
    }

    install_progress->close();

    if (detected_base_install) {
        QMessageBox::warning(
            this, tr("Install Results"),
            tr("To avoid possible conflicts, we discourage users from installing base games to the "
               "NAND.\nPlease, only use this feature to install updates and DLC."));
    }

    const QString install_results =
        (new_files.isEmpty() ? QString{}
                             : tr("%n file(s) were newly installed\n", "", new_files.size())) +
        (overwritten_files.isEmpty()
             ? QString{}
             : tr("%n file(s) were overwritten\n", "", overwritten_files.size())) +
        (failed_files.isEmpty() ? QString{}
                                : tr("%n file(s) failed to install\n", "", failed_files.size()));

    QMessageBox::information(this, tr("Install Results"), install_results);
    Common::FS::RemoveDirRecursively(Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) /
                                     "game_list");
    game_list->PopulateAsync(UISettings::values.game_dirs);
    ui->action_Install_File_NAND->setEnabled(true);
}

ContentManager::InstallResult MainWindow::InstallNCA(const QString& filename) {
    const QStringList tt_options{tr("System Application"),
                                 tr("System Archive"),
                                 tr("System Application Update"),
                                 tr("Firmware Package (Type A)"),
                                 tr("Firmware Package (Type B)"),
                                 tr("Game"),
                                 tr("Game Update"),
                                 tr("Game DLC"),
                                 tr("Delta Title")};
    bool ok;
    const auto item = QInputDialog::getItem(
        this, tr("Select NCA Install Type..."),
        tr("Please select the type of title you would like to install this NCA as:\n(In "
           "most instances, the default 'Game' is fine.)"),
        tt_options, 5, false, &ok);

    auto index = tt_options.indexOf(item);
    if (!ok || index == -1) {
        QMessageBox::warning(this, tr("Failed to Install"),
                             tr("The title type you selected for the NCA is invalid."));
        return ContentManager::InstallResult::Failure;
    }

    // If index is equal to or past Game, add the jump in TitleType.
    if (index >= 5) {
        index += static_cast<size_t>(FileSys::TitleType::Application) -
                 static_cast<size_t>(FileSys::TitleType::FirmwarePackageB);
    }

    const bool is_application = index >= static_cast<s32>(FileSys::TitleType::Application);
    const auto& fs_controller = QtCommon::system->GetFileSystemController();
    auto* registered_cache = is_application ? fs_controller.GetUserNANDContents()
                                            : fs_controller.GetSystemNANDContents();

    const auto progress_callback = [this](size_t size, size_t progress) {
        emit UpdateInstallProgress();
        if (install_progress->wasCanceled()) {
            return true;
        }
        return false;
    };
    return ContentManager::InstallNCA(*QtCommon::vfs, filename.toStdString(), *registered_cache,
                                      static_cast<FileSys::TitleType>(index), progress_callback);
}

void MainWindow::OnMenuRecentFile() {
    QAction* action = qobject_cast<QAction*>(sender());
    assert(action);

    const QString filename = action->data().toString();
    if (QFileInfo::exists(filename)) {
        BootGame(filename, ApplicationAppletParameters());
    } else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, tr("File not found"),
                                 tr("File \"%1\" not found").arg(filename));

        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void MainWindow::OnStartGame() {
    PreventOSSleep();

    QtCommon::emu_thread->SetRunning(true);

    UpdateMenuState();
    OnTasStateChanged();

    play_time_manager->SetProgramId(QtCommon::system->GetApplicationProcessProgramID());
    play_time_manager->Start();

    discord_rpc->Update();
    Common::FeralGamemode::Start();
}

void MainWindow::OnRestartGame() {
    if (!QtCommon::system->IsPoweredOn()) {
        return;
    }

    if (ConfirmShutdownGame()) {
        // Make a copy since ShutdownGame edits game_path
        const auto current_game = QString(current_game_path);
        ShutdownGame();
        BootGame(current_game, ApplicationAppletParameters());
    }
}

void MainWindow::OnPauseGame() {
    QtCommon::emu_thread->SetRunning(false);
    play_time_manager->Stop();
    UpdateMenuState();
    AllowOSSleep();
    Common::FeralGamemode::Stop();
}

void MainWindow::OnPauseContinueGame() {
    if (emulation_running) {
        if (QtCommon::emu_thread->IsRunning()) {
            OnPauseGame();
        } else {
            OnStartGame();
        }
    }
}

void MainWindow::OnStopGame() {
    if (ConfirmShutdownGame()) {
        play_time_manager->Stop();
        // Update game list to show new play time
        game_list->PopulateAsync(UISettings::values.game_dirs);
        if (OnShutdownBegin()) {
            OnShutdownBeginDialog();
        } else {
            OnEmulationStopped();
        }
    }
}

bool MainWindow::ConfirmShutdownGame() {
    if (UISettings::values.confirm_before_stopping.GetValue() == ConfirmStop::Ask_Always) {
        if (QtCommon::system->GetExitLocked()) {
            if (!ConfirmForceLockedExit()) {
                return false;
            }
        } else {
            if (!ConfirmChangeGame()) {
                return false;
            }
        }
    } else {
        if (UISettings::values.confirm_before_stopping.GetValue() ==
                ConfirmStop::Ask_Based_On_Game &&
            QtCommon::system->GetExitLocked()) {
            if (!ConfirmForceLockedExit()) {
                return false;
            }
        }
    }
    return true;
}

void MainWindow::OnLoadComplete() {
    loading_screen->OnLoadComplete();

    perf_overlay = new PerformanceOverlay(this);
    perf_overlay->setVisible(ui->action_Show_Performance_Overlay->isChecked());

    connect(perf_overlay, &PerformanceOverlay::closed, perf_overlay,
            [this]() { ui->action_Show_Performance_Overlay->setChecked(false); });
}

void MainWindow::OnExecuteProgram(std::size_t program_index) {
    const u64 previous_program_id = QtCommon::system->GetApplicationProcessProgramID();

    const auto current_path =
        QString::fromStdString(QtCommon::system->GetCurrentApplicationFilePath());

    LOG_INFO(Frontend, "ExecuteProgram requested, program_index={} previous_program_id={:016X}",
             program_index, previous_program_id);

    ShutdownGame();

    auto params = ApplicationAppletParameters();
    params.program_index = static_cast<s32>(program_index);
    params.launch_type = Service::AM::LaunchType::ApplicationInitiated;

    if (previous_program_id > static_cast<u64>(Service::AM::AppletProgramId::MaxProgramId)) {
        params.previous_program_index =
            static_cast<s32>(previous_program_id - FileSys::GetBaseTitleID(previous_program_id));
        params.program_id = previous_program_id;
    }

    const auto filename = current_path.isEmpty() ? last_filename_booted : current_path;

    LOG_DEBUG(Frontend, "ExecuteProgram booting from path: {}", filename.toStdString());

    BootGame(filename, params);
}

void MainWindow::OnExit() {
    ShutdownGame();
}

void MainWindow::OnSaveConfig() {
    QtCommon::system->ApplySettings();
    SaveAllConfigValues();
}

void MainWindow::ErrorDisplayDisplayError(QString error_code, QString error_text) {
    error_applet = new OverlayDialog(render_window, *QtCommon::system, error_code, error_text,
                                     QString{}, tr("OK"), Qt::AlignLeft | Qt::AlignVCenter);
    SCOPE_EXIT {
        error_applet->deleteLater();
        error_applet = nullptr;
    };
    error_applet->exec();

    emit ErrorDisplayFinished();
}

void MainWindow::ErrorDisplayRequestExit() {
    if (error_applet) {
        error_applet->reject();
    }
}

void MainWindow::OnMenuReportCompatibility() {
    QtCommon::Frontend::Critical(
        tr("Function Disabled"),
        tr("Compatibility list reporting is currently disabled. Check back later!"));

    // #if defined(ARCHITECTURE_x86_64) && !defined(__APPLE__)
    //     const auto& caps = g_cpu_caps;
    //     const bool has_fma = caps.fma;
    //     const auto processor_count = std::thread::hardware_concurrency();
    //     const bool has_4threads = processor_count == 0 || processor_count >= 4;
    //     const bool has_8gb_ram = Common::GetMemInfo().TotalPhysicalMemory >= 8_GiB;
    //     const bool has_broken_vulkan = UISettings::values.has_broken_vulkan;

    //     if (!has_fma || !has_4threads || !has_8gb_ram || has_broken_vulkan) {
    //         QMessageBox::critical(this, tr("Hardware requirements not met"),
    //                               tr("Your system does not meet the recommended hardware
    //                               requirements. "
    //                                  "Compatibility reporting has been disabled."));
    //         return;
    //     }

    //     if (!Settings::values.eden_token.GetValue().empty() &&
    //         !Settings::values.eden_username.GetValue().empty()) {
    //     } else {
    //         QMessageBox::critical(
    //             this, tr("Missing yuzu Account"),
    //             tr("In order to submit a game compatibility test case, you must set up your web
    //             token "
    //                "and "
    //                "username.<br><br/>To link your eden account, go to Emulation &gt;
    //                Configuration "
    //                "&gt; "
    //                "Web."));
    //     }
    // #else
    //     QMessageBox::critical(this, tr("Hardware requirements not met"),
    //                           tr("Your system does not meet the recommended hardware
    //                           requirements. "
    //                              "Compatibility reporting has been disabled."));
    // #endif
}

void MainWindow::OpenURL(const QUrl& url) {
    const bool open = QDesktopServices::openUrl(url);
    if (!open) {
        QMessageBox::warning(this, tr("Error opening URL"),
                             tr("Unable to open the URL \"%1\".").arg(url.toString()));
    }
}

void MainWindow::OnOpenModsPage() {
    OpenURL(QUrl(QStringLiteral("https://github.com/eden-emulator/yuzu-mod-archive")));
}

void MainWindow::OnOpenUserHandbook() {
    OpenURL(QUrl(QStringLiteral("https://git.eden-emu.dev/eden-emu/eden/src/branch/master/docs/user/README.md")));
}

void MainWindow::ToggleFullscreen() {
    if (!emulation_running) {
        return;
    }
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    } else {
        HideFullscreen();
    }
}

// We're going to return the screen that the given window has the most pixels on
static QScreen* GuessCurrentScreen(QWidget* window) {
    const QList<QScreen*> screens = QGuiApplication::screens();
    return *std::max_element(
        screens.cbegin(), screens.cend(), [window](const QScreen* left, const QScreen* right) {
            const QSize left_size = left->geometry().intersected(window->geometry()).size();
            const QSize right_size = right->geometry().intersected(window->geometry()).size();
            return (left_size.height() * left_size.width()) <
                   (right_size.height() * right_size.width());
        });
}

bool MainWindow::UsingExclusiveFullscreen() {
    return Settings::values.fullscreen_mode.GetValue() == Settings::FullscreenMode::Exclusive ||
           QGuiApplication::platformName() == QStringLiteral("wayland") ||
           QGuiApplication::platformName() == QStringLiteral("wayland-egl");
}

void MainWindow::ShowFullscreen() {
    const auto show_fullscreen = [this](QWidget* window) {
        if (UsingExclusiveFullscreen()) {
            window->showFullScreen();
            return;
        }
        window->hide();
        window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
        const auto screen_geometry = GuessCurrentScreen(window)->geometry();
        window->setGeometry(screen_geometry.x(), screen_geometry.y(), screen_geometry.width(),
                            screen_geometry.height() + 1);
        window->raise();
        window->showNormal();
    };

    if (ui->action_Single_Window_Mode->isChecked()) {
        UISettings::values.geometry = saveGeometry();

        ui->menubar->hide();
        statusBar()->hide();

        show_fullscreen(this);
    } else {
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
        show_fullscreen(render_window);
    }
}

void MainWindow::HideFullscreen() {
    if (ui->action_Single_Window_Mode->isChecked()) {
        if (UsingExclusiveFullscreen()) {
            showNormal();
            restoreGeometry(UISettings::values.geometry);
        } else {
            hide();
            setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
            restoreGeometry(UISettings::values.geometry);
            raise();
            show();
        }

        statusBar()->setVisible(ui->action_Show_Status_Bar->isChecked());
        ui->menubar->show();
        FitToScreenUnderGamescope(this);
    } else {
        if (UsingExclusiveFullscreen()) {
            render_window->showNormal();
            render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
        } else {
            render_window->hide();
            render_window->setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
            render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
            render_window->raise();
            render_window->show();
        }
    }
}

void MainWindow::FitToScreenUnderGamescope(QWidget* window) {
    // Under gamescope a window is scaled to fit together with any tooltip reaching past it, so
    // a smaller window grows and shrinks on hover: filling the screen keeps them inside
    if (!Gamescope::IsRunningUnder()) {
        return;
    }
    if (window->isFullScreen()) {
        return;
    }
    QScreen* screen = window->screen() ? window->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        window->setGeometry(screen->geometry());
    }
}

void MainWindow::ToggleWindowMode() {
    if (ui->action_Single_Window_Mode->isChecked()) {
        // Render in the main window...
        render_window->BackupGeometry();
        ui->horizontalLayout->addWidget(render_window);
        render_window->setFocusPolicy(Qt::StrongFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->setFocus();
            game_list->hide();
        }

    } else {
        // Render in a separate window...
        ui->horizontalLayout->removeWidget(render_window);
        render_window->setParent(nullptr);
        render_window->setFocusPolicy(Qt::NoFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->RestoreGeometry();
            game_list->show();
        }
    }
}

void MainWindow::ResetWindowSize(u32 width, u32 height) {
    const auto aspect_ratio = Layout::EmulationAspectRatio(Settings::values.aspect_ratio.GetValue(),
                                                           float(height) / width);
    if (!ui->action_Single_Window_Mode->isChecked()) {
        render_window->resize(height / aspect_ratio, height);
    } else {
        const bool show_status_bar = ui->action_Show_Status_Bar->isChecked();
        const auto status_bar_height = show_status_bar ? statusBar()->height() : 0;
        resize(height / aspect_ratio, height + menuBar()->height() + status_bar_height);
    }
}

void MainWindow::ResetWindowSize720() {
    ResetWindowSize(Layout::ScreenUndocked::Width, Layout::ScreenUndocked::Height);
}

void MainWindow::ResetWindowSize900() {
    ResetWindowSize(1600U, 900U);
}

void MainWindow::ResetWindowSize1080() {
    ResetWindowSize(Layout::ScreenDocked::Width, Layout::ScreenDocked::Height);
}

void MainWindow::SetGameListMode(Settings::GameListMode mode) {
    ui->action_Grid_View->setChecked(mode == Settings::GameListMode::GridView);
    ui->action_Tree_View->setChecked(mode == Settings::GameListMode::TreeView);
    ui->action_Carousel_View->setChecked(mode == Settings::GameListMode::CarouselView);

    UISettings::values.game_list_mode = mode;
    ui->action_Show_Game_Name->setEnabled(mode != Settings::GameListMode::TreeView);

    CheckIconSize();
    game_list->ResetViewMode();
}

void MainWindow::SetGridView() {
    SetGameListMode(Settings::GameListMode::GridView);
}

void MainWindow::SetTreeView() {
    SetGameListMode(Settings::GameListMode::TreeView);
}

void MainWindow::SetCarouselView() {
    SetGameListMode(Settings::GameListMode::CarouselView);
}

void MainWindow::CheckIconSize() {
    // When in grid/carousel view mode, with text off
    // there is no point in having icons turned off
    auto actions = game_size_actions->actions();
    if (UISettings::values.game_list_mode.GetValue() != Settings::GameListMode::TreeView &&
        !UISettings::values.show_game_name.GetValue()) {
        u32 newSize = UISettings::values.game_icon_size.GetValue();
        if (newSize == 0) {
            newSize = 64;
            UISettings::values.game_icon_size.SetValue(newSize);
        }

        // Then disable the "none" action and update that menu.
        for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
            const auto current_size = newSize;
            const auto size = default_game_icon_sizes[i].first;
            if (current_size == size)
                actions.at(i)->setChecked(true);
        }

        // Update this if you add anything before None.
        actions.at(0)->setEnabled(false);
    } else {
        actions.at(0)->setEnabled(true);
    }
}

void MainWindow::ToggleShowGameName() {
    auto& setting = UISettings::values.show_game_name;
    const bool newValue = !setting.GetValue();
    ui->action_Show_Game_Name->setChecked(newValue);
    setting.SetValue(newValue);

    CheckIconSize();

    game_list->UpdateIconSizes();
    game_list->RefreshGameDirectory();
}

void MainWindow::OnConfigure() {
    const auto old_theme = UISettings::values.theme;
    const bool old_discord_presence = UISettings::values.enable_discord_presence.GetValue();
    const auto old_language_index = Settings::values.language_index.GetValue();
    const bool old_gamemode = UISettings::values.enable_gamemode.GetValue();

    // Taken before the dialog exists (its Controls tab writes the players), from the copy in
    // use: during a game the global copy holds stale routing, which would invent swaps
    const auto players_before = Settings::values.players.GetValue();
    Settings::SetConfiguringGlobal(true);
    ConfigureDialog configure_dialog(this, hotkey_registry, input_subsystem.get(),
                                     vk_device_records, *QtCommon::system,
                                     !multiplayer_state->IsHostingPublicRoom());
    // Opened from a player chip: straight to that player's tab
    if (open_player_tab >= 0) {
        configure_dialog.OpenPlayerTab(open_player_tab);
        open_player_tab = -1;
    }
    connect(&configure_dialog, &ConfigureDialog::LanguageChanged, this,
            &MainWindow::OnLanguageChanged);
    connect(&configure_dialog, &ConfigureDialog::ExternalContentDirsChanged, this,
            &MainWindow::OnGameListRefresh);

    const auto result = configure_dialog.exec();
    if (result != QDialog::Accepted && !UISettings::values.configuration_applied &&
        !UISettings::values.reset_to_defaults) {
        // Runs if the user hit Cancel or closed the window, and did not ever press the Apply button
        // or `Reset to Defaults` button
        return;
    } else if (result == QDialog::Accepted) {
        // Only apply new changes if user hit Okay
        // This is here to avoid applying changes if the user hit Apply, made some changes, then hit
        // Cancel
        configure_dialog.ApplyConfiguration();
    } else if (UISettings::values.reset_to_defaults) {
        LOG_INFO(Frontend, "Resetting all settings to defaults");
        if (!Common::FS::RemoveFile(config->GetConfigFilePath())) {
            LOG_WARNING(Frontend, "Failed to remove configuration file");
        }
        if (!Common::FS::RemoveDirContentsRecursively(
                Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "custom")) {
            LOG_WARNING(Frontend, "Failed to remove custom configuration files");
        }
        if (!Common::FS::RemoveDirRecursively(
                Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_list")) {
            LOG_WARNING(Frontend, "Failed to remove game metadata cache files");
        }

        // Explicitly save the game directories, since reinitializing config does not explicitly do
        // so.
        QVector<UISettings::GameDir> old_game_dirs = std::move(UISettings::values.game_dirs);
        QVector<u64> old_favorited_ids = std::move(UISettings::values.favorited_ids);

        Settings::values.disabled_addons.clear();

        RestoreConsoleProfile();
        config = std::make_unique<QtConfig>();
        UISettings::values.reset_to_defaults = false;

        UISettings::values.game_dirs = std::move(old_game_dirs);
        UISettings::values.favorited_ids = std::move(old_favorited_ids);

        InitializeRecentFileMenuActions();

        SetDefaultUIGeometry();
        RestoreUIState();
    }
    MergeConsoleProfileEdits(players_before);
    ui->action_Show_Controller_Overlay->setChecked(
        Settings::values.console_debug_overlay.GetValue());
    ui->action_Show_Controller_Hints->setChecked(Settings::values.console_hints.GetValue());
    InitializeHotkeys();

    if (UISettings::values.theme != old_theme) {
        UpdateUITheme();
    }
    if (UISettings::values.enable_discord_presence.GetValue() != old_discord_presence) {
        SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
    }
    if (UISettings::values.enable_gamemode.GetValue() != old_gamemode) {
        SetGamemodeEnabled(UISettings::values.enable_gamemode.GetValue());
    }

    if (!multiplayer_state->IsHostingPublicRoom()) {
        multiplayer_state->UpdateCredentials();
    }

    emit UpdateThemedIcons();

    const auto reload = UISettings::values.is_game_list_reload_pending.exchange(false);
    if (reload || Settings::values.language_index.GetValue() != old_language_index) {
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }

    UISettings::values.configuration_applied = false;

    SaveAllConfigValues();

    if ((UISettings::values.hide_mouse || Settings::values.mouse_panning) && emulation_running) {
        render_window->installEventFilter(render_window);
        render_window->setAttribute(Qt::WA_Hover, true);
    } else {
        render_window->removeEventFilter(render_window);
        render_window->setAttribute(Qt::WA_Hover, false);
    }

    if (UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
    }

    // Restart camera config
    if (emulation_running) {
        render_window->FinalizeCamera();
        render_window->InitializeCamera();
    }

    if (!UISettings::values.has_broken_vulkan) {
        renderer_status_button->setEnabled(!emulation_running);
    }

    UpdateStatusButtons();
    controller_dialog->refreshConfiguration();
    QtCommon::system->ApplySettings();
}

void MainWindow::OnConfigureTas() {
    ConfigureTasDialog dialog(this);
    const auto result = dialog.exec();

    if (result != QDialog::Accepted && !UISettings::values.configuration_applied) {
        Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
        return;
    } else if (result == QDialog::Accepted) {
        dialog.ApplyConfiguration();
        OnSaveConfig();
    }
}

void MainWindow::OnTasStartStop() {
    if (!emulation_running) {
        return;
    }

    // Disable system buttons to prevent TAS from executing a hotkey
    auto* controller =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    controller->ResetSystemButtons();

    input_subsystem->GetTas()->StartStop();
    OnTasStateChanged();
}

void MainWindow::OnTasRecord() {
    if (!emulation_running) {
        return;
    }
    if (is_tas_recording_dialog_active) {
        return;
    }

    // Disable system buttons to prevent TAS from recording a hotkey
    auto* controller =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    controller->ResetSystemButtons();

    const bool is_recording = input_subsystem->GetTas()->Record();
    if (!is_recording) {
        if (Settings::values.tas_show_recording_dialog.GetValue()) {
            is_tas_recording_dialog_active = true;

            bool answer = question(this, tr("TAS Recording"), tr("Overwrite file of player 1?"),
                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            input_subsystem->GetTas()->SaveRecording(answer);
            is_tas_recording_dialog_active = false;
        } else {
            input_subsystem->GetTas()->SaveRecording(true);
        }
    }
    OnTasStateChanged();
}

void MainWindow::OnTasReset() {
    input_subsystem->GetTas()->Reset();
}

void MainWindow::OnToggleDockedMode() {
    const bool is_docked = Settings::IsDockedMode();
    auto* player_1 =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    auto* handheld =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);

    if (!is_docked && handheld->IsConnected()) {
        QMessageBox::warning(this, tr("Invalid config detected"),
                             tr("Handheld controller can't be used on docked mode. Pro "
                                "controller will be selected."));
        handheld->Disconnect();
        player_1->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);
        player_1->Connect();
        controller_dialog->refreshConfiguration();
    }

    Settings::values.use_docked_mode.SetValue(is_docked ? Settings::ConsoleMode::Handheld
                                                        : Settings::ConsoleMode::Docked);
    UpdateDockedButton();
    OnDockedModeChanged(is_docked, !is_docked, *QtCommon::system);
}

void MainWindow::OnToggleGpuAccuracy() {
    switch (Settings::values.gpu_accuracy.GetValue()) {
    case Settings::GpuAccuracy::Low:
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::High);
        break;
    case Settings::GpuAccuracy::High:
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
        break;
    }

    QtCommon::system->ApplySettings();
    UpdateGPUAccuracyButton();
}

void MainWindow::OnMute() {
    Settings::values.audio_muted = !Settings::values.audio_muted;
    UpdateVolumeUI();
}

void MainWindow::OnDecreaseVolume() {
    Settings::values.audio_muted = false;
    const auto current_volume = static_cast<s32>(Settings::values.volume.GetValue());
    int step = 5;
    if (current_volume <= 30) {
        step = 2;
    }
    if (current_volume <= 6) {
        step = 1;
    }
    Settings::values.volume.SetValue((std::max)(current_volume - step, 0));
    UpdateVolumeUI();
}

void MainWindow::OnIncreaseVolume() {
    Settings::values.audio_muted = false;
    const auto current_volume = static_cast<s32>(Settings::values.volume.GetValue());
    int step = 5;
    if (current_volume < 30) {
        step = 2;
    }
    if (current_volume < 6) {
        step = 1;
    }
    Settings::values.volume.SetValue(current_volume + step);
    UpdateVolumeUI();
}

void MainWindow::OnToggleAdaptingFilter() {
    auto filter = Settings::values.scaling_filter.GetValue();
    filter = Settings::ScalingFilter(u32(filter) + 1);
    if (u32(filter) > u32(Settings::EnumMetadata<Settings::ScalingFilter>::GetLast()))
        filter = Settings::EnumMetadata<Settings::ScalingFilter>::GetFirst();
    Settings::values.scaling_filter.SetValue(filter);
    filter_status_button->setChecked(true);
    UpdateFilterText();
}

void MainWindow::OnToggleGraphicsAPI() {
    auto api = Settings::values.renderer_backend.GetValue();
    switch (api) {
#ifdef HAS_OPENGL
    case Settings::RendererBackend::Vulkan:
        api = Settings::RendererBackend::OpenGL_GLSL;
        break;
    case Settings::RendererBackend::OpenGL_GLSL:
        api = Settings::RendererBackend::OpenGL_GLASM;
        break;
    case Settings::RendererBackend::OpenGL_GLASM:
        api = Settings::RendererBackend::OpenGL_SPIRV;
        break;
    case Settings::RendererBackend::OpenGL_SPIRV:
        api = Settings::RendererBackend::Null;
        break;
#else
    case Settings::RendererBackend::Vulkan:
        api = Settings::RendererBackend::Null;
        break;
#endif
    case Settings::RendererBackend::Null:
        api = Settings::RendererBackend::Vulkan;
        break;
    default:
        break;
    }
    Settings::values.renderer_backend.SetValue(api);
    renderer_status_button->setChecked(api == Settings::RendererBackend::Vulkan);
    UpdateAPIText();
}

void MainWindow::OnConfigurePerGame() {
    const u64 title_id = QtCommon::system->GetApplicationProcessProgramID();
    OpenPerGameConfiguration(title_id, current_game_path.toStdString());
}

void MainWindow::OpenPerGameConfiguration(u64 title_id, const std::string& file_name) {
    const auto v_file = Core::GetGameFileFromPath(QtCommon::vfs, file_name);

    Settings::SetConfiguringGlobal(false);
    ConfigurePerGame dialog(this, title_id, file_name, vk_device_records, *QtCommon::system);
    dialog.LoadFromFile(v_file);

    const auto result = dialog.exec();

    if (result != QDialog::Accepted && !UISettings::values.configuration_applied) {
        Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
        return;
    } else if (result == QDialog::Accepted) {
        // A game with its own controller config keeps its own in the file, not the engine's
        // runtime players (as a global save does, see SaveAllConfigValues)
        std::optional<std::array<Settings::PlayerInput, 10>> engine_players;
        if (console_profile && game_profiles && QtCommon::system->IsPoweredOn() &&
            QtCommon::system->GetApplicationProcessProgramID() == title_id &&
            !Settings::values.players.UsingGlobal()) {
            auto& game_players = Settings::values.players.GetValue();
            engine_players = game_players;
            game_players = *game_profiles;
        }
        dialog.ApplyConfiguration();
        if (engine_players && !Settings::values.players.UsingGlobal()) {
            Settings::values.players.GetValue() = *engine_players;
        }
        // (The running game's own value; otherwise the View menu keeps showing the global one)
        if (QtCommon::system->IsPoweredOn()) {
            ui->action_Show_Controller_Overlay->setChecked(
                Settings::values.console_debug_overlay.GetValue());
            ui->action_Show_Controller_Hints->setChecked(Settings::values.console_hints.GetValue());
        }
    }

    const auto reload = UISettings::values.is_game_list_reload_pending.exchange(false);
    if (reload) {
        OnGameListRefresh();
    }

    // Do not cause the global config to write local settings into the config file
    const bool is_powered_on = QtCommon::system->IsPoweredOn();
    Settings::RestoreGlobalState(is_powered_on);
    QtCommon::system->HIDCore().ReloadInputDevices();

    UISettings::values.configuration_applied = false;

    if (!is_powered_on) {
        SaveAllConfigValues();
    }
}

void MainWindow::OnLoadAmiibo() {
    if (QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning() ||
        is_amiibo_file_select_active)
        return;

    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();

    // Remove amiibo if one is connected
    if (virtual_amiibo->GetCurrentState() == InputCommon::VirtualAmiibo::State::TagNearby) {
        virtual_amiibo->CloseAmiibo();
        QMessageBox::warning(this, tr("Amiibo"), tr("The current amiibo has been removed"));
        return;
    }

    if (virtual_amiibo->GetCurrentState() != InputCommon::VirtualAmiibo::State::WaitingForAmiibo) {
        QMessageBox::warning(this, tr("Error"), tr("The current game is not looking for amiibos"));
        return;
    }

    is_amiibo_file_select_active = true;
    const QString extensions{QStringLiteral("*.bin")};
    const QString file_filter = tr("Amiibo File (%1);; All Files (*.*)").arg(extensions);
    const QString filename = QFileDialog::getOpenFileName(this, tr("Load Amiibo"), {}, file_filter);
    is_amiibo_file_select_active = false;

    if (filename.isEmpty()) {
        return;
    }

    LoadAmiibo(filename);
}

// TODO(crueter): does this need to be ported to QML?
bool MainWindow::question(QWidget* parent, const QString& title, const QString& text,
                          QMessageBox::StandardButtons buttons,
                          QMessageBox::StandardButton defaultButton) {
    QMessageBox* box_dialog = new QMessageBox(parent);
    box_dialog->setWindowTitle(title);
    box_dialog->setText(text);
    box_dialog->setStandardButtons(buttons);
    box_dialog->setDefaultButton(defaultButton);

    ControllerNavigation* controller_navigation =
        new ControllerNavigation(QtCommon::system->HIDCore(), box_dialog);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent,
            [box_dialog](Qt::Key key) {
                QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                QCoreApplication::postEvent(box_dialog, event);
            });
    int res = box_dialog->exec();

    controller_navigation->UnloadController();
    return res == QMessageBox::Yes;
}

void MainWindow::LoadAmiibo(const QString& filename) {
    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();
    const QString title = tr("Error loading Amiibo data");
    // Remove amiibo if one is connected
    if (virtual_amiibo->GetCurrentState() == InputCommon::VirtualAmiibo::State::TagNearby) {
        virtual_amiibo->CloseAmiibo();
        QMessageBox::warning(this, tr("Amiibo"), tr("The current amiibo has been removed"));
        return;
    }

    switch (virtual_amiibo->LoadAmiibo(filename.toStdString())) {
    case InputCommon::VirtualAmiibo::Info::NotAnAmiibo:
        QMessageBox::warning(this, title, tr("The selected file is not a valid amiibo"));
        break;
    case InputCommon::VirtualAmiibo::Info::UnableToLoad:
        QMessageBox::warning(this, title, tr("The selected file is already on use"));
        break;
    case InputCommon::VirtualAmiibo::Info::WrongDeviceState:
        QMessageBox::warning(this, title, tr("The current game is not looking for amiibos"));
        break;
    case InputCommon::VirtualAmiibo::Info::Unknown:
        QMessageBox::warning(this, title, tr("An unknown error occurred"));
        break;
    default:
        break;
    }
}

void MainWindow::OnOpenRootDataFolder() {
    QtCommon::Game::OpenRootDataFolder();
}

void MainWindow::OnOpenNANDFolder() {
    QtCommon::Game::OpenNANDFolder();
}

void MainWindow::OnOpenSDMCFolder() {
    QtCommon::Game::OpenSDMCFolder();
}

void MainWindow::OnOpenModFolder() {
    QtCommon::Game::OpenModFolder();
}

void MainWindow::OnOpenLogFolder() {
    QtCommon::Game::OpenLogFolder();
}

void MainWindow::OnVerifyInstalledContents() {
    QtCommon::Content::VerifyInstalledContents();
}

void MainWindow::OnInstallFirmware() {
    QtCommon::Content::InstallFirmware();
    OnCheckFirmwareDecryption();
}

void MainWindow::OnInstallFirmwareFromZIP() {
    QtCommon::Content::InstallFirmwareZip();
    OnCheckFirmwareDecryption();
}

// TODO(crueter): QtCommon this: game list populate can be a signal?
void MainWindow::OnInstallDecryptionKeys() {
    // Don't do this while emulation is running.
    if (QtCommon::emu_thread != nullptr && QtCommon::emu_thread->IsRunning())
        return;

    QtCommon::Content::InstallKeys();

    game_list->PopulateAsync(UISettings::values.game_dirs);
    OnCheckFirmwareDecryption();
}

void MainWindow::OnAbout() {
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}

void MainWindow::OnEdenDependencies() {
    DepsDialog depsDialog(this);
    depsDialog.exec();
}

void MainWindow::OnDataDialog() {
    DataDialog dataDialog(this);
    dataDialog.exec();

    // refresh stuff in case it was cleared
    OnGameListRefresh();
}

void MainWindow::OnToggleFilterBar() {
    game_list->SetFilterVisible(ui->action_Show_Filter_Bar->isChecked());
    if (ui->action_Show_Filter_Bar->isChecked())
        game_list->SetFilterFocus();
    else
        game_list->ClearFilter();
}

void MainWindow::OnToggleStatusBar() {
    statusBar()->setVisible(ui->action_Show_Status_Bar->isChecked());
}

void MainWindow::OnTogglePerfOverlay() {
    if (perf_overlay)
        perf_overlay->setVisible(ui->action_Show_Performance_Overlay->isChecked());
}

#ifdef HAS_RESHADE
void MainWindow::OnPostProcessingShaders() {
    if (post_processing_dialog == nullptr) {
        post_processing_dialog = new ConfigurePostProcessing(this);
        connect(post_processing_dialog, &QDialog::finished, post_processing_dialog, [this]() {
            post_processing_dialog->deleteLater();
            post_processing_dialog = nullptr;
            UpdatePostShaderText();
        });
    }

    post_processing_dialog->show();
    post_processing_dialog->raise();
    post_processing_dialog->activateWindow();
}
#endif

void MainWindow::OnGameListRefresh() {
    // Resets metadata cache and reloads
    QtCommon::Game::ResetMetadata(false);
    game_list->RefreshGameDirectory();
    game_list->RefreshExternalContent();
    SetFirmwareVersion();
}

void MainWindow::LaunchFirmwareApplet(u64 raw_program_id,
                                      std::optional<Service::NFP::CabinetMode> cabinet_mode) {
    auto const program_id = Service::AM::AppletProgramId(raw_program_id);
    auto result = FirmwareManager::VerifyFirmware(*QtCommon::system.get());
    using namespace QtCommon::StringLookup;
    switch (result) {
    case FirmwareManager::ErrorFirmwareMissing:
        QMessageBox::warning(this, tr("No firmware available"),
                             Lookup(FwCheckErrorFirmwareMissing));
        return;
    case FirmwareManager::ErrorFirmwareCorrupted:
        QMessageBox::warning(this, tr("Firmware Corrupted"), Lookup(FwCheckErrorFirmwareCorrupted));
        return;
    default:
        break;
    }
    auto bis_system = QtCommon::system->GetFileSystemController().GetSystemNANDContents();
    if (auto applet_nca =
            bis_system->GetEntry(u64(program_id), FileSys::ContentRecordType::Program);
        applet_nca) {
        if (auto const applet_id =
                [program_id] {
                    using namespace Service::AM;
                    switch (program_id) {
                    case AppletProgramId::OverlayDisplay:
                        return AppletId::OverlayDisplay;
                    case AppletProgramId::QLaunch:
                        return AppletId::QLaunch;
                    case AppletProgramId::Starter:
                        return AppletId::Starter;
                    case AppletProgramId::Auth:
                        return AppletId::Auth;
                    case AppletProgramId::Cabinet:
                        return AppletId::Cabinet;
                    case AppletProgramId::Controller:
                        return AppletId::Controller;
                    case AppletProgramId::DataErase:
                        return AppletId::DataErase;
                    case AppletProgramId::Error:
                        return AppletId::Error;
                    case AppletProgramId::NetConnect:
                        return AppletId::NetConnect;
                    case AppletProgramId::ProfileSelect:
                        return AppletId::ProfileSelect;
                    case AppletProgramId::SoftwareKeyboard:
                        return AppletId::SoftwareKeyboard;
                    case AppletProgramId::MiiEdit:
                        return AppletId::MiiEdit;
                    case AppletProgramId::Web:
                        return AppletId::Web;
                    case AppletProgramId::Shop:
                        return AppletId::Shop;
                    case AppletProgramId::PhotoViewer:
                        return AppletId::PhotoViewer;
                    case AppletProgramId::Settings:
                        return AppletId::Settings;
                    case AppletProgramId::OfflineWeb:
                        return AppletId::OfflineWeb;
                    case AppletProgramId::LoginShare:
                        return AppletId::LoginShare;
                    case AppletProgramId::WebAuth:
                        return AppletId::WebAuth;
                    case AppletProgramId::MyPage:
                        return AppletId::MyPage;
                    default:
                        return AppletId::None;
                    }
                }();
            applet_id != Service::AM::AppletId::None) {
            QtCommon::system->GetFrontendAppletHolder().SetCurrentAppletId(applet_id);
            if (cabinet_mode)
                QtCommon::system->GetFrontendAppletHolder().SetCabinetMode(*cabinet_mode);
            // ?
            auto const filename = QString::fromStdString((applet_nca->GetFullPath()));
            UISettings::values.roms_path = QFileInfo(filename).path().toStdString();
            BootGame(filename, LibraryAppletParameters(u64(program_id), applet_id));
        } else {
            QMessageBox::warning(this, tr("Unknown applet"),
                                 tr("Applet doesn't map to a known value."));
        }
    } else {
        QMessageBox::warning(this, tr("Record not found"),
                             tr("Applet not found. Please reinstall firmware."));
    }
}

void MainWindow::OnCreateHomeMenuDesktopShortcut() {
    QtCommon::Game::CreateHomeMenuShortcut(QtCommon::Game::ShortcutTarget::Desktop);
}

void MainWindow::OnCreateHomeMenuApplicationMenuShortcut() {
    QtCommon::Game::CreateHomeMenuShortcut(QtCommon::Game::ShortcutTarget::Applications);
}

void MainWindow::OnCaptureScreenshot() {
    if (QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning())
        return;

    const u64 title_id = QtCommon::system->GetApplicationProcessProgramID();
    const auto screenshot_path =
        QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::ScreenshotsDir));
    const auto date =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString filename = QStringLiteral("%1/%2_%3.png")
                           .arg(screenshot_path)
                           .arg(title_id, 16, 16, QLatin1Char{'0'})
                           .arg(date);

    if (!Common::FS::CreateDir(screenshot_path.toStdString()))
        return;

#ifdef _WIN32
    if (UISettings::values.enable_screenshot_save_as) {
        OnPauseGame();
        filename = QFileDialog::getSaveFileName(this, tr("Capture Screenshot"), filename,
                                                tr("PNG Image (*.png)"));
        OnStartGame();
        if (filename.isEmpty()) {
            return;
        }
    }
#endif
    render_window->CaptureScreenshot(filename);
}

#ifdef ENABLE_UPDATE_CHECKER
void MainWindow::OnEmulatorUpdateAvailable() {
    std::optional<Common::Net::Release> version = update_future.result();
    if (!version)
        return;

    UpdateDialog dialog(version.value(), this);
    dialog.exec();
}
#endif

void MainWindow::OnApplicationChanged(u64 program_id) {
    if (!emulation_running || program_id == 0)
        return;

    std::string title_name;
    std::string title_version;

    const FileSys::PatchManager pm(program_id, QtCommon::system->GetFileSystemController(),
                                   QtCommon::system->GetContentProvider());
    if (const auto metadata = pm.GetControlMetadata(); metadata.first != nullptr) {
        title_version = metadata.first->GetVersionString();
        title_name = metadata.first->GetApplicationName();
    }
    if (title_name.empty()) {
        title_name = std::format("{:016X}", program_id);
    }

    if (const auto* process = QtCommon::system->Kernel().ApplicationProcess(); process != nullptr) {
        const auto instruction_set_suffix = process->Is64Bit() ? tr("(64-bit)") : tr("(32-bit)");
        title_name =
            tr("%1 %2", "%1 is the title name. %2 indicates if the title is 64-bit or 32-bit")
                .arg(QString::fromStdString(title_name), instruction_set_suffix)
                .toStdString();
    }

    LOG_INFO(Frontend, "Now running: {:016X} | {} | {}", program_id, title_name, title_version);

    UpdateWindowTitle(title_name, title_version,
                      QtCommon::system->GPU().Renderer().GetDeviceVendor());

    // Switch to record playtime for the running program
    if (play_time_manager)
        play_time_manager->SetProgramId(program_id);
}

void MainWindow::UpdateWindowTitle(std::string_view title_name, std::string_view title_version,
                                   std::string_view gpu_vendor) {
    static const std::string build_id = std::string{Common::g_build_id};
    static const std::string yuzu_title =
        std::format("{} | {} | {}", std::string{Common::g_build_name},
                    std::string{Common::g_build_version}, std::string{Common::g_compiler_id});

    const auto override_build = std::vformat(std::string_view(Common::g_title_bar_format_idle),
                                             std::make_format_args(build_id));
    const auto window_title = override_build.empty() ? yuzu_title : override_build;

    if (title_name.empty()) {
        setWindowTitle(QString::fromStdString(window_title));
    } else {
        const auto run_title = [window_title, title_name, title_version, gpu_vendor]() {
            if (title_version.empty()) {
                return std::format("{} | {} | {}", window_title, title_name, gpu_vendor);
            }
            return std::format("{} | {} | {} | {}", window_title, title_name, title_version,
                               gpu_vendor);
        }();
        setWindowTitle(QString::fromStdString(run_title));
    }
}

std::string MainWindow::CreateTASFramesString(
    std::array<size_t, InputCommon::TasInput::PLAYER_NUMBER> frames) const {
    std::string string = "";
    size_t maxPlayerIndex = 0;
    for (size_t i = 0; i < frames.size(); i++) {
        if (frames[i] != 0) {
            if (maxPlayerIndex != 0)
                string += ", ";
            while (maxPlayerIndex++ != i)
                string += "0, ";
            string += std::to_string(frames[i]);
        }
    }
    return string;
}

QString MainWindow::GetTasStateDescription() const {
    auto [tas_status, current_tas_frame, total_tas_frames] = input_subsystem->GetTas()->GetStatus();
    std::string tas_frames_string = CreateTASFramesString(total_tas_frames);
    switch (tas_status) {
    case InputCommon::TasInput::TasState::Running:
        return tr("TAS state: Running %1/%2")
            .arg(current_tas_frame)
            .arg(QString::fromStdString(tas_frames_string));
    case InputCommon::TasInput::TasState::Recording:
        return tr("TAS state: Recording %1").arg(total_tas_frames[0]);
    case InputCommon::TasInput::TasState::Stopped:
        return tr("TAS state: Idle %1/%2")
            .arg(current_tas_frame)
            .arg(QString::fromStdString(tas_frames_string));
    default:
        return tr("TAS State: Invalid");
    }
}

void MainWindow::OnTasStateChanged() {
    bool is_running = false;
    bool is_recording = false;
    if (emulation_running) {
        const InputCommon::TasInput::TasState tas_status =
            std::get<0>(input_subsystem->GetTas()->GetStatus());
        is_running = tas_status == InputCommon::TasInput::TasState::Running;
        is_recording = tas_status == InputCommon::TasInput::TasState::Recording;
    }

    ui->action_TAS_Start->setText(is_running ? tr("&Stop Running") : tr("&Start"));
    ui->action_TAS_Record->setText(is_recording ? tr("Stop R&ecording") : tr("R&ecord"));

    ui->action_TAS_Start->setEnabled(emulation_running);
    ui->action_TAS_Record->setEnabled(emulation_running);
    ui->action_TAS_Reset->setEnabled(emulation_running);
}

void MainWindow::UpdateStatusBar() {
    if (QtCommon::emu_thread == nullptr || !QtCommon::system->IsPoweredOn()) {
        status_bar_update_timer.stop();
        return;
    }

    if (Settings::values.tas_enable)
        tas_label->setText(GetTasStateDescription());
    else
        tas_label->clear();

    auto results = QtCommon::system->GetAndResetPerfStats();
    auto& shader_notify = QtCommon::system->GPU().ShaderNotify();
    const int shaders_building = shader_notify.ShadersBuilding();

    emit statsUpdated(results, shader_notify);

    if (shaders_building > 0) {
        shader_building_label->setText(tr("Building: %n shader(s)", "", shaders_building));
        shader_building_label->setVisible(true);
    } else {
        shader_building_label->setVisible(false);
    }

    const auto res_info = Settings::values.resolution_info;
    const auto res_scale = res_info.up_factor;
    res_scale_label->setText(
        tr("Scale: %1x", "%1 is the resolution scaling factor").arg(res_scale));

    if (Settings::values.use_speed_limit.GetValue()) {
        emu_speed_label->setText(tr("Speed: %1% / %2%")
                                     .arg(results.emulation_speed * 100.0, 0, 'f', 0)
                                     .arg(Settings::SpeedLimit()));
    } else {
        emu_speed_label->setText(tr("Speed: %1%").arg(results.emulation_speed * 100.0, 0, 'f', 0));
    }

    QString fpsText = tr("Game: %1 FPS").arg(std::round(results.average_game_fps), 0, 'f', 0);
    if (!m_fpsSuffix.isEmpty())
        fpsText = fpsText % QStringLiteral(" (%1)").arg(m_fpsSuffix);

    game_fps_label->setText(fpsText);

    emu_frametime_label->setText(tr("Frame: %1 ms").arg(results.frametime * 1000.0, 0, 'f', 2));

    res_scale_label->setVisible(true);
    emu_speed_label->setVisible(!Settings::values.use_multi_core.GetValue());
    game_fps_label->setVisible(true);
    emu_frametime_label->setVisible(true);
    firmware_label->setVisible(false);
}

void MainWindow::UpdateGPUAccuracyButton() {
    const auto gpu_accuracy = Settings::values.gpu_accuracy.GetValue();
    const auto gpu_accuracy_text =
        ConfigurationShared::gpu_accuracy_texts_map.find(gpu_accuracy)->second;
    gpu_accuracy_button->setText(gpu_accuracy_text.toUpper());
    gpu_accuracy_button->setChecked(gpu_accuracy != Settings::GpuAccuracy::Low);
}

void MainWindow::UpdateDockedButton() {
    const auto console_mode = Settings::values.use_docked_mode.GetValue();
    dock_status_button->setChecked(Settings::IsDockedMode());
    dock_status_button->setText(
        ConfigurationShared::use_docked_mode_texts_map.find(console_mode)->second.toUpper());
}

void MainWindow::UpdateAPIText() {
    const auto api = Settings::values.renderer_backend.GetValue();
    const auto renderer_status_text =
        ConfigurationShared::renderer_backend_texts_map.find(api)->second;
    renderer_status_button->setText(renderer_status_text.toUpper());
}

void MainWindow::UpdateFilterText() {
    const auto filter = Settings::values.scaling_filter.GetValue();
    const auto filter_text = ConfigurationShared::scaling_filter_texts_map.find(filter)->second;
    filter_status_button->setText(filter_text.toUpper());
}

void MainWindow::UpdateAAText() {
    const auto aa_mode = Settings::values.anti_aliasing.GetValue();
    const auto aa_text = ConfigurationShared::anti_aliasing_texts_map.find(aa_mode)->second;
    aa_status_button->setText(aa_mode == Settings::AntiAliasing::None
                                  ? QStringLiteral(QT_TRANSLATE_NOOP("MainWindow", "NO AA"))
                                  : aa_text.toUpper());
}

#ifdef HAS_RESHADE
void MainWindow::UpdatePostShaderText() {
    const bool enabled = Settings::values.post_shader_enabled.GetValue();
    post_shader_status_button->setChecked(enabled);

    if (!enabled) {
        post_shader_status_button->setText(tr("NO FX"));
        return;
    }

    const std::string preset = VideoCore::GetActiveFxPreset();
    if (preset.empty()) {
        post_shader_status_button->setText(tr("FX"));
        return;
    }

    post_shader_status_button->setText(QString::fromStdString(preset).toUpper());
}
#endif

void MainWindow::UpdateVolumeUI() {
    const auto volume_value = static_cast<int>(Settings::values.volume.GetValue());
    volume_slider->setValue(volume_value);
    if (Settings::values.audio_muted) {
        volume_button->setChecked(false);
        volume_button->setText(tr("VOLUME: MUTE"));
    } else {
        volume_button->setChecked(true);
        volume_button->setText(tr("VOLUME: %1%", "Volume percentage (e.g. 50%)").arg(volume_value));
    }
}

void MainWindow::UpdateStatusButtons() {
    renderer_status_button->setChecked(Settings::values.renderer_backend.GetValue() ==
                                       Settings::RendererBackend::Vulkan);
    UpdateAPIText();
    UpdateGPUAccuracyButton();
    UpdateDockedButton();
    UpdateFilterText();
    UpdateAAText();
    UpdateVolumeUI();
}

// TODO(crueter): Use this for game list stuff
void MainWindow::UpdateUISettings() {
    if (!ui->action_Fullscreen->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
    }
    UISettings::values.state = saveState();
    UISettings::values.single_window_mode = ui->action_Single_Window_Mode->isChecked();
    UISettings::values.fullscreen = ui->action_Fullscreen->isChecked();
    UISettings::values.show_filter_bar = ui->action_Show_Filter_Bar->isChecked();
    UISettings::values.show_status_bar = ui->action_Show_Status_Bar->isChecked();
    UISettings::values.show_perf_overlay = ui->action_Show_Performance_Overlay->isChecked();

    UISettings::values.first_start = false;

    Settings::values.enable_overlay = ui->action_Enable_Overlay_Applet->isChecked();
}

void MainWindow::UpdateInputDrivers() {
    if (!input_subsystem)
        return;
    input_subsystem->PumpEvents();
}

void MainWindow::HideMouseCursor() {
    if (QtCommon::emu_thread == nullptr && UISettings::values.hide_mouse) {
        mouse_hide_timer.stop();
        ShowMouseCursor();
        return;
    }
    render_window->setCursor(QCursor(Qt::BlankCursor));
}

void MainWindow::ShowMouseCursor() {
    render_window->unsetCursor();
    if (QtCommon::emu_thread != nullptr && UISettings::values.hide_mouse)
        mouse_hide_timer.start();
}

void MainWindow::OnMouseActivity() {
    if (!Settings::values.mouse_panning) {
        ShowMouseCursor();
    }
}

void MainWindow::OnCheckFirmwareDecryption() {
    if (!ContentManager::AreKeysPresent()) {
        const auto res = QtCommon::Frontend::Warning(
            tr("Derivation Components Missing"),
            tr("Decryption keys are missing. Install them now?"),
            QtCommon::Frontend::StandardButton::Yes | QtCommon::Frontend::StandardButton::No);

        if (res == QtCommon::Frontend::StandardButton::Yes)
            OnInstallDecryptionKeys();
    }

    SetFirmwareVersion();
    UpdateMenuState();
}

bool MainWindow::CheckFirmwarePresence() {
    return FirmwareManager::CheckFirmwarePresence(*QtCommon::system.get());
}

void MainWindow::SetFirmwareVersion() {
    const auto pair = FirmwareManager::GetFirmwareVersion(*QtCommon::system.get());
    const auto firmware_data = pair.first;
    const auto result = pair.second;

    if (result.IsError() || !CheckFirmwarePresence()) {
        LOG_INFO(Frontend, "Installed firmware: No firmware available");
        ui->menu_Applets->setEnabled(false);
        ui->menu_Create_Shortcuts->setEnabled(false);
        firmware_label->setVisible(false);
        return;
    }

    firmware_label->setVisible(true);
    ui->menu_Applets->setEnabled(true);
    ui->menu_Create_Shortcuts->setEnabled(true);

    const std::string display_version(firmware_data.display_version.data());
    const std::string display_title(firmware_data.display_title.data());

    LOG_INFO(Frontend, "Installed firmware: {}", display_version);

    firmware_label->setText(QString::fromStdString(display_version));
    firmware_label->setToolTip(QString::fromStdString(display_title));
}

void MainWindow::SetFPSSuffix() {
    switch (Settings::values.current_speed_mode.GetValue()) {
    case Settings::SpeedMode::Slow:
        m_fpsSuffix = tr("Slow");
        break;
    case Settings::SpeedMode::Turbo:
        m_fpsSuffix = tr("Turbo");
        break;
    case Settings::SpeedMode::Standard:
        const bool limited = Settings::values.use_speed_limit.GetValue();
        m_fpsSuffix = limited ? QString{} : tr("Unlocked");
        break;
    }
}

bool MainWindow::SelectRomFSDumpTarget(const FileSys::ContentProvider& installed, u64 program_id,
                                       u64* selected_title_id, u8* selected_content_record_type) {
    using ContentInfo = std::tuple<u64, FileSys::TitleType, FileSys::ContentRecordType>;
    boost::container::flat_set<ContentInfo> available_title_ids;

    const auto RetrieveEntries = [&](FileSys::TitleType title_type,
                                     FileSys::ContentRecordType record_type) {
        const auto entries = installed.ListEntriesFilter(title_type, record_type);
        for (const auto& entry : entries) {
            if (FileSys::GetBaseTitleID(entry.title_id) == program_id &&
                installed.GetEntry(entry)->GetStatus() == Loader::ResultStatus::Success) {
                available_title_ids.insert({entry.title_id, title_type, record_type});
            }
        }
    };

    RetrieveEntries(FileSys::TitleType::Application, FileSys::ContentRecordType::Program);
    RetrieveEntries(FileSys::TitleType::Application, FileSys::ContentRecordType::HtmlDocument);
    RetrieveEntries(FileSys::TitleType::Application, FileSys::ContentRecordType::LegalInformation);
    RetrieveEntries(FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);

    if (available_title_ids.empty()) {
        return false;
    }

    size_t title_index = 0;

    if (available_title_ids.size() > 1) {
        QStringList list;
        for (auto& [title_id, title_type, record_type] : available_title_ids) {
            const auto hex_title_id = QString::fromStdString(std::format("{:X}", title_id));
            if (record_type == FileSys::ContentRecordType::Program) {
                list.push_back(QStringLiteral("Program [%1]").arg(hex_title_id));
            } else if (record_type == FileSys::ContentRecordType::HtmlDocument) {
                list.push_back(QStringLiteral("HTML document [%1]").arg(hex_title_id));
            } else if (record_type == FileSys::ContentRecordType::LegalInformation) {
                list.push_back(QStringLiteral("Legal information [%1]").arg(hex_title_id));
            } else {
                list.push_back(
                    QStringLiteral("DLC %1 [%2]").arg(title_id & 0x7FF).arg(hex_title_id));
            }
        }

        bool ok;
        const auto res = QInputDialog::getItem(
            this, tr("Select RomFS Dump Target"),
            tr("Please select which RomFS you would like to dump."), list, 0, false, &ok);
        if (!ok) {
            return false;
        }

        title_index = list.indexOf(res);
    }

    const auto& [title_id, title_type, record_type] = *available_title_ids.nth(title_index);
    *selected_title_id = title_id;
    *selected_content_record_type = static_cast<u8>(record_type);
    return true;
}

bool MainWindow::ConfirmClose() {
    if (QtCommon::emu_thread == nullptr ||
        UISettings::values.confirm_before_stopping.GetValue() == ConfirmStop::Ask_Never)
        return true;

    if (!QtCommon::system->GetExitLocked() &&
        UISettings::values.confirm_before_stopping.GetValue() == ConfirmStop::Ask_Based_On_Game)
        return true;

    const auto text = tr("Are you sure you want to close Eden?");
    return question(this, tr("Eden"), text);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!ConfirmClose()) {
        event->ignore();
        return;
    }
    // Debug Probes: who was which player when Eden closed
    if (Common::ControllerProbes() && !overlay_rows.empty()) {
        std::string players_now;
        for (const auto& row : overlay_rows) {
            players_now += std::format("\n  P{} {} ident={} profile=\"{}\"{}", row.player + 1,
                                       overlay_row_name[row.player].toStdString(),
                                       overlay_row_ident[row.player],
                                       ConsoleBindingSets()[slot_set[row.player]].profile_name,
                                       row.pad == 0 ? " (left)" : "");
        }
        LOG_INFO(Frontend, "console-mode: closing, mode {}, players:{}",
                 AppletModeName(Settings::values.controller_applet_mode.GetValue()), players_now);
    }
    closing = true;
    console_mode_timer.stop();

    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    UISettings::SaveWindowState();
    hotkey_registry.SaveHotkeys();

    // Unload controllers early
    controller_dialog->UnloadController();
    game_list->UnloadController();

    // Shutdown session if the emu thread is active...
    if (QtCommon::emu_thread != nullptr)
        ShutdownGame();

    render_window->close();
    multiplayer_state->Close();
    QtCommon::system->HIDCore().UnloadInputDevices();
    Network::Shutdown();

    QWidget::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    emit sizeChanged(event->size());
}

void MainWindow::moveEvent(QMoveEvent* event) {
    auto window_frame_height = frameGeometry().height() - geometry().height();
    emit positionChanged(event->pos() - QPoint{0, window_frame_height});
}

static bool IsSingleFileDropEvent(const QMimeData* mime) {
    return mime->hasUrls() && mime->urls().length() == 1;
}

void MainWindow::AcceptDropEvent(QDropEvent* event) {
    if (IsSingleFileDropEvent(event->mimeData())) {
        event->setDropAction(Qt::DropAction::LinkAction);
        event->accept();
    }
}

bool MainWindow::DropAction(QDropEvent* event) {
    if (!IsSingleFileDropEvent(event->mimeData())) {
        return false;
    }

    const QMimeData* mime_data = event->mimeData();
    const QString& filename = mime_data->urls().at(0).toLocalFile();

    if (emulation_running && QFileInfo(filename).suffix() == QStringLiteral("bin")) {
        // Amiibo
        LoadAmiibo(filename);
    } else {
        // Game
        if (ConfirmChangeGame()) {
            BootGame(filename, ApplicationAppletParameters());
        }
    }
    return true;
}

void MainWindow::dropEvent(QDropEvent* event) {
    DropAction(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    AcceptDropEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    AcceptDropEvent(event);
}

bool MainWindow::ConfirmChangeGame() {
    if (QtCommon::emu_thread == nullptr)
        return true;

    // Use custom question to link controller navigation
    return question(
        this, tr("Eden"),
        tr("Are you sure you want to stop the emulation? Any unsaved progress will be lost."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
}

bool MainWindow::ConfirmForceLockedExit() {
    if (QtCommon::emu_thread == nullptr)
        return true;

    const auto text = tr("The currently running application has requested Eden to not exit.\n\n"
                         "Would you like to bypass this and exit anyway?");

    return question(this, tr("Eden"), text);
}

void MainWindow::RequestGameExit() {
    if (!QtCommon::system->IsPoweredOn())
        return;

    QtCommon::system->SetExitRequested(true);
    QtCommon::system->GetAppletManager().RequestExit();
}

void MainWindow::filterBarSetChecked(bool state) {
    ui->action_Show_Filter_Bar->setChecked(state);
    emit(OnToggleFilterBar());
}

static void AdjustLinkColor() {
    QPalette new_pal(qApp->palette());

    if (UISettings::IsDarkTheme())
        new_pal.setColor(QPalette::Link, QColor(0, 190, 255, 255));
    else
        new_pal.setColor(QPalette::Link, QColor(0, 140, 200, 255));

    if (qApp->palette().color(QPalette::Link) != new_pal.color(QPalette::Link))
        qApp->setPalette(new_pal);
}

void MainWindow::UpdateUITheme() {
    const QString default_theme = QString::fromUtf8(UISettings::themes[size_t(UISettings::default_theme)].second);
    QString current_theme = QString::fromStdString(UISettings::values.theme);

    if (current_theme.isEmpty())
        current_theme = default_theme;

#ifdef _WIN32
    QIcon::setThemeName(current_theme);
    AdjustLinkColor();
#else
    if (current_theme == QStringLiteral("default") || current_theme == QStringLiteral("colorful")) {
        QIcon::setThemeName(current_theme == QStringLiteral("colorful") ? current_theme : startup_icon_theme);
        QIcon::setThemeSearchPaths(QStringList(default_theme_paths));
        if (isDarkMode()) {
            current_theme = QStringLiteral("default_dark");
        }
    } else {
        QIcon::setThemeName(current_theme);
        QIcon::setThemeSearchPaths(QStringList(QStringLiteral(":/icons")));
        AdjustLinkColor();
    }
#endif

    if (current_theme != default_theme) {
        QString theme_uri{QStringLiteral(":%1/style.qss").arg(current_theme)};
        QFile f(theme_uri);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            LOG_ERROR(Frontend, "Unable to open style \"{}\", fallback to the default theme",
                      UISettings::values.theme);
            current_theme = default_theme;
        }
    }

    QString theme_uri{QStringLiteral(":%1/style.qss").arg(current_theme)};
    QFile f(theme_uri);
    if (f.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&f);
        qApp->setStyleSheet(ts.readAll());
        setStyleSheet(ts.readAll());
    } else {
        LOG_ERROR(Frontend, "Unable to set style \"{}\", stylesheet file not found",
                  UISettings::values.theme);
        qApp->setStyleSheet({});
        setStyleSheet({});
    }

#ifdef _WIN32
    RemoveTitlebarFilter();
    ApplyGlobalDarkTitlebar(UISettings::IsDarkTheme());
#endif
}

void MainWindow::LoadTranslation() {
    bool loaded;

    if (UISettings::values.language.GetValue().empty()) {
        // If the selected language is empty, use system locale
        loaded = translator.load(QLocale(), {}, {}, QStringLiteral(":/languages/"));
    } else {
        // Otherwise load from the specified file
        loaded = translator.load(QString::fromStdString(UISettings::values.language.GetValue()),
                                 QStringLiteral(":/languages/"));
    }

    if (loaded)
        qApp->installTranslator(&translator);
    else
        UISettings::values.language = std::string("en");
}

void MainWindow::OnLanguageChanged(const QString& locale) {
    if (UISettings::values.language.GetValue() != std::string("en"))
        qApp->removeTranslator(&translator);

    QList<QAction*> actions = game_size_actions->actions();
    for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
        actions.at(i)->setText(GetTranslatedGameIconSize(i));
    }

    UISettings::values.language = locale.toStdString();
    LoadTranslation();
    ui->retranslateUi(this);
    multiplayer_state->retranslateUi();
    UpdateWindowTitle();
}

void MainWindow::SetDiscordEnabled([[maybe_unused]] bool state) {
#ifdef USE_DISCORD_PRESENCE
    if (state)
        discord_rpc = std::make_unique<DiscordRPC::DiscordImpl>(*QtCommon::system);
    else
        discord_rpc = std::make_unique<DiscordRPC::NullImpl>();
#else
    discord_rpc = std::make_unique<DiscordRPC::NullImpl>();
#endif
    discord_rpc->Update();
}

void MainWindow::SetGamemodeEnabled(bool state) {
    if (emulation_running) {
        if (state)
            Common::FeralGamemode::Start();
        else
            Common::FeralGamemode::Stop();
    }
}

void MainWindow::changeEvent(QEvent* event) {
#ifdef __unix__
    // PaletteChange event appears to only reach so far into the GUI, explicitly asking to
    // UpdateUITheme is a decent work around
    if (event->type() == QEvent::PaletteChange) {
        const QPalette test_palette(qApp->palette());
        const QString current_theme = QString::fromStdString(UISettings::values.theme);
        // Keeping eye on QPalette::Window to avoid looping. QPalette::Text might be useful too
        static QColor last_window_color;
        const QColor window_color = test_palette.color(QPalette::Active, QPalette::Window);
        if (last_window_color != window_color && (current_theme == QStringLiteral("default") ||
                                                  current_theme == QStringLiteral("colorful"))) {
            UpdateUITheme();
        }
        last_window_color = window_color;
    }
#endif // __unix__
    QWidget::changeEvent(event);
}

Service::AM::FrontendAppletParameters MainWindow::ApplicationAppletParameters() {
    return Service::AM::FrontendAppletParameters{
        .applet_id = Service::AM::AppletId::Application,
        .applet_type = Service::AM::AppletType::Application,
    };
}

Service::AM::FrontendAppletParameters MainWindow::LibraryAppletParameters(
    u64 program_id, Service::AM::AppletId applet_id) {
    return Service::AM::FrontendAppletParameters{
        .program_id = program_id,
        .applet_id = applet_id,
        .applet_type = Service::AM::AppletType::LibraryApplet,
    };
}

void VolumeButton::wheelEvent(QWheelEvent* event) {

    int num_degrees = event->angleDelta().y() / 8;
    int num_steps = (num_degrees / 15) * scroll_multiplier;
    // Stated in QT docs: Most mouse types work in steps of 15 degrees, in which case the delta
    // value is a multiple of 120; i.e., 120 units * 1/8 = 15 degrees.

    if (num_steps > 0) {
        Settings::values.volume.SetValue(
            (std::min)(200, Settings::values.volume.GetValue() + num_steps));
    } else {
        Settings::values.volume.SetValue(
            (std::max)(0, Settings::values.volume.GetValue() + num_steps));
    }

    scroll_multiplier = (std::min)(MaxMultiplier, scroll_multiplier * 2);
    scroll_timer.start(100); // reset the multiplier if no scroll event occurs within 100 ms

    emit VolumeChanged();
    event->accept();
}

void VolumeButton::ResetMultiplier() {
    scroll_multiplier = 1;
}

#ifdef main
#undef main
#endif

#if !defined(QT_STATICPLUGIN) || defined(__APPLE__)
#define VMA_IMPLEMENTATION 1
#include "video_core/vulkan_common/vma.h"
#endif
