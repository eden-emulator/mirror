// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "common/logging.h"
#include "common/settings.h"
#include "yuzu/util/gamescope.h"

namespace Gamescope {
namespace {

// Under gamescope (installed on the application only there). Gamescope shows one window at a
// time, scaled to fit the screen, with no title bar, and only one popup over it:
// - A dialog takes the whole screen as it shows, before it's mapped, so it's never scaled up
//   (blurry, and a tooltip past its edge rescaled it). A smaller dialog keeps its own size,
//   centred. Its own show handling runs first (a message box fixes its size
//   there), then its size is freed.
// - A dialog with no buttons of its own to leave it gets a Close button.
// - A closing menu can leave the mouse grabbed by the main window: taps then reach the main
//   window, which the open dialog blocks, so the dialog never gets them. A modal window shown
//   with no menu open lets go of any such grab.
// - A submenu's popup would hide its parent menu, which still took the mouse unseen: a
//   submenu opens in its menu's place instead.
// - A smaller dialog is a card on a darker screen, like its own window on a desktop, with an
//   Expand button to fill the screen (Shrink to go back).
class GamescopeDialogs final : public QObject {
public:
    explicit GamescopeDialogs(QWidget* main_) : QObject(main_), main{main_} {
        // The menu bar's menus got their actions before this was installed
        for (QMenu* menu : main->findChildren<QMenu*>()) {
            PrepareMenu(menu);
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (auto* menu = qobject_cast<QMenu*>(watched)) {
            if (event->type() == QEvent::ActionAdded) {
                PrepareMenu(menu);
                return false;
            }
            return TurnPage(menu, event);
        }
        if (event->type() == QEvent::LayoutRequest && watched->isWidgetType() &&
            watched->property("gamescope_fit").isValid()) {
            // A message box fixes its own size on every layout change: it lays itself out, then
            // takes the whole screen again
            // (Taken over here, so the layout is activated first, as Qt would before event())
            auto* widget = static_cast<QWidget*>(watched);
            if (QLayout* layout = widget->layout()) {
                layout->activate();
            }
            static_cast<QObject*>(widget)->event(event);
            if (QScreen* screen = widget->screen(); screen && widget->geometry() != screen->geometry()) {
                widget->setMinimumSize(0, 0);
                widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                widget->setGeometry(screen->geometry());
            }
            return true;
        }
        if (event->type() != QEvent::Show || !watched->isWidgetType()) {
            return false;
        }
        auto* widget = static_cast<QWidget*>(watched);
        if (widget == main || !widget->isWindow()) {
            return false;
        }
        if (widget->windowModality() != Qt::NonModal) {
            QTimer::singleShot(0, this, [this] {
                if (QApplication::activePopupWidget() || !main->windowHandle()) {
                    return;
                }
                main->windowHandle()->setMouseGrabEnabled(false);
                if (Settings::values.console_debug_probes.GetValue()) {
                    LOG_INFO(Frontend, "gamescope: dialog shown, mouse grab released");
                }
            });
        }
        // Eden's own overlays over the game (software keyboard, applet messages) size and place
        // themselves over it
        if (widget->windowType() != Qt::Dialog || widget->isFullScreen() ||
            (widget->windowFlags() & Qt::FramelessWindowHint)) {
            return false;
        }
        static_cast<QObject*>(widget)->event(event);
        FitDialog(widget);
        return true;
    }

private:
    static void FitDialog(QWidget* dialog) {
        QScreen* screen = dialog->screen() ? dialog->screen() : QGuiApplication::primaryScreen();
        if (!screen) {
            return;
        }
        const QRect area = screen->geometry();
        const QSize natural = dialog->size();
        const bool first = !dialog->property("gamescope_fit").isValid();
        dialog->setProperty("gamescope_fit", true);
        dialog->setMinimumSize(0, 0);
        dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        dialog->setGeometry(area);
        QLayout* layout = dialog->layout();
        if (!first || !layout) {
            return;
        }
        // A smaller dialog keeps its size, a card in the middle; it can be expanded to fill the
        // screen and shrunk back (each time it opens it starts as a card)
        const bool centred = natural.width() < area.width() * 85 / 100 &&
                             natural.height() < area.height() * 85 / 100;
        QList<QPushButton*> corner;
        if (NeedsClose(dialog)) {
            auto* close = new QPushButton(tr("Close"), dialog);
            connect(close, &QPushButton::clicked, dialog, &QWidget::close);
            corner.append(close);
        }
        QPushButton* expand = nullptr;
        if (centred && !IsSmallPrompt(dialog)) {
            expand = new QPushButton(tr("Expand"), dialog);
            expand->setToolTip(tr("Fill the screen"));
            corner.prepend(expand);
        }
        int strip = 0;
        for (QPushButton* button : corner) {
            button->setCursor(Qt::PointingHandCursor);
            button->adjustSize();
            strip = (std::max)(strip, button->height() + 8);
        }
        const QMargins own = layout->contentsMargins();
        const QRect card_area((area.width() - natural.width()) / 2,
                              (area.height() - natural.height()) / 2, natural.width(),
                              natural.height());
        QFrame* card = centred ? AddCard(dialog, card_area) : nullptr;
        // Card: margins around the dialog's own size, buttons above its top right corner.
        // Full: the buttons in a strip at the top right of the screen.
        // (The dialog's current layout each time, and guarded pointers: a message box rebuilds
        // its layout on a style change, so AddCard's style sheet already deleted the one read
        // above; the kept pointer crashed in QLayout::setContentsMargins)
        QList<QPointer<QPushButton>> corner_buttons;
        for (QPushButton* button : corner) {
            corner_buttons.append(button);
        }
        const auto place = [dialog_ptr = QPointer<QWidget>{dialog}, own, area, card_area,
                            card_ptr = QPointer<QFrame>{card}, corner_buttons, strip,
                            expand_ptr = QPointer<QPushButton>{expand}](bool full) {
            QWidget* const target = dialog_ptr.data();
            QLayout* const target_layout = target ? target->layout() : nullptr;
            if (!target_layout) {
                return;
            }
            QFrame* const target_card = card_ptr.data();
            QPushButton* const target_expand = expand_ptr.data();
            QList<QPushButton*> buttons;
            for (const QPointer<QPushButton>& button : corner_buttons) {
                if (button) {
                    buttons.append(button.data());
                }
            }
            if (full || !target_card) {
                target_layout->setContentsMargins(own +
                                                  QMargins(0, buttons.isEmpty() ? 0 : strip, 0, 0));
            } else {
                target_layout->setContentsMargins(own +
                                                  QMargins(card_area.left(), card_area.top(),
                                                           area.width() - card_area.right() - 1,
                                                           area.height() - card_area.bottom() - 1));
            }
            if (target_card) {
                target_card->setVisible(!full);
            }
            if (target_expand) {
                target_expand->setText(full ? tr("Shrink") : tr("Expand"));
                target_expand->setToolTip(full ? tr("Back to its own size")
                                               : tr("Fill the screen"));
                target_expand->adjustSize();
            }
            if (buttons.isEmpty()) {
                target->update();
                return;
            }
            int right = full || !target_card ? area.width() - 8 : card_area.right() + 1;
            const int top =
                full || !target_card
                    ? 6
                    : (std::max)(6, card_area.top() - 14 - buttons.first()->height() - 6);
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                (*it)->move(right - (*it)->width(), top);
                (*it)->raise();
                (*it)->show();
                right -= (*it)->width() + 6;
            }
            target->update();
        };
        if (expand) {
            connect(expand, &QPushButton::clicked, dialog, [place, expand] {
                // (The connection ends with the button, so expand is alive here)
                const bool full = !expand->property("gamescope_full").toBool();
                expand->setProperty("gamescope_full", full);
                place(full);
            });
        }
        // File pickers, per-game settings and the room browser open expanded (Shrink: a card)
        const bool starts_full = expand && (qobject_cast<QFileDialog*>(dialog) ||
                                            dialog->inherits("ConfigurePerGame") ||
                                            dialog->inherits("Lobby"));
        if (starts_full) {
            expand->setProperty("gamescope_full", true);
        }
        place(starts_full);
    }

    // A prompt or short form: a few lines and fields, nothing to gain from filling the screen
    static bool IsSmallPrompt(QWidget* dialog) {
        static constexpr const char* small[] = {
            "QMessageBox",       "QInputDialog",          "AboutDialog",
            "DataDialog",        "DirectConnectWindow",   "HostRoomWindow",
            "RyujinxDialog",     "LimitableInputDialog",  "SetPlayTimeDialog",
            "SequenceDialog",    "NewUserDialog",         "ConfigureProfileManagerDeleteDialog",
            "MigrationDialog",   "CalibrationConfigurationDialog",
        };
        for (const char* name : small) {
            if (dialog->inherits(name)) {
                return true;
            }
        }
        return false;
    }

    // A dialog leaves through its button row; without one (the room browser, Create Room) it
    // has no way out
    static bool NeedsClose(QWidget* dialog) {
        for (auto* box : dialog->findChildren<QDialogButtonBox*>()) {
            if (!box->buttons().isEmpty()) {
                return false;
            }
        }
        return true;
    }

    // The dialog's own area is a card on a darker screen, so it doesn't float edgeless
    static QFrame* AddCard(QWidget* dialog, const QRect& inner) {
        const QColor base = dialog->palette().color(QPalette::Window);
        if (dialog->objectName().isEmpty()) {
            dialog->setObjectName(QStringLiteral("gamescopeDialog"));
        }
        dialog->setStyleSheet(dialog->styleSheet() +
                              QStringLiteral("\n#%1 { background-color: %2; }")
                                  .arg(dialog->objectName(), base.darker(160).name()));
        auto* card = new QFrame(dialog);
        card->setObjectName(QStringLiteral("gamescopeCard"));
        card->setStyleSheet(QStringLiteral("#gamescopeCard { background-color: %1; border: 1px "
                                           "solid %2; border-radius: 10px; }")
                                .arg(base.name(), base.lighter(150).name()));
        card->setGeometry(inner.adjusted(-14, -14, 14, 14));
        card->lower();
        return card;
    }

    // A submenu opens in its menu's place: the menu swaps its entries for the submenu's, with
    // ‹ Back on top, so only one menu is ever up. Set up as a menu gets its first action, so
    // it's in place before the menu first measures itself.
    static void PrepareMenu(QMenu* menu) {
        if (menu->property("gamescope_menu").isValid()) {
            return;
        }
        menu->setProperty("gamescope_menu", true);
        connect(menu, &QMenu::aboutToShow, menu, [menu] {
            menu->setProperty("gamescope_pages", QVariantList{});
            ShowPage(menu, nullptr);
        });
    }

    // A tap (or Enter / Right) on a submenu's entry or on Back turns the page, the menu kept open
    static bool TurnPage(QMenu* menu, QEvent* event) {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Right &&
                key != Qt::Key_Left) {
                return false;
            }
        } else if (event->type() != QEvent::MouseButtonRelease) {
            return false;
        }
        QAction* active = menu->activeAction();
        if (!active || !active->isEnabled()) {
            return false;
        }
        QVariantList pages = menu->property("gamescope_pages").toList();
        const bool is_left = event->type() == QEvent::KeyPress &&
                             static_cast<QKeyEvent*>(event)->key() == Qt::Key_Left;
        if (active->property("gamescope_back").isValid() || (is_left && !pages.isEmpty())) {
            pages.removeLast();
        } else if (auto* sub = qobject_cast<QMenu*>(active->property("gamescope_sub").value<QObject*>());
                   sub && !is_left) {
            pages.append(QVariant::fromValue<QObject*>(sub));
        } else {
            return false;
        }
        menu->setProperty("gamescope_pages", pages);
        auto* page = pages.isEmpty() ? nullptr : qobject_cast<QMenu*>(pages.last().value<QObject*>());
        ShowPage(menu, page);
        return true;
    }

    // Shows the menu's own entries (page null) or a submenu's, submenus as entries ending in ›
    static void ShowPage(QMenu* menu, QMenu* page) {
        for (QAction* action : menu->actions()) {
            if (action->property("gamescope_added").isValid()) {
                menu->removeAction(action);
                action->deleteLater();
            } else if (action->property("gamescope_hidden").isValid()) {
                action->setVisible(true);
                action->setProperty("gamescope_hidden", QVariant{});
            }
        }
        const QList<QAction*> own = menu->actions();
        QAction* first = own.isEmpty() ? nullptr : own.first();
        const auto add = [menu](QAction* added, QAction* before) {
            added->setProperty("gamescope_added", true);
            menu->insertAction(before, added);
        };
        if (page) {
            for (QAction* action : own) {
                if (action->isVisible()) {
                    action->setVisible(false);
                    action->setProperty("gamescope_hidden", true);
                }
            }
            QMetaObject::invokeMethod(page, "aboutToShow");
            auto* back = new QAction(QStringLiteral("‹  ") + tr("Back"), menu);
            back->setProperty("gamescope_back", true);
            add(back, first);
            auto* separator = new QAction(menu);
            separator->setSeparator(true);
            add(separator, first);
            for (QAction* original : page->actions()) {
                if (!original->isVisible()) {
                    continue;
                }
                QAction* copy = nullptr;
                if (original->isSeparator()) {
                    copy = new QAction(menu);
                    copy->setSeparator(true);
                } else if (QMenu* sub = original->menu()) {
                    copy = new QAction(original->icon(), original->text() + QStringLiteral("  ›"), menu);
                    copy->setEnabled(original->isEnabled());
                    copy->setProperty("gamescope_sub", QVariant::fromValue<QObject*>(sub));
                } else {
                    copy = new QAction(original->icon(), original->text(), menu);
                    copy->setEnabled(original->isEnabled());
                    copy->setCheckable(original->isCheckable());
                    copy->setChecked(original->isChecked());
                    connect(copy, &QAction::triggered, original, [original] { original->trigger(); });
                }
                add(copy, first);
            }
        } else {
            for (QAction* action : own) {
                QMenu* sub = action->menu();
                if (!sub || !action->isVisible()) {
                    continue;
                }
                // (A hidden action reads as disabled: its state is taken first)
                const bool enabled = action->isEnabled();
                action->setVisible(false);
                action->setProperty("gamescope_hidden", true);
                auto* entry = new QAction(action->icon(), action->text() + QStringLiteral("  ›"), menu);
                entry->setEnabled(enabled);
                entry->setProperty("gamescope_sub", QVariant::fromValue<QObject*>(sub));
                add(entry, action);
            }
        }
        if (!menu->isVisible()) {
            return;
        }
        // An open menu turning its page: sized again and kept on the screen
        menu->adjustSize();
        if (QScreen* screen = menu->screen()) {
            const QRect area = screen->availableGeometry();
            QPoint at = menu->pos();
            at.setX((std::max)(area.left(), (std::min)(at.x(), area.right() - menu->width())));
            at.setY((std::max)(area.top(), (std::min)(at.y(), area.bottom() - menu->height())));
            menu->move(at);
        }
    }

    QWidget* main;
};
} // namespace

bool IsRunningUnder() {
    return qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") ||
           qgetenv("XDG_CURRENT_DESKTOP") == "gamescope";
}

// Opening a folder starts the desktop's file manager, which can't show under gamescope (in Game
// Mode it doesn't even reach a display): folders and files open in Eden's own file browser
class FolderBrowser final : public QObject {
    Q_OBJECT

public:
    explicit FolderBrowser(QWidget* main_) : QObject(main_), main{main_} {}

public slots:
    void Open(const QUrl& url) {
        const QFileInfo info(url.toLocalFile());
        const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        QWidget* parent = QApplication::activeWindow() ? QApplication::activeWindow() : main;
        QFileDialog browser(parent, folder);
        browser.setOption(QFileDialog::DontUseNativeDialog);
        browser.setFileMode(QFileDialog::ExistingFiles);
        browser.setLabelText(QFileDialog::Reject, tr("Close"));
        browser.setDirectory(folder);
        if (!info.isDir()) {
            browser.selectFile(info.fileName());
        }
        browser.exec();
    }

private:
    QWidget* main;
};

void FitDialogsAndMenus(QWidget* main) {
    if (IsRunningUnder()) {
        qApp->installEventFilter(new GamescopeDialogs(main));
        QDesktopServices::setUrlHandler(QStringLiteral("file"), new FolderBrowser(main), "Open");
    }
}

} // namespace Gamescope

#include "gamescope.moc"
