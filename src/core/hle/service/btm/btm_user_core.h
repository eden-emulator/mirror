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

namespace Service::BTM {

class IBtmUserCore final : public ServiceFramework<IBtmUserCore> {
public:
    explicit IBtmUserCore(Core::System& system_);
    ~IBtmUserCore() override;

private:
    Result AcquireBleScanEvent(Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result AcquireBleConnectionEvent(Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result AcquireBleServiceDiscoveryEvent(Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result AcquireBleMtuConfigEvent(Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmUserCore::AcquireBleScanEvent>, "AcquireBleScanEvent"},
        FunctionInfo{1, nullptr, "GetBleScanFilterParameter"},
        FunctionInfo{2, nullptr, "GetBleScanFilterParameter2"},
        FunctionInfo{3, nullptr, "StartBleScanForGeneral"},
        FunctionInfo{4, nullptr, "StopBleScanForGeneral"},
        FunctionInfo{5, nullptr, "GetBleScanResultsForGeneral"},
        FunctionInfo{6, nullptr, "StartBleScanForPaired"},
        FunctionInfo{7, nullptr, "StopBleScanForPaired"},
        FunctionInfo{8, nullptr, "StartBleScanForSmartDevice"},
        FunctionInfo{9, nullptr, "StopBleScanForSmartDevice"},
        FunctionInfo{10, nullptr, "GetBleScanResultsForSmartDevice"},
        FunctionInfo{17, C<&IBtmUserCore::AcquireBleConnectionEvent>, "AcquireBleConnectionEvent"},
        FunctionInfo{18, nullptr, "BleConnect"},
        FunctionInfo{19, nullptr, "BleDisconnect"},
        FunctionInfo{20, nullptr, "BleGetConnectionState"},
        FunctionInfo{21, nullptr, "AcquireBlePairingEvent"},
        FunctionInfo{22, nullptr, "BlePairDevice"},
        FunctionInfo{23, nullptr, "BleUnPairDevice"},
        FunctionInfo{24, nullptr, "BleUnPairDevice2"},
        FunctionInfo{25, nullptr, "BleGetPairedDevices"},
        FunctionInfo{26, C<&IBtmUserCore::AcquireBleServiceDiscoveryEvent>, "AcquireBleServiceDiscoveryEvent"},
        FunctionInfo{27, nullptr, "GetGattServices"},
        FunctionInfo{28, nullptr, "GetGattService"},
        FunctionInfo{29, nullptr, "GetGattIncludedServices"},
        FunctionInfo{30, nullptr, "GetBelongingGattService"},
        FunctionInfo{31, nullptr, "GetGattCharacteristics"},
        FunctionInfo{32, nullptr, "GetGattDescriptors"},
        FunctionInfo{33, C<&IBtmUserCore::AcquireBleMtuConfigEvent>, "AcquireBleMtuConfigEvent"},
        FunctionInfo{34, nullptr, "ConfigureBleMtu"},
        FunctionInfo{35, nullptr, "GetBleMtu"},
        FunctionInfo{36, nullptr, "RegisterBleGattDataPath"},
        FunctionInfo{37, nullptr, "UnregisterBleGattDataPath"}
    );
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* scan_event;
    Kernel::KEvent* connection_event;
    Kernel::KEvent* service_discovery_event;
    Kernel::KEvent* config_event;
};

} // namespace Service::BTM
