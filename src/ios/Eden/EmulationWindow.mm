// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

//
//  EmulationWindow.mm - Sudachi
//  Created by Jarrod Norwell on 1/18/24.
//

#import "EmulationWindow.h"
#import "EmulationSession/EmulationSession.h"

#include <SDL.h>

#include "common/logging/log.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/drivers/virtual_amiibo.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "input_common/main.h"

void EmulationWindow::OnSurfaceChanged(CA::MetalLayer* surface, CGSize size) {
    m_size = size;

    m_window_width = size.width;
    m_window_height = size.height;

    // Ensures that we emulate with the correct aspect ratio.
    // UpdateCurrentFramebufferLayout(m_window_width, m_window_height);

    window_info.render_surface = reinterpret_cast<void*>(surface);
    window_info.render_surface_scale = [[UIScreen mainScreen] nativeScale];
}

void EmulationWindow::OrientationChanged(UIInterfaceOrientation orientation) {
    is_portrait = orientation == UIInterfaceOrientationPortrait;
}

void EmulationWindow::OnTouchPressed(int id, float x, float y) {
    const auto [touch_x, touch_y] = MapToTouchScreen(x, y);
    EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchPressed(touch_x,
                                                                                       touch_y, id);
}

void EmulationWindow::OnTouchMoved(int id, float x, float y) {
    const auto [touch_x, touch_y] = MapToTouchScreen(x, y);
    EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchMoved(touch_x,
                                                                                     touch_y, id);
}

void EmulationWindow::OnTouchReleased(int id) {
    EmulationSession::GetInstance().GetInputSubsystem().GetTouchScreen()->TouchReleased(id);
}

void EmulationWindow::OnGamepadButtonEvent(int player_index, int button_id, bool pressed) {
    m_input_subsystem->GetVirtualGamepad()->SetButtonState(player_index, button_id, pressed);
}

void EmulationWindow::OnGamepadJoystickEvent(int player_index, int stick_id, float x, float y) {
    m_input_subsystem->GetVirtualGamepad()->SetStickPosition(player_index, stick_id, x, y);
}

void EmulationWindow::OnGamepadMotionEvent(int player_index, u64 delta_timestamp, float gyro_x, float gyro_y, float gyro_z, float accel_x, float accel_y, float accel_z) {
    m_input_subsystem->GetVirtualGamepad()->SetMotionState(player_index, delta_timestamp, gyro_x, gyro_y, gyro_z, accel_x, accel_y, accel_z);
}

void EmulationWindow::OnFrameDisplayed() {
    if (!m_first_frame) {
        m_first_frame = true;
    }
}

EmulationWindow::EmulationWindow(InputCommon::InputSubsystem* input_subsystem, CA::MetalLayer* surface, CGSize size, std::shared_ptr<Common::DynamicLibrary> driver_library) : m_input_subsystem{input_subsystem}, m_size{size}, m_driver_library{driver_library} {
    LOG_INFO(Frontend, "initializing");

    if (!surface) {
        LOG_CRITICAL(Frontend, "surface is nullptr");
        return;
    }

    OnSurfaceChanged(surface, m_size);
    window_info.render_surface_scale = [[UIScreen mainScreen] nativeScale];
    window_info.type = Core::Frontend::WindowSystemType::Cocoa;

    m_input_subsystem->Initialize();
}
