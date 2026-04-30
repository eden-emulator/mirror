// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <mutex>
#include <QApplication>
#include <QDialogButtonBox>
#include <QTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QItemSelectionModel>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "yuzu/applets/qt_profile_select.h"
#include "yuzu/main_window.h"
#include "yuzu/util/controller_navigation.h"

namespace {
QString FormatUserEntryText(const QString& username, Common::UUID uuid) {
    if (!username.isEmpty())
        return username;
    return QString::fromStdString(uuid.FormattedString());
}

QString GetImagePath(Common::UUID uuid) {
    const auto path =
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
        fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
    return QString::fromStdString(Common::FS::PathToUTF8String(path));
}

QPixmap GetIcon(Common::UUID uuid) {
    QPixmap icon{GetImagePath(uuid)};

    if (!icon) {
        icon.fill(Qt::black);
        icon.loadFromData(Core::Constants::ACCOUNT_BACKUP_JPEG.data(),
                          static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
    }

    return icon.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
} // Anonymous namespace

QtProfileSelectionDialog::QtProfileSelectionDialog(
    Core::System& system, QWidget* parent,
    const Core::Frontend::ProfileSelectParameters& parameters)
    : QDialog(parent), profile_manager{system.GetProfileManager()} {
    outer_layout = new QVBoxLayout(this);

    instruction_label = new QLabel();

    scroll_area = new QScrollArea;

    buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QtProfileSelectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QtProfileSelectionDialog::reject);

    outer_layout->addWidget(instruction_label);
    outer_layout->addWidget(scroll_area);
    outer_layout->addWidget(buttons);

    layout = new QVBoxLayout(scroll_area);
    tree_view = new QTreeView;
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);
    controller_navigation = new ControllerNavigation(system.HIDCore(), this);

    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setUniformRowHeights(true);
    tree_view->setIconSize({64, 64});
    tree_view->setContextMenuPolicy(Qt::NoContextMenu);

    item_model->insertColumns(0, 1);
    item_model->setHeaderData(0, Qt::Horizontal, tr("Users"));

    // We must register all custom types with the Qt Automoc system so that we are able to use it
    // with signals/slots. In this case, QList falls under the umbrella of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);

    // Keep selection index in sync with whatever moves the current row (mouse or gamepad).
    connect(tree_view->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                SelectUser(current);
            });
    connect(tree_view, &QTreeView::doubleClicked, this, &QtProfileSelectionDialog::accept);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent,
            [this](Qt::Key key) {
                if (!this->isActiveWindow())
                    return;
                switch (key) {
                case Qt::Key_Return: // + button
                case Qt::Key_Enter:  // A button — confirm currently highlighted profile
                    accept();
                    return;
                case Qt::Key_Escape: // B button — cancel, keep default profile
                    reject();
                    return;
                default:
                    // Up/Down (D-pad or L-stick) — move selection in the list.
                    QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                    QCoreApplication::sendEvent(tree_view, event);
                    return;
                }
            });

    const auto& profiles = profile_manager.GetAllUsers();
    for (const auto& user : profiles) {
        Service::Account::ProfileBase profile{};
        if (!profile_manager.GetProfileBase(user, profile))
            continue;

        const auto username = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(profile.username.data()), profile.username.size());

        list_items.push_back(QList<QStandardItem*>{new QStandardItem{
            GetIcon(user), FormatUserEntryText(QString::fromStdString(username), user)}});
    }

    for (const auto& item : list_items)
        item_model->appendRow(item);

    // Pre-select the active user, matching how configure_profile_manager identifies current user.
    preselect_row = std::clamp<int>(Settings::values.current_user.GetValue(), 0,
                                    static_cast<int>(profile_manager.GetUserCount()) - 1);
    user_index = preselect_row;

    SetWindowTitle(parameters);
    SetDialogPurpose(parameters);
    resize(550, 400);
}

void QtProfileSelectionDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Defer initial selection highlight until after exec() starts the event loop.
    // setCurrentIndex in the constructor fires before the widget is visible, so Qt
    // never paints the highlight. QTimer::singleShot(0) fires after the first paint.
    QTimer::singleShot(0, this, [this]() {
        const auto idx = item_model->index(preselect_row, 0);
        tree_view->setCurrentIndex(idx);
        tree_view->scrollTo(idx);
        tree_view->setFocus();
    });
}

QtProfileSelectionDialog::~QtProfileSelectionDialog() {
    controller_navigation->UnloadController();
};

int QtProfileSelectionDialog::exec() {
    // Skip profile selection when there's only one.
    if (profile_manager.GetUserCount() == 1) {
        user_index = 0;
        return QDialog::Accepted;
    }
    return QDialog::exec();
}

void QtProfileSelectionDialog::accept() {
    QDialog::accept();
}

void QtProfileSelectionDialog::reject() {
    user_index = 0;
    QDialog::reject();
}

int QtProfileSelectionDialog::GetIndex() const {
    return user_index;
}

void QtProfileSelectionDialog::SelectUser(const QModelIndex& index) {
    user_index = index.row();
}

void QtProfileSelectionDialog::SetWindowTitle(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    using Service::AM::Frontend::UiMode;
    switch (parameters.mode) {
    case UiMode::UserCreator:
    case UiMode::UserCreatorForStarter:
        setWindowTitle(tr("Profile Creator"));
        return;
    case UiMode::EnsureNetworkServiceAccountAvailable:
        setWindowTitle(tr("Profile Selector"));
        return;
    case UiMode::UserIconEditor:
        setWindowTitle(tr("Profile Icon Editor"));
        return;
    case UiMode::UserNicknameEditor:
        setWindowTitle(tr("Profile Nickname Editor"));
        return;
    case UiMode::NintendoAccountAuthorizationRequestContext:
    case UiMode::IntroduceExternalNetworkServiceAccount:
    case UiMode::IntroduceExternalNetworkServiceAccountForRegistration:
    case UiMode::NintendoAccountNnidLinker:
    case UiMode::LicenseRequirementsForNetworkService:
    case UiMode::LicenseRequirementsForNetworkServiceWithUserContextImpl:
    case UiMode::UserCreatorForImmediateNaLoginTest:
    case UiMode::UserQualificationPromoter:
    case UiMode::UserSelector:
    default:
        setWindowTitle(tr("Profile Selector"));
    }
}

void QtProfileSelectionDialog::SetDialogPurpose(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    using Service::AM::Frontend::UserSelectionPurpose;

    switch (parameters.purpose) {
    case UserSelectionPurpose::GameCardRegistration:
        instruction_label->setText(tr("Who will receive the points?"));
        return;
    case UserSelectionPurpose::EShopLaunch:
        instruction_label->setText(tr("Who is using Nintendo eShop?"));
        return;
    case UserSelectionPurpose::EShopItemShow:
        instruction_label->setText(tr("Who is making this purchase?"));
        return;
    case UserSelectionPurpose::PicturePost:
        instruction_label->setText(tr("Who is posting?"));
        return;
    case UserSelectionPurpose::NintendoAccountLinkage:
        instruction_label->setText(tr("Select a user to link to a Nintendo Account."));
        return;
    case UserSelectionPurpose::SettingsUpdate:
        instruction_label->setText(tr("Change settings for which user?"));
        return;
    case UserSelectionPurpose::SaveDataDeletion:
        instruction_label->setText(tr("Format data for which user?"));
        return;
    case UserSelectionPurpose::UserMigration:
        instruction_label->setText(tr("Which user will be transferred to another console?"));
        return;
    case UserSelectionPurpose::SaveDataTransfer:
        instruction_label->setText(tr("Send save data for which user?"));
        return;
    case UserSelectionPurpose::General:
    default:
        instruction_label->setText(tr("Select a user:"));
        return;
    }
}

QtProfileSelector::QtProfileSelector(MainWindow& parent) {
    connect(this, &QtProfileSelector::MainWindowSelectProfile, &parent,
            &MainWindow::ProfileSelectorSelectProfile, Qt::QueuedConnection);
    connect(this, &QtProfileSelector::MainWindowRequestExit, &parent,
            &MainWindow::ProfileSelectorRequestExit, Qt::QueuedConnection);
    connect(&parent, &MainWindow::ProfileSelectorFinishedSelection, this,
            &QtProfileSelector::MainWindowFinishedSelection, Qt::DirectConnection);
}

QtProfileSelector::~QtProfileSelector() = default;

void QtProfileSelector::Close() const {
    callback = {};
    emit MainWindowRequestExit();
}

void QtProfileSelector::SelectProfile(
    SelectProfileCallback callback_,
    const Core::Frontend::ProfileSelectParameters& parameters) const {
    callback = std::move(callback_);
    emit MainWindowSelectProfile(parameters);
}

void QtProfileSelector::MainWindowFinishedSelection(std::optional<Common::UUID> uuid) {
    if (callback) {
        callback(uuid);
    }
}
