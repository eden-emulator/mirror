// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>

#include "common/param_package.h"
#include "common/settings.h"
#include "ui_configure_input.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QString;
class QTimer;
class QToolButton;
class QWidget;

class InputProfiles;

namespace InputCommon {
class InputSubsystem;
}

namespace InputCommon::Polling {
enum class InputType;
} // namespace InputCommon::Polling

namespace Ui {
class ConfigureInputPlayer;
} // namespace Ui

namespace Core::HID {
class HIDCore;
class EmulatedController;
enum class NpadStyleIndex : u8;
} // namespace Core::HID

class ConfigureInputPlayer : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureInputPlayer(QWidget* parent, std::size_t player_index, QWidget* bottom_row,
                                  InputCommon::InputSubsystem* input_subsystem_,
                                  InputProfiles* profiles_, Core::HID::HIDCore& hid_core_,
                                  bool is_powered_on_, bool debug = false);
    ~ConfigureInputPlayer() override;

    /// Save all button configurations to settings file.
    void ApplyConfiguration();

    /// Set the connection state checkbox (used to sync state).
    void ConnectPlayer(bool connected);

    /// Update the input devices combobox.
    void UpdateInputDeviceCombobox();

    /// Redraws the tab from its emulated controller after the engine reloaded it
    void RefreshFromController();

    /// Use Any Controller as Joy-Cons: while this player plays as a single Joy-Con its mapping
    /// is set automatically, and read-only here (with a note why)
    [[nodiscard]] bool JoyConLocked() const {
        return joycon_locked;
    }

    /// In the automatic modes the tab hides the connection, device and motion choices
    void SetConnectionLocked(bool locked);

    /// Updates the list of controller profiles.
    void UpdateInputProfiles();

    /// Restore all buttons to their default values.
    void RestoreDefaults();

    /// Clear all input configuration.
    void ClearAll();

    /// The SDL controller (guid, port) the player's bindings point at now, if any
    [[nodiscard]] std::optional<std::pair<std::string, int>> CurrentSdlDevice() const;
    /// Clear's work: every binding emptied
    void ClearMappings();
    /// The player's motion bindings, kept across Clear and Defaults in the automatic modes
    /// (motion is hidden there); none otherwise
    [[nodiscard]] std::vector<Common::ParamPackage> KeptMotions() const;
    void RestoreMotions(const std::vector<Common::ParamPackage>& motions);

signals:
    /// Emitted when this controller is (dis)connected by the user.
    void Connected(bool connected);
    /// Emitted when the Handheld mode is selected (undocked with dual joycons attached).
    void HandheldStateChanged(bool is_handheld);
    /// Emitted when the input devices combobox is being refreshed.
    void RefreshInputDevices();
    /**
     * Emitted when the input profiles combobox is being refreshed.
     * The player_index represents the current player's index, and the profile combobox
     * will not be updated for this index as they are already updated by other mechanisms.
     */
    void RefreshInputProfiles(std::size_t player_index);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QString ButtonToText(const Common::ParamPackage& param);

    QString AnalogToText(const Common::ParamPackage& param, const std::string& dir);

    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    /// Load configuration settings.
    void LoadConfiguration();

    /// Called when the button was pressed.
    void HandleClick(QPushButton* button, std::size_t button_id,
                     std::function<void(const Common::ParamPackage&)> new_input_setter,
                     InputCommon::Polling::InputType type);

    /// Finish polling and configure input using the input_setter.
    void SetPollingResult(const Common::ParamPackage& params, bool abort);

    /// Checks whether a given input can be accepted.
    bool IsInputAcceptable(const Common::ParamPackage& params) const;

    /// Handle mouse button press events.
    void mousePressEvent(QMouseEvent* event) override;

    /// Handle mouse wheel move events.
    void wheelEvent(QWheelEvent* event) override;

    /// Handle key press events.
    void keyPressEvent(QKeyEvent* event) override;

    /// Handle combobox list refresh
    bool eventFilter(QObject* object, QEvent* event) override;

    /// Update UI to reflect current configuration.
    void UpdateUI();

    /// Sets the available controllers.
    void SetConnectableControllers();

    /// Gets the Controller Type for a given controller combobox index.
    Core::HID::NpadStyleIndex GetControllerTypeFromIndex(int index) const;

    /// Gets the controller combobox index for a given Controller Type.
    int GetIndexFromControllerType(Core::HID::NpadStyleIndex type) const;

    /// Update the available input devices.
    void UpdateInputDevices();

    /// Hides and disables controller settings based on the current controller type.
    void UpdateControllerAvailableButtons();

    /// Disables controller settings based on the current controller type.
    void UpdateControllerEnabledButtons();

    /// Shows or hides motion groupboxes based on the current controller type.
    void UpdateMotionButtons();

    /// Alters the button names based on the current controller type.
    void UpdateControllerButtonNames();

    /// Gets the default controller mapping for this device and auto configures the input to match.
    void UpdateMappingWithDefaults();

    /// Creates a controller profile.
    void CreateProfile();

    /// Deletes the selected controller profile.
    void DeleteProfile();

    /// Loads the selected controller profile.
    void LoadProfile();

    /// Saves the current controller configuration into a selected controller profile.
    void SaveProfile();

    std::unique_ptr<Ui::ConfigureInputPlayer> ui;

    std::size_t player_index;
    bool debug;
    bool is_powered_on;
    /// Players are assigned automatically: the connection and controller are not this tab's
    bool connection_locked{};
    /// The SDL controller (guid, port) the bindings last pointed at, for Defaults after Clear
    std::optional<std::pair<std::string, int>> locked_device;
    bool style_applied{};
    bool home_centred{};
    bool shoulders_raised{};
    /// The section boxes given a bold title in the automatic modes
    std::vector<QPointer<QGroupBox>> bolded_boxes;

    /// The profile card shown instead of the Profile group in the automatic modes (comboProfiles
    /// stays the selection)
    void BuildProfileCard();
    void UpdateProfileCard();
    /// The selected profile's mapping, kept to tell when the tab's mapping differs from it
    void RememberProfileMapping();
    std::string CurrentMappingKey() const;
    void SelectProfile(const QString& name);
    void RenameProfile();
    void DuplicateProfile();
    void DeleteProfileAsked();
    QGroupBox* profile_card{};
    QComboBox* card_profiles{};
    QPushButton* card_save{};
    QToolButton* card_remove{};
    QPushButton* card_save_new{};
    QPushButton* card_rename{};
    QPushButton* card_duplicate{};
    QPushButton* card_delete{};
    QLabel* card_status{};
    QLabel* joycon_note{};
    bool joycon_locked = false;
    void ApplyJoyConLock();
    QTimer* card_timer{};
    std::string card_profile_key;
    QString card_profile_name;
    /// The profile the engine last reported for this player
    QString card_engine_profile;

    InputCommon::InputSubsystem* input_subsystem;

    InputProfiles* profiles;

    std::unique_ptr<QTimer> timeout_timer;
    std::unique_ptr<QTimer> poll_timer;

    /// Stores a pair of "Connected Controllers" combobox index and Controller Type enum.
    std::vector<std::pair<int, Core::HID::NpadStyleIndex>> index_controller_type_pairs;

    /// This will be the the setting function when an input is awaiting configuration.
    std::optional<std::function<void(const Common::ParamPackage&)>> input_setter;

    Core::HID::EmulatedController* emulated_controller;

    static constexpr int ANALOG_SUB_BUTTONS_NUM = 4;

    /// Each button input is represented by a QPushButton.
    std::array<QPushButton*, Settings::NativeButton::NumButtons> button_map;

    /// A group of four QPushButtons represent one analog input. The buttons each represent up,
    /// down, left, right, respectively.
    std::array<std::array<QPushButton*, ANALOG_SUB_BUTTONS_NUM>, Settings::NativeAnalog::NumAnalogs>
        analog_map_buttons;

    /// Each motion input is represented by a QPushButton.
    std::array<QPushButton*, Settings::NativeMotion::NumMotions> motion_map;

    std::array<QLabel*, Settings::NativeAnalog::NumAnalogs> analog_map_deadzone_label;
    std::array<QSlider*, Settings::NativeAnalog::NumAnalogs> analog_map_deadzone_slider;
    std::array<QGroupBox*, Settings::NativeAnalog::NumAnalogs> analog_map_modifier_groupbox;
    std::array<QPushButton*, Settings::NativeAnalog::NumAnalogs> analog_map_modifier_button;
    std::array<QLabel*, Settings::NativeAnalog::NumAnalogs> analog_map_modifier_label;
    std::array<QSlider*, Settings::NativeAnalog::NumAnalogs> analog_map_modifier_slider;
    std::array<QGroupBox*, Settings::NativeAnalog::NumAnalogs> analog_map_range_groupbox;
    std::array<QSpinBox*, Settings::NativeAnalog::NumAnalogs> analog_map_range_spinbox;

    static const std::array<std::string, ANALOG_SUB_BUTTONS_NUM> analog_sub_buttons;

    /// A flag to indicate that the "Map Analog Stick" pop-up has been shown and accepted once.
    bool map_analog_stick_accepted{};

    /// List of physical devices users can map with. If a SDL backed device is selected, then you
    /// can use this device to get a default mapping.
    std::vector<Common::ParamPackage> input_devices;

    /// Bottom row is where console wide settings are held, and its "owned" by the parent
    /// ConfigureInput widget. On show, add this widget to the main layout. This will change the
    /// parent of the widget to this widget (but that's fine).
    QWidget* bottom_row;

    Core::HID::HIDCore& hid_core;
};
