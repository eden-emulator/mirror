//
//  EmulationWindow.h - Sudachi
//  Created by Jarrod Norwell on 1/18/24.
//

#pragma once

#import <Metal/Metal.hpp>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <memory>
#include <span>

#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "input_common/main.h"

class GraphicsContext_Apple final : public Core::Frontend::GraphicsContext {
public:
    explicit GraphicsContext_Apple(std::shared_ptr<Common::DynamicLibrary> driver_library)
        : m_driver_library{driver_library} {}

    ~GraphicsContext_Apple() = default;

    std::shared_ptr<Common::DynamicLibrary> GetDriverLibrary() override {
        return m_driver_library;
    }

private:
    std::shared_ptr<Common::DynamicLibrary> m_driver_library;
};

NS_ASSUME_NONNULL_BEGIN

class EmulationWindow final : public Core::Frontend::EmuWindow {
public:
    EmulationWindow(InputCommon::InputSubsystem* input_subsystem, CA::MetalLayer* surface, CGSize size,
                      std::shared_ptr<Common::DynamicLibrary> driver_library);

    ~EmulationWindow() = default;

    void OnSurfaceChanged(CA::MetalLayer* surface, CGSize size);
    void OrientationChanged(UIInterfaceOrientation orientation);
    void OnFrameDisplayed() override;

    void OnTouchPressed(int id, float x, float y);
    void OnTouchMoved(int id, float x, float y);
    void OnTouchReleased(int id);

    void OnGamepadButtonEvent(int player_index, int button_id, bool pressed);
    void OnGamepadJoystickEvent(int player_index, int stick_id, float x, float y);
    void OnGamepadMotionEvent(int player_index, u64 delta_timestamp, float gyro_x, float gyro_y, float gyro_z, float accel_x, float accel_y, float accel_z);

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return {std::make_unique<GraphicsContext_Apple>(m_driver_library)};
    }


    bool HasFirstFrame() const {
        return m_first_frame;
    }

    bool IsShown() const override {
        return true;
    };

private:
    float m_window_width{};
    float m_window_height{};
    CGSize m_size;
    bool is_portrait = true;

    InputCommon::InputSubsystem* m_input_subsystem{};
    std::shared_ptr<Common::DynamicLibrary> m_driver_library;

    bool m_first_frame = false;
};

NS_ASSUME_NONNULL_END
