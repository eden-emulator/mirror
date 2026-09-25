// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/result.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ngc/ngc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "frontend_common/firmware_manager.h"

namespace Service::NGC {

class IService final : public ServiceFramework<IService> {
public:
    explicit IService(Core::System& system_) : ServiceFramework{system_, "ngct:u"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, &IService::Match, "Match"},
            FunctionInfo{1, &IService::Filter, "Filter"}
        );
    }

private:
    void Match(HLERequestContext& ctx) {
        const auto buffer = ctx.ReadBuffer();
        const auto text = !buffer.empty()
            ? Common::StringFromFixedZeroTerminatedBuffer(reinterpret_cast<const char*>(buffer.data()), buffer.size())
            : std::string{};

        LOG_WARNING(Service_NGC, "(STUBBED) called, text={}", text);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        // Return false since we don't censor anything
        rb.Push(false);
    }

    void Filter(HLERequestContext& ctx) {
        const auto buffer = ctx.ReadBuffer();
        const auto text = !buffer.empty()
            ? Common::StringFromFixedZeroTerminatedBuffer(reinterpret_cast<const char*>(buffer.data()), buffer.size())
            : std::string{};

        LOG_WARNING(Service_NGC, "(STUBBED) called, text={}", text);

        // Return the same string since we don't censor anything
        ctx.WriteBuffer(buffer);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class NgcServiceImpl final : public ServiceFramework<NgcServiceImpl> {
public:
    explicit NgcServiceImpl(Core::System& system_) : ServiceFramework(system_, "ngc:u") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, &NgcServiceImpl::GetContentVersion, "GetContentVersion"},
            FunctionInfo{1, &NgcServiceImpl::Check, "Check"},
            FunctionInfo{2, &NgcServiceImpl::Mask, "Mask"},
            FunctionInfo{3, &NgcServiceImpl::Reload, "Reload"},
            FunctionInfo{4, &NgcServiceImpl::Check, "Check2"},
            FunctionInfo{5, &NgcServiceImpl::Mask, "Mask2"}
        );
    }

private:
    static constexpr u32 NgcContentVersion = 1;

    // This is nn::ngc::detail::ProfanityFilterOption
    struct ProfanityFilterOption {
        INSERT_PADDING_BYTES_NOINIT(0x20);
    };
    static_assert(sizeof(ProfanityFilterOption) == 0x20,
                  "ProfanityFilterOption has incorrect size");

    void GetContentVersion(HLERequestContext& ctx) {
        LOG_INFO(Service_NGC, "(STUBBED) called");

        // This calls nn::ngc::ProfanityFilter::GetContentVersion
        const u32 version = NgcContentVersion;

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(version);
    }

    void Check(HLERequestContext& ctx) {
        LOG_INFO(Service_NGC, "(STUBBED) called");

        struct InputParameters {
            u32 flags;
            ProfanityFilterOption option;
        };

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto params = rp.PopRaw<InputParameters>();
        [[maybe_unused]] const auto input = ctx.ReadBuffer(0);

        // This calls nn::ngc::ProfanityFilter::CheckProfanityWords
        const u32 out_flags = 0;

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(out_flags);
    }

    void Mask(HLERequestContext& ctx) {
        LOG_INFO(Service_NGC, "(STUBBED) called");

        struct InputParameters {
            u32 flags;
            ProfanityFilterOption option;
        };

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto params = rp.PopRaw<InputParameters>();
        const auto input = ctx.ReadBuffer(0);

        // This calls nn::ngc::ProfanityFilter::MaskProfanityWordsInText
        const u32 out_flags = 0;
        ctx.WriteBuffer(input);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(out_flags);
    }

    void Reload(HLERequestContext& ctx) {
        LOG_INFO(Service_NGC, "(STUBBED) called");

        // This reloads the database.

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class IServiceWithManagementApi final : public ServiceFramework<IServiceWithManagementApi> {
public:
    explicit IServiceWithManagementApi(Core::System& system_) : ServiceFramework(system_, "ngct:s") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            {0 , nullptr, "Match"},
            {1 , nullptr, "Filter"},
            FunctionInfo{100, nullptr, "ConfigureAutoUpdateSetting"},
            FunctionInfo{101, nullptr, "RequestResourceUpdateCheck"},
            FunctionInfo{110, nullptr, "Reload"},
            FunctionInfo{111, nullptr, "IsReloadRequired"},
            FunctionInfo{112, nullptr, "TryAcquireReloadRequestNotifier"},
            FunctionInfo{120, nullptr, "CalculateContentFingerprint"},
            FunctionInfo{130, nullptr, "TryEnableTemporalPassThrough"}
        );
    }
};

struct SaveDataHandle {
    u64 unk0;
};
static_assert(sizeof(SaveDataHandle) == 0x08);

class IUserShimScopedObject final : public ServiceFramework<IUserShimScopedObject> {
public:
    explicit IUserShimScopedObject(Core::System& system_) : ServiceFramework(system_, "IUserShimScopedObject") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{450, nullptr, "InitializeForSaveData"},
            FunctionInfo{451, nullptr, "FinalizeForSaveData"},
            FunctionInfo{452, D<&IUserShimScopedObject::OpenSaveData>, "OpenSaveData"},
            FunctionInfo{453, nullptr, "CloseSaveData"},
            FunctionInfo{454, D<&IUserShimScopedObject::ReadSaveSlot>, "ReadSaveSlot"},
            FunctionInfo{455, D<&IUserShimScopedObject::WriteSaveSlot>, "WriteSaveSlot"},
            FunctionInfo{456, nullptr, "FlushSaveSlot"},
            FunctionInfo{457, nullptr, "CommitSaveData"}
        );
    }

    Result OpenSaveData(Account::Uid unk0, Out<SaveDataHandle> unk1) {
        LOG_WARNING(Service_NGC, "stubbed");
        R_THROW(IPC::ResultNotSupported);
    }

    Result ReadSaveSlot(s32 offset, SaveDataHandle handle, OutBuffer<BufferAttr_HipcAutoSelect> out_data, Out<u32> out_size) {
        LOG_WARNING(Service_NGC, "stubbed");
        R_THROW(IPC::ResultNotSupported);
    }

    Result WriteSaveSlot(s32 offset, SaveDataHandle handle, InBuffer<BufferAttr_HipcAutoSelect> out_data) {
        LOG_WARNING(Service_NGC, "stubbed");
        // to implement
        R_SUCCEED();
    }
};

class IUserService final : public ServiceFramework<IUserService> {
public:
    explicit IUserService(Core::System& system_) : ServiceFramework(system_, "stpl:u") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            {0 , D<&IUserService::Cmd0>, "Cmd0"}
        );
    }
    Result Cmd0(u32 unk0, OutInterface<IUserShimScopedObject> out_interface) {
        LOG_WARNING(Service_NGC, "stubbed");
        *out_interface = std::make_shared<IUserShimScopedObject>(system);
        R_SUCCEED();
    }
};

class ISystemShimScopedObject final : public ServiceFramework<ISystemShimScopedObject> {
public:
    explicit ISystemShimScopedObject(Core::System& system_) : ServiceFramework(system_, "ISystemShimScopedObject") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{106, nullptr, "Cmd106"},
            FunctionInfo{107, nullptr, "Cmd107"},
            FunctionInfo{108, D<&ISystemShimScopedObject::Cmd108>, "Cmd108"},
            FunctionInfo{207, nullptr, "Cmd207"},
            FunctionInfo{208, D<&ISystemShimScopedObject::Cmd208>, "Cmd208"},
            FunctionInfo{209, nullptr, "Cmd209"},
            FunctionInfo{210, nullptr, "Cmd210"},
            FunctionInfo{211, nullptr, "Cmd211"},
            FunctionInfo{212, nullptr, "Cmd212"}
        );
    }

    Result Cmd108() {
        LOG_WARNING(Service_NGC, "stubbed");
        R_THROW(IPC::ResultNotSupported);
    }

    Result Cmd208(Out<std::array<u8, 0x20>> unk0) {
        LOG_WARNING(Service_NGC, "stubbed");
        R_THROW(IPC::ResultNotSupported);
    }
};

class ISystemService final : public ServiceFramework<ISystemService> {
public:
    explicit ISystemService(Core::System& system_) : ServiceFramework(system_, "stpl:sys") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            {0 , D<&ISystemService::Cmd0>, "Cmd0"}
        );
    }
    Result Cmd0(OutInterface<ISystemShimScopedObject> out_interface) {
        LOG_WARNING(Service_NGC, "stubbed");
        *out_interface = std::make_shared<ISystemShimScopedObject>(system);
        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ngct:u", std::make_shared<IService>(system), 4);
    server_manager->RegisterNamedService("ngct:s", std::make_shared<IServiceWithManagementApi>(system), 4);
    server_manager->RegisterNamedService("ngc:u", std::make_shared<NgcServiceImpl>(system), 4);

    // +23.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 23) {
        server_manager->RegisterNamedService("stpl:u", std::make_shared<IUserService>(system), 4);
        server_manager->RegisterNamedService("stpl:sys", std::make_shared<ISystemService>(system), 4);
    }

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NGC
