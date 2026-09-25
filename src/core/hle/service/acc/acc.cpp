// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>

#include "common/common_types.h"
#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include <ranges>
#include "common/stb.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/server_manager.h"
#include "core/loader/loader.h"

namespace Service::Account {

// Thumbnails are hard coded to be at least this size
constexpr std::size_t THUMBNAIL_SIZE = 0x24000;

static std::filesystem::path GetImagePath(const Common::UUID& uuid) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
           fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
}

static void JPGToMemory(void* context, void* data, int len) {
    std::vector<u8>* jpg_image = static_cast<std::vector<u8>*>(context);
    unsigned char* jpg = static_cast<unsigned char*>(data);
    jpg_image->insert(jpg_image->end(), jpg, jpg + len);
}

static void SanitizeJPEGImageSize(std::vector<u8>& image) {
    constexpr std::size_t max_jpeg_image_size = 0x20000;
    constexpr int profile_dimensions = 256;
    int original_width, original_height, color_channels;

    const auto plain_image =
        stbi_load_from_memory(image.data(), static_cast<int>(image.size()), &original_width,
                              &original_height, &color_channels, STBI_rgb);

    // Resize image to match 256*256
    if (original_width != profile_dimensions || original_height != profile_dimensions) {
        // Use vector instead of array to avoid overflowing the stack
        std::vector<u8> out_image(profile_dimensions * profile_dimensions * STBI_rgb);
        stbir_resize_uint8_srgb(plain_image, original_width, original_height, 0, out_image.data(),
                                profile_dimensions, profile_dimensions, 0, STBI_rgb, 0,
                                STBIR_FILTER_BOX);
        image.clear();
        if (!stbi_write_jpg_to_func(JPGToMemory, &image, profile_dimensions, profile_dimensions,
                                    STBI_rgb, out_image.data(), 0)) {
            LOG_ERROR(Service_ACC, "Failed to resize the user provided image.");
        }
    }

    image.resize((std::min)(image.size(), max_jpeg_image_size));
}

class IManagerForSystemService final : public ServiceFramework<IManagerForSystemService> {
public:
    explicit IManagerForSystemService(Core::System& system_, Common::UUID uuid)
        : ServiceFramework{system_, "IManagerForSystemService"}, account_id{uuid} {
    }

private:
    Result CheckAvailability() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result GetAccountId(Out<u64> out_account_id) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        *out_account_id = account_id.Hash();
        R_SUCCEED();
    }

    Result LoadIdTokenCacheDeprecated() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result LoadIdTokenCache() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result GetNetworkServiceLicenseCacheEx(Out<u32> out_license, Out<s64> out_expiration) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        *out_license = 0;
        *out_expiration = 0;

        R_SUCCEED();
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, D<&IManagerForSystemService::CheckAvailability>, "CheckAvailability"},
            FunctionInfo{1, D<&IManagerForSystemService::GetAccountId>, "GetAccountId"},
            FunctionInfo{2, nullptr, "EnsureIdTokenCacheAsync"},
            FunctionInfo{3, D<&IManagerForSystemService::LoadIdTokenCacheDeprecated>, "LoadIdTokenCacheDeprecated"}, // 19.0.0+
            FunctionInfo{4, D<&IManagerForSystemService::LoadIdTokenCache>, "LoadIdTokenCache"}, // 19.0.0+
            FunctionInfo{100, nullptr, "SetSystemProgramIdentification"},
            FunctionInfo{101, nullptr, "RefreshNotificationTokenAsync"}, // 7.0.0+
            FunctionInfo{110, nullptr, "GetServiceEntryRequirementCache"}, // 4.0.0+
            FunctionInfo{111, nullptr, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            FunctionInfo{112, nullptr, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            FunctionInfo{113, nullptr, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            FunctionInfo{120, nullptr, "GetNintendoAccountId"},
            FunctionInfo{121, nullptr, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            FunctionInfo{130, nullptr, "GetNintendoAccountUserResourceCache"},
            FunctionInfo{131, nullptr, "RefreshNintendoAccountUserResourceCacheAsync"},
            FunctionInfo{132, nullptr, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            FunctionInfo{133, nullptr, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            FunctionInfo{134, nullptr, "RefreshNintendoAccountVerificationUrlCache"}, // 9.0.0+
            FunctionInfo{135, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            FunctionInfo{136, nullptr, "GetNintendoAccountUserResourceCache"}, // 19.0.0+
            FunctionInfo{140, nullptr, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            FunctionInfo{141, nullptr, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            FunctionInfo{142, nullptr, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            FunctionInfo{143, D<&IManagerForSystemService::GetNetworkServiceLicenseCacheEx>, "GetNetworkServiceLicenseCacheEx"}, // 15.0.0+
            FunctionInfo{150, nullptr, "CreateAuthorizationRequest"},
            FunctionInfo{160, nullptr, "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            FunctionInfo{161, nullptr, "RequireReauthenticationOfNetworkServiceAccount"},
            FunctionInfo{170, nullptr, "CreateDeviceHistoryRequest"}, // 17.0.0+
            FunctionInfo{180, nullptr, "GetRequestForNintendoAccountReauthentication"} // 18.0.0+
        );
    }
    Common::UUID account_id;
};

// 3.0.0+
class IFloatingRegistrationRequest final : public ServiceFramework<IFloatingRegistrationRequest> {
public:
    explicit IFloatingRegistrationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IFloatingRegistrationRequest"} {
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetSessionId"},
            FunctionInfo{12, nullptr, "GetAccountId"},
            FunctionInfo{13, nullptr, "GetLinkedNintendoAccountId"},
            FunctionInfo{14, nullptr, "GetNickname"},
            FunctionInfo{15, nullptr, "GetProfileImage"},
            FunctionInfo{16, nullptr, "GetProfileLargeImage", MakeVersionGate({18,0,0})},
            FunctionInfo{21, nullptr, "LoadIdTokenCache"},
            FunctionInfo{100, nullptr, "RegisterUser"}, // [1.0.0-3.0.2] RegisterAsync
            FunctionInfo{101, nullptr, "RegisterUserWithUid"}, // [1.0.0-3.0.2] RegisterWithUidAsync
            FunctionInfo{102, nullptr, "RegisterNetworkServiceAccountAsync", MakeVersionGate({4,0,0})},
            FunctionInfo{103, nullptr, "RegisterNetworkServiceAccountWithUidAsync", MakeVersionGate({4,0,0})},
            FunctionInfo{110, nullptr, "SetSystemProgramIdentification"},
            FunctionInfo{111, nullptr, "EnsureIdTokenCacheAsync"}
        );
    }
};

class IAdministrator final : public ServiceFramework<IAdministrator> {
public:
    explicit IAdministrator(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAdministrator"} {
    }

private:
    void IsLinkedWithNintendoAccount(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false);
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "CheckAvailability"},
            FunctionInfo{1, nullptr, "GetAccountId"},
            FunctionInfo{2, nullptr, "EnsureIdTokenCacheAsync"},
            FunctionInfo{3, nullptr, "LoadIdTokenCache"},
            FunctionInfo{100, nullptr, "SetSystemProgramIdentification"},
            FunctionInfo{101, nullptr, "RefreshNotificationTokenAsync", MakeVersionGate({7,0,0})},
            FunctionInfo{110, nullptr, "GetServiceEntryRequirementCache"}, // 4.0.0+
            FunctionInfo{111, nullptr, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            FunctionInfo{112, nullptr, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            FunctionInfo{113, nullptr, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            FunctionInfo{120, nullptr, "GetNintendoAccountId"},
            FunctionInfo{121, nullptr, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            FunctionInfo{130, nullptr, "GetNintendoAccountUserResourceCache"},
            FunctionInfo{131, nullptr, "RefreshNintendoAccountUserResourceCacheAsync"},
            FunctionInfo{132, nullptr, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            FunctionInfo{133, nullptr, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            FunctionInfo{134, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsync"}, // 9.0.0+
            FunctionInfo{135, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            FunctionInfo{140, nullptr, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            FunctionInfo{141, nullptr, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            FunctionInfo{142, nullptr, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            FunctionInfo{143, nullptr, "GetNetworkServiceLicenseCacheEx"},
            FunctionInfo{150, nullptr, "CreateAuthorizationRequest"},
            FunctionInfo{160, nullptr, "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            FunctionInfo{161, nullptr, "RequireReauthenticationOfNetworkServiceAccount"},
            FunctionInfo{170, nullptr, "CreateDeviceHistoryRequest"}, // 17.0.0+
            FunctionInfo{180, nullptr, "GetRequestForNintendoAccountReauthentication"}, // 18.0.0+
            FunctionInfo{200, nullptr, "IsRegistered"},
            FunctionInfo{201, nullptr, "RegisterAsync"},
            FunctionInfo{202, nullptr, "UnregisterAsync"},
            FunctionInfo{203, nullptr, "DeleteRegistrationInfoLocally"},
            FunctionInfo{220, nullptr, "SynchronizeProfileAsync"},
            FunctionInfo{221, nullptr, "UploadProfileAsync"},
            FunctionInfo{222, nullptr, "SynchronizaProfileAsyncIfSecondsElapsed"},
            FunctionInfo{250, &IAdministrator::IsLinkedWithNintendoAccount, "IsLinkedWithNintendoAccount"},
            FunctionInfo{251, nullptr, "CreateProcedureToLinkWithNintendoAccount"},
            FunctionInfo{252, nullptr, "ResumeProcedureToLinkWithNintendoAccount"},
            FunctionInfo{255, nullptr, "CreateProcedureToUpdateLinkageStateOfNintendoAccount"},
            FunctionInfo{256, nullptr, "ResumeProcedureToUpdateLinkageStateOfNintendoAccount"},
            FunctionInfo{260, nullptr, "CreateProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            FunctionInfo{261, nullptr, "ResumeProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            FunctionInfo{280, nullptr, "ProxyProcedureToAcquireApplicationAuthorizationForNintendoAccount"},
            FunctionInfo{290, nullptr, "GetRequestForNintendoAccountUserResourceView"}, // 8.0.0+
            FunctionInfo{300, nullptr, "TryRecoverNintendoAccountUserStateAsync"}, // 6.0.0+
            FunctionInfo{400, nullptr, "IsServiceEntryRequirementCacheRefreshRequiredForOnlinePlay"}, // 6.1.0+
            FunctionInfo{401, nullptr, "RefreshServiceEntryRequirementCacheForOnlinePlayAsync"}, // 6.1.0+
            FunctionInfo{900, nullptr, "GetAuthenticationInfoForWin"}, // 9.0.0+
            FunctionInfo{901, nullptr, "ImportAsyncForWin"}, // 9.0.0+
            FunctionInfo{997, nullptr, "DebugUnlinkNintendoAccountAsync"},
            FunctionInfo{998, nullptr, "DebugSetAvailabilityErrorDetail"}
        );
    }
};

class IAuthorizationRequest final : public ServiceFramework<IAuthorizationRequest> {
public:
    explicit IAuthorizationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAuthorizationRequest"} {
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetSessionId"},
            FunctionInfo{10, nullptr, "InvokeWithoutInteractionAsync"},
            FunctionInfo{19, nullptr, "IsAuthorized"},
            FunctionInfo{20, nullptr, "GetAuthorizationCode"},
            FunctionInfo{21, nullptr, "GetIdToken"},
            FunctionInfo{22, nullptr, "GetState"}
        );
    }
};

class IOAuthProcedure final : public ServiceFramework<IOAuthProcedure> {
public:
    explicit IOAuthProcedure(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedure"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "PrepareAsync"},
            FunctionInfo{1, nullptr, "GetRequest"},
            FunctionInfo{2, nullptr, "ApplyResponse"},
            FunctionInfo{3, nullptr, "ApplyResponseAsync"},
            FunctionInfo{10, nullptr, "Suspend"}
        );
    }
};

// 3.0.0+
class IOAuthProcedureForExternalNsa final : public ServiceFramework<IOAuthProcedureForExternalNsa> {
public:
    explicit IOAuthProcedureForExternalNsa(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForExternalNsa"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "PrepareAsync"},
            FunctionInfo{1, nullptr, "GetRequest"},
            FunctionInfo{2, nullptr, "ApplyResponse"},
            FunctionInfo{3, nullptr, "ApplyResponseAsync"},
            FunctionInfo{10, nullptr, "Suspend"},
            FunctionInfo{100, nullptr, "GetAccountId"},
            FunctionInfo{101, nullptr, "GetLinkedNintendoAccountId"},
            FunctionInfo{102, nullptr, "GetNickname"},
            FunctionInfo{103, nullptr, "GetProfileImage"},
            FunctionInfo{104, nullptr, "GetProfileLargeImage"} // 18.0.0+
        );
    }
};

class IOAuthProcedureForNintendoAccountLinkage final
    : public ServiceFramework<IOAuthProcedureForNintendoAccountLinkage> {
public:
    explicit IOAuthProcedureForNintendoAccountLinkage(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForNintendoAccountLinkage"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "PrepareAsync"},
            FunctionInfo{1, nullptr, "GetRequest"},
            FunctionInfo{2, nullptr, "ApplyResponse"},
            FunctionInfo{3, nullptr, "ApplyResponseAsync"},
            FunctionInfo{10, nullptr, "Suspend"},
            FunctionInfo{100, nullptr, "GetRequestWithTheme"},
            FunctionInfo{101, nullptr, "IsNetworkServiceAccountReplaced"},
            FunctionInfo{199, nullptr, "GetUrlForIntroductionOfExtraMembership"}, // 2.0.0 - 5.1.0
            FunctionInfo{200, nullptr, "ApplyAsyncWithAuthorizedToken"},
            FunctionInfo{210, nullptr, "IsProfileAvailable"}, // 17.0.0+
            FunctionInfo{220, nullptr, "RegisterUserAsyncWithoutProfile"}, // 17.0.0+
            FunctionInfo{221, nullptr, "RegisterUserWithProfileAsync"}, // 17.0.0+
            FunctionInfo{230, nullptr, "RegisterUserWithLargeImageProfileAsync"}, // 18.0.0+
        );
    }
};

class INotifier final : public ServiceFramework<INotifier> {
public:
    explicit INotifier(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "INotifier"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetSystemEvent"}
        );
    }
};

class IProfileCommon : public ServiceFramework<IProfileCommon> {
public:
    explicit IProfileCommon(Core::System& system_, const char* name, bool editor_commands,
                            Common::UUID user_id_, ProfileManager& profile_manager_)
        : ServiceFramework{system_, name}, profile_manager{profile_manager_}, user_id{user_id_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &IProfileCommon::Get, "Get"},
            FunctionInfo{1, &IProfileCommon::GetBase, "GetBase"},
            FunctionInfo{10, &IProfileCommon::GetImageSize, "GetImageSize"},
            FunctionInfo{11, &IProfileCommon::LoadImage, "LoadImage"},
            FunctionInfo{20, &IProfileCommon::Unknown20, "Unknown20"},
            FunctionInfo{21, &IProfileCommon::Unknown21, "Unknown21"},
            FunctionInfo{30, &IProfileCommon::Unknown30, "Unknown30"}
        };
        // clang-format on
        RegisterHandlers(functions);

        if (editor_commands) {
            // clang-format off
            static const FunctionInfo editor_functions[] = {
                FunctionInfo{100, &IProfileCommon::Store, "Store"},
                FunctionInfo{101, &IProfileCommon::StoreWithImage, "StoreWithImage"},
                FunctionInfo{110, &IProfileCommon::Unknown110, "Unknown110"}
            };
            // clang-format on
            RegisterHandlers(editor_functions);
        }
    }

protected:
    void Unknown20(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // inbytes: 0x0, outbytes: 0x4

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown21(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // buffers: [0x6], inbytes: 0x0, outbytes: 0x4

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown30(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // inbytes: 0x0, outbytes: 0x10

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown110(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // buffer_entry_sizes: [0x80, 0x0], buffers: [0x19, 0x5], inbytes: 0x38, outbytes: 0x0

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Get(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        UserData data{};
        if (profile_manager.GetProfileBaseAndData(user_id, profile_base, data)) {
            ctx.WriteBuffer(data);
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base and data for user=0x{}",
                      user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void GetBase(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        if (profile_manager.GetProfileBase(user_id, profile_base)) {
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base for user=0x{}", user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void LoadImage(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);
        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            ctx.WriteBuffer(Core::Constants::ACCOUNT_BACKUP_JPEG);
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);

        ctx.WriteBuffer(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void GetImageSize(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);

        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> token_data(0x100);
        std::fill(token_data.begin(), token_data.end(), u8(0));

        (void)ctx.WriteBuffer(token_data);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(token_data.size()));
    }

    void GetNintendoAccountUserResourceCacheForApplication(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> nas_user_base_for_application(0x68);
        (void)ctx.WriteBuffer(nas_user_base_for_application);

        if (ctx.CanWriteBuffer(1)) {
            std::vector<u8> unknown_out_buffer(ctx.GetWriteBufferSize(1));
            (void)ctx.WriteBuffer(unknown_out_buffer, 1);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager.GetLastOpenedUser().Hash());
    }

    void Store(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto user_data = ctx.ReadBuffer();

        LOG_DEBUG(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                  Common::StringFromFixedZeroTerminatedBuffer(
                      reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                  base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        if (!profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update user data and base!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void StoreWithImage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto image_data = ctx.ReadBufferA(0);
        const auto user_data = ctx.ReadBufferX(0);

        LOG_INFO(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                 Common::StringFromFixedZeroTerminatedBuffer(
                     reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                 base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Write,
                                 Common::FS::FileType::BinaryFile);

        if (!image.IsOpen() || !image.SetSize(image_data.size()) ||
            image.Write(image_data) != image_data.size() ||
            !profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update profile data, base, and image!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    ProfileManager& profile_manager;
    Common::UUID user_id{}; ///< The user id this profile refers to.
};

class IProfile final : public IProfileCommon {
public:
    explicit IProfile(Core::System& system_, Common::UUID user_id_,
                      ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfile", false, user_id_, profile_manager_} {}
};

class IProfileEditor final : public IProfileCommon {
public:
    explicit IProfileEditor(Core::System& system_, Common::UUID user_id_,
                            ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfileEditor", true, user_id_, profile_manager_} {}
};

class ISessionObject final : public ServiceFramework<ISessionObject> {
public:
    explicit ISessionObject(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "ISessionObject"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{999, nullptr, "Dummy"}
        );
    }
};

class IGuestLoginRequest final : public ServiceFramework<IGuestLoginRequest> {
public:
    explicit IGuestLoginRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IGuestLoginRequest"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetSessionId"},
            FunctionInfo{11, nullptr, "Unknown"}, // 1.0.0 - 2.3.0 (the name is blank on Switchbrew)
            FunctionInfo{12, nullptr, "GetAccountId"},
            FunctionInfo{13, nullptr, "GetLinkedNintendoAccountId"},
            FunctionInfo{14, nullptr, "GetNickname"},
            FunctionInfo{15, nullptr, "GetProfileImage"},
            FunctionInfo{16, nullptr, "GetProfileLargeImage"}, // 18.0.0+
            FunctionInfo{21, nullptr, "LoadIdTokenCache"}, // 3.0.0+
        );
    }
};

class EnsureTokenIdCacheAsyncInterface final : public IAsyncContext {
public:
    explicit EnsureTokenIdCacheAsyncInterface(Core::System& system_) : IAsyncContext{system_} {
        MarkComplete();
    }
    ~EnsureTokenIdCacheAsyncInterface() = default;

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class IManagerForApplication final : public ServiceFramework<IManagerForApplication> {
public:
    explicit IManagerForApplication(Core::System& system_,
                                    const std::shared_ptr<ProfileManager>& profile_manager_)
        : ServiceFramework{system_, "IManagerForApplication"},
          ensure_token_id{std::make_shared<EnsureTokenIdCacheAsyncInterface>(system)},
          profile_manager{profile_manager_} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, &IManagerForApplication::CheckAvailability, "CheckAvailability"},
            FunctionInfo{1, &IManagerForApplication::GetAccountId, "GetAccountId"},
            FunctionInfo{2, &IManagerForApplication::EnsureIdTokenCacheAsync, "EnsureIdTokenCacheAsync"},
            FunctionInfo{3, &IManagerForApplication::LoadIdTokenCacheDeprecated, "LoadIdTokenCacheDeprecated"},
            FunctionInfo{4, &IManagerForApplication::LoadIdTokenCache, "LoadIdTokenCache"},
            FunctionInfo{130, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCacheForApplication"},
            FunctionInfo{136, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCache"}, // 19.0.0+
            FunctionInfo{150, nullptr, "CreateAuthorizationRequest"},
            FunctionInfo{160, &IManagerForApplication::StoreOpenContext, "StoreOpenContext"},
            FunctionInfo{170, nullptr, "LoadNetworkServiceLicenseKindAsync"}
        );
    }

private:
    void CheckAvailability(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetAccountId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager->GetLastOpenedUser().Hash());
    }

    void EnsureIdTokenCacheAsync(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface(ctx, ensure_token_id);
    }

    void LoadIdTokenCacheDeprecated(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        ensure_token_id->LoadIdTokenCache(ctx);
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> token_data(0x100);
        std::fill(token_data.begin(), token_data.end(), u8(0));

        ctx.WriteBuffer(token_data);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(token_data.size()));
    }

    void GetNintendoAccountUserResourceCacheForApplication(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> nas_user_base_for_application(0x68);
        ctx.WriteBuffer(nas_user_base_for_application);

        if (ctx.CanWriteBuffer(1)) {
            std::vector<u8> unknown_out_buffer(ctx.GetWriteBufferSize(1));
            ctx.WriteBuffer(unknown_out_buffer, 1);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager->GetLastOpenedUser().Hash());
    }

    void StoreOpenContext(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        profile_manager->StoreOpenedUsers();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    std::shared_ptr<EnsureTokenIdCacheAsyncInterface> ensure_token_id{};
    std::shared_ptr<ProfileManager> profile_manager;
};

// 6.0.0+
class IAsyncNetworkServiceLicenseKindContext final
    : public ServiceFramework<IAsyncNetworkServiceLicenseKindContext> {
public:
    explicit IAsyncNetworkServiceLicenseKindContext(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncNetworkServiceLicenseKindContext"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetSystemEvent"},
            FunctionInfo{1, nullptr, "Cancel"},
            FunctionInfo{2, nullptr, "HasDone"},
            FunctionInfo{3, nullptr, "GetResult"},
            FunctionInfo{4, nullptr, "GetNetworkServiceLicenseKind"}
        );
    }
};

// 8.0.0+
class IOAuthProcedureForUserRegistration final
    : public ServiceFramework<IOAuthProcedureForUserRegistration> {
public:
    explicit IOAuthProcedureForUserRegistration(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForUserRegistration"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "PrepareAsync"},
            FunctionInfo{1, nullptr, "GetRequest"},
            FunctionInfo{2, nullptr, "ApplyResponse"},
            FunctionInfo{3, nullptr, "ApplyResponseAsync"},
            FunctionInfo{10, nullptr, "Suspend"},
            FunctionInfo{100, nullptr, "GetAccountId"},
            FunctionInfo{101, nullptr, "GetLinkedNintendoAccountId"},
            FunctionInfo{102, nullptr, "GetNickname"},
            FunctionInfo{103, nullptr, "GetProfileImage"},
            FunctionInfo{104, nullptr, "GetProfileLargeImage"}, // 18.0.0+
            FunctionInfo{110, nullptr, "RegisterUserAsync"},
            FunctionInfo{111, nullptr, "GetUid"},
            FunctionInfo{200, nullptr, "ApplyResponseForUserCreationAsync"}, // 17.0.0+
            FunctionInfo{205, nullptr, "SuspendAfterApplyResponse"}, // 17.0.0+
            FunctionInfo{210, nullptr, "IsProfileAvailable"}, // 17.0.0+
            FunctionInfo{220, nullptr, "RegisterUserAsyncWithoutProfile"}, // 17.0.0+
            FunctionInfo{221, nullptr, "RegisterUserWithProfileAsync"}, // 17.0.0+
            FunctionInfo{230, nullptr, "RegisterUserWithLargeImageProfileAsync"} // 18.0.0+
        );
    }
};

class DAUTH_O final : public ServiceFramework<DAUTH_O> {
public:
    explicit DAUTH_O(Core::System& system_, Common::UUID) : ServiceFramework{system_, "dauth:o"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "EnsureAuthenticationTokenCacheAsync"},
            FunctionInfo{1, nullptr, "LoadAuthenticationTokenCache"},
            FunctionInfo{2, nullptr, "InvalidateAuthenticationTokenCache"},
            FunctionInfo{3, nullptr, "IsDeviceAuthenticationTokenCacheAvailable"},
            FunctionInfo{10, nullptr, "EnsureEdgeTokenCacheAsync"},
            FunctionInfo{11, nullptr, "LoadEdgeTokenCache"},
            FunctionInfo{12, nullptr, "InvalidateEdgeTokenCache"},
            FunctionInfo{13, nullptr, "IsEdgeTokenCacheAvailable"},
            FunctionInfo{20, nullptr, "EnsureApplicationAuthenticationCacheAsync"},
            FunctionInfo{21, nullptr, "LoadApplicationAuthenticationTokenCache"},
            FunctionInfo{22, nullptr, "LoadApplicationNetworkServiceClientConfigCache"},
            FunctionInfo{23, nullptr, "IsApplicationAuthenticationCacheAvailable"},
            FunctionInfo{24, nullptr, "InvalidateApplicationAuthenticationCache"}
        );
    }
};

// 6.0.0+
class IAsyncResult final : public ServiceFramework<IAsyncResult> {
public:
    explicit IAsyncResult(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncResult"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetResult"},
            FunctionInfo{1, nullptr, "Cancel"},
            FunctionInfo{2, nullptr, "IsAvailable"},
            FunctionInfo{3, nullptr, "GetSystemEvent"}
        );
    }
};

void Module::Interface::GetUserCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(static_cast<u32>(profile_manager->GetUserCount()));
}

void Module::Interface::GetUserExistence(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->UserExists(user_id));
}

void Module::Interface::ListAllUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetOpenUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetLastOpenedUser(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw<Common::UUID>(profile_manager->GetLastOpenedUser());
}

void Module::Interface::GetProfile(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfile>(ctx, system, user_id, *profile_manager);
}

void Module::Interface::IsUserRegistrationRequestPermitted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->CanSystemRegisterUser());
}

void Module::Interface::InitializeApplicationInfo(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

void Module::Interface::InitializeApplicationInfoRestricted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(Partial implementation) called");

    // TODO(ogniK): We require checking if the user actually owns the title and what not. As of
    // currently, we assume the user owns the title. InitializeApplicationInfoBase SHOULD be called
    // first then we do extra checks if the game is a digital copy.

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

Result Module::Interface::InitializeApplicationInfoBase() {
    if (application_info) {
        LOG_ERROR(Service_ACC, "Application already initialized");
        return Account::ResultApplicationInfoAlreadyInitialized;
    }

    // TODO(ogniK): This should be changed to reflect the target process for when we have multiple
    // processes emulated. As we don't actually have pid support we should assume we're just using
    // our own process
    Glue::ApplicationLaunchProperty launch_property{};
    const auto result = system.GetARPManager().GetLaunchProperty(
        &launch_property, system.GetApplicationProcessProgramID());

    if (result != ResultSuccess) {
        LOG_ERROR(Service_ACC, "Failed to get launch property");
        return Account::ResultInvalidApplication;
    }

    switch (launch_property.base_game_storage_id) {
    case FileSys::StorageId::GameCard:
        application_info.application_type = ApplicationType::GameCard;
        break;
    case FileSys::StorageId::Host:
    case FileSys::StorageId::NandSystem:
    case FileSys::StorageId::NandUser:
    case FileSys::StorageId::SdCard:
    case FileSys::StorageId::None: // Yuzu specific, differs from hardware
        application_info.application_type = ApplicationType::Digital;
        break;
    default:
        LOG_ERROR(Service_ACC, "Invalid game storage ID! storage_id={}",
                  launch_property.base_game_storage_id);
        return Account::ResultInvalidApplication;
    }

    LOG_WARNING(Service_ACC, "ApplicationInfo init required");
    // TODO(ogniK): Actual initialization here

    return ResultSuccess;
}

void Module::Interface::GetBaasAccountManagerForApplication(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForApplication>(ctx, system, profile_manager);
}

void Module::Interface::IsUserAccountSwitchLocked(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    FileSys::NACP nacp;
    const auto res = system.GetAppLoader().ReadControlData(nacp);

    bool is_locked = false;

    if (res != Loader::ResultStatus::Success) {
        const FileSys::PatchManager pm{system.GetApplicationProcessProgramID(),
                                       system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto nacp_unique = pm.GetControlMetadata().first;

        if (nacp_unique != nullptr) {
            is_locked = nacp_unique->GetUserAccountSwitchLock();
        } else {
            LOG_ERROR(Service_ACC, "nacp_unique is null!");
        }
    } else {
        is_locked = nacp.GetUserAccountSwitchLock();
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_locked);
}

void Module::Interface::InitializeApplicationInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::BeginUserRegistration(HLERequestContext& ctx) {
    const auto user_id = Common::UUID::MakeRandom();
    profile_manager->CreateNewUser(user_id, "Eden");

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(user_id);
}

void Module::Interface::CompleteUserRegistration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    profile_manager->WriteUserSaveFile();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::DeleteUser(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());
    if (!profile_manager->RemoveUser(user_id)) {
        LOG_ERROR(Service_ACC, "Failed to delete user with uuid={}", user_id.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(1U);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::SetUserPosition(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    u64 position = rp.Pop<u64>();
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_ACC, "called, position={} user_id={}", position, user_id.FormattedString());

    profile_manager->SetUserPosition(position, user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetProfileEditor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_ACC, "called, user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfileEditor>(ctx, system, user_id, *profile_manager);
}

void Module::Interface::GetBaasAccountAdministrator(HLERequestContext &ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IAdministrator>(ctx, system, uuid);
}

void Module::Interface::ListQualifiedUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    // All users should be qualified. We don't actually have parental control or anything to do with
    // nintendo online currently. We're just going to assume the user running the game has access to
    // the game regardless of parental control settings.
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenContextStoredUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    ctx.WriteBuffer(profile_manager->GetStoredOpenedUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::StoreSaveDataThumbnailApplication(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}", uuid.RawString());

    // TODO(ogniK): Check if application ID is zero on acc initialize. As we don't have a reliable
    // way of confirming things like the TID, we're going to assume a non zero value for the time
    // being.
    constexpr u64 tid{1};
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::GetBaasAccountManagerForSystemService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForSystemService>(ctx, system, uuid);
}

void Module::Interface::StoreSaveDataThumbnailSystem(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}, tid={:016X}", uuid.RawString(), tid);
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::GetPinCodeLength(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0);
}

void Module::Interface::StoreSaveDataThumbnail(HLERequestContext& ctx, const Common::UUID& uuid,
                                               const u64 tid) {
    IPC::ResponseBuilder rb{ctx, 2};

    if (tid == 0) {
        LOG_ERROR(Service_ACC, "TitleID is not valid!");
        rb.Push(Account::ResultInvalidApplication);
        return;
    }

    if (uuid.IsInvalid()) {
        LOG_ERROR(Service_ACC, "User ID is not valid!");
        rb.Push(Account::ResultInvalidUserId);
        return;
    }
    const auto thumbnail_size = ctx.GetReadBufferSize();
    if (thumbnail_size != THUMBNAIL_SIZE) {
        LOG_ERROR(Service_ACC, "Buffer size is empty! size={:X} expecting {:X}", thumbnail_size,
                  THUMBNAIL_SIZE);
        rb.Push(Account::ResultInvalidArrayLength);
        return;
    }

    // TODO(ogniK): Construct save data thumbnail
    rb.Push(ResultSuccess);
}

void Module::Interface::TrySelectUserWithoutInteractionDeprecated(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(ResultSuccess);
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::ranges::all_of(user_list, [](const auto& user) { return user.IsInvalid(); })) {
        rb.Push(ResultUnknown); // TODO(ogniK): Find the correct error code
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    // Select the first user we have
    rb.Push(ResultSuccess);
    rb.PushRaw(profile_manager->GetUser(0)->uuid);
}

void Module::Interface::TrySelectUserWithoutInteraction(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(ResultSuccess);
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::ranges::all_of(user_list, [](const auto& user) { return user.IsInvalid(); })) {
        rb.Push(ResultUnknown); // TODO(ogniK): Find the correct error code
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    // Select the first user we have
    rb.Push(ResultSuccess);
    rb.PushRaw(profile_manager->GetUser(0)->uuid);
}

Module::Interface::Interface(std::shared_ptr<Module> module_,
                             std::shared_ptr<ProfileManager> profile_manager_,
                             Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)},
      profile_manager{std::move(profile_manager_)} {}

Module::Interface::~Interface() = default;

class ACC_AA final : public Module::Interface {
public:
    explicit ACC_AA(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:aa") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "EnsureCacheAsync"},
            FunctionInfo{1, nullptr, "LoadCache"},
            FunctionInfo{2, nullptr, "GetDeviceAccountId"},
            FunctionInfo{50, nullptr, "RegisterNotificationTokenAsync"},   // 1.0.0 - 6.2.0
            FunctionInfo{51, nullptr, "UnregisterNotificationTokenAsync"}, // 1.0.0 - 6.2.0
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    ~ACC_AA() override = default;
};

class ACC_SU final : public Module::Interface {
public:
    explicit ACC_SU(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:su") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, &ACC_SU::GetUserCount, "GetUserCount"},
            FunctionInfo{1, &ACC_SU::GetUserExistence, "GetUserExistence"},
            FunctionInfo{2, &ACC_SU::ListAllUsers, "ListAllUsers"},
            FunctionInfo{3, &ACC_SU::ListOpenUsers, "ListOpenUsers"},
            FunctionInfo{4, &ACC_SU::GetLastOpenedUser, "GetLastOpenedUser"},
            FunctionInfo{5, &ACC_SU::GetProfile, "GetProfile"},
            FunctionInfo{6, nullptr, "GetProfileDigest"},
            FunctionInfo{50, &ACC_SU::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            FunctionInfo{51, &ACC_SU::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            FunctionInfo{52, &ACC_SU::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // 19.0.0+
            FunctionInfo{60, &ACC_SU::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"},
            FunctionInfo{99, nullptr, "DebugActivateOpenContextRetention"},
            FunctionInfo{100, nullptr, "GetUserRegistrationNotifier"},
            FunctionInfo{101, nullptr, "GetUserStateChangeNotifier"},
            FunctionInfo{102, &ACC_SU::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            FunctionInfo{103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
            FunctionInfo{104, nullptr, "GetProfileUpdateNotifier"},
            FunctionInfo{105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
            FunctionInfo{106, nullptr, "GetProfileSyncNotifier"},
            FunctionInfo{110, &ACC_SU::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
            FunctionInfo{111, nullptr, "ClearSaveDataThumbnail"},
            FunctionInfo{112, nullptr, "LoadSaveDataThumbnail"},
            FunctionInfo{113, nullptr, "GetSaveDataThumbnailExistence"},
            FunctionInfo{120, nullptr, "ListOpenUsersInApplication"},
            FunctionInfo{130, nullptr, "ActivateOpenContextRetention"},
            FunctionInfo{140, &ACC_SU::ListQualifiedUsers, "ListQualifiedUsers"},
            FunctionInfo{150, nullptr, "AuthenticateApplicationAsync"},
            FunctionInfo{151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            FunctionInfo{152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            FunctionInfo{190, nullptr, "GetUserLastOpenedApplication"},
            FunctionInfo{191, nullptr, "ActivateOpenContextHolder"},
            FunctionInfo{200, &ACC_SU::BeginUserRegistration, "BeginUserRegistration"},
            FunctionInfo{201, &ACC_SU::CompleteUserRegistration, "CompleteUserRegistration"},
            FunctionInfo{202, nullptr, "CancelUserRegistration"},
            FunctionInfo{203, &ACC_SU::DeleteUser, "DeleteUser"},
            FunctionInfo{204, &ACC_SU::SetUserPosition, "SetUserPosition"},
            FunctionInfo{205, &ACC_SU::GetProfileEditor, "GetProfileEditor"},
            FunctionInfo{206, nullptr, "CompleteUserRegistrationForcibly"},
            FunctionInfo{210, nullptr, "CreateFloatingRegistrationRequest"},
            FunctionInfo{211, nullptr, "CreateProcedureToRegisterUserWithNintendoAccount"},
            FunctionInfo{212, nullptr, "ResumeProcedureToRegisterUserWithNintendoAccount"},
            FunctionInfo{230, nullptr, "AuthenticateServiceAsync"},
            FunctionInfo{250, &ACC_SU::GetBaasAccountAdministrator, "GetBaasAccountAdministrator"},
            FunctionInfo{290, nullptr, "ProxyProcedureForGuestLoginWithNintendoAccount"},
            FunctionInfo{291, nullptr, "ProxyProcedureForFloatingRegistrationWithNintendoAccount"},
            FunctionInfo{299, nullptr, "SuspendBackgroundDaemon"},
            FunctionInfo{400, nullptr, "SetPinCode"}, // 18.0.0+
            FunctionInfo{401, &ACC_SU::GetPinCodeLength, "GetPinCodeLength"}, // 18.0.0+
            FunctionInfo{402, nullptr, "GetPinCode"}, // 18.0.0+
            FunctionInfo{403, nullptr, "GetPinCodeParity"},
            FunctionInfo{404, nullptr, "VerifyPinCode"},
            FunctionInfo{405, nullptr, "IsPinCodeVerificationForbidden"},
            FunctionInfo{410, nullptr, "GetPinCodeErrorCount"}, // 18.0.0+
            FunctionInfo{411, nullptr, "ResetPinCodeErrorCount"}, // 18.0.0+
            FunctionInfo{412, nullptr, "IncrementPinCodeErrorCount"}, // 18.0.0+
            FunctionInfo{900, nullptr, "SetUserUnqualifiedForDebug"},
            FunctionInfo{901, nullptr, "UnsetUserUnqualifiedForDebug"},
            FunctionInfo{902, nullptr, "ListUsersUnqualifiedForDebug"},
            FunctionInfo{910, nullptr, "RefreshFirmwareSettingsForDebug"},
            FunctionInfo{997, nullptr, "DebugInvalidateTokenCacheForUser"},
            FunctionInfo{998, nullptr, "DebugSetUserStateClose"},
            FunctionInfo{999, nullptr, "DebugSetUserStateOpen"}
        );
    }
    ~ACC_SU() override = default;
};

class ACC_U0 final : public Module::Interface {
public:
    ACC_U0(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:u0") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, &ACC_U0::GetUserCount, "GetUserCount"},
            FunctionInfo{1, &ACC_U0::GetUserExistence, "GetUserExistence"},
            FunctionInfo{2, &ACC_U0::ListAllUsers, "ListAllUsers"},
            FunctionInfo{3, &ACC_U0::ListOpenUsers, "ListOpenUsers"},
            FunctionInfo{4, &ACC_U0::GetLastOpenedUser, "GetLastOpenedUser"},
            FunctionInfo{5, &ACC_U0::GetProfile, "GetProfile"},
            FunctionInfo{6, nullptr, "GetProfileDigest"}, // 3.0.0+
            FunctionInfo{50, &ACC_U0::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            FunctionInfo{51, &ACC_U0::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            FunctionInfo{52, &ACC_U0::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
            FunctionInfo{60, &ACC_U0::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"}, // 5.0.0 - 5.1.0
            FunctionInfo{99, nullptr, "DebugActivateOpenContextRetention"}, // 6.0.0+
            FunctionInfo{100, &ACC_U0::InitializeApplicationInfo, "InitializeApplicationInfo"},
            FunctionInfo{101, &ACC_U0::GetBaasAccountManagerForApplication, "GetBaasAccountManagerForApplication"},
            FunctionInfo{102, nullptr, "AuthenticateApplicationAsync"},
            FunctionInfo{103, nullptr, "CheckNetworkServiceAvailabilityAsync"}, // 4.0.0+
            FunctionInfo{110, &ACC_U0::StoreSaveDataThumbnailApplication, "StoreSaveDataThumbnail"},
            FunctionInfo{111, nullptr, "ClearSaveDataThumbnail"},
            FunctionInfo{120, nullptr, "CreateGuestLoginRequest"},
            FunctionInfo{130, nullptr, "LoadOpenContext"}, // 5.0.0+
            FunctionInfo{131, &ACC_U0::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"}, // 6.0.0+
            FunctionInfo{140, &ACC_U0::InitializeApplicationInfoRestricted, "InitializeApplicationInfoRestricted"}, // 6.0.0+
            FunctionInfo{141, &ACC_U0::ListQualifiedUsers, "ListQualifiedUsers"}, // 6.0.0+
            FunctionInfo{150, &ACC_U0::IsUserAccountSwitchLocked, "IsUserAccountSwitchLocked"}, // 6.0.0+
            FunctionInfo{160, &ACC_U0::InitializeApplicationInfoV2, "InitializeApplicationInfoV2"}
        );
    }
    ~ACC_U0() override = default;
};

class ACC_U1 final : public Module::Interface {
public:
    ACC_U1(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:u1") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, &ACC_U1::GetUserCount, "GetUserCount"},
            FunctionInfo{1, &ACC_U1::GetUserExistence, "GetUserExistence"},
            FunctionInfo{2, &ACC_U1::ListAllUsers, "ListAllUsers"},
            FunctionInfo{3, &ACC_U1::ListOpenUsers, "ListOpenUsers"},
            FunctionInfo{4, &ACC_U1::GetLastOpenedUser, "GetLastOpenedUser"},
            FunctionInfo{5, &ACC_U1::GetProfile, "GetProfile"},
            FunctionInfo{6, nullptr, "GetProfileDigest"},
            FunctionInfo{50, &ACC_U1::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            FunctionInfo{51, &ACC_U1::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            FunctionInfo{52, &ACC_U1::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
            FunctionInfo{60, &ACC_U1::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"},
            FunctionInfo{99, nullptr, "DebugActivateOpenContextRetention"},
            FunctionInfo{100, nullptr, "GetUserRegistrationNotifier"},
            FunctionInfo{101, nullptr, "GetUserStateChangeNotifier"},
            FunctionInfo{102, &ACC_U1::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            FunctionInfo{103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
            FunctionInfo{104, nullptr, "GetProfileUpdateNotifier"},
            FunctionInfo{105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
            FunctionInfo{106, nullptr, "GetProfileSyncNotifier"},
            FunctionInfo{110, &ACC_U1::StoreSaveDataThumbnailApplication, "StoreSaveDataThumbnail"},
            FunctionInfo{111, nullptr, "ClearSaveDataThumbnail"},
            FunctionInfo{112, nullptr, "LoadSaveDataThumbnail"},
            FunctionInfo{113, nullptr, "GetSaveDataThumbnailExistence"},
            FunctionInfo{120, nullptr, "ListOpenUsersInApplication"},
            FunctionInfo{130, nullptr, "ActivateOpenContextRetention"},
            FunctionInfo{140, &ACC_U1::ListQualifiedUsers, "ListQualifiedUsers"},
            FunctionInfo{150, nullptr, "AuthenticateApplicationAsync"},
            FunctionInfo{151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            FunctionInfo{152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            FunctionInfo{190, nullptr, "GetUserLastOpenedApplication"},
            FunctionInfo{191, nullptr, "ActivateOpenContextHolder"},
            FunctionInfo{401, &ACC_U1::GetPinCodeLength, "GetPinCodeLength"}, // 18.0.0+
            FunctionInfo{402, nullptr, "GetPinCode"}, // 18.0.0+
            FunctionInfo{997, nullptr, "DebugInvalidateTokenCacheForUser"},
            FunctionInfo{998, nullptr, "DebugSetUserStateClose"},
            FunctionInfo{999, nullptr, "DebugSetUserStateOpen"}
        );
    }
    ~ACC_U1() override = default;
};

class DAUTH_0 final : public ServiceFramework<DAUTH_0> {
public:
    explicit DAUTH_0(Core::System& system_) : ServiceFramework{system_, "dauth:0"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "EnsureAuthenticationTokenCacheAsync"},
            FunctionInfo{1, nullptr, "LoadAuthenticationTokenCache"},
            FunctionInfo{2, nullptr, "InvalidateAuthenticationTokenCache"},
            FunctionInfo{3, nullptr, "IsDeviceAuthenticationTokenCacheAvailable"},
            FunctionInfo{10, nullptr, "EnsureEdgeTokenCacheAsync"},
            FunctionInfo{11, nullptr, "LoadEdgeTokenCache"},
            FunctionInfo{12, nullptr, "InvalidateEdgeTokenCache"},
            FunctionInfo{13, nullptr, "IsEdgeTokenCacheAvailable"},
            FunctionInfo{20, nullptr, "EnsureApplicationAuthenticationCacheAsync"},
            FunctionInfo{21, nullptr, "LoadApplicationAuthenticationTokenCache"},
            FunctionInfo{22, nullptr, "LoadApplicationNetworkServiceClientConfigCache"},
            FunctionInfo{23, nullptr, "IsApplicationAuthenticationCacheAvailable"},
            FunctionInfo{24, nullptr, "InvalidateApplicationAuthenticationCache"},
            FunctionInfo{30, nullptr, "EnsureGameCardAuthenticationCacheAsync"},
            FunctionInfo{31, nullptr, "LoadGameCardAuthenticationTokenCache"},
            FunctionInfo{32, nullptr, "IsGameCardAuthenticationCacheAvailable"},
            FunctionInfo{33, nullptr, "InvalidateGameCardAuthenticationCache"},
            FunctionInfo{1000, nullptr, "GetInactiveElicenseUsedEvent"},
            FunctionInfo{9000, nullptr, "ImportVirtualClientCertificate"},
            FunctionInfo{9010, nullptr, "DeleteVirtualClientCertificate"}
        );
    }
};

class ACC_E final : public Module::Interface {
public:
    explicit ACC_E(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            { 0, nullptr, "GetUserCount"},
            { 1, nullptr, "GetUserExistence"},
            { 2, nullptr, "ListAllUsers"},
            { 3, nullptr, "ListOpenUsers"},
            { 4, nullptr, "GetLastOpenedUser"},
            { 5, nullptr, "GetProfile"},
            { 6, nullptr, "GetProfileDigest"},
            { 50, nullptr, "IsUserRegistrationRequestPermitted"},
            { 51, nullptr, "TrySelectUserWithoutInteractionDeprecated"},
            { 52, nullptr, "TrySelectUserWithoutInteraction"},
            { 99, nullptr, "DebugActivateOpenContextRetention"},
            { 100, nullptr, "GetUserRegistrationNotifier"},
            { 101, nullptr, "GetUserStateChangeNotifier"},
            { 102, nullptr, "GetBaasAccountManagerForSystemService"},
            { 103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
            { 104, nullptr, "GetProfileUpdateNotifier"},
            { 105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
            { 106, nullptr, "GetProfileSyncNotifier"},
            { 110, nullptr, "StoreSaveDataThumbnail"},
            { 111, nullptr, "ClearSaveDataThumbnail"},
            { 112, nullptr, "LoadSaveDataThumbnail"},
            { 113, nullptr, "GetSaveDataThumbnailExistence"},
            { 120, nullptr, "ListOpenUsersInApplication"},
            { 130, nullptr, "ActivateOpenContextRetention"},
            { 140, nullptr, "ListQualifiedUsers"},
            { 151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            { 152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            { 170, nullptr, "GetNasOp2MembershipStateChangeNotifier"},
            { 191, nullptr, "UpdateNotificationReceiverInfo"},
            { 200, nullptr, "BeginUserRegistration"},
            { 201, nullptr, "CompleteUserRegistration"},
            { 202, nullptr, "CancelUserRegistration"},
            { 203, nullptr, "DeleteUser"},
            { 204, nullptr, "SetUserPosition"},
            { 205, nullptr, "GetProfileEditor"},
            { 206, nullptr, "CompleteUserRegistrationForcibly"},
            { 210, nullptr, "CreateFloatingRegistrationRequest"},
            { 211, nullptr, "CreateProcedureToRegisterUserWithNintendoAccount"},
            { 212, nullptr, "ResumeProcedureToRegisterUserWithNintendoAccount"},
            { 213, nullptr, "CreateProcedureToCreateUserWithNintendoAccount"},
            { 214, nullptr, "ResumeProcedureToCreateUserWithNintendoAccount"},
            { 215, nullptr, "ResumeProcedureToCreateUserWithNintendoAccountAfterApplyResponse"},
            { 230, nullptr, "AuthenticateServiceAsync"},
            { 250, nullptr, "GetBaasAccountAdministrator"},
            { 251, nullptr, "SynchronizeNetworkServiceAccountsSnapshotAsync"},
            { 290, nullptr, "ProxyProcedureForGuestLoginWithNintendoAccount"},
            { 291, nullptr, "ProxyProcedureForFloatingRegistrationWithNintendoAccount"},
            { 292, nullptr, "ProxyProcedureForDeviceMigrationAuthenticatingOperatingUser"},
            { 293, nullptr, "ProxyProcedureForDeviceMigrationDownload"},
            { 299, nullptr, "SuspendBackgroundDaemon"},
            { 350, nullptr, "CreateDeviceMigrationUserExportRequest"},
            { 351, nullptr, "UploadNasCredential"},
            { 352, nullptr, "CreateDeviceMigrationUserImportRequest"},
            { 353, nullptr, "DeleteUserMigrationSaveData"},
            { 400, nullptr, "SetPinCode"},
            { 401, nullptr, "GetPinCodeLength"},
            { 402, nullptr, "GetPinCode"},
            { 403, nullptr, "GetPinCodeParity"},
            { 404, nullptr, "VerifyPinCode"},
            { 405, nullptr, "IsPinCodeVerificationForbidden"},
            { 410, nullptr, "GetPinCodeErrorCount"},
            { 411, nullptr, "ResetPinCodeErrorCount"},
            { 412, nullptr, "IncrementPinCodeErrorCount"},
            { 413, nullptr, "SetPinCodeErrorCount"},
            { 420, nullptr, "SetStartPenaltyTime"},
            { 421, nullptr, "GetStartPenaltyTime"},
            { 900, nullptr, "SetUserUnqualifiedForDebug"},
            { 901, nullptr, "UnsetUserUnqualifiedForDebug"},
            { 902, nullptr, "ListUsersUnqualifiedForDebug"},
            { 910, nullptr, "RefreshFirmwareSettingsForDebug"},
            { 997, nullptr, "DebugInvalidateTokenCacheForUser"},
            { 998, nullptr, "DebugSetUserStateClose"},
            { 999, nullptr, "DebugSetUserStateOpen"},
            { 1000, nullptr, "CreateIAccountEntityServiceForApplication"},
            { 1100, nullptr, "CreateIUserStateManager"},
            { 10050, nullptr, "IsUserRegistrationRequestPermittedForAccountPolicy"},
            { 10105, nullptr, "CheckNetworkServiceAvailabilityAsyncForAccountPolicy"}
        );
    }
};

class ACC_E_U1 final : public Module::Interface {
public:
    explicit ACC_E_U1(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e:u1") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetUserCount"},
            FunctionInfo{1, nullptr, "GetUserExistence"},
            FunctionInfo{2, nullptr, "ListAllUsers"},
            FunctionInfo{3, nullptr, "ListOpenUsers"},
            FunctionInfo{4, nullptr, "GetLastOpenedUser"},
            FunctionInfo{5, nullptr, "GetProfile"},
            FunctionInfo{6, nullptr, "GetProfileDigest"},
            FunctionInfo{50, nullptr, "IsUserRegistrationRequestPermitted"},
            FunctionInfo{51, nullptr, "TrySelectUserWithoutInteractionDeprecated"},
            FunctionInfo{99, nullptr, "DebugActivateOpenContextRetention"},
            FunctionInfo{100, nullptr, "GetUserRegistrationNotifier"},
            FunctionInfo{101, nullptr, "GetUserStateChangeNotifier"},
            FunctionInfo{102, nullptr, "GetBaasAccountManagerForSystemService"},
            FunctionInfo{103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
            FunctionInfo{104, nullptr, "GetProfileUpdateNotifier"},
            FunctionInfo{105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
            FunctionInfo{106, nullptr, "GetProfileSyncNotifier"},
            FunctionInfo{110, nullptr, "StoreSaveDataThumbnail"},
            FunctionInfo{111, nullptr, "ClearSaveDataThumbnail"},
            FunctionInfo{112, nullptr, "LoadSaveDataThumbnail"},
            FunctionInfo{113, nullptr, "GetSaveDataThumbnailExistence"},
            FunctionInfo{120, nullptr, "ListOpenUsersInApplication"},
            FunctionInfo{130, nullptr, "ActivateOpenContextRetention"},
            FunctionInfo{140, nullptr, "ListQualifiedUsers"},
            FunctionInfo{151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            FunctionInfo{152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            FunctionInfo{170, nullptr, "GetNasOp2MembershipStateChangeNotifier"},
            FunctionInfo{191, nullptr, "UpdateNotificationReceiverInfo"},
            FunctionInfo{997, nullptr, "DebugInvalidateTokenCacheForUser"},
            FunctionInfo{998, nullptr, "DebugSetUserStateClose"},
            FunctionInfo{999, nullptr, "DebugSetUserStateOpen"}
        );
    }
};

class ACC_E_U2 final : public Module::Interface {
public:
    explicit ACC_E_U2(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e:u2") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetUserCount"},
            FunctionInfo{1, nullptr, "GetUserExistence"},
            FunctionInfo{2, nullptr, "ListAllUsers"},
            FunctionInfo{3, nullptr, "ListOpenUsers"},
            FunctionInfo{4, nullptr, "GetLastOpenedUser"},
            FunctionInfo{5, nullptr, "GetProfile"},
            FunctionInfo{6, nullptr, "GetProfileDigest"},
            FunctionInfo{50, nullptr, "#IsUserRegistrationRequestPermitted"},
            FunctionInfo{51, nullptr, "TrySelectUserWithoutInteractionDeprecated"},
            FunctionInfo{52, nullptr, "TrySelectUserWithoutInteraction"},
            FunctionInfo{99, nullptr, "DebugActivateOpenContextRetention"},
            FunctionInfo{100, nullptr, "GetUserRegistrationNotifier"},
            FunctionInfo{101, nullptr, "GetUserStateChangeNotifier"},
            FunctionInfo{102, nullptr, "GetBaasAccountManagerForSystemService"},
            FunctionInfo{103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
            FunctionInfo{104, nullptr, "GetProfileUpdateNotifier"},
            FunctionInfo{105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
            FunctionInfo{106, nullptr, "GetProfileSyncNotifier"},
            FunctionInfo{110, nullptr, "StoreSaveDataThumbnail"},
            FunctionInfo{111, nullptr, "ClearSaveDataThumbnail"},
            FunctionInfo{112, nullptr, "LoadSaveDataThumbnail"},
            FunctionInfo{113, nullptr, "GetSaveDataThumbnailExistence"},
            FunctionInfo{120, nullptr, "ListOpenUsersInApplication"},
            FunctionInfo{130, nullptr, "ActivateOpenContextRetention"},
            FunctionInfo{140, nullptr, "ListQualifiedUsers"},
            FunctionInfo{151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            FunctionInfo{152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            FunctionInfo{170, nullptr, "GetNasOp2MembershipStateChangeNotifier"},
            FunctionInfo{191, nullptr, "UpdateNotificationReceiverInfo"},
            FunctionInfo{205, nullptr, "GetProfileEditor"},
            FunctionInfo{401, nullptr, "GetPinCodeLength"},
            FunctionInfo{402, nullptr, "GetPinCode"},
            FunctionInfo{403, nullptr, "GetPinCodeParity"},
            FunctionInfo{404, nullptr, "VerifyPinCode"},
            FunctionInfo{405, nullptr, "IsPinCodeVerificationForbidden"},
            FunctionInfo{997, nullptr, "DebugInvalidateTokenCacheForUser"},
            FunctionInfo{998, nullptr, "DebugSetUserStateClose"},
            FunctionInfo{999, nullptr, "DebugSetUserStateOpen"}
        );
    }
};

void LoopProcess(Core::System& system) {
    auto module = std::make_shared<Module>();
    auto profile_manager = std::make_shared<ProfileManager>();
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("acc:aa", std::make_shared<ACC_AA>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:su", std::make_shared<ACC_SU>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:u0", std::make_shared<ACC_U0>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:u1", std::make_shared<ACC_U1>(module, profile_manager, system));

    server_manager->RegisterNamedService("acc:e", std::make_shared<ACC_E>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:e:u1", std::make_shared<ACC_E_U1>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:e:u2", std::make_shared<ACC_E_U2>(module, profile_manager, system));

    server_manager->RegisterNamedService("dauth:0", std::make_shared<DAUTH_0>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Account
