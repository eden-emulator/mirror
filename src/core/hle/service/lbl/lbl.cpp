// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <memory>

#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/lbl/lbl.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::LBL {

class LBL final : public ServiceFramework<LBL> {
public:
    explicit LBL(Core::System& system_) : ServiceFramework{system_, "lbl"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    enum class BacklightSwitchStatus : u32 {
        Off = 0,
        On = 1,
    };

    void SaveCurrentSetting(HLERequestContext& ctx) {
        LOG_WARNING(Service_LBL, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void LoadCurrentSetting(HLERequestContext& ctx) {
        LOG_WARNING(Service_LBL, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetCurrentBrightnessSetting(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        auto brightness = rp.Pop<float>();

        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        current_brightness = brightness;
        update_instantly = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentBrightnessSetting(HLERequestContext& ctx) {
        auto brightness = current_brightness;
        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(brightness);
    }

    void SwitchBacklightOn(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto fade_time = rp.Pop<u64_le>();
        LOG_WARNING(Service_LBL, "(STUBBED) called, fade_time={}", fade_time);

        backlight_enabled = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SwitchBacklightOff(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto fade_time = rp.Pop<u64_le>();
        LOG_WARNING(Service_LBL, "(STUBBED) called, fade_time={}", fade_time);

        backlight_enabled = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetBacklightSwitchStatus(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushEnum<BacklightSwitchStatus>(backlight_enabled ? BacklightSwitchStatus::On
                                                             : BacklightSwitchStatus::Off);
    }

    void EnableDimming(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        dimming = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DisableDimming(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        dimming = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsDimmingEnabled(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(dimming);
    }

    void EnableAutoBrightnessControl(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");
        auto_brightness = true;
        update_instantly = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DisableAutoBrightnessControl(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");
        auto_brightness = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsAutoBrightnessControlEnabled(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(auto_brightness);
    }

    void SetAmbientLightSensorValue(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto light_value = rp.Pop<float>();

        LOG_DEBUG(Service_LBL, "called light_value={}", light_value);

        ambient_light_value = light_value;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetAmbientLightSensorValue(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(ambient_light_value);
    }

    void SetBrightnessReflectionDelayLevel(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetBrightnessReflectionDelayLevel(HLERequestContext& ctx) {
        // This is intentional, the function is hard coded to return 0.0f on hardware
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0.0f);
    }

    void SetCurrentBrightnessMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentBrightnessMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        // This function is suppose to return something but it seems like it doesn't
    }

    void SetCurrentAmbientLightSensorMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentAmbientLightSensorMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        // This function is suppose to return something but it seems like it doesn't
    }

    void IsAmbientLightSensorAvailable(HLERequestContext& ctx) {
        LOG_WARNING(Service_LBL, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        // TODO(ogniK): Only return true if there's no device error
        rb.Push(true);
    }

    void SetCurrentBrightnessSettingForVrMode(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        auto brightness = rp.Pop<float>();

        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        current_vr_brightness = brightness;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentBrightnessSettingForVrMode(HLERequestContext& ctx) {
        auto brightness = current_vr_brightness;
        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(brightness);
    }

    void EnableVrMode(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);

        vr_mode_enabled = true;
    }

    void DisableVrMode(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);

        vr_mode_enabled = false;
    }

    void IsVrModeEnabled(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(vr_mode_enabled);
    }

    void IsAutoBrightnessControlSupported(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(auto_brightness_supported);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &LBL::SaveCurrentSetting, "SaveCurrentSetting"},
        FunctionInfo{1, &LBL::LoadCurrentSetting, "LoadCurrentSetting"},
        FunctionInfo{2, &LBL::SetCurrentBrightnessSetting, "SetCurrentBrightnessSetting"},
        FunctionInfo{3, &LBL::GetCurrentBrightnessSetting, "GetCurrentBrightnessSetting"},
        FunctionInfo{4, nullptr, "ApplyCurrentBrightnessSettingToBacklight"},
        FunctionInfo{5, nullptr, "GetBrightnessSettingAppliedToBacklight"},
        FunctionInfo{6, &LBL::SwitchBacklightOn, "SwitchBacklightOn"},
        FunctionInfo{7, &LBL::SwitchBacklightOff, "SwitchBacklightOff"},
        FunctionInfo{8, &LBL::GetBacklightSwitchStatus, "GetBacklightSwitchStatus"},
        FunctionInfo{9, &LBL::EnableDimming, "EnableDimming"},
        FunctionInfo{10, &LBL::DisableDimming, "DisableDimming"},
        FunctionInfo{11, &LBL::IsDimmingEnabled, "IsDimmingEnabled"},
        FunctionInfo{12, &LBL::EnableAutoBrightnessControl, "EnableAutoBrightnessControl"},
        FunctionInfo{13, &LBL::DisableAutoBrightnessControl, "DisableAutoBrightnessControl"},
        FunctionInfo{14, &LBL::IsAutoBrightnessControlEnabled, "IsAutoBrightnessControlEnabled"},
        FunctionInfo{15, &LBL::SetAmbientLightSensorValue, "SetAmbientLightSensorValue"},
        FunctionInfo{16, &LBL::GetAmbientLightSensorValue, "GetAmbientLightSensorValue"},
        FunctionInfo{17, &LBL::SetBrightnessReflectionDelayLevel, "SetBrightnessReflectionDelayLevel"},
        FunctionInfo{18, &LBL::GetBrightnessReflectionDelayLevel, "GetBrightnessReflectionDelayLevel"},
        FunctionInfo{19, &LBL::SetCurrentBrightnessMapping, "SetCurrentBrightnessMapping"},
        FunctionInfo{20, &LBL::GetCurrentBrightnessMapping, "GetCurrentBrightnessMapping"},
        FunctionInfo{21, &LBL::SetCurrentAmbientLightSensorMapping, "SetCurrentAmbientLightSensorMapping"},
        FunctionInfo{22, &LBL::GetCurrentAmbientLightSensorMapping, "GetCurrentAmbientLightSensorMapping"},
        FunctionInfo{23, &LBL::IsAmbientLightSensorAvailable, "IsAmbientLightSensorAvailable"},
        FunctionInfo{24, &LBL::SetCurrentBrightnessSettingForVrMode, "SetCurrentBrightnessSettingForVrMode"},
        FunctionInfo{25, &LBL::GetCurrentBrightnessSettingForVrMode, "GetCurrentBrightnessSettingForVrMode"},
        FunctionInfo{26, &LBL::EnableVrMode, "EnableVrMode"},
        FunctionInfo{27, &LBL::DisableVrMode, "DisableVrMode"},
        FunctionInfo{28, &LBL::IsVrModeEnabled, "IsVrModeEnabled"},
        FunctionInfo{29, &LBL::IsAutoBrightnessControlSupported, "IsAutoBrightnessControlSupported"}
    );
    bool vr_mode_enabled = false;
    float current_brightness = 1.0f;
    float ambient_light_value = 0.0f;
    float current_vr_brightness = 1.0f;
    bool dimming = true;
    bool backlight_enabled = true;
    bool update_instantly = false;
    bool auto_brightness = false;
    bool auto_brightness_supported = true; // TODO(ogniK): Move to system settings
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("lbl", std::make_shared<LBL>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::LBL
