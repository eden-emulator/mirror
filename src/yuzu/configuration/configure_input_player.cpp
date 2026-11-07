// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "common/assert.h"
#include "common/controller_console_state.h"
#include "common/param_package.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "input_common/drivers/keyboard.h"
#include "input_common/drivers/mouse.h"
#include "input_common/main.h"
#include "qt_common/config/qt_config.h"
#include "ui_configure_input_player.h"
#include "yuzu/bootmanager.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_input_player_widget.h"
#include "yuzu/configuration/configure_mouse_panning.h"
#include "yuzu/configuration/configure_player_assignment.h"
#include "yuzu/configuration/input_profiles.h"
#include "yuzu/util/limitable_input_dialog.h"

const std::array<std::string, ConfigureInputPlayer::ANALOG_SUB_BUTTONS_NUM>
    ConfigureInputPlayer::analog_sub_buttons{{
        "up",
        "down",
        "left",
        "right",
    }};

namespace {

QString GetKeyName(int key_code) {
    switch (key_code) {
    case Qt::Key_Shift:
        return QObject::tr("Shift");
    case Qt::Key_Control:
        return QObject::tr("Ctrl");
    case Qt::Key_Alt:
        return QObject::tr("Alt");
    case Qt::Key_Meta:
        return {};
    default:
        return QKeySequence(key_code).toString();
    }
}

QString GetButtonName(Common::Input::ButtonNames button_name) {
    switch (button_name) {
    case Common::Input::ButtonNames::ButtonLeft:
        return QObject::tr("Left");
    case Common::Input::ButtonNames::ButtonRight:
        return QObject::tr("Right");
    case Common::Input::ButtonNames::ButtonDown:
        return QObject::tr("Down");
    case Common::Input::ButtonNames::ButtonUp:
        return QObject::tr("Up");
    case Common::Input::ButtonNames::TriggerZ:
        return QObject::tr("Z");
    case Common::Input::ButtonNames::TriggerR:
        return QObject::tr("R");
    case Common::Input::ButtonNames::TriggerL:
        return QObject::tr("L");
    case Common::Input::ButtonNames::TriggerZR:
        return QObject::tr("ZR");
    case Common::Input::ButtonNames::TriggerZL:
        return QObject::tr("ZL");
    case Common::Input::ButtonNames::TriggerSR:
        return QObject::tr("SR");
    case Common::Input::ButtonNames::TriggerSL:
        return QObject::tr("SL");
    case Common::Input::ButtonNames::ButtonStickL:
        return QObject::tr("Stick L");
    case Common::Input::ButtonNames::ButtonStickR:
        return QObject::tr("Stick R");
    case Common::Input::ButtonNames::ButtonA:
        return QObject::tr("A");
    case Common::Input::ButtonNames::ButtonB:
        return QObject::tr("B");
    case Common::Input::ButtonNames::ButtonX:
        return QObject::tr("X");
    case Common::Input::ButtonNames::ButtonY:
        return QObject::tr("Y");
    case Common::Input::ButtonNames::ButtonStart:
        return QObject::tr("Start");
    case Common::Input::ButtonNames::ButtonPlus:
        return QObject::tr("Plus");
    case Common::Input::ButtonNames::ButtonMinus:
        return QObject::tr("Minus");
    case Common::Input::ButtonNames::ButtonHome:
        return QObject::tr("Home");
    case Common::Input::ButtonNames::ButtonCapture:
        return QObject::tr("Capture");
    case Common::Input::ButtonNames::L1:
        return QObject::tr("L1");
    case Common::Input::ButtonNames::L2:
        return QObject::tr("L2");
    case Common::Input::ButtonNames::L3:
        return QObject::tr("L3");
    case Common::Input::ButtonNames::R1:
        return QObject::tr("R1");
    case Common::Input::ButtonNames::R2:
        return QObject::tr("R2");
    case Common::Input::ButtonNames::R3:
        return QObject::tr("R3");
    case Common::Input::ButtonNames::Circle:
        return QObject::tr("Circle");
    case Common::Input::ButtonNames::Cross:
        return QObject::tr("Cross");
    case Common::Input::ButtonNames::Square:
        return QObject::tr("Square");
    case Common::Input::ButtonNames::Triangle:
        return QObject::tr("Triangle");
    case Common::Input::ButtonNames::Share:
        return QObject::tr("Share");
    case Common::Input::ButtonNames::Options:
        return QObject::tr("Options");
    case Common::Input::ButtonNames::Home:
        return QObject::tr("Home");
    case Common::Input::ButtonNames::Touch:
        return QObject::tr("Touch");
    case Common::Input::ButtonNames::ButtonMouseWheel:
        return QObject::tr("Wheel", "Indicates the mouse wheel");
    case Common::Input::ButtonNames::ButtonBackward:
        return QObject::tr("Backward");
    case Common::Input::ButtonNames::ButtonForward:
        return QObject::tr("Forward");
    case Common::Input::ButtonNames::ButtonTask:
        return QObject::tr("Task");
    case Common::Input::ButtonNames::ButtonExtra:
        return QObject::tr("Extra");
    default:
        return QObject::tr("[undefined]");
    }
}

QString GetDirectionName(const std::string& direction) {
    if (direction == "left") {
        return QObject::tr("Left");
    }
    if (direction == "right") {
        return QObject::tr("Right");
    }
    if (direction == "up") {
        return QObject::tr("Up");
    }
    if (direction == "down") {
        return QObject::tr("Down");
    }
    UNIMPLEMENTED_MSG("Unimplemented direction name={}", direction);
    return QString::fromStdString(direction);
}

void SetAnalogParam(const Common::ParamPackage& input_param, Common::ParamPackage& analog_param,
                    const std::string& button_name) {
    // The poller returned a complete axis, so set all the buttons
    if (input_param.Has("axis_x") && input_param.Has("axis_y")) {
        analog_param = input_param;
        return;
    }
    // Check if the current configuration has either no engine or an axis binding.
    // Clears out the old binding and adds one with analog_from_button.
    if (!analog_param.Has("engine") || analog_param.Has("axis_x") || analog_param.Has("axis_y")) {
        analog_param = {
            {"engine", "analog_from_button"},
        };
    }
    analog_param.Set(button_name, input_param.Serialize());
}
} // namespace

QString ConfigureInputPlayer::ButtonToText(const Common::ParamPackage& param) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    }

    const QString toggle = QString::fromStdString(param.Get("toggle", false) ? "~" : "");
    const QString inverted = QString::fromStdString(param.Get("inverted", false) ? "!" : "");
    const QString invert = QString::fromStdString(param.Get("invert", "+") == "-" ? "-" : "");
    const QString turbo = QString::fromStdString(param.Get("turbo", false) ? "$" : "");
    const auto common_button_name = input_subsystem->GetButtonName(param);

    // Retrieve the names from Qt
    if (param.Get("engine", "") == "keyboard") {
        const QString button_str = GetKeyName(param.Get("code", 0));
        return QObject::tr("%1%2%3%4").arg(turbo, toggle, inverted, button_str);
    }

    if (common_button_name == Common::Input::ButtonNames::Invalid) {
        return QObject::tr("[invalid]");
    }

    if (common_button_name == Common::Input::ButtonNames::Engine) {
        return QString::fromStdString(param.Get("engine", ""));
    }

    if (common_button_name == Common::Input::ButtonNames::Value) {
        if (param.Has("hat")) {
            const QString hat = GetDirectionName(param.Get("direction", ""));
            return QObject::tr("%1%2%3Hat %4").arg(turbo, toggle, inverted, hat);
        }
        if (param.Has("axis")) {
            const QString axis = QString::fromStdString(param.Get("axis", ""));
            return QObject::tr("%1%2%3Axis %4").arg(toggle, inverted, invert, axis);
        }
        if (param.Has("axis_x") && param.Has("axis_y") && param.Has("axis_z")) {
            const QString axis_x = QString::fromStdString(param.Get("axis_x", ""));
            const QString axis_y = QString::fromStdString(param.Get("axis_y", ""));
            const QString axis_z = QString::fromStdString(param.Get("axis_z", ""));
            return QObject::tr("%1%2Axis %3,%4,%5").arg(toggle, inverted, axis_x, axis_y, axis_z);
        }
        if (param.Has("motion")) {
            const QString motion = QString::fromStdString(param.Get("motion", ""));
            return QObject::tr("%1%2Motion %3").arg(toggle, inverted, motion);
        }
        if (param.Has("button")) {
            const QString button = QString::fromStdString(param.Get("button", ""));
            return QObject::tr("%1%2%3Button %4").arg(turbo, toggle, inverted, button);
        }
    }

    QString button_name = GetButtonName(common_button_name);
    if (param.Has("hat")) {
        return QObject::tr("%1%2%3Hat %4").arg(turbo, toggle, inverted, button_name);
    }
    if (param.Has("axis")) {
        return QObject::tr("%1%2%3Axis %4").arg(toggle, inverted, invert, button_name);
    }
    if (param.Has("motion")) {
        return QObject::tr("%1%2Axis %3").arg(toggle, inverted, button_name);
    }
    if (param.Has("button")) {
        return QObject::tr("%1%2%3Button %4").arg(turbo, toggle, inverted, button_name);
    }

    return QObject::tr("[unknown]");
}

QString ConfigureInputPlayer::AnalogToText(const Common::ParamPackage& param,
                                           const std::string& dir) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    }

    if (param.Get("engine", "") == "analog_from_button") {
        return ButtonToText(Common::ParamPackage{param.Get(dir, "")});
    }

    if (!param.Has("axis_x") || !param.Has("axis_y")) {
        return QObject::tr("[unknown]");
    }

    const auto engine_str = param.Get("engine", "");
    const QString axis_x_str = QString::fromStdString(param.Get("axis_x", ""));
    const QString axis_y_str = QString::fromStdString(param.Get("axis_y", ""));
    const bool invert_x = param.Get("invert_x", "+") == "-";
    const bool invert_y = param.Get("invert_y", "+") == "-";

    if (dir == "modifier") {
        return QObject::tr("[unused]");
    }

    if (dir == "left") {
        const QString invert_x_str = QString::fromStdString(invert_x ? "+" : "-");
        return QObject::tr("Axis %1%2").arg(axis_x_str, invert_x_str);
    }
    if (dir == "right") {
        const QString invert_x_str = QString::fromStdString(invert_x ? "-" : "+");
        return QObject::tr("Axis %1%2").arg(axis_x_str, invert_x_str);
    }
    if (dir == "up") {
        const QString invert_y_str = QString::fromStdString(invert_y ? "-" : "+");
        return QObject::tr("Axis %1%2").arg(axis_y_str, invert_y_str);
    }
    if (dir == "down") {
        const QString invert_y_str = QString::fromStdString(invert_y ? "+" : "-");
        return QObject::tr("Axis %1%2").arg(axis_y_str, invert_y_str);
    }

    return QObject::tr("[unknown]");
}

namespace {

/// The SDL controller (guid, port) a player's bindings point at
std::optional<std::pair<std::string, int>> SdlDevice(const Settings::PlayerInput& player) {
    for (const std::string& raw : player.buttons) {
        const Common::ParamPackage param{raw};
        if (!raw.empty() && param.Get("engine", "") == "sdl") {
            return std::pair<std::string, int>{param.Get("guid", ""), param.Get("port", 0)};
        }
    }
    return std::nullopt;
}

/// Points a player's SDL bindings at one controller, leaving other engines alone
void KeepSdlDevice(Settings::PlayerInput& player, const std::pair<std::string, int>& device) {
    const auto keep = [&device](auto& params) {
        for (std::string& raw : params) {
            Common::ParamPackage param{raw};
            if (raw.empty() || param.Get("engine", "") != "sdl") {
                continue;
            }
            if (param.Has("guid2") && param.Get("guid2", "") == param.Get("guid", "")) {
                param.Set("guid2", device.first);
            }
            param.Set("guid", device.first);
            param.Set("port", device.second);
            raw = param.Serialize();
        }
    };
    keep(player.buttons);
    keep(player.analogs);
    keep(player.motions);
}

} // Anonymous namespace

ConfigureInputPlayer::ConfigureInputPlayer(QWidget* parent, std::size_t player_index_,
                                           QWidget* bottom_row_,
                                           InputCommon::InputSubsystem* input_subsystem_,
                                           InputProfiles* profiles_, Core::HID::HIDCore& hid_core_,
                                           bool is_powered_on_, bool debug_)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureInputPlayer>()),
      player_index{player_index_}, debug{debug_}, is_powered_on{is_powered_on_},
      input_subsystem{input_subsystem_}, profiles(profiles_),
      timeout_timer(std::make_unique<QTimer>()), poll_timer(std::make_unique<QTimer>()),
      bottom_row{bottom_row_}, hid_core{hid_core_} {
    if (player_index == 0) {
        auto* emulated_controller_p1 =
            hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        auto* emulated_controller_handheld =
            hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        emulated_controller_p1->SaveCurrentConfig();
        emulated_controller_p1->EnableConfiguration();
        emulated_controller_handheld->SaveCurrentConfig();
        emulated_controller_handheld->EnableConfiguration();
        if (emulated_controller_handheld->IsConnected(true)) {
            emulated_controller_p1->Disconnect();
            emulated_controller = emulated_controller_handheld;
        } else {
            emulated_controller = emulated_controller_p1;
        }
    } else {
        emulated_controller = hid_core.GetEmulatedControllerByIndex(player_index);
        emulated_controller->SaveCurrentConfig();
        emulated_controller->EnableConfiguration();
    }
    ui->setupUi(this);

    setFocusPolicy(Qt::ClickFocus);

    button_map = {
        ui->buttonA,        ui->buttonB,       ui->buttonX,         ui->buttonY,
        ui->buttonLStick,   ui->buttonRStick,  ui->buttonL,         ui->buttonR,
        ui->buttonZL,       ui->buttonZR,      ui->buttonPlus,      ui->buttonMinus,
        ui->buttonDpadLeft, ui->buttonDpadUp,  ui->buttonDpadRight, ui->buttonDpadDown,
        ui->buttonSLLeft,   ui->buttonSRLeft,  ui->buttonHome,      ui->buttonScreenshot,
        ui->buttonSLRight,  ui->buttonSRRight,
    };

    analog_map_buttons = {{
        {
            ui->buttonLStickUp,
            ui->buttonLStickDown,
            ui->buttonLStickLeft,
            ui->buttonLStickRight,
        },
        {
            ui->buttonRStickUp,
            ui->buttonRStickDown,
            ui->buttonRStickLeft,
            ui->buttonRStickRight,
        },
    }};

    motion_map = {
        ui->buttonMotionLeft,
        ui->buttonMotionRight,
    };

    analog_map_deadzone_label = {ui->labelLStickDeadzone, ui->labelRStickDeadzone};
    analog_map_deadzone_slider = {ui->sliderLStickDeadzone, ui->sliderRStickDeadzone};
    analog_map_modifier_groupbox = {ui->buttonLStickModGroup, ui->buttonRStickModGroup};
    analog_map_modifier_button = {ui->buttonLStickMod, ui->buttonRStickMod};
    analog_map_modifier_label = {ui->labelLStickModifierRange, ui->labelRStickModifierRange};
    analog_map_modifier_slider = {ui->sliderLStickModifierRange, ui->sliderRStickModifierRange};
    analog_map_range_groupbox = {ui->buttonLStickRangeGroup, ui->buttonRStickRangeGroup};
    analog_map_range_spinbox = {ui->spinboxLStickRange, ui->spinboxRStickRange};

    ui->controllerFrame->SetController(emulated_controller);

    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        auto* const button = button_map[button_id];

        if (button == nullptr) {
            continue;
        }

        connect(button, &QPushButton::clicked, [=, this] {
            HandleClick(
                button, button_id,
                [=, this](const Common::ParamPackage& params) {
                    emulated_controller->SetButtonParam(button_id, params);
                },
                InputCommon::Polling::InputType::Button);
        });

        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QPushButton::customContextMenuRequested,
                [=, this](const QPoint& menu_location) {
                    QMenu context_menu;
                    Common::ParamPackage param = emulated_controller->GetButtonParam(button_id);
                    context_menu.addAction(tr("Clear"), [&] {
                        emulated_controller->SetButtonParam(button_id, {});
                        button_map[button_id]->setText(tr("[not set]"));
                    });
                    if (param.Has("code") || param.Has("button") || param.Has("hat")) {
                        context_menu.addAction(tr("Invert button"), [&] {
                            const bool invert_value = !param.Get("inverted", false);
                            param.Set("inverted", invert_value);
                            button_map[button_id]->setText(ButtonToText(param));
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                        context_menu.addAction(tr("Toggle button"), [&] {
                            const bool toggle_value = !param.Get("toggle", false);
                            param.Set("toggle", toggle_value);
                            button_map[button_id]->setText(ButtonToText(param));
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                        context_menu.addAction(tr("Turbo button"), [&] {
                            const bool turbo_value = !param.Get("turbo", false);
                            param.Set("turbo", turbo_value);
                            button_map[button_id]->setText(ButtonToText(param));
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                    }
                    if (param.Has("axis")) {
                        context_menu.addAction(tr("Invert axis"), [&] {
                            const bool toggle_value = !(param.Get("invert", "+") == "-");
                            param.Set("invert", toggle_value ? "-" : "+");
                            button_map[button_id]->setText(ButtonToText(param));
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                        context_menu.addAction(tr("Invert button"), [&] {
                            const bool invert_value = !param.Get("inverted", false);
                            param.Set("inverted", invert_value);
                            button_map[button_id]->setText(ButtonToText(param));
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                        context_menu.addAction(tr("Set threshold"), [&] {
                            const int button_threshold =
                                static_cast<int>(param.Get("threshold", 0.5f) * 100.0f);
                            const int new_threshold = QInputDialog::getInt(
                                this, tr("Set threshold"), tr("Choose a value between 0% and 100%"),
                                button_threshold, 0, 100);
                            param.Set("threshold", new_threshold / 100.0f);

                            if (button_id == Settings::NativeButton::ZL) {
                                ui->sliderZLThreshold->setValue(new_threshold);
                            }
                            if (button_id == Settings::NativeButton::ZR) {
                                ui->sliderZRThreshold->setValue(new_threshold);
                            }
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                        context_menu.addAction(tr("Toggle axis"), [&] {
                            const bool toggle_value = !param.Get("toggle", false);
                            param.Set("toggle", toggle_value);
                            button_map[button_id]->setText(ButtonToText(param));
                            emulated_controller->SetButtonParam(button_id, param);
                        });
                    }
                    context_menu.exec(button_map[button_id]->mapToGlobal(menu_location));
                });
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        auto* const button = motion_map[motion_id];
        if (button == nullptr) {
            continue;
        }

        connect(button, &QPushButton::clicked, [=, this] {
            HandleClick(
                button, motion_id,
                [=, this](const Common::ParamPackage& params) {
                    emulated_controller->SetMotionParam(motion_id, params);
                },
                InputCommon::Polling::InputType::Motion);
        });

        button->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(button, &QPushButton::customContextMenuRequested,
                [=, this](const QPoint& menu_location) {
                    QMenu context_menu;
                    Common::ParamPackage param = emulated_controller->GetMotionParam(motion_id);
                    context_menu.addAction(tr("Clear"), [&] {
                        emulated_controller->SetMotionParam(motion_id, {});
                        motion_map[motion_id]->setText(tr("[not set]"));
                    });
                    if (param.Has("motion")) {
                        context_menu.addAction(tr("Set gyro threshold"), [&] {
                            const int gyro_threshold =
                                static_cast<int>(param.Get("threshold", 0.007f) * 1000.0f);
                            const int new_threshold = QInputDialog::getInt(
                                this, tr("Set threshold"), tr("Choose a value between 0% and 100%"),
                                gyro_threshold, 0, 100);
                            param.Set("threshold", new_threshold / 1000.0f);
                            emulated_controller->SetMotionParam(motion_id, param);
                        });
                        context_menu.addAction(tr("Calibrate sensor"), [&] {
                            emulated_controller->StartMotionCalibration();
                        });
                    }
                    context_menu.exec(motion_map[motion_id]->mapToGlobal(menu_location));
                });
    }

    connect(ui->sliderZLThreshold, &QSlider::valueChanged, [=, this] {
        Common::ParamPackage param =
            emulated_controller->GetButtonParam(Settings::NativeButton::ZL);
        if (param.Has("threshold")) {
            const auto slider_value = ui->sliderZLThreshold->value();
            param.Set("threshold", slider_value / 100.0f);
            emulated_controller->SetButtonParam(Settings::NativeButton::ZL, param);
        }
    });

    connect(ui->sliderZRThreshold, &QSlider::valueChanged, [=, this] {
        Common::ParamPackage param =
            emulated_controller->GetButtonParam(Settings::NativeButton::ZR);
        if (param.Has("threshold")) {
            const auto slider_value = ui->sliderZRThreshold->value();
            param.Set("threshold", slider_value / 100.0f);
            emulated_controller->SetButtonParam(Settings::NativeButton::ZR, param);
        }
    });

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];

            if (analog_button == nullptr) {
                continue;
            }

            connect(analog_button, &QPushButton::clicked, [=, this] {
                if (!map_analog_stick_accepted) {
                    map_analog_stick_accepted =
                        QMessageBox::information(
                            this, tr("Map Analog Stick"),
                            tr("After pressing OK, first move your joystick horizontally, and then "
                               "vertically.\nTo invert the axes, first move your joystick "
                               "vertically, and then horizontally."),
                            QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok;
                    if (!map_analog_stick_accepted) {
                        return;
                    }
                }
                HandleClick(
                    analog_map_buttons[analog_id][sub_button_id], analog_id,
                    [=, this](const Common::ParamPackage& params) {
                        Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
                        SetAnalogParam(params, param, analog_sub_buttons[sub_button_id]);
                        // Correct axis direction for inverted sticks
                        if (input_subsystem->IsStickInverted(param)) {
                            switch (analog_id) {
                            case Settings::NativeAnalog::LStick: {
                                const bool invert_value = param.Get("invert_x", "+") == "-";
                                const std::string invert_str = invert_value ? "+" : "-";
                                param.Set("invert_x", invert_str);
                                break;
                            }
                            case Settings::NativeAnalog::RStick: {
                                const bool invert_value = param.Get("invert_y", "+") == "-";
                                const std::string invert_str = invert_value ? "+" : "-";
                                param.Set("invert_y", invert_str);
                                break;
                            }
                            default:
                                break;
                            }
                        }
                        emulated_controller->SetStickParam(analog_id, param);
                    },
                    InputCommon::Polling::InputType::Stick);
            });

            analog_button->setContextMenuPolicy(Qt::CustomContextMenu);

            connect(analog_button, &QPushButton::customContextMenuRequested,
                    [=, this](const QPoint& menu_location) {
                        QMenu context_menu;
                        Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
                        context_menu.addAction(tr("Clear"), [&] {
                            if (param.Get("engine", "") != "analog_from_button") {
                                emulated_controller->SetStickParam(analog_id, {});
                                for (auto button : analog_map_buttons[analog_id]) {
                                    button->setText(tr("[not set]"));
                                }
                                return;
                            }
                            switch (sub_button_id) {
                            case 0:
                                param.Erase("up");
                                break;
                            case 1:
                                param.Erase("down");
                                break;
                            case 2:
                                param.Erase("left");
                                break;
                            case 3:
                                param.Erase("right");
                                break;
                            }
                            emulated_controller->SetStickParam(analog_id, param);
                            analog_map_buttons[analog_id][sub_button_id]->setText(tr("[not set]"));
                        });
                        context_menu.addAction(tr("Center axis"), [&] {
                            const auto stick_value =
                                emulated_controller->GetSticksValues()[analog_id];
                            const float offset_x = stick_value.x.properties.offset;
                            const float offset_y = stick_value.y.properties.offset;
                            float raw_value_x = stick_value.x.raw_value;
                            float raw_value_y = stick_value.y.raw_value;
                            // See Core::HID::SanitizeStick() to obtain the original raw axis value
                            if (std::abs(offset_x) < 0.5f) {
                                if (raw_value_x > 0) {
                                    raw_value_x *= 1 + offset_x;
                                } else {
                                    raw_value_x *= 1 - offset_x;
                                }
                            }
                            if (std::abs(offset_x) < 0.5f) {
                                if (raw_value_y > 0) {
                                    raw_value_y *= 1 + offset_y;
                                } else {
                                    raw_value_y *= 1 - offset_y;
                                }
                            }
                            param.Set("offset_x", -raw_value_x + offset_x);
                            param.Set("offset_y", -raw_value_y + offset_y);
                            emulated_controller->SetStickParam(analog_id, param);
                        });
                        context_menu.addAction(tr("Invert axis"), [&] {
                            if (sub_button_id == 2 || sub_button_id == 3) {
                                const bool invert_value = param.Get("invert_x", "+") == "-";
                                const std::string invert_str = invert_value ? "+" : "-";
                                param.Set("invert_x", invert_str);
                                emulated_controller->SetStickParam(analog_id, param);
                            }
                            if (sub_button_id == 0 || sub_button_id == 1) {
                                const bool invert_value = param.Get("invert_y", "+") == "-";
                                const std::string invert_str = invert_value ? "+" : "-";
                                param.Set("invert_y", invert_str);
                                emulated_controller->SetStickParam(analog_id, param);
                            }
                            for (int analog_sub_button_id = 0;
                                 analog_sub_button_id < ANALOG_SUB_BUTTONS_NUM;
                                 ++analog_sub_button_id) {
                                analog_map_buttons[analog_id][analog_sub_button_id]->setText(
                                    AnalogToText(param, analog_sub_buttons[analog_sub_button_id]));
                            }
                        });
                        context_menu.exec(analog_map_buttons[analog_id][sub_button_id]->mapToGlobal(
                            menu_location));
                    });
        }

        // Handle clicks for the modifier buttons as well.
        connect(analog_map_modifier_button[analog_id], &QPushButton::clicked, [=, this] {
            HandleClick(
                analog_map_modifier_button[analog_id], analog_id,
                [=, this](const Common::ParamPackage& params) {
                    Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
                    param.Set("modifier", params.Serialize());
                    emulated_controller->SetStickParam(analog_id, param);
                },
                InputCommon::Polling::InputType::Button);
        });

        analog_map_modifier_button[analog_id]->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(
            analog_map_modifier_button[analog_id], &QPushButton::customContextMenuRequested,
            [=, this](const QPoint& menu_location) {
                QMenu context_menu;
                Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
                context_menu.addAction(tr("Clear"), [&] {
                    param.Set("modifier", "");
                    analog_map_modifier_button[analog_id]->setText(tr("[not set]"));
                    emulated_controller->SetStickParam(analog_id, param);
                });
                context_menu.addAction(tr("Toggle button"), [&] {
                    Common::ParamPackage modifier_param =
                        Common::ParamPackage{param.Get("modifier", "")};
                    const bool toggle_value = !modifier_param.Get("toggle", false);
                    modifier_param.Set("toggle", toggle_value);
                    param.Set("modifier", modifier_param.Serialize());
                    analog_map_modifier_button[analog_id]->setText(ButtonToText(modifier_param));
                    emulated_controller->SetStickParam(analog_id, param);
                });
                context_menu.addAction(tr("Invert button"), [&] {
                    Common::ParamPackage modifier_param =
                        Common::ParamPackage{param.Get("modifier", "")};
                    const bool invert_value = !modifier_param.Get("inverted", false);
                    modifier_param.Set("inverted", invert_value);
                    param.Set("modifier", modifier_param.Serialize());
                    analog_map_modifier_button[analog_id]->setText(ButtonToText(modifier_param));
                    emulated_controller->SetStickParam(analog_id, param);
                });
                context_menu.exec(
                    analog_map_modifier_button[analog_id]->mapToGlobal(menu_location));
            });

        connect(analog_map_range_spinbox[analog_id], qOverload<int>(&QSpinBox::valueChanged),
                [=, this] {
                    Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
                    const auto spinbox_value = analog_map_range_spinbox[analog_id]->value();
                    param.Set("range", spinbox_value / 100.0f);
                    emulated_controller->SetStickParam(analog_id, param);
                });

        connect(analog_map_deadzone_slider[analog_id], &QSlider::valueChanged, [=, this] {
            Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
            const auto slider_value = analog_map_deadzone_slider[analog_id]->value();
            analog_map_deadzone_label[analog_id]->setText(tr("Deadzone: %1%").arg(slider_value));
            param.Set("deadzone", slider_value / 100.0f);
            emulated_controller->SetStickParam(analog_id, param);
        });

        connect(analog_map_modifier_slider[analog_id], &QSlider::valueChanged, [=, this] {
            Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
            const auto slider_value = analog_map_modifier_slider[analog_id]->value();
            analog_map_modifier_label[analog_id]->setText(
                tr("Modifier Range: %1%").arg(slider_value));
            param.Set("modifier_scale", slider_value / 100.0f);
            emulated_controller->SetStickParam(analog_id, param);
        });
    }

    if (player_index_ == 0) {
        connect(ui->mousePanningButton, &QPushButton::clicked, [this, input_subsystem_] {
            const auto right_stick_param =
                emulated_controller->GetStickParam(Settings::NativeAnalog::RStick);
            ConfigureMousePanning dialog(this, input_subsystem_,
                                         right_stick_param.Get("deadzone", 0.0f),
                                         right_stick_param.Get("range", 1.0f));
            if (dialog.exec() == QDialog::Accepted) {
                dialog.ApplyConfiguration();
            }
        });
    } else {
        ui->mousePanningWidget->hide();
    }

    // Player Connected checkbox
    connect(ui->groupConnectedController, &QGroupBox::toggled,
            [this](bool checked) { emit Connected(checked); });

    if (player_index == 0) {
        connect(ui->comboControllerType, qOverload<int>(&QComboBox::currentIndexChanged),
                [this](int index) {
                    emit HandheldStateChanged(GetControllerTypeFromIndex(index) ==
                                              Core::HID::NpadStyleIndex::Handheld);
                });
    }

    if (debug || player_index == 9) {
        ui->groupConnectedController->setCheckable(false);
    }

    // The Debug Controller can only choose the Pro Controller.
    if (debug) {
        ui->buttonScreenshot->setEnabled(false);
        ui->buttonHome->setEnabled(false);
        ui->comboControllerType->addItem(tr("Pro Controller"));
    } else {
        SetConnectableControllers();
    }

    UpdateControllerAvailableButtons();
    UpdateControllerEnabledButtons();
    UpdateControllerButtonNames();
    UpdateMotionButtons();
    connect(ui->comboControllerType, qOverload<int>(&QComboBox::currentIndexChanged), [this](int) {
        UpdateControllerAvailableButtons();
        UpdateControllerEnabledButtons();
        UpdateControllerButtonNames();
        UpdateMotionButtons();
        const Core::HID::NpadStyleIndex type =
            GetControllerTypeFromIndex(ui->comboControllerType->currentIndex());

        if (player_index == 0) {
            auto* emulated_controller_p1 =
                hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
            auto* emulated_controller_handheld =
                hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
            bool is_connected = emulated_controller->IsConnected(true);

            emulated_controller_p1->SetNpadStyleIndex(type);
            emulated_controller_handheld->SetNpadStyleIndex(type);
            if (is_connected) {
                if (type == Core::HID::NpadStyleIndex::Handheld) {
                    emulated_controller_p1->Disconnect();
                    emulated_controller_handheld->Connect(true);
                    emulated_controller = emulated_controller_handheld;
                } else {
                    emulated_controller_handheld->Disconnect();
                    emulated_controller_p1->Connect(true);
                    emulated_controller = emulated_controller_p1;
                }
            }
            ui->controllerFrame->SetController(emulated_controller);
        }
        emulated_controller->SetNpadStyleIndex(type);
    });

    connect(ui->comboDevices, qOverload<int>(&QComboBox::activated), this,
            &ConfigureInputPlayer::UpdateMappingWithDefaults);
    ui->comboDevices->installEventFilter(this);

    ui->comboDevices->setCurrentIndex(-1);

    timeout_timer->setSingleShot(true);
    connect(timeout_timer.get(), &QTimer::timeout, [this] { SetPollingResult({}, true); });

    connect(poll_timer.get(), &QTimer::timeout, [this] {
        const auto& params = input_subsystem->GetNextInput();
        if (params.Has("engine") && IsInputAcceptable(params)) {
            SetPollingResult(params, false);
            return;
        }
    });

    UpdateInputProfiles();

    connect(ui->buttonProfilesNew, &QPushButton::clicked, this,
            &ConfigureInputPlayer::CreateProfile);
    connect(ui->buttonProfilesDelete, &QPushButton::clicked, this,
            &ConfigureInputPlayer::DeleteProfile);
    connect(ui->comboProfiles, qOverload<int>(&QComboBox::activated), this,
            &ConfigureInputPlayer::LoadProfile);
    connect(ui->buttonProfilesSave, &QPushButton::clicked, this,
            &ConfigureInputPlayer::SaveProfile);

    LoadConfiguration();
}

ConfigureInputPlayer::~ConfigureInputPlayer() {
    if (player_index == 0) {
        auto* emulated_controller_p1 =
            hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        auto* emulated_controller_handheld =
            hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        emulated_controller_p1->DisableConfiguration();
        emulated_controller_handheld->DisableConfiguration();
    } else {
        emulated_controller->DisableConfiguration();
    }
}

void ConfigureInputPlayer::ApplyConfiguration() {
    if (player_index == 0) {
        auto* emulated_controller_p1 =
            hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        auto* emulated_controller_handheld =
            hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        emulated_controller_p1->DisableConfiguration();
        emulated_controller_p1->SaveCurrentConfig();
        emulated_controller_p1->EnableConfiguration();
        emulated_controller_handheld->DisableConfiguration();
        emulated_controller_handheld->SaveCurrentConfig();
        emulated_controller_handheld->EnableConfiguration();
        return;
    }
    emulated_controller->DisableConfiguration();
    emulated_controller->SaveCurrentConfig();
    emulated_controller->EnableConfiguration();
}

void ConfigureInputPlayer::showEvent(QShowEvent* event) {
    if (bottom_row == nullptr) {
        return;
    }
    QWidget::showEvent(event);
    ui->main->addWidget(bottom_row);
}

void ConfigureInputPlayer::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureInputPlayer::RetranslateUI() {
    ui->retranslateUi(this);
    UpdateUI();
}

void ConfigureInputPlayer::LoadConfiguration() {
    emulated_controller->ReloadFromSettings();

    UpdateUI();
    UpdateInputDeviceCombobox();

    if (debug) {
        return;
    }

    const int comboBoxIndex =
        GetIndexFromControllerType(emulated_controller->GetNpadStyleIndex(true));
    ui->comboControllerType->setCurrentIndex(comboBoxIndex);
    ui->groupConnectedController->setChecked(emulated_controller->IsConnected(true));
}

void ConfigureInputPlayer::ConnectPlayer(bool connected) {
    ui->groupConnectedController->setChecked(connected);
    if (connected) {
        emulated_controller->Connect(true);
    } else {
        emulated_controller->Disconnect();
    }
}

void ConfigureInputPlayer::RefreshFromController() {
    UpdateUI();
    UpdateInputDeviceCombobox();
    ApplyJoyConLock();
}

void ConfigureInputPlayer::ApplyJoyConLock() {
    int held = 0;
    {
        auto& state = Common::GetControllerConsoleState();
        std::scoped_lock lock{state.mutex};
        if (state.engine_active && player_index < state.joycon_held.size()) {
            held = state.joycon_held[player_index];
        }
    }
    const bool locked = held != 0;
    const bool changed = locked != joycon_locked;
    joycon_locked = locked;
    for (auto* button : button_map) {
        if (button) {
            button->setEnabled(!locked);
        }
    }
    for (std::size_t i = 0; i < analog_map_buttons.size(); ++i) {
        for (auto* button : analog_map_buttons[i]) {
            if (button) {
                button->setEnabled(!locked);
            }
        }
        for (QWidget* widget : std::initializer_list<QWidget*>{
                 analog_map_modifier_button[i], analog_map_deadzone_slider[i],
                 analog_map_modifier_slider[i], analog_map_range_spinbox[i]}) {
            if (widget) {
                widget->setEnabled(!locked);
            }
        }
    }
    // (Saving would keep the Joy-Con's arrangement as the player's own mapping)
    if (card_save) {
        card_save->setEnabled(!locked);
        card_save_new->setEnabled(!locked);
    }
    if (joycon_note) {
        joycon_note->setVisible(locked);
        if (locked) {
            joycon_note->setText(
                (held == 1 ? tr("P%1 plays as a sideways Joy-Con for this game")
                           : tr("P%1 plays as an upright Joy-Con for this game"))
                    .arg(player_index + 1) +
                tr(": its controls are set automatically. Pick a controller type to set them "
                   "yourself."));
        }
    }
    if (card_status) {
        card_status->setVisible(!locked);
    }
    if (changed && !locked && profile_card && profile_card->isVisible()) {
        UpdateProfileCard();
    }
}

void ConfigureInputPlayer::SetConnectionLocked(bool locked) {
    const bool changed = connection_locked != locked || !style_applied;
    connection_locked = locked;
    style_applied = true;
    // The automatic modes draw the tab like the Controllers tab, keeping the spacing (the page
    // fills the Deck's screen exactly)
    if (changed) {
        setStyleSheet(
            locked ? ConfigurePlayerAssignment::Themed(QStringLiteral(
                         "QLabel { background: transparent; }"
                         "QGroupBox { border: 1px solid palette(mid); border-radius: 8px; "
                         "margin-top: 1.1em; padding-bottom: 5px; "
                         "background: palette(alternate-base); }"
                         "QGroupBox::title { subcontrol-origin: margin; "
                         "subcontrol-position: top center; padding: 0 3px; }"
                         "QGroupBox QGroupBox { border: none; background: transparent; "
                         "padding-bottom: 0px; }"
                         "QGroupBox QGroupBox::title { padding: 0px; }"
                         // The single-button boxes carry their label inside, above the button
                         "QGroupBox#buttonShoulderButtonsButtonZLGroup, "
                         "QGroupBox#buttonShoulderButtonsButtonLGroup, "
                         "QGroupBox#buttonMiscButtonsMinusGroup, "
                         "QGroupBox#buttonMiscButtonsPlusGroup, "
                         "QGroupBox#buttonShoulderButtonsZRGroup, "
                         "QGroupBox#buttonShoulderButtonsRGroup, "
                         "QGroupBox#buttonMiscButtonsHomeGroup, "
                         "QGroupBox#buttonMiscButtonsScreenshotGroup { margin-top: 0px; "
                         "padding-top: 1.3em; }"
                         // The top row starts level with the section cards beside it
                         "QGroupBox#buttonShoulderButtonsButtonZLGroup, "
                         "QGroupBox#buttonShoulderButtonsButtonLGroup, "
                         "QGroupBox#buttonMiscButtonsMinusGroup, "
                         "QGroupBox#buttonMiscButtonsPlusGroup, "
                         "QGroupBox#buttonShoulderButtonsZRGroup, "
                         "QGroupBox#buttonShoulderButtonsRGroup { margin-top: 1.1em; }"
                         "QGroupBox#buttonShoulderButtonsButtonZLGroup::title, "
                         "QGroupBox#buttonShoulderButtonsButtonLGroup::title, "
                         "QGroupBox#buttonMiscButtonsMinusGroup::title, "
                         "QGroupBox#buttonMiscButtonsPlusGroup::title, "
                         "QGroupBox#buttonShoulderButtonsZRGroup::title, "
                         "QGroupBox#buttonShoulderButtonsRGroup::title, "
                         "QGroupBox#buttonMiscButtonsHomeGroup::title, "
                         "QGroupBox#buttonMiscButtonsScreenshotGroup::title { "
                         "subcontrol-origin: padding; subcontrol-position: top center; "
                         "top: 3px; padding: 0px; }"
                         "QPushButton { border: 1px solid palette(mid); border-radius: 6px; "
                         "padding: 2px 6px; background: palette(base); }"
                         "QPushButton:hover { border-color: palette(highlight); }"
                         "QPushButton:pressed, QPushButton:checked { "
                         "background: palette(highlight); color: palette(highlighted-text); }")) +
                         ConfigurePlayerAssignment::ModernComboStyle()
                   : QString{});
        if (!locked) {
            // The manual modes get the tab exactly as it was
            if (home_centred) {
                home_centred = false;
                ui->miscButtons->setStretch(0, 0);
                delete ui->miscButtons->takeAt(ui->miscButtons->count() - 1);
            }
            if (shoulders_raised) {
                shoulders_raised = false;
                for (QPushButton* button :
                     {ui->buttonL, ui->buttonR, ui->buttonMinus, ui->buttonPlus}) {
                    if (auto* box = qobject_cast<QBoxLayout*>(
                            button->parentWidget() ? button->parentWidget()->layout() : nullptr)) {
                        delete box->takeAt(box->count() - 1);
                    }
                }
            }
            if (QLayout* connect = ui->groupConnectedController->layout()) {
                connect->setAlignment(ui->comboControllerType, Qt::Alignment{});
            }
            // Back to the inherited fonts
            for (QGroupBox* box : bolded_boxes) {
                if (box) {
                    box->setFont(QFont());
                    for (QWidget* child : box->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                        child->setFont(QFont());
                    }
                }
            }
            bolded_boxes.clear();
        }
        if (locked) {
            ConfigurePlayerAssignment::ModernizeCombos(this);
            // Bold section titles (a style sheet can't bold only a title)
            QFont normal = font();
            normal.setBold(false); // explicit, or the box's bold is inherited after all
            QFont bold = normal;
            bold.setBold(true);
            for (QGroupBox* box : findChildren<QGroupBox*>()) {
                // No box around a section
                bool inside_box = false;
                for (QWidget* up = box->parentWidget(); up && up != this; up = up->parentWidget()) {
                    inside_box = inside_box || qobject_cast<QGroupBox*>(up) != nullptr;
                }
                const bool section =
                    !inside_box && !box->objectName().startsWith(QStringLiteral("buttonMisc")) &&
                    !box->objectName().startsWith(QStringLiteral("buttonShoulderButtons"));
                if (!section) {
                    continue;
                }
                box->setFont(bold);
                for (QWidget* child : box->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                    child->setFont(normal);
                }
                bolded_boxes.push_back(box);
            }
            // Home centred under the controller
            if (!home_centred) {
                home_centred = true;
                ui->miscButtons->setStretch(0, 1);
                ui->miscButtons->addStretch(1);
            }
            // Connect Controller's drop-down level with the profile card's
            if (QLayout* connect = ui->groupConnectedController->layout()) {
                connect->setAlignment(ui->comboControllerType, Qt::AlignTop);
            }
            // L, R, Minus and Plus line up with ZL and ZR: a fixed space the size of ZL's
            // trigger slider under each
            if (!shoulders_raised) {
                shoulders_raised = true;
                const int slider = ui->sliderZLThreshold->sizeHint().height();
                for (QPushButton* button :
                     {ui->buttonL, ui->buttonR, ui->buttonMinus, ui->buttonPlus}) {
                    if (auto* box = qobject_cast<QBoxLayout*>(
                            button->parentWidget() ? button->parentWidget()->layout() : nullptr)) {
                        box->addSpacerItem(new QSpacerItem(
                            0, slider + box->spacing(), QSizePolicy::Minimum, QSizePolicy::Fixed));
                    }
                }
            }
        }
    }
    if (!profile_card && locked) {
        BuildProfileCard();
    }
    ui->profilesGroup->setVisible(!locked);
    if (profile_card) {
        profile_card->setVisible(locked);
        if (locked) {
            card_timer->start();
            UpdateProfileCard();
        } else {
            card_timer->stop();
        }
    }
    ApplyJoyConLock();
    ui->devicesGroup->setVisible(!locked);
    // The automatic modes bind motion themselves (the controller's own, or its real device's)
    ui->buttonMotionLeftGroup->setVisible(!locked);
    ui->buttonMotionRightGroup->setVisible(!locked);
    if (ui->groupConnectedController->isCheckable() == !locked) {
        return;
    }
    const bool blocked = ui->groupConnectedController->blockSignals(true);
    ui->groupConnectedController->setCheckable(!locked);
    if (!locked) {
        ui->groupConnectedController->setChecked(emulated_controller->IsConnected(true));
    }
    ui->groupConnectedController->blockSignals(blocked);
}

namespace {

/// A binding without its controller (guid, port), its fields in a fixed order
std::string MappingKey(const std::string& raw) {
    if (raw.empty()) {
        return {};
    }
    Common::ParamPackage param{raw};
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
        fields.push_back(field);
        field.clear();
    }
    std::sort(fields.begin(), fields.end());
    std::string out;
    for (const auto& f : fields) {
        out += f + ';';
    }
    return out;
}

/// Asks the engine to put a profile on a player's controller ("" drops it): the engine owns the
/// mappings in the automatic modes
void RequestProfile(std::size_t player, const QString& name, bool restore = false) {
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.profile_requests.push_back({static_cast<int>(player), name.toStdString(), restore});
}

/// The box layout that holds a widget, searched from a top layout down
QBoxLayout* LayoutHolding(QLayout* layout, QWidget* widget) {
    if (!layout) {
        return nullptr;
    }
    if (layout->indexOf(widget) >= 0) {
        return qobject_cast<QBoxLayout*>(layout);
    }
    for (int i = 0; i < layout->count(); ++i) {
        if (auto* found = LayoutHolding(layout->itemAt(i)->layout(), widget)) {
            return found;
        }
    }
    return nullptr;
}

} // Anonymous namespace

void ConfigureInputPlayer::BuildProfileCard() {
    profile_card = new QGroupBox(tr("Mapping Profile"), this);
    auto* column = new QVBoxLayout(profile_card);
    // Connect Controller's margins, so the two drop-downs are level
    if (const QLayout* connect = ui->groupConnectedController->layout()) {
        column->setContentsMargins(connect->contentsMargins());
    }
    auto* row = new QHBoxLayout;
    card_profiles = new QComboBox(profile_card);
    card_profiles->setPlaceholderText(tr("No profile"));
    ConfigurePlayerAssignment::ModernizeCombos(profile_card);
    // Fills the row up to its buttons, which end at the card's edge
    card_profiles->setMinimumWidth(160);
    card_profiles->setToolTip(tr("Picking a profile applies it to this player at once."));
    card_save = new QPushButton(tr("Save"), profile_card);
    card_save->setDefault(false);
    card_save->setAutoDefault(false);
    // The profile's actions, all in view beside Save (Defaults, below the drawing, resets)
    const auto action = [this](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text, profile_card);
        button->setAutoDefault(false);
        button->setToolTip(tip);
        return button;
    };
    card_save_new = action(tr("Save as New"), tr("Keep this mapping as a new profile"));
    card_rename = action(tr("Rename"), tr("Rename the selected profile"));
    card_duplicate = action(tr("Duplicate"), tr("Copy the selected profile under a new name"));
    card_delete = action(tr("Delete"), tr("Delete the selected profile; this player keeps its "
                                          "current mapping"));
    // (Inside the drop-down) takes the profile off: the mappings from before it come back
    card_remove = ConfigurePlayerAssignment::AddClearButton(card_profiles);
    card_remove->setToolTip(tr("Remove the profile from this player"));
    row->addWidget(card_profiles, 1);
    row->addWidget(card_save);
    row->addWidget(card_save_new);
    row->addWidget(card_rename);
    row->addWidget(card_duplicate);
    row->addWidget(card_delete);
    column->addLayout(row);
    card_status = new QLabel(profile_card);
    card_status->setWordWrap(true);
    column->addWidget(card_status);
    joycon_note = new QLabel(profile_card);
    joycon_note->setWordWrap(true);
    joycon_note->setVisible(false);
    column->addWidget(joycon_note);
    // No taller than its rows: the drawing below gets the height
    profile_card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    connect(card_save_new, &QPushButton::clicked, this, [this] {
        const int count = ui->comboProfiles->count();
        CreateProfile();
        if (ui->comboProfiles->count() > count) {
            RequestProfile(player_index, ui->comboProfiles->currentText());
        }
        RememberProfileMapping();
        UpdateProfileCard();
    });
    connect(card_rename, &QPushButton::clicked, this, &ConfigureInputPlayer::RenameProfile);
    connect(card_duplicate, &QPushButton::clicked, this, &ConfigureInputPlayer::DuplicateProfile);
    connect(card_delete, &QPushButton::clicked, this, &ConfigureInputPlayer::DeleteProfileAsked);

    // The engine owns the mappings here: picking or saving a profile goes through it
    connect(card_profiles, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        ui->comboProfiles->setCurrentIndex(index);
        RequestProfile(player_index, ui->comboProfiles->currentText());
        RememberProfileMapping();
        UpdateProfileCard();
    });
    connect(card_remove, &QToolButton::clicked, this, [this] {
        RequestProfile(player_index, {}, true);
        ui->comboProfiles->setCurrentIndex(-1);
        RememberProfileMapping();
        UpdateProfileCard();
    });
    connect(card_save, &QPushButton::clicked, this, [this] {
        if (ui->comboProfiles->currentIndex() < 0) {
            CreateProfile();
        } else {
            SaveProfile();
        }
        // (A new profile left unnamed leaves nothing selected)
        if (ui->comboProfiles->currentIndex() >= 0) {
            RequestProfile(player_index, ui->comboProfiles->currentText());
        }
        RememberProfileMapping();
        UpdateProfileCard();
    });

    // Mapping edits come from many places (polling, menus, defaults): look again now and then
    card_timer = new QTimer(this);
    card_timer->setInterval(500);
    connect(card_timer, &QTimer::timeout, this, &ConfigureInputPlayer::UpdateProfileCard);

    if (auto* layout = LayoutHolding(this->layout(), ui->profilesGroup)) {
        layout->insertWidget(layout->indexOf(ui->profilesGroup) + 1, profile_card, 1);
    }
    // A bold title like the other sections, with its contents in the normal font
    QFont normal = font();
    normal.setBold(false); // explicit, or the card's bold is inherited after all
    QFont bold = normal;
    bold.setBold(true);
    profile_card->setFont(bold);
    for (QWidget* child : profile_card->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        child->setFont(normal);
    }
    profile_card->setVisible(false);
}

void ConfigureInputPlayer::UpdateProfileCard() {
    if (!profile_card || !profile_card->isVisible()) {
        return;
    }
    // Mirror the profile list the rest of the tab keeps in comboProfiles
    bool same = card_profiles->count() == ui->comboProfiles->count();
    for (int i = 0; same && i < card_profiles->count(); ++i) {
        same = card_profiles->itemText(i) == ui->comboProfiles->itemText(i);
    }
    if (!same || card_profiles->currentIndex() != ui->comboProfiles->currentIndex()) {
        const bool blocked = card_profiles->blockSignals(true);
        card_profiles->clear();
        for (int i = 0; i < ui->comboProfiles->count(); ++i) {
            card_profiles->addItem(ui->comboProfiles->itemText(i));
        }
        card_profiles->setCurrentIndex(ui->comboProfiles->currentIndex());
        card_profiles->blockSignals(blocked);
    }
    // The engine changes the player's profile too (a swap, the Controllers tab): follow it
    const QString carried =
        QString::fromStdString(Settings::values.players.GetValue()[player_index].profile_name);
    if (carried != card_engine_profile) {
        card_engine_profile = carried;
        const bool blocked = ui->comboProfiles->blockSignals(true);
        ui->comboProfiles->setCurrentIndex(ui->comboProfiles->findText(carried));
        ui->comboProfiles->blockSignals(blocked);
        card_profiles->setCurrentIndex(ui->comboProfiles->currentIndex());
    }
    const QString name = ui->comboProfiles->currentText();
    if (ui->comboProfiles->currentIndex() >= 0 && name != card_profile_name) {
        RememberProfileMapping();
    }

    const QString follows = tr("Applies to this player's controller, whichever one it is.");
    const bool selected = ui->comboProfiles->currentIndex() >= 0;
    card_remove->setVisible(selected);
    card_rename->setEnabled(selected);
    card_duplicate->setEnabled(selected);
    card_delete->setEnabled(selected);
    if (ui->comboProfiles->currentIndex() < 0) {
        card_save->setToolTip(tr("Keep this mapping as a new profile (asks for its name)"));
        // In Modern Console a remap lasts the session; a profile is what keeps controls
        card_status->setText(tr("No profile: this player uses its usual controls. Changes made "
                                "here last for this session; Save as New keeps them."));
        card_status->setPalette(palette());
        return;
    }
    card_save->setToolTip(tr("Save this mapping to \u201c%1\u201d").arg(name));
    QPalette status = palette();
    if (CurrentMappingKey() != card_profile_key) {
        card_status->setText(tr("\u25cf Unsaved changes \u00b7 %1").arg(follows));
        status.setColor(QPalette::WindowText, QColor(208, 138, 30));
    } else {
        card_status->setText(tr("\u2713 Saved \u00b7 %1").arg(follows));
        status.setColor(QPalette::WindowText, status.color(QPalette::PlaceholderText));
    }
    card_status->setPalette(status);
}

std::string ConfigureInputPlayer::CurrentMappingKey() const {
    // Motion is the engine's while players are assigned automatically, so it is left out
    std::string key;
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        key += MappingKey(emulated_controller->GetButtonParam(i).Serialize()) + '|';
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        key += MappingKey(emulated_controller->GetStickParam(i).Serialize()) + '|';
    }
    return key;
}

void ConfigureInputPlayer::RememberProfileMapping() {
    card_profile_name = ui->comboProfiles->currentText();
    // The player keeps its profile's name, so the card shows it the next time too
    Settings::values.players.GetValue()[player_index].profile_name =
        ui->comboProfiles->currentIndex() < 0 ? std::string{} : card_profile_name.toStdString();
    card_profile_key.clear();
    if (ui->comboProfiles->currentIndex() < 0) {
        return;
    }
    const auto read = profiles->PeekProfile(card_profile_name.toStdString(), player_index);
    if (!read) {
        return;
    }
    for (const auto& button : read->buttons) {
        card_profile_key += MappingKey(button) + '|';
    }
    for (const auto& analog : read->analogs) {
        card_profile_key += MappingKey(analog) + '|';
    }
}

void ConfigureInputPlayer::SelectProfile(const QString& name) {
    UpdateInputProfiles();
    ui->comboProfiles->setCurrentIndex(ui->comboProfiles->findText(name));
    emit RefreshInputProfiles(player_index);
    RequestProfile(player_index, name);
    RememberProfileMapping();
    UpdateProfileCard();
}

void ConfigureInputPlayer::RenameProfile() {
    const QString old_name = ui->comboProfiles->currentText();
    if (ui->comboProfiles->currentIndex() < 0) {
        return;
    }
    const auto new_name =
        LimitableInputDialog::GetText(this, tr("Rename Profile"), tr("Enter a new name:"), 1, 30,
                                      LimitableInputDialog::InputLimiter::Filesystem);
    if (new_name.isEmpty() || new_name == old_name) {
        return;
    }
    if (!InputProfiles::IsProfileNameValid(new_name.toStdString()) ||
        !profiles->RenameProfile(old_name.toStdString(), new_name.toStdString())) {
        QMessageBox::critical(
            this, tr("Rename Input Profile"),
            tr("Could not rename \u201c%1\u201d to \u201c%2\u201d.").arg(old_name, new_name));
        return;
    }
    SelectProfile(new_name);
}

void ConfigureInputPlayer::DuplicateProfile() {
    const QString name = ui->comboProfiles->currentText();
    if (ui->comboProfiles->currentIndex() < 0) {
        return;
    }
    QString copy = tr("%1 copy").arg(name);
    for (int n = 2; ui->comboProfiles->findText(copy) >= 0; ++n) {
        copy = tr("%1 copy %2").arg(name).arg(n);
    }
    if (!profiles->DuplicateProfile(name.toStdString(), copy.toStdString())) {
        QMessageBox::critical(this, tr("Duplicate Input Profile"),
                              tr("Could not duplicate \u201c%1\u201d.").arg(name));
        return;
    }
    SelectProfile(copy);
}

void ConfigureInputPlayer::DeleteProfileAsked() {
    const QString name = ui->comboProfiles->currentText();
    if (ui->comboProfiles->currentIndex() < 0) {
        return;
    }
    if (QMessageBox::question(this, tr("Delete Input Profile"),
                              tr("Delete the profile \u201c%1\u201d? This player keeps its "
                                 "current mapping.")
                                  .arg(name)) != QMessageBox::Yes) {
        return;
    }
    DeleteProfile();
    RequestProfile(player_index, {});
    RememberProfileMapping();
    UpdateProfileCard();
}

void ConfigureInputPlayer::UpdateInputDeviceCombobox() {
    // Skip input device persistence if "Input Devices" is set to "Any".
    if (ui->comboDevices->currentIndex() == 0) {
        UpdateInputDevices();
        return;
    }

    const auto devices = emulated_controller->GetMappedDevices();
    UpdateInputDevices();

    if (devices.empty()) {
        return;
    }

    if (devices.size() > 2) {
        ui->comboDevices->setCurrentIndex(0);
        return;
    }

    const auto first_engine = devices[0].Get("engine", "");
    const auto first_guid = devices[0].Get("guid", "");
    const auto first_port = devices[0].Get("port", 0);
    const auto first_pad = devices[0].Get("pad", 0);

    if (devices.size() == 1) {
        const auto devices_it = std::find_if(
            input_devices.begin(), input_devices.end(),
            [first_engine, first_guid, first_port, first_pad](const Common::ParamPackage& param) {
                return param.Get("engine", "") == first_engine &&
                       param.Get("guid", "") == first_guid && param.Get("port", 0) == first_port &&
                       param.Get("pad", 0) == first_pad;
            });
        const int device_index =
            devices_it != input_devices.end()
                ? static_cast<int>(std::distance(input_devices.begin(), devices_it))
                : 0;
        ui->comboDevices->setCurrentIndex(device_index);
        return;
    }

    const auto second_engine = devices[1].Get("engine", "");
    const auto second_guid = devices[1].Get("guid", "");
    const auto second_port = devices[1].Get("port", 0);

    const bool is_keyboard_mouse = (first_engine == "keyboard" || first_engine == "mouse") &&
                                   (second_engine == "keyboard" || second_engine == "mouse");

    if (is_keyboard_mouse) {
        ui->comboDevices->setCurrentIndex(2);
        return;
    }

    const bool is_engine_equal = first_engine == second_engine;
    const bool is_port_equal = first_port == second_port;

    if (is_engine_equal && is_port_equal) {
        const auto devices_it = std::find_if(
            input_devices.begin(), input_devices.end(),
            [first_engine, first_guid, second_guid, first_port](const Common::ParamPackage& param) {
                const bool is_guid_valid =
                    (param.Get("guid", "") == first_guid &&
                     param.Get("guid2", "") == second_guid) ||
                    (param.Get("guid", "") == second_guid && param.Get("guid2", "") == first_guid);
                return param.Get("engine", "") == first_engine && is_guid_valid &&
                       param.Get("port", 0) == first_port;
            });
        const int device_index =
            devices_it != input_devices.end()
                ? static_cast<int>(std::distance(input_devices.begin(), devices_it))
                : 0;
        ui->comboDevices->setCurrentIndex(device_index);
    } else {
        ui->comboDevices->setCurrentIndex(0);
    }
}

void ConfigureInputPlayer::RestoreDefaults() {
    if (joycon_locked) {
        return;
    }
    // In the automatic modes the device list is hidden (and may say "Any"): the defaults are
    // those of the controller the bindings point at, or pointed at before Clear
    if (connection_locked) {
        if (const auto device = CurrentSdlDevice()) {
            locked_device = device;
        }
        if (locked_device) {
            const auto it = std::find_if(
                input_devices.begin(), input_devices.end(), [this](const auto& param) {
                    return param.Get("engine", "") == "sdl" &&
                           param.Get("guid", "") == locked_device->first &&
                           param.Get("port", 0) == locked_device->second;
                });
            if (it != input_devices.end()) {
                ui->comboDevices->setCurrentIndex(
                    static_cast<int>(std::distance(input_devices.begin(), it)));
            }
        }
    }
    // (Motion is the engine's in these modes, and hidden: a DSU binding stays)
    const auto motions = KeptMotions();
    UpdateMappingWithDefaults();
    RestoreMotions(motions);
}

std::optional<std::pair<std::string, int>> ConfigureInputPlayer::CurrentSdlDevice() const {
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        const auto param = emulated_controller->GetButtonParam(button_id);
        if (param.Get("engine", "") == "sdl") {
            return std::pair<std::string, int>{param.Get("guid", ""), param.Get("port", 0)};
        }
    }
    return std::nullopt;
}

std::vector<Common::ParamPackage> ConfigureInputPlayer::KeptMotions() const {
    std::vector<Common::ParamPackage> motions;
    if (connection_locked) {
        for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
            motions.push_back(
                emulated_controller->GetMotionParam(static_cast<std::size_t>(motion_id)));
        }
    }
    return motions;
}

void ConfigureInputPlayer::RestoreMotions(const std::vector<Common::ParamPackage>& motions) {
    for (std::size_t motion_id = 0; motion_id < motions.size(); ++motion_id) {
        emulated_controller->SetMotionParam(motion_id, motions[motion_id]);
    }
}

void ConfigureInputPlayer::ClearAll() {
    if (joycon_locked) {
        return;
    }
    // (The controller is remembered, for Defaults afterwards)
    if (const auto device = CurrentSdlDevice()) {
        locked_device = device;
    }
    // (Motion is the engine's in the automatic modes, and hidden: a DSU binding stays)
    const auto motions = KeptMotions();
    ClearMappings();
    RestoreMotions(motions);
}

void ConfigureInputPlayer::ClearMappings() {
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        const auto* const button = button_map[button_id];
        if (button == nullptr) {
            continue;
        }
        emulated_controller->SetButtonParam(button_id, {});
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            const auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];
            if (analog_button == nullptr) {
                continue;
            }
            emulated_controller->SetStickParam(analog_id, {});
        }
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        const auto* const motion_button = motion_map[motion_id];
        if (motion_button == nullptr) {
            continue;
        }
        emulated_controller->SetMotionParam(motion_id, {});
    }

    UpdateUI();
    UpdateInputDevices();
}

void ConfigureInputPlayer::UpdateUI() {
    for (int button = 0; button < Settings::NativeButton::NumButtons; ++button) {
        const Common::ParamPackage param = emulated_controller->GetButtonParam(button);
        button_map[button]->setText(ButtonToText(param));
    }

    const Common::ParamPackage ZL_param =
        emulated_controller->GetButtonParam(Settings::NativeButton::ZL);
    if (ZL_param.Has("threshold")) {
        const int button_threshold = static_cast<int>(ZL_param.Get("threshold", 0.5f) * 100.0f);
        ui->sliderZLThreshold->setValue(button_threshold);
    }

    const Common::ParamPackage ZR_param =
        emulated_controller->GetButtonParam(Settings::NativeButton::ZR);
    if (ZR_param.Has("threshold")) {
        const int button_threshold = static_cast<int>(ZR_param.Get("threshold", 0.5f) * 100.0f);
        ui->sliderZRThreshold->setValue(button_threshold);
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        const Common::ParamPackage param = emulated_controller->GetMotionParam(motion_id);
        motion_map[motion_id]->setText(ButtonToText(param));
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        const Common::ParamPackage param = emulated_controller->GetStickParam(analog_id);
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];

            if (analog_button == nullptr) {
                continue;
            }

            analog_button->setText(AnalogToText(param, analog_sub_buttons[sub_button_id]));
        }

        analog_map_modifier_button[analog_id]->setText(
            ButtonToText(Common::ParamPackage{param.Get("modifier", "")}));

        const auto deadzone_label = analog_map_deadzone_label[analog_id];
        const auto deadzone_slider = analog_map_deadzone_slider[analog_id];
        const auto modifier_groupbox = analog_map_modifier_groupbox[analog_id];
        const auto modifier_label = analog_map_modifier_label[analog_id];
        const auto modifier_slider = analog_map_modifier_slider[analog_id];
        const auto range_groupbox = analog_map_range_groupbox[analog_id];
        const auto range_spinbox = analog_map_range_spinbox[analog_id];

        int slider_value;
        const bool is_controller = input_subsystem->IsController(param);

        if (is_controller) {
            slider_value = static_cast<int>(param.Get("deadzone", 0.15f) * 100);
            deadzone_label->setText(tr("Deadzone: %1%").arg(slider_value));
            deadzone_slider->setValue(slider_value);
            range_spinbox->setValue(static_cast<int>(param.Get("range", 0.95f) * 100));
        } else {
            slider_value = static_cast<int>(param.Get("modifier_scale", 0.5f) * 100);
            modifier_label->setText(tr("Modifier Range: %1%").arg(slider_value));
            modifier_slider->setValue(slider_value);
        }

        deadzone_label->setVisible(is_controller);
        deadzone_slider->setVisible(is_controller);
        modifier_groupbox->setVisible(!is_controller);
        modifier_label->setVisible(!is_controller);
        modifier_slider->setVisible(!is_controller);
        range_groupbox->setVisible(is_controller);
    }
}

void ConfigureInputPlayer::SetConnectableControllers() {
    const auto npad_style_set = hid_core.GetSupportedStyleTag();
    index_controller_type_pairs.clear();
    ui->comboControllerType->clear();

    const auto add_item = [&](Core::HID::NpadStyleIndex controller_type,
                              const QString& controller_name) {
        index_controller_type_pairs.emplace_back(ui->comboControllerType->count(), controller_type);
        ui->comboControllerType->addItem(controller_name);
    };

    if (npad_style_set.fullkey == 1) {
        add_item(Core::HID::NpadStyleIndex::Fullkey, tr("Pro Controller"));
    }

    if (npad_style_set.joycon_dual == 1) {
        add_item(Core::HID::NpadStyleIndex::JoyconDual, tr("Dual Joycons"));
    }

    if (npad_style_set.joycon_left == 1) {
        add_item(Core::HID::NpadStyleIndex::JoyconLeft, tr("Left Joycon"));
    }

    if (npad_style_set.joycon_right == 1) {
        add_item(Core::HID::NpadStyleIndex::JoyconRight, tr("Right Joycon"));
    }

    if (player_index == 0 && npad_style_set.handheld == 1) {
        add_item(Core::HID::NpadStyleIndex::Handheld, tr("Handheld"));
    }

    if (npad_style_set.gamecube == 1) {
        add_item(Core::HID::NpadStyleIndex::GameCube, tr("GameCube Controller"));
    }

    // Disable all unsupported controllers
    if (!Settings::values.enable_all_controllers) {
        return;
    }

    if (npad_style_set.palma == 1) {
        add_item(Core::HID::NpadStyleIndex::Pokeball, tr("Poke Ball Plus"));
    }

    if (npad_style_set.lark == 1) {
        add_item(Core::HID::NpadStyleIndex::NES, tr("NES Controller"));
    }

    if (npad_style_set.lucia == 1) {
        add_item(Core::HID::NpadStyleIndex::SNES, tr("SNES Controller"));
    }

    if (npad_style_set.lagoon == 1) {
        add_item(Core::HID::NpadStyleIndex::N64, tr("N64 Controller"));
    }

    if (npad_style_set.lager == 1) {
        add_item(Core::HID::NpadStyleIndex::SegaGenesis, tr("Sega Genesis"));
    }
}

Core::HID::NpadStyleIndex ConfigureInputPlayer::GetControllerTypeFromIndex(int index) const {
    const auto it =
        std::find_if(index_controller_type_pairs.begin(), index_controller_type_pairs.end(),
                     [index](const auto& pair) { return pair.first == index; });

    if (it == index_controller_type_pairs.end()) {
        return Core::HID::NpadStyleIndex::Fullkey;
    }

    return it->second;
}

int ConfigureInputPlayer::GetIndexFromControllerType(Core::HID::NpadStyleIndex type) const {
    const auto it =
        std::find_if(index_controller_type_pairs.begin(), index_controller_type_pairs.end(),
                     [type](const auto& pair) { return pair.second == type; });

    if (it == index_controller_type_pairs.end()) {
        return -1;
    }

    return it->first;
}

void ConfigureInputPlayer::UpdateInputDevices() {
    input_devices = input_subsystem->GetInputDevices();
    ui->comboDevices->clear();
    for (const auto& device : input_devices) {
        ui->comboDevices->addItem(QString::fromStdString(device.Get("display", "Unknown")), {});
    }
}

void ConfigureInputPlayer::UpdateControllerAvailableButtons() {
    auto layout = GetControllerTypeFromIndex(ui->comboControllerType->currentIndex());
    if (debug) {
        layout = Core::HID::NpadStyleIndex::Fullkey;
    }

    // List of all the widgets that will be hidden by any of the following layouts that need
    // "unhidden" after the controller type changes
    const std::array<QWidget*, 19> layout_show = {
        ui->buttonShoulderButtonsSLSRLeft,
        ui->buttonShoulderButtonsSLSRRight,
        ui->horizontalSpacerShoulderButtonsWidget,
        ui->horizontalSpacerShoulderButtonsWidget2,
        ui->horizontalSpacerShoulderButtonsWidget3,
        ui->horizontalSpacerShoulderButtonsWidget4,
        ui->buttonShoulderButtonsLeft,
        ui->buttonMiscButtonsMinusGroup,
        ui->bottomLeft,
        ui->LStick,
        ui->Dpad,
        ui->buttonShoulderButtonsRight,
        ui->buttonMiscButtonsPlusGroup,
        ui->buttonMiscButtonsHomeGroup,
        ui->bottomRight,
        ui->faceButtons,
        ui->RStick,
        ui->buttonMiscButtonsMinusGroup,
        ui->buttonMiscButtonsScreenshotGroup,
    };

    for (auto* widget : layout_show) {
        widget->show();
    }

    std::vector<QWidget*> layout_hidden;
    switch (layout) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::Handheld:
        layout_hidden = {
            ui->buttonShoulderButtonsSLSRLeft,          ui->buttonShoulderButtonsSLSRRight,
            ui->horizontalSpacerShoulderButtonsWidget2, ui->horizontalSpacerShoulderButtonsWidget4,
            ui->buttonMiscButtonsScreenshotGroup,
        };
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        layout_hidden = {
            ui->buttonShoulderButtonsSLSRRight,
            ui->horizontalSpacerShoulderButtonsWidget2,
            ui->horizontalSpacerShoulderButtonsWidget3,
            ui->buttonShoulderButtonsRight,
            ui->buttonMiscButtonsPlusGroup,
            ui->buttonMiscButtonsHomeGroup,
            ui->faceButtons,
            ui->RStick,
            ui->buttonMiscButtonsScreenshotGroup,
        };
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        layout_hidden = {
            ui->buttonShoulderButtonsSLSRLeft,
            ui->horizontalSpacerShoulderButtonsWidget,
            ui->horizontalSpacerShoulderButtonsWidget4,
            ui->buttonShoulderButtonsLeft,
            ui->buttonMiscButtonsMinusGroup,
            ui->LStick,
            ui->Dpad,
            ui->buttonMiscButtonsScreenshotGroup,
        };
        break;
    case Core::HID::NpadStyleIndex::GameCube:
        layout_hidden = {
            ui->buttonShoulderButtonsSLSRLeft,
            ui->buttonShoulderButtonsSLSRRight,
            ui->horizontalSpacerShoulderButtonsWidget2,
            ui->horizontalSpacerShoulderButtonsWidget4,
            ui->buttonMiscButtonsMinusGroup,
            ui->buttonMiscButtonsScreenshotGroup,
        };
        break;
    case Core::HID::NpadStyleIndex::JoyconDual:
        layout_hidden = {
            ui->buttonShoulderButtonsSLSRLeft,
            ui->buttonShoulderButtonsSLSRRight,
            ui->buttonMiscButtonsScreenshotGroup,
        };
        break;
    default:
        break;
    }

    for (auto* widget : layout_hidden) {
        widget->hide();
    }
}

void ConfigureInputPlayer::UpdateControllerEnabledButtons() {
    auto layout = GetControllerTypeFromIndex(ui->comboControllerType->currentIndex());
    if (debug) {
        layout = Core::HID::NpadStyleIndex::Fullkey;
    }

    // List of all the widgets that will be disabled by any of the following layouts that need
    // "enabled" after the controller type changes
    const std::array<QWidget*, 3> layout_enable = {
        ui->buttonLStickPressedGroup,
        ui->groupRStickPressed,
        ui->buttonShoulderButtonsButtonLGroup,
    };

    for (auto* widget : layout_enable) {
        widget->setEnabled(true);
    }

    std::vector<QWidget*> layout_disable;
    switch (layout) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::JoyconLeft:
    case Core::HID::NpadStyleIndex::JoyconRight:
        break;
    case Core::HID::NpadStyleIndex::GameCube:
        layout_disable = {
            ui->buttonHome,
            ui->buttonLStickPressedGroup,
            ui->groupRStickPressed,
            ui->buttonShoulderButtonsButtonLGroup,
        };
        break;
    default:
        break;
    }

    for (auto* widget : layout_disable) {
        widget->setEnabled(false);
    }
}

void ConfigureInputPlayer::UpdateMotionButtons() {
    if (debug) {
        // Motion isn't used with the debug controller, hide both groupboxes.
        ui->buttonMotionLeftGroup->hide();
        ui->buttonMotionRightGroup->hide();
        return;
    }

    // Show/hide the "Motion 1/2" groupboxes depending on the currently selected controller.
    switch (GetControllerTypeFromIndex(ui->comboControllerType->currentIndex())) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconLeft:
    case Core::HID::NpadStyleIndex::Handheld:
        // Show "Motion 1" and hide "Motion 2".
        ui->buttonMotionLeftGroup->show();
        ui->buttonMotionRightGroup->hide();
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        // Show "Motion 2" and hide "Motion 1".
        ui->buttonMotionLeftGroup->hide();
        ui->buttonMotionRightGroup->show();
        break;
    case Core::HID::NpadStyleIndex::GameCube:
        // Hide both "Motion 1/2".
        ui->buttonMotionLeftGroup->hide();
        ui->buttonMotionRightGroup->hide();
        break;
    case Core::HID::NpadStyleIndex::JoyconDual:
    default:
        // Show both "Motion 1/2".
        ui->buttonMotionLeftGroup->show();
        ui->buttonMotionRightGroup->show();
        break;
    }
}

void ConfigureInputPlayer::UpdateControllerButtonNames() {
    auto layout = GetControllerTypeFromIndex(ui->comboControllerType->currentIndex());
    if (debug) {
        layout = Core::HID::NpadStyleIndex::Fullkey;
    }

    switch (layout) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::JoyconLeft:
    case Core::HID::NpadStyleIndex::JoyconRight:
        ui->buttonMiscButtonsPlusGroup->setTitle(tr("Plus"));
        ui->buttonShoulderButtonsButtonZLGroup->setTitle(tr("ZL"));
        ui->buttonShoulderButtonsZRGroup->setTitle(tr("ZR"));
        ui->buttonShoulderButtonsRGroup->setTitle(tr("R"));
        ui->LStick->setTitle(tr("Left Stick"));
        ui->RStick->setTitle(tr("Right Stick"));
        break;
    case Core::HID::NpadStyleIndex::GameCube:
        ui->buttonMiscButtonsPlusGroup->setTitle(tr("Start / Pause"));
        ui->buttonShoulderButtonsButtonZLGroup->setTitle(tr("L"));
        ui->buttonShoulderButtonsZRGroup->setTitle(tr("R"));
        ui->buttonShoulderButtonsRGroup->setTitle(tr("Z"));
        ui->LStick->setTitle(tr("Control Stick"));
        ui->RStick->setTitle(tr("C-Stick"));
        break;
    default:
        break;
    }
}

void ConfigureInputPlayer::UpdateMappingWithDefaults() {
    if (ui->comboDevices->currentIndex() == 0) {
        return;
    }

    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        const auto* const button = button_map[button_id];
        if (button == nullptr) {
            continue;
        }
        emulated_controller->SetButtonParam(button_id, {});
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
            const auto* const analog_button = analog_map_buttons[analog_id][sub_button_id];
            if (analog_button == nullptr) {
                continue;
            }
            emulated_controller->SetStickParam(analog_id, {});
        }
    }

    for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
        const auto* const motion_button = motion_map[motion_id];
        if (motion_button == nullptr) {
            continue;
        }
        emulated_controller->SetMotionParam(motion_id, {});
    }

    // Reset keyboard or mouse bindings
    if (ui->comboDevices->currentIndex() == 1 || ui->comboDevices->currentIndex() == 2) {
        for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
            emulated_controller->SetButtonParam(
                button_id, Common::ParamPackage{InputCommon::GenerateKeyboardParam(
                               QtConfig::default_buttons[button_id])});
        }
        for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
            Common::ParamPackage analog_param{};
            for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; ++sub_button_id) {
                Common::ParamPackage params{InputCommon::GenerateKeyboardParam(
                    QtConfig::default_analogs[analog_id][sub_button_id])};
                SetAnalogParam(params, analog_param, analog_sub_buttons[sub_button_id]);
            }

            analog_param.Set("modifier", InputCommon::GenerateKeyboardParam(
                                             QtConfig::default_stick_mod[analog_id]));
            emulated_controller->SetStickParam(analog_id, analog_param);
        }

        for (int motion_id = 0; motion_id < Settings::NativeMotion::NumMotions; ++motion_id) {
            emulated_controller->SetMotionParam(
                motion_id, Common::ParamPackage{InputCommon::GenerateKeyboardParam(
                               QtConfig::default_motions[motion_id])});
        }

        // If mouse is selected we want to override with mappings from the driver
        if (ui->comboDevices->currentIndex() == 1) {
            UpdateUI();
            return;
        }
    }

    // Reset controller bindings
    const auto& device = input_devices[ui->comboDevices->currentIndex()];
    auto button_mappings = input_subsystem->GetButtonMappingForDevice(device);
    auto analog_mappings = input_subsystem->GetAnalogMappingForDevice(device);
    auto motion_mappings = input_subsystem->GetMotionMappingForDevice(device);

    for (const auto& button_mapping : button_mappings) {
        const std::size_t index = button_mapping.first;
        emulated_controller->SetButtonParam(index, button_mapping.second);
    }
    for (const auto& analog_mapping : analog_mappings) {
        const std::size_t index = analog_mapping.first;
        emulated_controller->SetStickParam(index, analog_mapping.second);
    }
    for (const auto& motion_mapping : motion_mappings) {
        const std::size_t index = motion_mapping.first;
        emulated_controller->SetMotionParam(index, motion_mapping.second);
    }

    UpdateUI();
}

void ConfigureInputPlayer::HandleClick(
    QPushButton* button, std::size_t button_id,
    std::function<void(const Common::ParamPackage&)> new_input_setter,
    InputCommon::Polling::InputType type) {
    if (timeout_timer->isActive()) {
        return;
    }
    if (button == ui->buttonMotionLeft || button == ui->buttonMotionRight) {
        button->setText(tr("Shake!"));
    } else {
        button->setText(tr("[waiting]"));
    }
    button->setFocus();

    input_setter = std::move(new_input_setter);

    input_subsystem->BeginMapping(type);

    QWidget::grabMouse();
    QWidget::grabKeyboard();

    if (type == InputCommon::Polling::InputType::Button) {
        ui->controllerFrame->BeginMappingButton(button_id);
    } else if (type == InputCommon::Polling::InputType::Stick) {
        ui->controllerFrame->BeginMappingAnalog(button_id);
    }

    timeout_timer->start(4000); // Cancel after 4 seconds
    poll_timer->start(25);      // Check for new inputs every 25ms
}

void ConfigureInputPlayer::SetPollingResult(const Common::ParamPackage& params, bool abort) {
    timeout_timer->stop();
    poll_timer->stop();
    input_subsystem->StopMapping();

    QWidget::releaseMouse();
    QWidget::releaseKeyboard();

    if (!abort) {
        (*input_setter)(params);
    }

    UpdateUI();
    UpdateInputDeviceCombobox();
    ui->controllerFrame->EndMapping();

    input_setter = std::nullopt;
}

bool ConfigureInputPlayer::IsInputAcceptable(const Common::ParamPackage& params) const {
    if (ui->comboDevices->currentIndex() == 0) {
        return true;
    }

    if (params.Has("motion")) {
        return true;
    }

    // Keyboard/Mouse
    if (ui->comboDevices->currentIndex() == 1 || ui->comboDevices->currentIndex() == 2) {
        return params.Get("engine", "") == "keyboard" || params.Get("engine", "") == "mouse";
    }

    const auto& current_input_device = input_devices[ui->comboDevices->currentIndex()];
    return params.Get("engine", "") == current_input_device.Get("engine", "") &&
           (params.Get("guid", "") == current_input_device.Get("guid", "") ||
            params.Get("guid", "") == current_input_device.Get("guid2", "")) &&
           params.Get("port", 0) == current_input_device.Get("port", 0);
}

void ConfigureInputPlayer::mousePressEvent(QMouseEvent* event) {
    if (!input_setter || !event) {
        return;
    }

    const auto button = GRenderWindow::QtButtonToMouseButton(event->button());
    input_subsystem->GetMouse()->PressButton(0, 0, button);
    input_subsystem->GetMouse()->NotifyChanged();
}

void ConfigureInputPlayer::wheelEvent(QWheelEvent* event) {
    const int x = event->angleDelta().x();
    const int y = event->angleDelta().y();
    input_subsystem->GetMouse()->MouseWheelChange(x, y);
    input_subsystem->GetMouse()->NotifyChanged();
}

void ConfigureInputPlayer::keyPressEvent(QKeyEvent* event) {
    if (!input_setter || !event) {
        return;
    }
    event->ignore();
    if (event->key() != Qt::Key_Escape) {
        input_subsystem->GetKeyboard()->PressKey(event->key());
    }
}

bool ConfigureInputPlayer::eventFilter(QObject* object, QEvent* event) {
    if (object == ui->comboDevices && event->type() == QEvent::MouseButtonPress) {
        RefreshInputDevices();
    }
    return object->eventFilter(object, event);
}

void ConfigureInputPlayer::CreateProfile() {
    const auto profile_name =
        LimitableInputDialog::GetText(this, tr("New Profile"), tr("Enter a profile name:"), 1, 30,
                                      LimitableInputDialog::InputLimiter::Filesystem);

    if (profile_name.isEmpty()) {
        return;
    }

    if (!InputProfiles::IsProfileNameValid(profile_name.toStdString())) {
        QMessageBox::critical(this, tr("Create Input Profile"),
                              tr("The given profile name is not valid!"));
        return;
    }

    ApplyConfiguration();

    if (!profiles->CreateProfile(profile_name.toStdString(), player_index)) {
        QMessageBox::critical(this, tr("Create Input Profile"),
                              tr("Failed to create the input profile \"%1\"").arg(profile_name));
        UpdateInputProfiles();
        emit RefreshInputProfiles(player_index);
        return;
    }

    emit RefreshInputProfiles(player_index);

    ui->comboProfiles->addItem(profile_name);
    ui->comboProfiles->setCurrentIndex(ui->comboProfiles->count() - 1);
}

void ConfigureInputPlayer::DeleteProfile() {
    const QString profile_name = ui->comboProfiles->itemText(ui->comboProfiles->currentIndex());

    if (profile_name.isEmpty()) {
        return;
    }

    if (!profiles->DeleteProfile(profile_name.toStdString())) {
        QMessageBox::critical(this, tr("Delete Input Profile"),
                              tr("Failed to delete the input profile \"%1\"").arg(profile_name));
        UpdateInputProfiles();
        emit RefreshInputProfiles(player_index);
        return;
    }

    emit RefreshInputProfiles(player_index);

    ui->comboProfiles->removeItem(ui->comboProfiles->currentIndex());
    ui->comboProfiles->setCurrentIndex(-1);
}

void ConfigureInputPlayer::LoadProfile() {
    const QString profile_name = ui->comboProfiles->itemText(ui->comboProfiles->currentIndex());

    if (profile_name.isEmpty()) {
        return;
    }

    ApplyConfiguration();

    // In the automatic modes the profile goes onto the player's current controller (the one
    // saved with it would read as a swap)
    auto& player = Settings::values.players.GetValue()[player_index];
    const auto device = connection_locked ? SdlDevice(player) : std::nullopt;

    if (!profiles->LoadProfile(profile_name.toStdString(), player_index)) {
        QMessageBox::critical(this, tr("Load Input Profile"),
                              tr("Failed to load the input profile \"%1\"").arg(profile_name));
        UpdateInputProfiles();
        emit RefreshInputProfiles(player_index);
        return;
    }
    if (device) {
        KeepSdlDevice(player, *device);
    }

    LoadConfiguration();
}

void ConfigureInputPlayer::SaveProfile() {
    static constexpr size_t HANDHELD_INDEX = 8;
    const QString profile_name = ui->comboProfiles->itemText(ui->comboProfiles->currentIndex());

    if (profile_name.isEmpty()) {
        return;
    }

    ApplyConfiguration();

    // When we're in handheld mode, only the handheld emulated controller bindings are updated
    const bool is_handheld = player_index == 0 && emulated_controller->GetNpadIdType() ==
                                                      Core::HID::NpadIdType::Handheld;
    const auto profile_player_index = is_handheld ? HANDHELD_INDEX : player_index;

    if (!profiles->SaveProfile(profile_name.toStdString(), profile_player_index)) {
        QMessageBox::critical(this, tr("Save Input Profile"),
                              tr("Failed to save the input profile \"%1\"").arg(profile_name));
        UpdateInputProfiles();
        emit RefreshInputProfiles(player_index);
        return;
    }
}

void ConfigureInputPlayer::UpdateInputProfiles() {
    ui->comboProfiles->clear();

    // Set current profile as empty by default
    int profile_index = -1;

    // Add every available profile and search the player profile to set it as current one
    auto& current_profile = Settings::values.players.GetValue()[player_index].profile_name;
    std::vector<std::string> profile_names = profiles->GetInputProfileNames();
    std::string profile_name;
    for (size_t i = 0; i < profile_names.size(); i++) {
        profile_name = profile_names[i];
        ui->comboProfiles->addItem(QString::fromStdString(profile_name));
        if (current_profile == profile_name) {
            profile_index = (int)i;
        }
    }

    LOG_DEBUG(Frontend, "Setting the current input profile to index {}", profile_index);
    ui->comboProfiles->setCurrentIndex(profile_index);
}
