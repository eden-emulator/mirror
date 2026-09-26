// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Core {
class System;
}

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::BTM {

class IBtmSystemCore final : public ServiceFramework<IBtmSystemCore> {
public:
    explicit IBtmSystemCore(Core::System& system_);
    ~IBtmSystemCore() override;

private:
    Result StartGamepadPairing();
    Result CancelGamepadPairing();
    Result EnableRadio();
    Result DisableRadio();
    Result IsRadioEnabled(Out<bool> out_is_enabled);

    Result AcquireRadioEvent(Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetDiscoveredAudioDevice(OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices, s32 count, Out<s32> out_total);
    Result AcquireAudioDeviceConnectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);

    Result GetConnectedAudioDevices(
        Out<s32> out_count,
        OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices);

    Result GetPairedAudioDevices(
        Out<s32> out_count,
        OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices);

    Result RequestAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid);
    Result CancelAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmSystemCore::StartGamepadPairing>, "StartGamepadPairing"},
        FunctionInfo{1, C<&IBtmSystemCore::CancelGamepadPairing>, "CancelGamepadPairing"},
        FunctionInfo{2, nullptr, "ClearGamepadPairingDatabase"},
        FunctionInfo{3, nullptr, "GetPairedGamepadCount"},
        FunctionInfo{4, C<&IBtmSystemCore::EnableRadio>, "EnableRadio"},
        FunctionInfo{5, C<&IBtmSystemCore::DisableRadio>, "DisableRadio"},
        FunctionInfo{6, C<&IBtmSystemCore::IsRadioEnabled>, "IsRadioEnabled"},
        FunctionInfo{7, C<&IBtmSystemCore::AcquireRadioEvent>, "AcquireRadioEvent"},
        FunctionInfo{8, nullptr, "AcquireGamepadPairingEvent"},
        FunctionInfo{9, nullptr, "IsGamepadPairingStarted"},
        FunctionInfo{10, nullptr, "StartAudioDeviceDiscovery"},
        FunctionInfo{11, nullptr, "StopAudioDeviceDiscovery"},
        FunctionInfo{12, nullptr, "IsDiscoveryingAudioDevice"},
        FunctionInfo{13, C<&IBtmSystemCore::GetDiscoveredAudioDevice>, "GetDiscoveredAudioDevice"},
        FunctionInfo{14, C<&IBtmSystemCore::AcquireAudioDeviceConnectionEvent>, "AcquireAudioDeviceConnectionEvent"},
        FunctionInfo{15, nullptr, "ConnectAudioDevice"},
        FunctionInfo{16, nullptr, "IsConnectingAudioDevice"},
        FunctionInfo{17, C<&IBtmSystemCore::GetConnectedAudioDevices>, "GetConnectedAudioDevices"},
        FunctionInfo{18, nullptr, "DisconnectAudioDevice"},
        FunctionInfo{19, nullptr, "AcquirePairedAudioDeviceInfoChangedEvent"},
        FunctionInfo{20, C<&IBtmSystemCore::GetPairedAudioDevices>, "GetPairedAudioDevices"},
        FunctionInfo{21, nullptr, "RemoveAudioDevicePairing"},
        FunctionInfo{22, C<&IBtmSystemCore::RequestAudioDeviceConnectionRejection>, "RequestAudioDeviceConnectionRejection"},
        FunctionInfo{23, C<&IBtmSystemCore::CancelAudioDeviceConnectionRejection>, "CancelAudioDeviceConnectionRejection"}
    );
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* radio_event;
    Kernel::KEvent* audio_device_connection_event;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

} // namespace Service::BTM
