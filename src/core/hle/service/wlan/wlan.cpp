// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/wlan/wlan.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/server_manager.h"

namespace Service::WLAN {

class ILocalManager final : public ServiceFramework<ILocalManager> {
public:
    explicit ILocalManager(Core::System& system_)
        : ServiceFramework{system_, "wlan:lcl"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "OpenMasterMode" },
            FunctionInfo{0, nullptr, "OpenMode_2" },
            FunctionInfo{1, nullptr, "CloseMasterMode" },
            FunctionInfo{1, nullptr, "CloseMode_2" },
            FunctionInfo{2, nullptr, "OpenClientMode" },
            FunctionInfo{2, nullptr, "GetMacAddress_2" },
            FunctionInfo{3, nullptr, "CloseClientMode" },
            FunctionInfo{3, nullptr, "CreateBss" },
            FunctionInfo{4, nullptr, "OpenSpectatorMode" },
            FunctionInfo{4, nullptr, "DestroyBss" },
            FunctionInfo{5, nullptr, "CloseSpectatorMode" },
            FunctionInfo{5, nullptr, "StartScan_2" },
            FunctionInfo{6, nullptr, "GetMacAddress_2" },
            FunctionInfo{6, nullptr, "StopScan_2" },
            FunctionInfo{7, nullptr, "CreateBss" },
            FunctionInfo{7, nullptr, "Connect_2" },
            FunctionInfo{8, nullptr, "DestroyBss" },
            FunctionInfo{8, nullptr, "CancelConnect_2" },
            FunctionInfo{9, nullptr, "StartScan_2" },
            FunctionInfo{9, nullptr, "Join" },
            FunctionInfo{10, nullptr, "StopScan_2" },
            FunctionInfo{10, nullptr, "CancelJoin" },
            FunctionInfo{11, nullptr, "Connect_2" },
            FunctionInfo{11, nullptr, "Disconnect_2" },
            FunctionInfo{12, nullptr, "CancelConnect_2" },
            FunctionInfo{12, nullptr, "SetBeaconLostCount" },
            FunctionInfo{13, nullptr, "Join" },
            FunctionInfo{13, nullptr, "GetSystemEvent_2" },
            FunctionInfo{14, nullptr, "CancelJoin" },
            FunctionInfo{14, nullptr, "GetConnectionStatus_2" },
            FunctionInfo{15, nullptr, "Disconnect_2" },
            FunctionInfo{15, nullptr, "GetClientStatus" },
            FunctionInfo{16, nullptr, "SetBeaconLostCount" },
            FunctionInfo{16, nullptr, "GetBssIndicationEvent" },
            FunctionInfo{17, nullptr, "GetSystemEvent_2" },
            FunctionInfo{17, nullptr, "GetBssIndicationInfo" },
            FunctionInfo{18, nullptr, "GetConnectionStatus_2" },
            FunctionInfo{18, nullptr, "GetState_2" },
            FunctionInfo{19, nullptr, "GetClientStatus" },
            FunctionInfo{19, nullptr, "GetAllowedChannels" },
            FunctionInfo{20, nullptr, "GetBssIndicationEvent" },
            FunctionInfo{20, nullptr, "AddIe" },
            FunctionInfo{21, nullptr, "GetBssIndicationInfo" },
            FunctionInfo{21, nullptr, "DeleteIe" },
            FunctionInfo{22, nullptr, "GetState_2" },
            FunctionInfo{22, nullptr, "PutFrameRaw" },
            FunctionInfo{23, nullptr, "GetAllowedChannels" },
            FunctionInfo{23, nullptr, "CancelGetFrame" },
            FunctionInfo{24, nullptr, "AddIe" },
            FunctionInfo{24, nullptr, "CreateRxEntry" },
            FunctionInfo{25, nullptr, "DeleteIe" },
            FunctionInfo{25, nullptr, "DeleteRxEntry" },
            FunctionInfo{26, nullptr, "PutFrameRaw" },
            FunctionInfo{26, nullptr, "AddEthertypeToRxEntry" },
            FunctionInfo{27, nullptr, "CancelGetFrame" },
            FunctionInfo{27, nullptr, "DeleteEthertypeFromRxEntry" },
            FunctionInfo{28, nullptr, "CreateRxEntry" },
            FunctionInfo{28, nullptr, "AddMatchingDataToRxEntry" },
            FunctionInfo{29, nullptr, "DeleteRxEntry" },
            FunctionInfo{29, nullptr, "RemoveMatchingDataFromRxEntry" },
            FunctionInfo{30, nullptr, "AddEthertypeToRxEntry" },
            FunctionInfo{30, nullptr, "GetScanResult_2" },
            FunctionInfo{31, nullptr, "DeleteEthertypeFromRxEntry" },
            FunctionInfo{31, nullptr, "PutActionFrameOneShot" },
            FunctionInfo{32, nullptr, "AddMatchingDataToRxEntry" },
            FunctionInfo{32, nullptr, "SetActionFrameWithBeacon" },
            FunctionInfo{33, nullptr, "RemoveMatchingDataFromRxEntry" },
            FunctionInfo{33, nullptr, "CancelActionFrameWithBeacon" },
            FunctionInfo{34, nullptr, "GetScanResult_2" },
            FunctionInfo{34, nullptr, "CreateRxEntryForActionFrame" },
            FunctionInfo{35, nullptr, "PutActionFrameOneShot" },
            FunctionInfo{35, nullptr, "DeleteRxEntryForActionFrame" },
            FunctionInfo{36, nullptr, "SetActionFrameWithBeacon" },
            FunctionInfo{36, nullptr, "AddSubtypeToRxEntryForActionFrame" },
            FunctionInfo{37, nullptr, "CancelActionFrameWithBeacon" },
            FunctionInfo{37, nullptr, "DeleteSubtypeFromRxEntryForActionFrame" },
            FunctionInfo{38, nullptr, "CreateRxEntryForActionFrame" },
            FunctionInfo{38, nullptr, "CancelGetActionFrame" },
            FunctionInfo{39, nullptr, "DeleteRxEntryForActionFrame" },
            FunctionInfo{39, nullptr, "GetRssi_2" },
            FunctionInfo{40, nullptr, "AddSubtypeToRxEntryForActionFrame" },
            FunctionInfo{40, nullptr, "SetMaxAssociationNumber" },
            FunctionInfo{41, nullptr, "DeleteSubtypeFromRxEntryForActionFrame" },
            FunctionInfo{41, nullptr, "Cmd41" },
            FunctionInfo{42, nullptr, "CancelGetActionFrame" },
            FunctionInfo{42, nullptr, "Cmd42" },
            FunctionInfo{43, nullptr, "GetRssi_2" },
            FunctionInfo{43, nullptr, "Cmd43" },
            FunctionInfo{44, nullptr, "SetMaxAssociationNumber" },
            FunctionInfo{45, nullptr, "OpenLcsMasterMode" },
            FunctionInfo{46, nullptr, "CloseLcsMasterMode" },
            FunctionInfo{47, nullptr, "OpenLcsClientMode" },
            FunctionInfo{48, nullptr, "CloseLcsClientMode" },
            FunctionInfo{49, nullptr, "GetChannelStats" },
            FunctionInfo{50, nullptr, "Cmd50" },
            FunctionInfo{51, nullptr, "Cmd51" },
            FunctionInfo{52, nullptr, "Cmd52" },
        };
        RegisterHandlers(functions);
    }
};

class ILocalGetFrame final : public ServiceFramework<ILocalGetFrame> {
public:
    explicit ILocalGetFrame(Core::System& system_)
        : ServiceFramework{system_, "wlan:lg"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "GetFrameRaw" },
        };
        RegisterHandlers(functions);
    }
};

class ILocalGetActionFrame final : public ServiceFramework<ILocalGetActionFrame> {
public:
    explicit ILocalGetActionFrame(Core::System& system_)
        : ServiceFramework{system_, "wlan:lga"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "GetActionFrame" },
        };
        RegisterHandlers(functions);
    }
};

class ISocketGetFrame final : public ServiceFramework<ISocketGetFrame> {
public:
    explicit ISocketGetFrame(Core::System& system_)
        : ServiceFramework{system_, "wlan:sg"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "GetFrameRaw" },
        };
        RegisterHandlers(functions);
    }
};

class ISocketManager final : public ServiceFramework<ISocketManager> {
public:
    explicit ISocketManager(Core::System& system_)
        : ServiceFramework{system_, "wlan:soc"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "PutFrameRaw_2" },
            FunctionInfo{1, nullptr, "CancelGetFrame_2" },
            FunctionInfo{2, nullptr, "CreateRxEntry_2" },
            FunctionInfo{3, nullptr, "DeleteRxEntry_2" },
            FunctionInfo{4, nullptr, "AddEthertypeToRxEntry_2" },
            FunctionInfo{5, nullptr, "DeleteEthertypeFromRxEntry_2" },
            FunctionInfo{6, nullptr, "GetMacAddress_3" },
            FunctionInfo{7, nullptr, "SwitchTsfTimerFunction" },
            FunctionInfo{8, nullptr, "GetDeltaTimeBetweenSystemAndTsf" },
            FunctionInfo{9, nullptr, "RegisterSharedMemory" },
            FunctionInfo{10, nullptr, "UnregisterSharedMemory" },
            FunctionInfo{11, nullptr, "EnableSharedMemory" },
            FunctionInfo{12, nullptr, "SetMulticastFilter" },
        };
        RegisterHandlers(functions);
    }
};

class IDetectManager final : public ServiceFramework<IDetectManager> {
public:
    explicit IDetectManager(Core::System& system_)
        : ServiceFramework{system_, "wlan:dtc"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "Cmd0" },
            FunctionInfo{1, nullptr, "Cmd1" },
            FunctionInfo{2, nullptr, "Cmd2" },
            FunctionInfo{3, nullptr, "Cmd3" },
            FunctionInfo{4, nullptr, "Cmd4" },
            FunctionInfo{5, nullptr, "Cmd5" },
            FunctionInfo{6, nullptr, "Cmd6" },
            FunctionInfo{7, nullptr, "Cmd7" },
            FunctionInfo{8, nullptr, "Cmd8" },
            FunctionInfo{9, nullptr, "Cmd9" },
            FunctionInfo{10, nullptr, "Cmd10" },
            FunctionInfo{11, nullptr, "Cmd11" },
            FunctionInfo{12, nullptr, "Cmd12" },
            FunctionInfo{13, nullptr, "Cmd13" },
            FunctionInfo{14, nullptr, "Cmd14" },
            FunctionInfo{15, nullptr, "Cmd15" },
            FunctionInfo{16, nullptr, "Cmd16" },
            FunctionInfo{17, nullptr, "Cmd17" },
            FunctionInfo{18, nullptr, "Cmd18" },
            FunctionInfo{19, nullptr, "Cmd19" },
            FunctionInfo{20, nullptr, "Cmd20" },
            FunctionInfo{21, nullptr, "Cmd21" },
            FunctionInfo{22, nullptr, "Cmd22" },
            FunctionInfo{23, nullptr, "Cmd23" },
            FunctionInfo{24, nullptr, "Cmd24" },
            FunctionInfo{25, nullptr, "Cmd25" },
            FunctionInfo{26, nullptr, "Cmd26" },
            FunctionInfo{27, nullptr, "Cmd27" },
        };
        RegisterHandlers(functions);
    }
};

class IPrivateServiceCreator final : public ServiceFramework<IPrivateServiceCreator> {
public:
    explicit IPrivateServiceCreator(Core::System& system_)
        : ServiceFramework{system_, "wlan:p"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "CreateWirelessCommunicationService" },
            FunctionInfo{1, nullptr, "CreatePrivateWirelessCommunicationService" },
        };
        RegisterHandlers(functions);
    }
};

class ISfDriverServiceCreator final : public ServiceFramework<ISfDriverServiceCreator> {
public:
    explicit ISfDriverServiceCreator(Core::System& system_)
        : ServiceFramework{system_, "wlan:nd"}
    {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "CreateDriverService" },
        };
        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("wlan:lcl", std::make_shared<ILocalManager>(system), 10);
    server_manager->RegisterNamedService("wlan:lg", std::make_shared<ILocalGetFrame>(system), 10);
    server_manager->RegisterNamedService("wlan:lga", std::make_shared<ILocalGetActionFrame>(system), 10);
    server_manager->RegisterNamedService("wlan:sg", std::make_shared<ISocketGetFrame>(system), 10);
    server_manager->RegisterNamedService("wlan:soc", std::make_shared<ISocketManager>(system), 10);
    server_manager->RegisterNamedService("wlan:dtc", std::make_shared<IDetectManager>(system), 4);
    server_manager->RegisterNamedService("wlan:p", std::make_shared<IPrivateServiceCreator>(system), 30);
    server_manager->RegisterNamedService("wlan:nd", std::make_shared<ISfDriverServiceCreator>(system), 5);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::WLAN
