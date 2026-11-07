// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>

#include <array>
#include <bitset>
#include <chrono>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTranslator>
#include <qaction.h>

#include "common/common_types.h"
#include "common/settings_enums.h"
#include "common/settings_input.h"
#include "frontend_common/content_manager.h"
#include "input_common/drivers/tas_input.h"
#include "qt_common/config/qt_config.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"
#include "yuzu/hotkeys.h"
#include "yuzu/user_data_migration.h"

#ifdef __unix__
#include <QDBusObjectPath>
#include <QSocketNotifier>
#include <QVariant>
#include <QtDBus/QDBusInterface>
#endif

#ifdef ENABLE_UPDATE_CHECKER
#include <QFuture>
#include <QFutureWatcher>
#include "common/net/net.h"
#endif

class QtConfig;
class EmuThread;
class GameList;
class GImageInfo;
class GRenderWindow;
class LoadingScreen;
class OverlayDialog;
class ControllerDialog;
class QLabel;
class QShortcut;
class MultiplayerState;
class QPushButton;
class QProgressDialog;
class QSlider;
class QHBoxLayout;
class WaitTreeWidget;
class PerformanceOverlay;
#ifdef HAS_RESHADE
class ConfigurePostProcessing;
#endif
enum class GameListOpenTarget;
enum class DumpRomFSTarget;
class GameListPlaceholder;

class QtAmiiboSettingsDialog;
class QtControllerSelectorDialog;
class QtProfileSelectionDialog;
class QtSoftwareKeyboardDialog;
class QtNXWebEngineView;

namespace VideoCore {
class ShaderNotify;
}
namespace Core {
enum class SystemResultStatus : u32;
} // namespace Core

namespace Core::Frontend {
struct CabinetParameters;
struct ControllerParameters;
struct InlineAppearParameters;
struct InlineTextParameters;
struct KeyboardInitializeParameters;
struct ProfileSelectParameters;
} // namespace Core::Frontend

namespace DiscordRPC {
class DiscordInterface;
}

namespace PlayTime {
class PlayTimeManager;
}

namespace FileSys {
class ContentProvider;
class ManualContentProvider;
class VfsFilesystem;
} // namespace FileSys

namespace InputCommon {
class InputSubsystem;
}

namespace Service::AM {
struct FrontendAppletParameters;
enum class AppletId : u32;
// this causes errors on debian -> enum class AppletProgramId : u64;
} // namespace Service::AM

namespace Service::AM::Frontend {
enum class SwkbdResult : u32;
enum class SwkbdTextCheckResult : u32;
enum class SwkbdReplyType : u32;
enum class WebExitReason : u32;
} // namespace Service::AM::Frontend

namespace Service::NFC {
class NfcDevice;
} // namespace Service::NFC

namespace Service::NFP {
enum class CabinetMode : u8;
} // namespace Service::NFP

namespace Ui {
class MainWindow;
}

enum class EmulatedDirectoryTarget {
    NAND,
    SDMC,
};

namespace VkDeviceInfo {
class Record;
}

class VolumeButton : public QPushButton {
    Q_OBJECT
public:
    explicit VolumeButton(QWidget* parent = nullptr) : QPushButton(parent), scroll_multiplier(1) {
        connect(&scroll_timer, &QTimer::timeout, this, &VolumeButton::ResetMultiplier);
    }

signals:
    void VolumeChanged();

protected:
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void ResetMultiplier();

private:
    int scroll_multiplier;
    QTimer scroll_timer;
    constexpr static int MaxMultiplier = 8;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

    /// Max number of recently loaded items to keep track of
    static const int max_recent_files_item = 10;

public:
    void filterBarSetChecked(bool state);
    void UpdateUITheme();
    explicit MainWindow(bool has_broken_vulkan);
    ~MainWindow() override;

    bool DropAction(QDropEvent* event);
    void AcceptDropEvent(QDropEvent* event);

    std::filesystem::path GetShortcutPath(QtCommon::Game::ShortcutTarget target);

signals:

    /**
     * Signal that is emitted when a new EmuThread has been created and an emulation session is
     * about to start. At this time, the core system emulation has been initialized, and all
     * emulation handles and memory should be valid.
     */
    void EmulationStarting();

    /**
     * Signal that is emitted when emulation is about to stop. At this time, the EmuThread and core
     * system emulation handles and memory are still valid, but are about become invalid.
     */
    void EmulationStopping();

    // Signal that tells widgets to update icons to use the current theme
    void UpdateThemedIcons();

    void UpdateInstallProgress();

    void AmiiboSettingsFinished(bool is_success, const std::string& name);

    void ControllerSelectorReconfigureFinished(bool is_success);

    void ErrorDisplayFinished();

    void ProfileSelectorFinishedSelection(std::optional<Common::UUID> uuid);

    void SoftwareKeyboardSubmitNormalText(Service::AM::Frontend::SwkbdResult result,
                                          std::u16string submitted_text, bool confirmed);
    void SoftwareKeyboardSubmitInlineText(Service::AM::Frontend::SwkbdReplyType reply_type,
                                          std::u16string submitted_text, s32 cursor_position);

    void WebBrowserExtractOfflineRomFS();
    void WebBrowserClosed(Service::AM::Frontend::WebExitReason exit_reason, std::string last_url);

    void SigInterrupt();
    void sizeChanged(const QSize& size);
    void positionChanged(const QPoint& pos);
    void statsUpdated(const Core::PerfStatsResults& results,
                      const VideoCore::ShaderNotify& shaders);

public slots:
    void OnLoadComplete();
    void OnExecuteProgram(std::size_t program_index);
    void OnExit();
    void OnSaveConfig();
    void AmiiboSettingsShowDialog(const Core::Frontend::CabinetParameters& parameters,
                                  std::shared_ptr<Service::NFC::NfcDevice> nfp_device);
    void AmiiboSettingsRequestExit();
    void ControllerSelectorReconfigureControllers(
        const Core::Frontend::ControllerParameters& parameters);
    void ControllerSelectorRequestExit();
    void SoftwareKeyboardInitialize(
        bool is_inline, Core::Frontend::KeyboardInitializeParameters initialize_parameters);
    void SoftwareKeyboardShowNormal();
    void SoftwareKeyboardShowTextCheck(
        Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
        std::u16string text_check_message);
    void SoftwareKeyboardShowInline(Core::Frontend::InlineAppearParameters appear_parameters);
    void SoftwareKeyboardHideInline();
    void SoftwareKeyboardInlineTextChanged(Core::Frontend::InlineTextParameters text_parameters);
    void SoftwareKeyboardExit();
    void ErrorDisplayDisplayError(QString error_code, QString error_text);
    void ErrorDisplayRequestExit();
    void ProfileSelectorSelectProfile(const Core::Frontend::ProfileSelectParameters& parameters);
    void ProfileSelectorRequestExit();
    void WebBrowserOpenWebPage(const std::string& main_url, const std::string& additional_args,
                               bool is_local);
    void WebBrowserRequestExit();
    void OnAppFocusStateChanged(Qt::ApplicationState state);
    void OnTasStateChanged();

private:
    /// Updates an action's shortcut and text to reflect an updated hotkey from the hotkey registry.
    void LinkActionShortcut(QAction* action, const QString& action_name,
                            const bool tas_allowed = false);

    void InitializeWidgets();
    void InitializeDebugWidgets();
    void InitializeRecentFileMenuActions();

    void SetDefaultUIGeometry();
    void RestoreUIState();

    void ConnectWidgetEvents();
    void ConnectMenuEvents();
    void UpdateMenuState();
    void RefreshRuntimeCheats();
    void ClearRuntimeCheats();

    void SetupPrepareForSleep();

    void PreventOSSleep();
    void AllowOSSleep();

    bool LoadROM(const QString& filename, Service::AM::FrontendAppletParameters params);
    void BootGame(const QString& filename, Service::AM::FrontendAppletParameters params,
                  StartGameType with_config = StartGameType::Normal);
    void BootGameFromList(const QString& filename, StartGameType with_config);
    void ShutdownGame();

    void SetDiscordEnabled(bool state);
    void LoadAmiibo(const QString& filename);

    bool SelectAndSetCurrentUser(const Core::Frontend::ProfileSelectParameters& parameters);

    /**
     * Stores the filename in the recently loaded files list.
     * The new filename is stored at the beginning of the recently loaded files list.
     * After inserting the new entry, duplicates are removed meaning that if
     * this was inserted from \a OnMenuRecentFile(), the entry will be put on top
     * and remove from its previous position.
     *
     * Finally, this function calls \a UpdateRecentFiles() to update the UI.
     *
     * @param filename the filename to store
     */
    void StoreRecentFile(const QString& filename);

    /**
     * Updates the recent files menu.
     * Menu entries are rebuilt from the configuration file.
     * If there is no entry in the menu, the menu is greyed out.
     */
    void UpdateRecentFiles();

    /**
     * If the emulation is running,
     * asks the user if he really want to close the emulator
     *
     * @return true if the user confirmed
     */
    bool ConfirmClose();
    bool ConfirmChangeGame();
    bool ConfirmForceLockedExit();
    void RequestGameExit();
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

    std::string CreateTASFramesString(
        std::array<size_t, InputCommon::TasInput::PLAYER_NUMBER> frames) const;

#ifdef __unix__
    void SetupSigInterrupts();
    static void HandleSigInterrupt(int);
    void OnSigInterruptNotifierActivated();
#endif
    void SetGamemodeEnabled(bool state);

    Service::AM::FrontendAppletParameters ApplicationAppletParameters();
    Service::AM::FrontendAppletParameters LibraryAppletParameters(u64 program_id,
                                                                  Service::AM::AppletId applet_id);

private slots:
    void OnStartGame();
    void OnRestartGame();
    void OnPauseGame();
    void OnPauseContinueGame();
    void OnStopGame();
    void OnPrepareForSleep(bool prepare_sleep);
    void OnMenuReportCompatibility();
    void OnOpenModsPage();
    void OnOpenUserHandbook();

    /// Called whenever a user selects a game in the game list widget.
    void OnGameListLoadFile(QString game_path, u64 program_id);
    void OnGameListOpenFolder(u64 program_id, GameListOpenTarget target,
                              const std::string& game_path);
    void OnGameListRemoveInstalledEntry(u64 program_id, QtCommon::Game::InstalledEntryType type);
    void OnGameListRemoveFile(u64 program_id, QtCommon::Game::GameListRemoveTarget target,
                              const std::string& game_path);
    void OnGameListRemovePlayTimeData(u64 program_id);
    void OnGameListSetPlayTime(u64 program_id);
    void OnGameListDumpRomFS(u64 program_id, const std::string& game_path, DumpRomFSTarget target);
    void OnGameListVerifyIntegrity(const std::string& game_path);
    void OnGameListCopyTID(u64 program_id);
    void OnGameListCreateShortcut(u64 program_id, const std::string& game_path,
                                  const QtCommon::Game::ShortcutTarget target);
    void OnGameListOpenDirectory(const QString& directory);
    void OnGameListAddDirectory();
    void OnGameListShowList(bool show);
    void OnGameListOpenPerGameProperties(const std::string& file);
    void OnLinkToRyujinx(const u64& program_id);
    void OnMenuLoadFile();
    void OnMenuLoadFolder();
    void IncrementInstallProgress();
    void OnMenuInstallToNAND();
    void OnMenuRecentFile();
    void OnConfigure();
    void OnConfigureTas();
    void OnDecreaseVolume();
    void OnIncreaseVolume();
    void OnMute();
    void OnTasStartStop();
    void OnTasRecord();
    void OnTasReset();
    void OnToggleGraphicsAPI();
    void OnToggleDockedMode();
    void OnToggleGpuAccuracy();
    void OnToggleAdaptingFilter();
    void OnConfigurePerGame();
    void OnLoadAmiibo();
    void OnOpenRootDataFolder();
    void OnOpenNANDFolder();
    void OnOpenSDMCFolder();
    void OnOpenModFolder();
    void OnOpenLogFolder();
    void OnVerifyInstalledContents();
    void OnInstallFirmware();
    void OnInstallFirmwareFromZIP();
    void OnInstallDecryptionKeys();
    void OnAbout();
    void OnEdenDependencies();
    void OnDataDialog();
    void OnToggleFilterBar();
    void OnToggleStatusBar();
    void OnTogglePerfOverlay();
#ifdef HAS_RESHADE
    void OnPostProcessingShaders();
#endif
    void OnGameListRefresh();
    void InitializeHotkeys();
    void ToggleFullscreen();
    bool UsingExclusiveFullscreen();
    void ShowFullscreen();
    void HideFullscreen();
    void ToggleWindowMode();
    void ResetWindowSize(u32 width, u32 height);
    void ResetWindowSize720();
    void ResetWindowSize900();
    void ResetWindowSize1080();

    void SetGameListMode(Settings::GameListMode mode);
    void SetGridView();
    void SetTreeView();
    void SetCarouselView();

    void CheckIconSize();
    void ToggleShowGameName();

    void LaunchFirmwareApplet(u64 program_id, std::optional<Service::NFP::CabinetMode> mode);
    void OnCreateHomeMenuDesktopShortcut();
    void OnCreateHomeMenuApplicationMenuShortcut();
    void OnCaptureScreenshot();
    void OnCheckFirmwareDecryption();
    void OnLanguageChanged(const QString& locale);
    void OnMouseActivity();
    bool OnShutdownBegin();
    void OnShutdownBeginDialog();
    void OnEmulationStopped();
    void OnEmulationStopTimeExpired();

#ifdef ENABLE_UPDATE_CHECKER
    void OnEmulatorUpdateAvailable();
#endif

private:
    void RemovePlayTimeData(u64 program_id);
    bool SelectRomFSDumpTarget(const FileSys::ContentProvider&, u64 program_id,
                               u64* selected_title_id, u8* selected_content_record_type);
    ContentManager::InstallResult InstallNCA(const QString& filename);
    void UpdateWindowTitle(std::string_view title_name = {}, std::string_view title_version = {},
                           std::string_view gpu_vendor = {});
    void OnApplicationChanged(u64 program_id);
    void UpdateDockedButton();
    void UpdateAPIText();
    void UpdateFilterText();
    void UpdateAAText();
    void UpdatePostShaderText();
    void UpdateVolumeUI();
    void UpdateStatusBar();
    void UpdateGPUAccuracyButton();
    void UpdateStatusButtons();
    void UpdateUISettings();
    void UpdateInputDrivers();
    void HideMouseCursor();
    void ShowMouseCursor();
    void OpenURL(const QUrl& url);
    void LoadTranslation();
    void OpenPerGameConfiguration(u64 title_id, const std::string& file_name);
    bool CheckFirmwarePresence();
    void SetFirmwareVersion();
    void SetFPSSuffix();
    /**
     * Open (or not) the right confirm dialog based on current setting and game exit lock
     * @returns true if the player confirmed or the settings do no require it
     */
    bool ConfirmShutdownGame();

    QString GetTasStateDescription() const;
    bool CreateShortcutMessagesGUI(QWidget* parent, int imsg, const QString& game_title);

    /**
     * Mimic the behavior of QMessageBox::question but link controller navigation to the dialog
     * The only difference is that it returns a boolean.
     *
     * @returns true if buttons contains QMessageBox::Yes and the user clicks on the "Yes" button.
     */
    bool question(QWidget* parent, const QString& title, const QString& text,
                  QMessageBox::StandardButtons buttons =
                      QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No),
                  QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

    std::unique_ptr<Ui::MainWindow> ui;

    std::unique_ptr<DiscordRPC::DiscordInterface> discord_rpc;
    std::unique_ptr<PlayTime::PlayTimeManager> play_time_manager;
    std::shared_ptr<InputCommon::InputSubsystem> input_subsystem;

#ifdef ENABLE_UPDATE_CHECKER
    QFuture<std::optional<Common::Net::Release>> update_future;
    QFutureWatcher<std::optional<Common::Net::Release>> update_watcher;
#endif

    MultiplayerState* multiplayer_state = nullptr;

    GRenderWindow* render_window = nullptr;
    GameList* game_list = nullptr;
    LoadingScreen* loading_screen = nullptr;
    QTimer shutdown_timer;
    OverlayDialog* shutdown_dialog{};
    PerformanceOverlay* perf_overlay = nullptr;
#ifdef HAS_RESHADE
    ConfigurePostProcessing* post_processing_dialog = nullptr;
#endif

    GameListPlaceholder* game_list_placeholder = nullptr;

    std::vector<VkDeviceInfo::Record> vk_device_records;

    // Status bar elements
    QLabel* message_label = nullptr;
    QLabel* shader_building_label = nullptr;
    QLabel* res_scale_label = nullptr;
    QLabel* emu_speed_label = nullptr;
    QLabel* game_fps_label = nullptr;
    QLabel* emu_frametime_label = nullptr;
    QLabel* tas_label = nullptr;
    QLabel* firmware_label = nullptr;
    QPushButton* gpu_accuracy_button = nullptr;
    QPushButton* renderer_status_button = nullptr;
    QPushButton* refresh_button = nullptr;
    QPushButton* dock_status_button = nullptr;
    QPushButton* filter_status_button = nullptr;
    QPushButton* aa_status_button = nullptr;
    QPushButton* post_shader_status_button = nullptr;
    VolumeButton* volume_button = nullptr;
    QWidget* volume_popup = nullptr;
    QSlider* volume_slider = nullptr;
    QTimer status_bar_update_timer;

    // Stores what suffix to add to the FPS counter, e.g. Unlocked.
    QString m_fpsSuffix{};

    UserDataMigrator user_data_migrator;
    std::unique_ptr<QtConfig> config;

    // Whether emulation is currently running in yuzu.
    bool emulation_running = false;
    // The path to the game currently running
    QString current_game_path;
    // Whether a user was set on the command line (skips UserSelector if it's forced to show up)
    bool user_flag_cmd_line = false;

    bool auto_paused = false;
    bool auto_muted = false;
    QTimer mouse_hide_timer;
    QTimer update_input_timer;

    QString startup_icon_theme;

    QActionGroup* game_size_actions;
    std::vector<QAction*> runtime_cheat_actions;

    /// The console modes' engine: assigns the connected controllers to players, on a timer
    QTimer console_mode_timer;
    /// Eden is closing: the engine stops (it would reload the inputs closeEvent unloaded)
    bool closing = false;
    void ApplyConsoleControllerMode();
    /// connection instance -> player slot; kept after a disconnect so the pad reclaims its player
    std::map<u32, int> pad_slots;
    /// Per connection: its controller's Bluetooth address, the player it started as, and
    /// whether its player was a guess on a reconnect
    std::map<u32, std::string> inst_address;
    std::map<u32, int> pad_origin;
    std::set<u32> pad_guessed;
    /// Returning pads waiting a moment for their Bluetooth address before they claim a player
    std::map<u32, std::chrono::steady_clock::time_point> claim_wait_since;
    /// Players taken by a guess, kept for the Bluetooth controller that held them
    struct AddressClaim {
        int slot;
        int origin;
        std::chrono::steady_clock::time_point at;
    };
    std::map<std::string, AddressClaim> address_claims;
    /// Each Bluetooth controller's starting player, by address: kept through Turn Off and
    /// reconnects
    std::map<std::string, int> address_origin;
    /// Names given to controllers ("bt:<address>", "handheld" or "id:<identity>"). Never saved:
    /// owners can't tell identical controllers apart in a later session
    std::map<std::string, std::string> controller_names;
    /// The mode and each player's refused controller type as last logged
    std::optional<Settings::AppletMode> logged_mode;
    std::array<std::string, 8> refused_logged{};
    /// When the deck's controller was last asked to turn its motion sensors back on
    qint64 deck_motion_asked_at = 0;
    /// Pads whose player was removed: they sit out until they reconnect (connection instance)
    std::set<u32> benched_pads;
    /// When each player's controller was last turned off (Turn Off)
    std::array<qint64, 10> turned_off_at{};
    /// Each pad's Bluetooth address by BluetoothKey ("" none or unknown), and the pads not yet
    /// told apart from others of their model
    std::map<std::string, std::string> pad_bluetooth;
    std::set<std::string> pad_bt_pending;
    void LearnBluetoothPads();
    /// Reads the pads' and the real controllers' inputs
    void WatchSteamPadInputs();
    /// The last time a pad had input together with exactly one real controller: when Steam
    /// then moves a controller to that pad, that is the one
    struct LoneInput {
        u32 pad;
        std::string controller;
        std::chrono::steady_clock::time_point at;
    };
    std::optional<LoneInput> last_lone_input;
    /// connection instance -> stable identity (serial, else model GUID), to match a returning pad
    std::map<u32, std::string> pad_identity;
    /// identity -> the binding set the controller carries, so its mappings follow it
    std::map<std::string, int> pad_sets;
    /// player -> the set its controller carries, as of the last tick
    std::array<int, 10> slot_set{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    /// set -> the controller whose default mapping it holds (0: the user's, or empty); a set
    /// holding a gone controller's defaults is rebuilt for the one taking it
    std::array<u32, 10> set_default_of{};
    /// This session's binding sets: profiles and remaps stay here, so the user's own setup
    /// (the manual modes') is never touched
    std::optional<std::array<Settings::PlayerInput, 10>> session_sets;
    /// The set a change goes to: the session's, or a game's own profile set
    Settings::PlayerInput& SessionSet(int set);
    /// The profile each seat (player number) starts a session with
    void ApplySeatProfiles();
    void SetSeatProfile(int player, const std::string& name);
    /// A set's mappings before this session's first profile on it, for taking the profile off
    std::map<int, Settings::PlayerInput> set_before_profile;
    /// The handheld's own pad: never a player while other pads are connected
    u32 builtin_pad_id = 0;
    /// The built-in's stable identity: the exclusion must survive its instance changing
    std::string builtin_identity;
    /// Latched once the built-in has been identified
    bool builtin_seen_first = false;
    /// Warn once per engagement when the built-in cannot be identified
    bool builtin_warned = false;
    /// Overlay rows per player: when it appeared, when its pad left, and its name
    std::array<qint64, 10> overlay_row_since{};
    std::array<qint64, 10> overlay_row_gone{};
    std::array<QString, 10> overlay_row_name{};
    /// Per player: the controller behind the row (its identity), and its flash (1 green, 2 red)
    /// with when it began; a red then green keeps the green's start in overlay_green_at
    std::array<std::string, 10> overlay_row_ident{};
    /// The key each player's input history is kept under (identical controllers can share an
    /// identity)
    std::array<std::string, 10> overlay_row_history{};
    std::array<int, 10> overlay_flash{};
    std::array<qint64, 10> overlay_flash_since{};
    std::array<qint64, 10> overlay_green_at{};
    /// Per player: the real device its motion comes from under Steam (0: none)
    std::array<u32, 10> overlay_motion_source{};
    /// The overlay's rows as the last assignment tick left them: flash 1 green, 2 red
    struct OverlayRow {
        int player;
        QString text;
        u32 pad;
        int flash;
        qint64 flash_since;
        /// The player its controller started as (-1: this one), and whether it was a guess
        int origin = -1;
        bool guessed = false;
        /// Its controller's battery (percent, -1 unknown) and whether it charges
        int battery = -1;
        bool charging = false;
    };
    std::vector<OverlayRow> overlay_rows;
    std::vector<bool> overlay_blink;
    /// The room the players panel had last: redrawn when a top-right panel changes
    float overlay_room = 0.0f;
    std::set<u32> overlay_active_pads;
    /// Polls controller input faster than the assignment tick, for the blue highlight
    QTimer console_activity_timer;
    /// The overlay's player rows as drawn, for the mouse
    std::vector<std::pair<int, QRect>> overlay_hits;
    int overlay_drag_player = -1;
    int overlay_drag_dy = 0;
    bool overlay_dragging = false;
    QPoint overlay_press;
    /// The row under the mouse (-1: none) and its buttons as drawn: swap above (-1) or below
    /// (1), Turn Off (0), Config (2), rename (3); while naming apply (4), remove (5), cancel (6)
    int overlay_hover_player = -1;
    bool overlay_button_down = false;
    struct OverlayButton {
        QRect rect;
        int player;
        int action;
        int target;
    };
    std::vector<OverlayButton> overlay_buttons;
    bool HandleOverlayMouse(QEvent* event);
    /// Each player's given name as shown, and the name being typed (player -1: none)
    std::array<std::string, 10> overlay_custom_name{};
    /// The name of the controller that last left each player, for its hint
    std::array<QString, 10> overlay_left_name{};
    /// Batteries, read at most every 10 seconds, and the controllers already warned of a low one
    struct Battery {
        int percent = -1;
        bool charging = false;
        bool wired = false;
    };
    std::map<u32, std::pair<qint64, Battery>> battery_read;
    /// A controller's first BlueZ battery reading and when, by address (BlueZ says 50% at first)
    std::map<std::string, std::pair<qint64, int>> bluez_first;
    /// Whose controller each pad's battery reading is (Steam handle or identity, handheld or not)
    std::map<u32, std::string> battery_whose;
    std::set<std::string> battery_low_told;
    /// Use Any Controller as Joy-Cons: each player's type before the engine changed it, and the
    /// type it set
    std::array<std::optional<Settings::ControllerType>, 10> joycon_auto_from{};
    std::array<std::optional<Settings::ControllerType>, 10> joycon_auto_set{};
    /// A single Joy-Con set by it: held sideways (else upright), as the game holds it
    std::array<bool, 10> joycon_auto_sideways{};
    /// Pause When a Controller Disconnects: since when each player waits (0 none), and the
    /// players a pause is for
    std::array<qint64, 10> waiting_since{};
    std::set<int> disconnect_paused;
    /// A handover pause: the player (-1 none) the handheld's controls took over mid-game when
    /// the last external controller left (it may be docked). Goes on at a press on the handheld
    /// or when an external controller returns. Also who held each player at the last tick.
    int handover_paused = -1;
    bool handover_pressed = false;
    std::array<bool, 10> held_by_external{};
    /// Whether the handheld's controls held a player at the last tick (in Modern Console they
    /// may have played all along: no handover)
    bool handheld_was_playing = false;
    /// What BlueZ last said of a controller (by address): connected, and when asked and answered
    struct BluezConnection {
        bool connected = true;
        qint64 asked_ms = 0;
        qint64 answered_ms = 0;
    };
    std::map<std::string, BluezConnection> bluez_connected;
    /// What BlueZ last said of a controller's battery (by address; -1 nothing yet), and when
    struct BluezBattery {
        int percent = -1;
        qint64 asked_ms = 0;
    };
    std::map<std::string, BluezBattery> bluez_battery;
    /// The pause for a disconnect is about to come (its notice is drawn first)
    bool disconnect_pause_pending = false;
    /// The game was seen paused (until then, running isn't a resume by hand)
    bool disconnect_pause_seen = false;
    /// + is held on another controller during that pause: on without those players once let go
    bool disconnect_continue_held = false;
    std::set<int> pause_done;
    /// Player Lights: the player each controller's lights were last set to (by connection)
    std::map<u32, int> lights_player;
    int overlay_edit_player = -1;
    QString overlay_edit_text;
    /// The player whose tab the next settings dialog opens on (-1: as usual)
    int open_player_tab = -1;
    /// Under gamescope, makes a window fill the screen (see the definition)
    void FitToScreenUnderGamescope(QWidget* window);
    void RenderControllerOverlay();
    void PollControllerActivity();
    /// The last buttons each controller pressed, as the game sees them, newest last; by
    /// controller, so the history follows a swap
    struct ButtonPress {
        int button;
        qint64 at;
    };
    std::map<std::string, std::deque<ButtonPress>> press_history;
    /// The controller each player's buttons were last read for (a new one records no presses)
    std::array<std::string, 10> history_ident{};
    std::array<std::bitset<32>, 10> buttons_down{};
    /// Stick moves in the history: StickMoveButton + stick * 4 + direction (left, up, right,
    /// down), stick 0 the left one
    static constexpr int StickMoveButton = 64;
    /// The direction each player's sticks point, plus one (0 at rest)
    std::array<std::array<int, 2>, 10> stick_direction{};
    /// Reads the players' buttons; true when the history changed (a press, or one expiring)
    bool PollButtonHistory();
    /// A controller named on screen for a moment: it joined as a player, or left one
    struct ControllerHint {
        QString name;
        int player;
        bool joined;
        qint64 at;
        /// Left because Turn Off turned its controller off
        bool turned_off = false;
        /// Said instead of joined / left ("" none): how the player's controller changed
        QString note;
        /// Says the game is paused for a disconnect: kept while it is, shown with hints off too
        bool pause_notice = false;
    };
    std::vector<ControllerHint> controller_hints;
    /// A game running with no controller: since when (0 none), and whether it was said
    qint64 no_controller_since = 0;
    bool no_controller_told = false;
    /// The flashes already turned into hints, per player (green, red)
    std::array<qint64, 10> hinted_join{};
    std::array<qint64, 10> hinted_leave{};
    /// True while the overlay animates (hints fading, history entries shrinking away)
    bool OverlayAnimating() const;
    /// The user's own players while a console mode drives them: what the config file sees, and
    /// what leaving the mode puts back
    std::optional<std::array<Settings::PlayerInput, 10>> console_profile;
    /// The running game's own players, when it has input profiles of its own
    std::optional<std::array<Settings::PlayerInput, 10>> game_profiles;
    /// The binding sets the pads carry: the user's own, with a game's input profiles on top
    std::array<Settings::PlayerInput, 10> ConsoleBindingSets() const;
    /// Saves the configuration, writing the user's own players while a console mode runs
    void SaveAllConfigValues();
    /// Puts the user's own players back (leaving a console mode, or before the config saves)
    void RestoreConsoleProfile();
    /// Folds edits made in the Controls tab during a console mode into the user's profile
    void MergeConsoleProfileEdits(const std::array<Settings::PlayerInput, 10>& before);

    // Debugger panes
    ControllerDialog* controller_dialog = nullptr;

    QAction* actions_recent_files[max_recent_files_item];

    // stores default icon theme search paths for the platform
    QStringList default_theme_paths;

    HotkeyRegistry hotkey_registry;

    QTranslator translator;

    // Install progress dialog
    QProgressDialog* install_progress = nullptr;

    // Last game booted, used for multi-process apps
    QString last_filename_booted;

    // Applets
    QtAmiiboSettingsDialog* cabinet_applet = nullptr;
    QtControllerSelectorDialog* controller_applet = nullptr;
    QtProfileSelectionDialog* profile_select_applet = nullptr;
    QDialog* error_applet = nullptr;
    QtSoftwareKeyboardDialog* software_keyboard = nullptr;
    QtNXWebEngineView* web_applet = nullptr;

    // True if amiibo file select is visible
    bool is_amiibo_file_select_active{};

    // True if load file select is visible
    bool is_load_file_select_active{};

    // True if TAS recording dialog is visible
    bool is_tas_recording_dialog_active{};

#ifdef __unix__
    QSocketNotifier* sig_interrupt_notifier;
    static std::array<int, 3> sig_interrupt_fds;
#endif

    std::filesystem::path GetEdenCommand();

    void CreateShortcut(const std::string& game_path, const u64 program_id,
                        const std::string& game_title, QtCommon::Game::ShortcutTarget target,
                        std::string arguments, const bool needs_title);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
};
