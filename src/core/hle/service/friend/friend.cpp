// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <queue>
#include "common/logging.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/friend/friend.h"
#include "core/hle/service/friend/friend_interface.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"

namespace Service::Friend {

class IFriendService final : public ServiceFramework<IFriendService> {
public:
    explicit IFriendService(Core::System& system_)
        : ServiceFramework{system_, "IFriendService"}, service_context{system, "IFriendService"} {
        completion_event = service_context.CreateEvent("IFriendService:CompletionEvent");
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    ~IFriendService() override {
        service_context.CloseEvent(completion_event);
    }

private:
    enum class PresenceFilter : u32 {
        None = 0,
        Online = 1,
        OnlinePlay = 2,
        OnlineOrOnlinePlay = 3,
    };

    struct SizedFriendFilter {
        PresenceFilter presence;
        u8 is_favorite;
        u8 same_app;
        u8 same_app_played;
        u8 arbitrary_app_played;
        u64 group_id;
    };
    static_assert(sizeof(SizedFriendFilter) == 0x10, "SizedFriendFilter is an invalid size");

    struct FriendsUserSetting {
        Common::UUID uuid;
        u32 presence_permission;
        u32 play_log_permission;
        u64 friend_request_reception;
        char friend_code[0x20];
        u64 friend_code_next_issuable_time;
        u8 unk_x48[0x7B8];
    };
    static_assert(sizeof(FriendsUserSetting) == 0x800, "FriendsUserSetting is an invalid size");

    void GetCompletionEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        auto& readable_event = completion_event->GetReadableEvent();

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(readable_event.Signal(system.Kernel()));
        rb.PushCopyObjects(ctx, readable_event);
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called.");

        // TODO (jarrodnorwell)

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendList(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto friend_offset = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();
        [[maybe_unused]] const auto filter = rp.PopRaw<SizedFriendFilter>();
        const auto pid = rp.Pop<u64>();
        LOG_WARNING(Service_Friend, "(STUBBED) called, offset={}, uuid=0x{}, pid={}", friend_offset,
                    uuid.RawString(), pid);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        rb.Push<u32>(0); // Friend count
        // TODO(ogniK): Return a buffer of u64s which are the "NetworkServiceAccountId"
    }

    void CheckFriendListAvailability(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid{rp.PopRaw<Common::UUID>()};

        LOG_WARNING(Service_Friend, "(STUBBED) called, uuid=0x{}", uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(true);
    }

    void GetBlockedUserListIds(HLERequestContext& ctx) {
        // This is safe to stub, as there should be no adverse consequences from reporting no
        // blocked users.
        LOG_WARNING(Service_Friend, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Indicates there are no blocked users
    }

    void CheckBlockedUserListAvailability(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid{rp.PopRaw<Common::UUID>()};

        LOG_WARNING(Service_Friend, "(STUBBED) called, uuid=0x{}", uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(true);
    }

    void DeclareCloseOnlinePlaySession(HLERequestContext& ctx) {
        // Stub used by Splatoon 2
        LOG_WARNING(Service_Friend, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void UpdateUserPresence(HLERequestContext& ctx) {
        // Stub used by Retro City Rampage
        LOG_WARNING(Service_Friend, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetPlayHistoryRegistrationKey(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto local_play = rp.Pop<bool>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_WARNING(Service_Friend, "(STUBBED) called, local_play={}, uuid=0x{}", local_play,
                    uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendCount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    void GetNewlyFriendCount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    void RequestSyncFriendList(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called.");

        // TODO (jarrodnorwell)

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendListForViewer(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetReceivedFriendRequestCount(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(0);
        rb.Push(0);
    }

    void GetUserPresenceView(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "(STUBBED) called, uuid={}.", uuid.RawString());

        u8 buf[0xe0]{};
        ctx.WriteBuffer(buf);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
    }

    void GetPlayHistoryStatistics(HLERequestContext& ctx) {
        LOG_ERROR(Service_Friend, "(STUBBED) called, check in out");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void LoadUserSetting(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_WARNING(Service_Friend, "(STUBBED) called");

        FriendsUserSetting setting{};
        setting.uuid = uuid;
        setting.presence_permission = 2;
        setting.play_log_permission = 5;
        setting.friend_request_reception = 1;
        setting.friend_code_next_issuable_time = 99999999999;
        strcpy(setting.friend_code, "0000-0000-0000");
        ctx.WriteBuffer(setting);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RequestListSummaryOverlayNotification(HLERequestContext& ctx) {
        LOG_INFO(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetReceivedFriendInvitationCountCache(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called, check in out");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IFriendService::GetCompletionEvent, "GetCompletionEvent"},
        FunctionInfo{1, &IFriendService::Cancel, "Cancel"},
        FunctionInfo{10100, nullptr, "GetFriendListIds"},
        FunctionInfo{10101, &IFriendService::GetFriendList, "GetFriendList"},
        FunctionInfo{10102, nullptr, "UpdateFriendInfo"},
        FunctionInfo{10110, nullptr, "GetFriendProfileImage"},
        FunctionInfo{10111, nullptr, "GetFriendProfileImageWithImageSize"}, // 18.0.0+
        FunctionInfo{10120, &IFriendService::CheckFriendListAvailability, "CheckFriendListAvailability"},
        FunctionInfo{10121, nullptr, "EnsureFriendListAvailable"},
        FunctionInfo{10200, nullptr, "SendFriendRequestForApplication"},
        FunctionInfo{10211, nullptr, "AddFacedFriendRequestForApplication"},
        FunctionInfo{10400, &IFriendService::GetBlockedUserListIds, "GetBlockedUserListIds"},
        FunctionInfo{10420, &IFriendService::CheckBlockedUserListAvailability, "CheckBlockedUserListAvailability"},
        FunctionInfo{10421, nullptr, "EnsureBlockedUserListAvailable"},
        FunctionInfo{10500, nullptr, "GetProfileList"},
        FunctionInfo{10501, nullptr, "GetProfileListV2"}, // 18.0.0+
        FunctionInfo{10600, nullptr, "DeclareOpenOnlinePlaySession"},
        FunctionInfo{10601, &IFriendService::DeclareCloseOnlinePlaySession, "DeclareCloseOnlinePlaySession"},
        FunctionInfo{10610, &IFriendService::UpdateUserPresence, "UpdateUserPresence"},
        FunctionInfo{10700, &IFriendService::GetPlayHistoryRegistrationKey, "GetPlayHistoryRegistrationKey"},
        FunctionInfo{10701, nullptr, "GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId"},
        FunctionInfo{10702, nullptr, "AddPlayHistory"},
        FunctionInfo{11000, nullptr, "GetProfileImageUrl"},
        FunctionInfo{11001, nullptr, "GetProfileImageUrlV2"}, // 18.0.0+
        FunctionInfo{20100, &IFriendService::GetFriendCount, "GetFriendCount"},
        FunctionInfo{20101, &IFriendService::GetNewlyFriendCount, "GetNewlyFriendCount"},
        FunctionInfo{20102, nullptr, "GetFriendDetailedInfo"},
        FunctionInfo{20103, nullptr, "SyncFriendList"},
        FunctionInfo{20104, &IFriendService::RequestSyncFriendList, "RequestSyncFriendList"},
        FunctionInfo{20105, &IFriendService::GetFriendListForViewer, "GetFriendListForViewerV1"},
        FunctionInfo{20106, nullptr, "UpdateFriendInfoForViewerV1"},
        FunctionInfo{20107, nullptr, "GetFriendDetailedInfoV2"}, // 20.0.0+
        FunctionInfo{20108, &IFriendService::GetFriendListForViewer, "GetFriendListForViewerV2"}, // 22.0.0+
        FunctionInfo{20109, nullptr, "UpdateFriendInfoForViewerV2"}, // 22.0.0+
        FunctionInfo{20110, nullptr, "LoadFriendSettingV1"},
        FunctionInfo{20111, nullptr, "LoadFriendSettingV2"}, // 22.0.0+
        FunctionInfo{20200, &IFriendService::GetReceivedFriendRequestCount, "GetReceivedFriendRequestCount"},
        FunctionInfo{20201, nullptr, "GetFriendRequestListV1"},
        FunctionInfo{20202, nullptr, "GetFriendRequestListV2"}, // 20.0.0+
        FunctionInfo{20203, nullptr, "GetFriendRequestReceivedNotificationCount"}, // 22.0.0+
        FunctionInfo{20300, nullptr, "GetFriendCandidateList"},
        FunctionInfo{20301, nullptr, "GetNintendoNetworkIdInfo"},
        FunctionInfo{20302, nullptr, "GetSnsAccountLinkage"}, // 5.0.0-19.0.1
        FunctionInfo{20303, nullptr, "GetSnsAccountProfile"}, // 5.0.0-19.0.1
        FunctionInfo{20304, nullptr, "GetSnsAccountFriendList"}, // 5.0.0-19.0.1
        FunctionInfo{20400, nullptr, "GetBlockedUserListV1"},
        FunctionInfo{20401, nullptr, "SyncBlockedUserList"},
        FunctionInfo{20402, nullptr, "GetBlockedUserListV2"}, // 20.0.0+
        FunctionInfo{20500, nullptr, "GetProfileExtraListV1"},
        FunctionInfo{20501, nullptr, "GetRelationship"},
        FunctionInfo{20502, nullptr, "GetProfileExtraListV2"}, // 19.0.0+
        FunctionInfo{20600, &IFriendService::GetUserPresenceView, "GetUserPresenceViewV1"},
        FunctionInfo{20601, &IFriendService::GetUserPresenceView, "GetUserPresenceViewV2"}, // 19.0.0+
        FunctionInfo{20700, nullptr, "GetPlayHistoryListV1"},
        FunctionInfo{20701, &IFriendService::GetPlayHistoryStatistics, "GetPlayHistoryStatistics"},
        FunctionInfo{20702, nullptr, "GetPlayHistoryListV2"}, // 19.0.0+
        FunctionInfo{20800, &IFriendService::LoadUserSetting, "LoadUserSettingV1"},
        FunctionInfo{20801, nullptr, "SyncUserSetting"},
        FunctionInfo{20802, &IFriendService::LoadUserSetting, "LoadUserSettingV2"}, // 19.0.0+
        FunctionInfo{20900, &IFriendService::RequestListSummaryOverlayNotification, "RequestListSummaryOverlayNotification"},
        FunctionInfo{21000, nullptr, "GetExternalApplicationCatalog"},
        FunctionInfo{22000, nullptr, "GetReceivedFriendInvitationListV1"},
        FunctionInfo{22001, nullptr, "GetReceivedFriendInvitationDetailedInfoV1"},
        FunctionInfo{22002, nullptr, "GetReceivedFriendInvitationListV2"}, // 19.0.0+
        FunctionInfo{22003, nullptr, "GetReceivedFriendInvitationDetailedInfoV2"}, // 19.0.0+
        FunctionInfo{22010, &IFriendService::GetReceivedFriendInvitationCountCache, "GetReceivedFriendInvitationCountCache"},
        FunctionInfo{30100, nullptr, "DropFriendNewlyFlags"},
        FunctionInfo{30101, nullptr, "DeleteFriend"},
        FunctionInfo{30110, nullptr, "DropFriendNewlyFlag"},
        FunctionInfo{30120, nullptr, "ChangeFriendFavoriteFlag"},
        FunctionInfo{30121, nullptr, "ChangeFriendOnlineNotificationFlag"},
        FunctionInfo{30130, nullptr, "SetFriendNote"}, // 22.0.0+
        FunctionInfo{30131, nullptr, "RequestUploadPendingNote"}, // 22.0.0+
        FunctionInfo{30190, nullptr, "RequestSyncLocalUpdates"}, // 22.0.0+
        FunctionInfo{30200, nullptr, "SendFriendRequest"},
        FunctionInfo{30201, nullptr, "SendFriendRequestWithApplicationInfoV1"},
        FunctionInfo{30202, nullptr, "CancelFriendRequest"},
        FunctionInfo{30203, nullptr, "AcceptFriendRequest"},
        FunctionInfo{30204, nullptr, "RejectFriendRequest"},
        FunctionInfo{30205, nullptr, "ReadFriendRequest"},
        FunctionInfo{30210, nullptr, "GetFacedFriendRequestRegistrationKey"},
        FunctionInfo{30211, nullptr, "AddFacedFriendRequest"},
        FunctionInfo{30212, nullptr, "CancelFacedFriendRequest"},
        FunctionInfo{30213, nullptr, "GetFacedFriendRequestProfileImage"},
        FunctionInfo{30214, nullptr, "GetFacedFriendRequestProfileImageFromPath"},
        FunctionInfo{30215, nullptr, "SendFriendRequestWithExternalApplicationCatalogId"},
        FunctionInfo{30216, nullptr, "ResendFacedFriendRequest"},
        FunctionInfo{30217, nullptr, "SendFriendRequestWithNintendoNetworkIdInfo"},
        FunctionInfo{30218, nullptr, "SendFriendRequestWithApplicationInfoV2"}, // 20.0.0+
        FunctionInfo{30300, nullptr, "GetSnsAccountLinkPageUrl"}, // 5.0.0-19.0.1
        FunctionInfo{30301, nullptr, "UnlinkSnsAccount"}, // 5.0.0-19.0.1
        FunctionInfo{30400, nullptr, "BlockUser"},
        FunctionInfo{30401, nullptr, "BlockUserWithApplicationInfoV1"},
        FunctionInfo{30402, nullptr, "UnblockUser"},
        FunctionInfo{30403, nullptr, "BlockUserWithApplicationInfoV2"}, // 20.0.0+
        FunctionInfo{30500, nullptr, "GetProfileExtraFromFriendCodeV1"},
        FunctionInfo{30501, nullptr, "GetProfileExtraFromFriendCodeV2"}, // 19.0.0+
        FunctionInfo{30700, nullptr, "DeletePlayHistory"},
        FunctionInfo{30701, nullptr, "AddPlayHistoryWithApplication"}, // 19.0.0+
        FunctionInfo{30810, nullptr, "ChangePresencePermission"},
        FunctionInfo{30811, nullptr, "ChangeFriendRequestReception"},
        FunctionInfo{30812, nullptr, "ChangePlayLogPermission"},
        FunctionInfo{30820, nullptr, "IssueFriendCode"},
        FunctionInfo{30830, nullptr, "ClearPlayLog"},
        FunctionInfo{30900, nullptr, "SendFriendInvitationV1"},
        FunctionInfo{30901, nullptr, "SendFriendInvitationV2"}, // 19.0.0+
        FunctionInfo{30910, nullptr, "ReadFriendInvitation"},
        FunctionInfo{30911, nullptr, "ReadAllFriendInvitations"},
        FunctionInfo{31000, nullptr, "OpenUser"}, // 19.0.0+
        FunctionInfo{40100, nullptr, "DeleteFriendListCache"},
        FunctionInfo{40400, nullptr, "DeleteBlockedUserListCache"},
        FunctionInfo{49900, nullptr, "DeleteNetworkServiceAccountCache"}
    );
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event;
};

class INotificationService final : public ServiceFramework<INotificationService> {
public:
    explicit INotificationService(Core::System& system_, Common::UUID uuid_)
        : ServiceFramework{system_, "INotificationService"}, uuid{uuid_}
        , service_context{system_, "INotificationService"} {
        notification_event = service_context.CreateEvent("INotificationService:NotifyEvent");
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    ~INotificationService() override {
        service_context.CloseEvent(notification_event);
    }

private:
    void GetEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, notification_event->GetReadableEvent());
    }

    void Clear(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");
        while (!notifications.empty()) {
            notifications.pop();
        }
        std::memset(&states, 0, sizeof(States));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Pop(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        if (notifications.empty()) {
            LOG_ERROR(Service_Friend, "No notifications in queue!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultNoNotifications);
            return;
        }

        const auto notification = notifications.front();
        notifications.pop();

        switch (notification.notification_type) {
        case NotificationTypes::HasUpdatedFriendsList:
            states.has_updated_friends = false;
            break;
        case NotificationTypes::HasReceivedFriendRequest:
            states.has_received_friend_request = false;
            break;
        default:
            // HOS seems not have an error case for an unknown notification
            LOG_WARNING(Service_Friend, "Unknown notification {:08X}",
                        notification.notification_type);
            break;
        }

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw<SizedNotificationInfo>(notification);
    }

    enum class NotificationTypes : u32 {
        HasUpdatedFriendsList = 0x65,
        HasReceivedFriendRequest = 0x1
    };

    struct SizedNotificationInfo {
        NotificationTypes notification_type;
        INSERT_PADDING_WORDS(
            1); // TODO(ogniK): This doesn't seem to be used within any IPC returns as of now
        u64_le account_id;
    };
    static_assert(sizeof(SizedNotificationInfo) == 0x10,
                  "SizedNotificationInfo is an incorrect size");

    struct States {
        bool has_updated_friends;
        bool has_received_friend_request;
    };

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &INotificationService::GetEvent, "GetEvent"},
        FunctionInfo{1, &INotificationService::Clear, "Clear"},
        FunctionInfo{2, &INotificationService::Pop, "Pop"}
    );
    Common::UUID uuid;
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* notification_event;
    std::queue<SizedNotificationInfo> notifications;
    States states{};
};

void Module::Interface::CreateFriendService(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IFriendService>(ctx, system);
    LOG_DEBUG(Service_Friend, "called");
}

void Module::Interface::CreateNotificationService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto uuid = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_Friend, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotificationService>(ctx, system, uuid);
}

Module::Interface::Interface(std::shared_ptr<Module> module_, Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)}
{}

Module::Interface::~Interface() = default;

class IServiceForApplication final : public ServiceFramework<IServiceForApplication> {
public:
    explicit IServiceForApplication(Core::System& system_)
        : ServiceFramework{system_, "nd:app"}
    {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetReceivableNeighborInfoCountMax"},
        FunctionInfo{10, nullptr, "IsNeighborDetectionEnabled"}
    );
};

class IServiceForSystem final : public ServiceFramework<IServiceForSystem> {
public:
    explicit IServiceForSystem(Core::System& system_)
        : ServiceFramework{system_, "nd:sys"}
    {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetReceivableNeighborInfoCountMax"},
        FunctionInfo{10, nullptr, "IsNeighborDetectionEnabled"},
        FunctionInfo{200, nullptr, "SetSystemData"},
        FunctionInfo{201, nullptr, "ClearSystemData"},
        FunctionInfo{203, nullptr, "GetReceivableNeighborInfoCountForSystem"},
        FunctionInfo{204, nullptr, "ReceiveNeighborInfoForSystem"},
        FunctionInfo{205, nullptr, "SetSender"},
        FunctionInfo{206, nullptr, "GetSender"},
        FunctionInfo{207, nullptr, "CreateScannerForSystem"},
        FunctionInfo{208, nullptr, "CreateReceiveEventHolderForSystem"},
        FunctionInfo{223, nullptr, "EnableNeighborDetection"},
        FunctionInfo{224, nullptr, "DisableNeighborDetection"},
        FunctionInfo{226, nullptr, "EnablePowerSave"},
        FunctionInfo{227, nullptr, "DisablePowerSave"},
        FunctionInfo{228, nullptr, "IsPowerSaveEnabled"},
        FunctionInfo{232, nullptr, "ClearBlockedUsers"},
        FunctionInfo{233, nullptr, "GetBlockedUserCount"},
        FunctionInfo{234, nullptr, "BlockUserByLocalUserId"},
        FunctionInfo{235, nullptr, "BlockUserByNetworkUserId"},
        FunctionInfo{236, nullptr, "UnblockUserByLocalUserId"},
        FunctionInfo{237, nullptr, "UnblockUserByNetworkUserId"},
        FunctionInfo{240, nullptr, "DeleteApplication"},
        FunctionInfo{250, nullptr, "InitializeApplicationInfo"},
        FunctionInfo{260, nullptr, "CreateAccountSystemSaveDataAccessSuppressor"},
        FunctionInfo{300, nullptr, "AddReceivedNeighborInfoForSystemForDebug"},
        FunctionInfo{301, nullptr, "GetSendDataForDebug"},
        FunctionInfo{302, nullptr, "ClearReceiveCounterForDebug"},
        FunctionInfo{303, nullptr, "GetNextReceiveCounterForDebug"},
        FunctionInfo{304, nullptr, "ListBlockedUsersForDebug"},
        FunctionInfo{305, nullptr, "RefreshSendDataIdForDebug"},
        FunctionInfo{306, nullptr, "ReloadFwdbgSettingsForDebug"},
        FunctionInfo{307, nullptr, "EnableApplicationForDebug"},
        FunctionInfo{308, nullptr, "GetNextReceiveCountersForDebug"},
        FunctionInfo{309, nullptr, "ListApplicationInfoForDebug"},
        FunctionInfo{310, nullptr, "SetApplicationDataForDebug"},
        FunctionInfo{400, nullptr, "GetNetworkUserId"},
        FunctionInfo{401, nullptr, "DeleteNetworkUserId"}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>();

    server_manager->RegisterNamedService("nd:app", std::make_shared<IServiceForApplication>(system));
    server_manager->RegisterNamedService("nd:sys", std::make_shared<IServiceForSystem>(system));

    server_manager->RegisterNamedService("friend:a", std::make_shared<Friend>(module, system, "friend:a"));
    server_manager->RegisterNamedService("friend:m", std::make_shared<Friend>(module, system, "friend:m"));
    server_manager->RegisterNamedService("friend:s", std::make_shared<Friend>(module, system, "friend:s"));
    server_manager->RegisterNamedService("friend:u", std::make_shared<Friend>(module, system, "friend:u"));
    server_manager->RegisterNamedService("friend:v", std::make_shared<Friend>(module, system, "friend:v"));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Friend
