// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NIM {
class IShopServiceAsync final : public ServiceFramework<IShopServiceAsync> {
public:
    explicit IShopServiceAsync(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAsync"},
          service_context{system_, "IShopServiceAsync"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, D<&IShopServiceAsync::Cancel>, "Cancel"},
            FunctionInfo{1, D<&IShopServiceAsync::GetSize>, "GetSize"},
            FunctionInfo{2, D<&IShopServiceAsync::Read>, "Read"},
            FunctionInfo{3, D<&IShopServiceAsync::GetErrorCode>, "GetErrorCode"},
            FunctionInfo{4, D<&IShopServiceAsync::Request>, "Request"},
            FunctionInfo{5, D<&IShopServiceAsync::Prepare>, "Prepare"}
        };
        // clang-format on

        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("IShopServiceAsync:Completion");
    }

    ~IShopServiceAsync() override {
        CancelImpl();
        service_context.CloseEvent(completion_event);
    }

    Kernel::KReadableEvent* GetEvent() const {
        return &completion_event->GetReadableEvent();
    }

private:
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event;

    std::jthread worker;
    std::atomic<u32> error_code{0};

    std::mutex data_mutex;
    std::vector<u8> download_data;

    void CancelImpl() {
        worker.request_stop();
        if (worker.joinable()) {
            worker.join();
        }
    }

    Result Cancel() {
        LOG_DEBUG(Service_NIM, "called");
        CancelImpl();
        R_SUCCEED();
    }

    Result GetSize(Out<u64> out_size) {
        LOG_DEBUG(Service_NIM, "called");
        std::scoped_lock lock{data_mutex};
        *out_size = download_data.size();
        R_SUCCEED();
    }

    Result Read(Out<u64> out_size, u64 offset, OutBuffer<BufferAttr_HipcAutoSelect> out_buffer) {
        std::scoped_lock lock{data_mutex};

        u64 actual_read = 0;
        if (offset < download_data.size()) {
            actual_read = std::min<u64>(out_buffer.size(), download_data.size() - offset);
            std::memcpy(out_buffer.data(), download_data.data() + offset, actual_read);
        }

        *out_size = actual_read;
        R_SUCCEED();
    }

    Result GetErrorCode(Out<u32> out_error_code) {
        LOG_DEBUG(Service_NIM, "called");
        *out_error_code = error_code.load();
        R_SUCCEED();
    }

    Result Request() {
        LOG_DEBUG(Service_NIM, "(STUBBED) called");
        CancelImpl();

        error_code.store(0);
        completion_event->Clear(system.Kernel());

        {
            std::scoped_lock lock{data_mutex};
            download_data.clear();
        }

        worker = std::jthread([this](const std::stop_token& stop_token) {
            if (stop_token.stop_requested()) {
                error_code.store(1);
            } else {
                std::scoped_lock lock{data_mutex};
                // Dummy JSON response, else it fails...
                const std::string dummy_response = "{}";
                download_data.assign(dummy_response.begin(), dummy_response.end());
                error_code.store(0);
            }
            completion_event->Signal(system.Kernel());
        });

        R_SUCCEED();
    }

    Result Prepare(InArray<char, BufferAttr_HipcMapAlias> in_path, InArray<char, BufferAttr_HipcMapAlias> in_post) {
        LOG_DEBUG(Service_NIM, "called");
        if (!in_path.empty()) {
            std::string url(in_path.data(), in_path.size());
            LOG_INFO(Service_NIM, "Preparing request for URL: {}", url);
        }
        R_SUCCEED();
    }
};

class IShopServiceAccessor final : public ServiceFramework<IShopServiceAccessor> {
public:
    explicit IShopServiceAccessor(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAccessor"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &IShopServiceAccessor::CreateAsyncInterface, "CreateAsyncInterface"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateAsyncInterface(HLERequestContext& ctx) {LOG_DEBUG(Service_NIM, "called");
        auto async_interface = std::make_shared<IShopServiceAsync>(system);

        IPC::ResponseBuilder rb{ctx, 2, 1, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, async_interface->GetEvent());
        rb.PushIpcInterface<IShopServiceAsync>(ctx, std::move(async_interface));
    }
};

class IShopServiceAccessServer final : public ServiceFramework<IShopServiceAccessServer> {
public:
    explicit IShopServiceAccessServer(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAccessServer"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &IShopServiceAccessServer::CreateAccessorInterface, "CreateAccessorInterface"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateAccessorInterface(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessor>(ctx, system);
    }
};

class NIM final : public ServiceFramework<NIM> {
public:
    explicit NIM(Core::System& system_) : ServiceFramework{system_, "nim"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "CreateSystemUpdateTask"},
            FunctionInfo{1, nullptr, "DestroySystemUpdateTask"},
            FunctionInfo{2, nullptr, "ListSystemUpdateTask"},
            FunctionInfo{3, nullptr, "RequestSystemUpdateTaskRun"},
            FunctionInfo{4, nullptr, "GetSystemUpdateTaskInfo"},
            FunctionInfo{5, nullptr, "CommitSystemUpdateTask"},
            FunctionInfo{6, nullptr, "CreateNetworkInstallTask"},
            FunctionInfo{7, nullptr, "DestroyNetworkInstallTask"},
            FunctionInfo{8, nullptr, "ListNetworkInstallTask"},
            FunctionInfo{9, nullptr, "RequestNetworkInstallTaskRun"},
            FunctionInfo{10, nullptr, "GetNetworkInstallTaskInfo"},
            FunctionInfo{11, nullptr, "CommitNetworkInstallTask"},
            FunctionInfo{12, nullptr, "RequestLatestSystemUpdateMeta"},
            FunctionInfo{14, nullptr, "ListApplicationNetworkInstallTask"},
            FunctionInfo{15, nullptr, "ListNetworkInstallTaskContentMeta"},
            FunctionInfo{16, nullptr, "RequestLatestVersion"},
            FunctionInfo{17, nullptr, "SetNetworkInstallTaskAttribute"},
            FunctionInfo{18, nullptr, "AddNetworkInstallTaskContentMeta"},
            FunctionInfo{19, nullptr, "GetDownloadedSystemDataPath"},
            FunctionInfo{20, nullptr, "CalculateNetworkInstallTaskRequiredSize"},
            FunctionInfo{21, nullptr, "IsExFatDriverIncluded"},
            FunctionInfo{22, nullptr, "GetBackgroundDownloadStressTaskInfo"},
            FunctionInfo{23, nullptr, "RequestDeviceAuthenticationToken"},
            FunctionInfo{24, nullptr, "RequestGameCardRegistrationStatus"},
            FunctionInfo{25, nullptr, "RequestRegisterGameCard"},
            FunctionInfo{26, nullptr, "RequestRegisterNotificationToken"},
            FunctionInfo{27, nullptr, "RequestDownloadTaskList"},
            FunctionInfo{28, nullptr, "RequestApplicationControl"},
            FunctionInfo{29, nullptr, "RequestLatestApplicationControl"},
            FunctionInfo{30, nullptr, "RequestVersionList"},
            FunctionInfo{31, nullptr, "CreateApplyDeltaTask"},
            FunctionInfo{32, nullptr, "DestroyApplyDeltaTask"},
            FunctionInfo{33, nullptr, "ListApplicationApplyDeltaTask"},
            FunctionInfo{34, nullptr, "RequestApplyDeltaTaskRun"},
            FunctionInfo{35, nullptr, "GetApplyDeltaTaskInfo"},
            FunctionInfo{36, nullptr, "ListApplyDeltaTask"},
            FunctionInfo{37, nullptr, "CommitApplyDeltaTask"},
            FunctionInfo{38, nullptr, "CalculateApplyDeltaTaskRequiredSize"},
            FunctionInfo{39, nullptr, "PrepareShutdown"},
            FunctionInfo{40, nullptr, "ListApplyDeltaTask"},
            FunctionInfo{41, nullptr, "ClearNotEnoughSpaceStateOfApplyDeltaTask"},
            FunctionInfo{42, nullptr, "CreateApplyDeltaTaskFromDownloadTask"},
            FunctionInfo{43, nullptr, "GetBackgroundApplyDeltaStressTaskInfo"},
            FunctionInfo{44, nullptr, "GetApplyDeltaTaskRequiredStorage"},
            FunctionInfo{45, nullptr, "CalculateNetworkInstallTaskContentsSize"},
            FunctionInfo{46, nullptr, "PrepareShutdownForSystemUpdate"},
            FunctionInfo{47, nullptr, "FindMaxRequiredApplicationVersionOfTask"},
            FunctionInfo{48, nullptr, "CommitNetworkInstallTaskPartially"},
            FunctionInfo{49, nullptr, "ListNetworkInstallTaskCommittedContentMeta"},
            FunctionInfo{50, nullptr, "ListNetworkInstallTaskNotCommittedContentMeta"},
            FunctionInfo{51, nullptr, "FindMaxRequiredSystemVersionOfTask"},
            FunctionInfo{52, nullptr, "GetNetworkInstallTaskErrorContext"},
            FunctionInfo{53, nullptr, "CreateLocalCommunicationReceiveApplicationTask"},
            FunctionInfo{54, nullptr, "DestroyLocalCommunicationReceiveApplicationTask"},
            FunctionInfo{55, nullptr, "ListLocalCommunicationReceiveApplicationTask"},
            FunctionInfo{56, nullptr, "RequestLocalCommunicationReceiveApplicationTaskRun"},
            FunctionInfo{57, nullptr, "GetLocalCommunicationReceiveApplicationTaskInfo"},
            FunctionInfo{58, nullptr, "CommitLocalCommunicationReceiveApplicationTask"},
            FunctionInfo{59, nullptr, "ListLocalCommunicationReceiveApplicationTaskContentMeta"},
            FunctionInfo{60, nullptr, "CreateLocalCommunicationSendApplicationTask"},
            FunctionInfo{61, nullptr, "RequestLocalCommunicationSendApplicationTaskRun"},
            FunctionInfo{62, nullptr, "GetLocalCommunicationReceiveApplicationTaskErrorContext"},
            FunctionInfo{63, nullptr, "GetLocalCommunicationSendApplicationTaskInfo"},
            FunctionInfo{64, nullptr, "DestroyLocalCommunicationSendApplicationTask"},
            FunctionInfo{65, nullptr, "GetLocalCommunicationSendApplicationTaskErrorContext"},
            FunctionInfo{66, nullptr, "CalculateLocalCommunicationReceiveApplicationTaskRequiredSize"},
            FunctionInfo{67, nullptr, "ListApplicationLocalCommunicationReceiveApplicationTask"},
            FunctionInfo{68, nullptr, "ListApplicationLocalCommunicationSendApplicationTask"},
            FunctionInfo{69, nullptr, "CreateLocalCommunicationReceiveSystemUpdateTask"},
            FunctionInfo{70, nullptr, "DestroyLocalCommunicationReceiveSystemUpdateTask"},
            FunctionInfo{71, nullptr, "ListLocalCommunicationReceiveSystemUpdateTask"},
            FunctionInfo{72, nullptr, "RequestLocalCommunicationReceiveSystemUpdateTaskRun"},
            FunctionInfo{73, nullptr, "GetLocalCommunicationReceiveSystemUpdateTaskInfo"},
            FunctionInfo{74, nullptr, "CommitLocalCommunicationReceiveSystemUpdateTask"},
            FunctionInfo{75, nullptr, "GetLocalCommunicationReceiveSystemUpdateTaskErrorContext"},
            FunctionInfo{76, nullptr, "CreateLocalCommunicationSendSystemUpdateTask"},
            FunctionInfo{77, nullptr, "RequestLocalCommunicationSendSystemUpdateTaskRun"},
            FunctionInfo{78, nullptr, "GetLocalCommunicationSendSystemUpdateTaskInfo"},
            FunctionInfo{79, nullptr, "DestroyLocalCommunicationSendSystemUpdateTask"},
            FunctionInfo{80, nullptr, "GetLocalCommunicationSendSystemUpdateTaskErrorContext"},
            FunctionInfo{81, nullptr, "ListLocalCommunicationSendSystemUpdateTask"},
            FunctionInfo{82, nullptr, "GetReceivedSystemDataPath"},
            FunctionInfo{83, nullptr, "CalculateApplyDeltaTaskOccupiedSize"},
            FunctionInfo{84, nullptr, "ReloadErrorSimulation"},
            FunctionInfo{85, nullptr, "ListNetworkInstallTaskContentMetaFromInstallMeta"},
            FunctionInfo{86, nullptr, "ListNetworkInstallTaskOccupiedSize"},
            FunctionInfo{87, nullptr, "RequestQueryAvailableELicenses"},
            FunctionInfo{88, nullptr, "RequestAssignELicenses"},
            FunctionInfo{89, nullptr, "RequestExtendELicenses"},
            FunctionInfo{90, nullptr, "RequestSyncELicenses"},
            FunctionInfo{91, nullptr, "Unknown91"}, //6.0.0-14.1.2
            FunctionInfo{92, nullptr, "Unknown92"}, //21.0.0+
            FunctionInfo{93, nullptr, "RequestReportActiveELicenses"},
            FunctionInfo{94, nullptr, "RequestReportActiveELicensesPassively"},
            FunctionInfo{95, nullptr, "RequestRegisterDynamicRightsNotificationToken"},
            FunctionInfo{96, nullptr, "RequestAssignAllDeviceLinkedELicenses"},
            FunctionInfo{97, nullptr, "RequestRevokeAllELicenses"},
            FunctionInfo{98, nullptr, "RequestPrefetchForDynamicRights"},
            FunctionInfo{99, nullptr, "CreateNetworkInstallTask"},
            FunctionInfo{100, nullptr, "ListNetworkInstallTaskRightsIds"},
            FunctionInfo{101, nullptr, "RequestDownloadETickets"},
            FunctionInfo{102, nullptr, "RequestQueryDownloadableContents"},
            FunctionInfo{103, nullptr, "DeleteNetworkInstallTaskContentMeta"},
            FunctionInfo{104, nullptr, "RequestIssueEdgeTokenForDebug"},
            FunctionInfo{105, nullptr, "RequestQueryAvailableELicenses2"},
            FunctionInfo{106, nullptr, "RequestAssignELicenses2"},
            FunctionInfo{107, nullptr, "GetNetworkInstallTaskStateCounter"},
            FunctionInfo{108, nullptr, "InvalidateDynamicRightsNaIdTokenCacheForDebug"},
            FunctionInfo{109, nullptr, "ListNetworkInstallTaskPartialInstallContentMeta"},
            FunctionInfo{110, nullptr, "ListNetworkInstallTaskRightsIdsFromIndex"},
            FunctionInfo{111, nullptr, "AddNetworkInstallTaskContentMetaForUser"},
            FunctionInfo{112, nullptr, "RequestAssignELicensesAndDownloadETickets"},
            FunctionInfo{113, nullptr, "RequestQueryAvailableCommonELicenses"},
            FunctionInfo{114, nullptr, "SetNetworkInstallTaskExtendedAttribute"},
            FunctionInfo{115, nullptr, "GetNetworkInstallTaskExtendedAttribute"},
            FunctionInfo{116, nullptr, "GetAllocatorInfo"},
            FunctionInfo{117, nullptr, "RequestQueryDownloadableContentsByApplicationId"},
            FunctionInfo{118, nullptr, "MarkNoDownloadRightsErrorResolved"},
            FunctionInfo{119, nullptr, "GetApplyDeltaTaskAllAppliedContentMeta"},
            FunctionInfo{120, nullptr, "PrioritizeNetworkInstallTask"},
            FunctionInfo{121, nullptr, "RequestQueryAvailableCommonELicenses2"},
            FunctionInfo{122, nullptr, "RequestAssignCommonELicenses"},
            FunctionInfo{123, nullptr, "RequestAssignCommonELicenses2"},
            FunctionInfo{124, nullptr, "IsNetworkInstallTaskFrontOfQueue"},
            FunctionInfo{125, nullptr, "PrioritizeApplyDeltaTask"},
            FunctionInfo{126, nullptr, "RerouteDownloadingPatch"},
            FunctionInfo{127, nullptr, "UnmarkNoDownloadRightsErrorResolved"},
            FunctionInfo{128, nullptr, "RequestContentsSize"},
            FunctionInfo{129, nullptr, "RequestContentsAuthorizationToken"},
            FunctionInfo{130, nullptr, "RequestCdnVendorDiscovery"},
            FunctionInfo{131, nullptr, "RefreshDebugAvailability"},
            FunctionInfo{132, nullptr, "ClearResponseSimulationEntry"},
            FunctionInfo{133, nullptr, "RegisterResponseSimulationEntry"},
            FunctionInfo{134, nullptr, "GetProcessedCdnVendors"},
            FunctionInfo{135, nullptr, "RefreshRuntimeBehaviorsForDebug"},
            FunctionInfo{136, nullptr, "RequestOnlineSubscriptionFreeTrialAvailability"},
            FunctionInfo{137, nullptr, "GetNetworkInstallTaskContentMetaCount"},
            FunctionInfo{138, nullptr, "RequestRevokeELicenses"},
            FunctionInfo{139, nullptr, "EnableNetworkConnectionToUseApplicationCore"},
            FunctionInfo{140, nullptr, "DisableNetworkConnectionToUseApplicationCore"},
            FunctionInfo{141, nullptr, "IsNetworkConnectionEnabledToUseApplicationCore"},
            FunctionInfo{142, nullptr, "RequestCheckSafeSystemVersion"},
            FunctionInfo{143, nullptr, "RequestApplicationIcon"},
            FunctionInfo{144, nullptr, "RequestDownloadIdbeIconFile"},
            FunctionInfo{147, nullptr, "Unknown147"}, //18.0.0+
            FunctionInfo{148, nullptr, "Unknown148"}, //18.0.0+
            FunctionInfo{150, nullptr, "Unknown150"}, //19.0.0+
            FunctionInfo{151, nullptr, "Unknown151"}, //20.0.0+
            FunctionInfo{152, nullptr, "Unknown152"}, //20.0.0+
            FunctionInfo{153, nullptr, "Unknown153"}, //20.0.0+
            FunctionInfo{154, nullptr, "Unknown154"}, //20.0.0+
            FunctionInfo{155, nullptr, "Unknown155"}, //20.0.0+
            FunctionInfo{156, nullptr, "Unknown156"}, //20.0.0+
            FunctionInfo{157, nullptr, "Unknown157"}, //20.0.0+
            FunctionInfo{158, nullptr, "Unknown158"}, //20.0.0+
            FunctionInfo{159, nullptr, "Unknown159"}, //20.0.0+
            FunctionInfo{160, nullptr, "Unknown160"}, //20.0.0+
            FunctionInfo{161, nullptr, "Unknown161"}, //20.0.0+
            FunctionInfo{162, nullptr, "Unknown162"}, //20.0.0+
            FunctionInfo{163, nullptr, "Unknown163"}, //20.0.0+
            FunctionInfo{164, nullptr, "Unknown164"}, //20.0.0+
            FunctionInfo{165, nullptr, "Unknown165"}, //20.0.0+
            FunctionInfo{166, nullptr, "Unknown166"}, //20.0.0+
            FunctionInfo{167, nullptr, "Unknown167"}, //20.0.0+
            FunctionInfo{168, nullptr, "Unknown168"}, //20.0.0+
            FunctionInfo{169, nullptr, "Unknown169"}, //20.0.0+
            FunctionInfo{170, nullptr, "Unknown170"}, //20.0.0+
            FunctionInfo{171, nullptr, "Unknown171"}, //20.0.0+
            FunctionInfo{172, nullptr, "Unknown172"}, //20.0.0+
            FunctionInfo{173, nullptr, "Unknown173"}, //20.0.0+
            FunctionInfo{174, nullptr, "Unknown174"}, //20.0.0+
            FunctionInfo{175, nullptr, "Unknown175"}, //20.0.0+
            FunctionInfo{176, nullptr, "Unknown176"}, //20.0.0+
            FunctionInfo{177, nullptr, "Unknown177"}, //20.0.0+
            FunctionInfo{2000, nullptr, "Unknown2000"}, //20.0.0+
            FunctionInfo{2001, nullptr, "Unknown2001"}, //20.0.0+
            FunctionInfo{2002, nullptr, "Unknown2002"}, //20.0.0+
            FunctionInfo{2003, nullptr, "Unknown2003"}, //20.0.0+
            FunctionInfo{2004, nullptr, "Unknown2004"}, //20.0.0+
            FunctionInfo{2007, nullptr, "Unknown2007"}, //20.0.0+
            FunctionInfo{2011, nullptr, "Unknown2011"}, //20.0.0+
            FunctionInfo{2012, nullptr, "Unknown2012"}, //20.0.0+
            FunctionInfo{2013, nullptr, "Unknown2013"}, //20.0.0+
            FunctionInfo{2014, nullptr, "Unknown2014"}, //20.0.0+
            FunctionInfo{2015, nullptr, "Unknown2015"}, //20.0.0+
            FunctionInfo{2016, nullptr, "Unknown2016"}, //20.0.0+
            FunctionInfo{2017, nullptr, "Unknown2017"}, //20.0.0+
            FunctionInfo{2018, nullptr, "Unknown2018"}, //20.0.0+
            FunctionInfo{2019, nullptr, "Unknown2019"}, //20.0.0+
            FunctionInfo{2020, nullptr, "Unknown2020"}, //20.0.0+
            FunctionInfo{2021, nullptr, "Unknown2021"}, //20.0.0+
            FunctionInfo{2022, nullptr, "Unknown2022"}, //20.0.0+
            FunctionInfo{2023, nullptr, "Unknown2023"}, //20.0.0+
            FunctionInfo{2024, nullptr, "Unknown2024"}, //20.0.0+
            FunctionInfo{2025, nullptr, "Unknown2025"}, //20.0.0+
            FunctionInfo{2026, nullptr, "Unknown2026"}, //20.0.0+
            FunctionInfo{2027, nullptr, "Unknown2027"}, //20.0.0+
            FunctionInfo{2028, nullptr, "Unknown2028"}, //20.0.0+
            FunctionInfo{2029, nullptr, "Unknown2029"}, //20.0.0+
            FunctionInfo{2030, nullptr, "Unknown2030"}, //20.0.0+
            FunctionInfo{2031, nullptr, "Unknown2031"}, //20.0.0+
            FunctionInfo{2032, nullptr, "Unknown2032"}, //20.0.0+
            FunctionInfo{2033, nullptr, "Unknown2033"}, //20.0.0+
            FunctionInfo{2034, nullptr, "Unknown2034"}, //20.0.0+
            FunctionInfo{2035, nullptr, "Unknown2035"}, //20.0.0+
            FunctionInfo{2036, nullptr, "Unknown2036"}, //20.0.0+
            FunctionInfo{2037, nullptr, "Unknown2037"}, //20.0.0+
            FunctionInfo{2038, nullptr, "Unknown2038"}, //20.0.0+
            FunctionInfo{2039, nullptr, "Unknown2039"}, //20.0.0+
            FunctionInfo{2040, nullptr, "Unknown2040"}, //20.0.0+
            FunctionInfo{2041, nullptr, "Unknown2041"}, //20.0.0+
            FunctionInfo{2042, nullptr, "Unknown2042"}, //20.0.0+
            FunctionInfo{2043, nullptr, "Unknown2043"}, //20.0.0+
            FunctionInfo{2044, nullptr, "Unknown2044"}, //20.0.0+
            FunctionInfo{2045, nullptr, "Unknown2045"}, //20.0.0+
            FunctionInfo{2046, nullptr, "Unknown2046"}, //20.0.0+
            FunctionInfo{2047, nullptr, "Unknown2047"}, //20.0.0+
            FunctionInfo{2048, nullptr, "Unknown2048"}, //20.0.0+
            FunctionInfo{2049, nullptr, "Unknown2049"}, //20.0.0+
            FunctionInfo{2050, nullptr, "Unknown2050"}, //20.0.0+
            FunctionInfo{2051, nullptr, "Unknown2051"}, //20.0.0+
            FunctionInfo{3000, nullptr, "RequestLatestApplicationIcon"}, //17.0.0+
            FunctionInfo{3001, nullptr, "RequestDownloadIdbeLatestIconFile"}, //17.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class NIM_ECA final : public ServiceFramework<NIM_ECA> {
public:
    explicit NIM_ECA(Core::System& system_) : ServiceFramework{system_, "nim:eca"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &NIM_ECA::CreateServerInterface, "CreateServerInterface"},
            FunctionInfo{1, nullptr, "RefreshDebugAvailability"},
            FunctionInfo{2, nullptr, "ClearDebugResponse"},
            FunctionInfo{3, nullptr, "RegisterDebugResponse"},
            FunctionInfo{4, &NIM_ECA::IsLargeResourceAvailable, "IsLargeResourceAvailable"},
            FunctionInfo{5, &NIM_ECA::CreateServerInterface2, "CreateServerInterface2"} // 17.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateServerInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessServer>(ctx, system);
    }

    void IsLargeResourceAvailable(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const auto unknown{rp.Pop<u64>()};

        LOG_INFO(Service_NIM, "(STUBBED) called, unknown={}", unknown);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false);
    }

    void CreateServerInterface2(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "(STUBBED) called.");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessServer>(ctx, system);
    }
};

class NIM_SHP final : public ServiceFramework<NIM_SHP> {
public:
    explicit NIM_SHP(Core::System& system_) : ServiceFramework{system_, "nim:shp"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "RequestDeviceAuthenticationToken"},
            FunctionInfo{1, nullptr, "RequestCachedDeviceAuthenticationToken"},
            FunctionInfo{2, nullptr, "RequestEdgeToken"},
            FunctionInfo{3, nullptr, "RequestCachedEdgeToken"},
            FunctionInfo{100, nullptr, "RequestRegisterDeviceAccount"},
            FunctionInfo{101, nullptr, "RequestUnregisterDeviceAccount"},
            FunctionInfo{102, nullptr, "RequestDeviceAccountStatus"},
            FunctionInfo{103, nullptr, "GetDeviceAccountInfo"},
            FunctionInfo{104, nullptr, "RequestDeviceRegistrationInfo"},
            FunctionInfo{105, nullptr, "RequestTransferDeviceAccount"},
            FunctionInfo{106, nullptr, "RequestSyncRegistration"},
            FunctionInfo{107, nullptr, "IsOwnDeviceId"},
            FunctionInfo{200, nullptr, "RequestRegisterNotificationToken"},
            FunctionInfo{300, nullptr, "RequestUnlinkDevice"},
            FunctionInfo{301, nullptr, "RequestUnlinkDeviceIntegrated"},
            FunctionInfo{302, nullptr, "RequestLinkDevice"},
            FunctionInfo{303, nullptr, "HasDeviceLink"},
            FunctionInfo{304, nullptr, "RequestUnlinkDeviceAll"},
            FunctionInfo{305, nullptr, "RequestCreateVirtualAccount"},
            FunctionInfo{306, nullptr, "RequestDeviceLinkStatus"},
            FunctionInfo{400, nullptr, "GetAccountByVirtualAccount"},
            FunctionInfo{401, nullptr, "GetVirtualAccount"},
            FunctionInfo{500, nullptr, "RequestSyncTicketLegacy"},
            FunctionInfo{501, nullptr, "RequestDownloadTicket"},
            FunctionInfo{502, nullptr, "RequestDownloadTicketForPrepurchasedContents"},
            FunctionInfo{503, nullptr, "RequestSyncTicket"},
            FunctionInfo{504, nullptr, "RequestDownloadTicketForPrepurchasedContents2"},
            FunctionInfo{505, nullptr, "RequestDownloadTicketForPrepurchasedContentsForAccount"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IEnsureNetworkClockAvailabilityService final
    : public ServiceFramework<IEnsureNetworkClockAvailabilityService> {
public:
    explicit IEnsureNetworkClockAvailabilityService(Core::System& system_)
        : ServiceFramework{system_, "IEnsureNetworkClockAvailabilityService"},
          service_context{system_, "IEnsureNetworkClockAvailabilityService"} {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &IEnsureNetworkClockAvailabilityService::StartTask, "StartTask"},
            FunctionInfo{1, &IEnsureNetworkClockAvailabilityService::GetFinishNotificationEvent,
             "GetFinishNotificationEvent"},
            FunctionInfo{2, &IEnsureNetworkClockAvailabilityService::GetResult, "GetResult"},
            FunctionInfo{3, &IEnsureNetworkClockAvailabilityService::Cancel, "Cancel"},
            FunctionInfo{4, &IEnsureNetworkClockAvailabilityService::IsProcessing, "IsProcessing"},
            FunctionInfo{5, &IEnsureNetworkClockAvailabilityService::GetServerTime, "GetServerTime"}
        };
        RegisterHandlers(functions);

        finished_event =
            service_context.CreateEvent("IEnsureNetworkClockAvailabilityService:FinishEvent");
    }

    ~IEnsureNetworkClockAvailabilityService() override {
        service_context.CloseEvent(finished_event);
    }

private:
    void StartTask(HLERequestContext& ctx) {
        // No need to connect to the internet, just finish the task straight away.
        LOG_DEBUG(Service_NIM, "called");
        finished_event->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFinishNotificationEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, finished_event->GetReadableEvent());
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");
        finished_event->Clear(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsProcessing(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushRaw<u32>(0); // We instantly process the request
    }

    void GetServerTime(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        const s64 server_time{std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count()};
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<s64>(server_time);
    }

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* finished_event;
};

class NTC final : public ServiceFramework<NTC> {
public:
    explicit NTC(Core::System& system_) : ServiceFramework{system_, "ntc"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &NTC::OpenEnsureNetworkClockAvailabilityService, "OpenEnsureNetworkClockAvailabilityService"},
            FunctionInfo{100, &NTC::SuspendAutonomicTimeCorrection, "SuspendAutonomicTimeCorrection"},
            FunctionInfo{101, &NTC::ResumeAutonomicTimeCorrection, "ResumeAutonomicTimeCorrection"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenEnsureNetworkClockAvailabilityService(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IEnsureNetworkClockAvailabilityService>(ctx, system);
    }

    // TODO(ogniK): Do we need these?
    void SuspendAutonomicTimeCorrection(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ResumeAutonomicTimeCorrection(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class NIM_ECAS final : public ServiceFramework<NIM_ECAS> {
public:
    explicit NIM_ECAS(Core::System& system_) : ServiceFramework{system_, "nim:ecas"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "RegisterSpecialClient"},
            FunctionInfo{1, nullptr, "UnregisterSpecialClient"}
        };
        // clang-format on
        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nim", std::make_shared<NIM>(system));
    server_manager->RegisterNamedService("nim:eca", std::make_shared<NIM_ECA>(system));
    server_manager->RegisterNamedService("nim:ecas", std::make_shared<NIM_ECAS>(system));
    server_manager->RegisterNamedService("nim:shp", std::make_shared<NIM_SHP>(system));
    server_manager->RegisterNamedService("ntc", std::make_shared<NTC>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NIM
