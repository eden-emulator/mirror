// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "core/core.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/api_version.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ncm/ncm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NCM {

class ILocationResolver final : public ServiceFramework<ILocationResolver> {
public:
    explicit ILocationResolver(Core::System& system_, FileSys::StorageId id)
        : ServiceFramework{system_, "ILocationResolver"}, storage{id} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ResolveProgramPath"},
            FunctionInfo{1, nullptr, "RedirectProgramPath"},
            FunctionInfo{2, nullptr, "ResolveApplicationControlPath"},
            FunctionInfo{3, nullptr, "ResolveApplicationHtmlDocumentPath"},
            FunctionInfo{4, nullptr, "ResolveDataPath"},
            FunctionInfo{5, nullptr, "RedirectApplicationControlPath"},
            FunctionInfo{6, nullptr, "RedirectApplicationHtmlDocumentPath"},
            FunctionInfo{7, nullptr, "ResolveApplicationLegalInformationPath"},
            FunctionInfo{8, nullptr, "RedirectApplicationLegalInformationPath"},
            FunctionInfo{9, nullptr, "Refresh"},
            FunctionInfo{10, nullptr, "RedirectApplicationProgramPath"},
            FunctionInfo{11, nullptr, "ClearApplicationRedirection"},
            FunctionInfo{12, nullptr, "EraseProgramRedirection"},
            FunctionInfo{13, nullptr, "EraseApplicationControlRedirection"},
            FunctionInfo{14, nullptr, "EraseApplicationHtmlDocumentRedirection"},
            FunctionInfo{15, nullptr, "EraseApplicationLegalInformationRedirection"},
            FunctionInfo{16, nullptr, "ResolveProgramPathForDebug"},
            FunctionInfo{17, nullptr, "RedirectProgramPathForDebug"},
            FunctionInfo{18, nullptr, "RedirectApplicationProgramPathForDebug"},
            FunctionInfo{19, nullptr, "EraseProgramRedirectionForDebug"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    [[maybe_unused]] FileSys::StorageId storage;
};

class IRegisteredLocationResolver final : public ServiceFramework<IRegisteredLocationResolver> {
public:
    explicit IRegisteredLocationResolver(Core::System& system_)
        : ServiceFramework{system_, "IRegisteredLocationResolver"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ResolveProgramPath"},
            FunctionInfo{1, nullptr, "RegisterProgramPath"},
            FunctionInfo{2, nullptr, "UnregisterProgramPath"},
            FunctionInfo{3, nullptr, "RedirectProgramPath"},
            FunctionInfo{4, nullptr, "ResolveHtmlDocumentPath"},
            FunctionInfo{5, nullptr, "RegisterHtmlDocumentPath"},
            FunctionInfo{6, nullptr, "UnregisterHtmlDocumentPath"},
            FunctionInfo{7, nullptr, "RedirectHtmlDocumentPath"},
            FunctionInfo{8, nullptr, "Refresh"},
            FunctionInfo{9, nullptr, "RefreshExcluding"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IAddOnContentLocationResolver final : public ServiceFramework<IAddOnContentLocationResolver> {
public:
    explicit IAddOnContentLocationResolver(Core::System& system_)
        : ServiceFramework{system_, "IAddOnContentLocationResolver"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ResolveAddOnContentPath"},
            FunctionInfo{1, nullptr, "RegisterAddOnContentStorage"},
            FunctionInfo{2, nullptr, "UnregisterAllAddOnContentPath"},
            FunctionInfo{3, nullptr, "RefreshApplicationAddOnContent"},
            FunctionInfo{4, nullptr, "UnregisterApplicationAddOnContent"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IContentStorage final : public ServiceFramework<IContentStorage> {
public:
    explicit IContentStorage(Core::System& system_, FileSys::StorageId id)
        : ServiceFramework{system_, "IContentStorage"}, storage{id} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void GeneratePlaceHolderId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NCM, "called");

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(FileSys::PlaceholderCache::Generate());
    }

    void CreatePlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] FileSys::NcaID content_id{};
        FileSys::NcaID placeholder_id{};
        if constexpr (HLE::ApiVersion::HOS_VERSION_MAJOR >= 16) {
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
            content_id = rp.PopRaw<FileSys::NcaID>();
        } else {
            content_id = rp.PopRaw<FileSys::NcaID>();
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
        }
        const auto size = rp.Pop<s64>();

        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr && size >= 0 &&
            (placeholder_cache->Exists(placeholder_id) ||
             placeholder_cache->Create(placeholder_id, static_cast<u64>(size)));

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}, size={}", static_cast<u32>(storage),
                      size);
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}, size={}", static_cast<u32>(storage),
                        size);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void DeletePlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();

        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr &&
            (!placeholder_cache->Exists(placeholder_id) || placeholder_cache->Delete(placeholder_id));

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}", static_cast<u32>(storage));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void WritePlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        const auto offset = rp.Pop<u64>();
        const auto data = ctx.ReadBuffer();

        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const std::vector<u8> write_data{data.begin(), data.end()};
        const bool succeeded =
            placeholder_cache != nullptr &&
            placeholder_cache->Write(placeholder_id, offset, write_data);

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}, offset={}, size={}",
                      static_cast<u32>(storage), offset, data.size());
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}, offset={}, size={}",
                        static_cast<u32>(storage), offset, data.size());
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void Register(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        FileSys::NcaID content_id{};
        FileSys::NcaID placeholder_id{};
        if constexpr (HLE::ApiVersion::HOS_VERSION_MAJOR >= 16) {
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
            content_id = rp.PopRaw<FileSys::NcaID>();
        } else {
            content_id = rp.PopRaw<FileSys::NcaID>();
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
        }

        auto& fsc = system.GetFileSystemController();
        auto* const placeholder_cache = fsc.GetPlaceholderCacheForStorage(storage);
        auto* const registered_cache = fsc.GetRegisteredCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr && registered_cache != nullptr &&
            placeholder_cache->Register(registered_cache, placeholder_id, content_id);

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}", static_cast<u32>(storage));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void Delete(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto content_id = rp.PopRaw<FileSys::NcaID>();

        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const bool succeeded = registered_cache != nullptr && registered_cache->Delete(content_id);
        if (succeeded) {
            registered_cache->Refresh();
        }

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}", static_cast<u32>(storage));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IContentStorage::GeneratePlaceHolderId, "GeneratePlaceHolderId"},
        FunctionInfo{1, &IContentStorage::CreatePlaceHolder, "CreatePlaceHolder"},
        FunctionInfo{2, &IContentStorage::DeletePlaceHolder, "DeletePlaceHolder"},
        FunctionInfo{4, &IContentStorage::WritePlaceHolder, "WritePlaceHolder"},
        FunctionInfo{5, &IContentStorage::Register, "Register"},
        FunctionInfo{6, &IContentStorage::Delete, "Delete"}
    );
    FileSys::StorageId storage;
};

class IContentMetaDatabase final : public ServiceFramework<IContentMetaDatabase> {
public:
    explicit IContentMetaDatabase(Core::System& system_, FileSys::StorageId id)
        : ServiceFramework{system_, "IContentMetaDatabase"}, storage{id} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    struct ContentMetaKey {
        u64 id;
        u32 version;
        FileSys::TitleType type;
        u8 install_type;
        std::array<u8, 2> padding;
    };
    static_assert(sizeof(ContentMetaKey) == 0x10);

    void Set(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();

        const auto entry_matches = [&key](const ContentMetaKey& entry) {
            return entry.id == key.id && entry.version == key.version && entry.type == key.type &&
                   entry.install_type == key.install_type;
        };
        if (std::find_if(entries.begin(), entries.end(), entry_matches) == entries.end()) {
            entries.push_back(key);
        }

        LOG_DEBUG(Service_NCM,
                  "called, storage_id={}, title_id={:016X}, version={}, type={}, size={}",
                  static_cast<u32>(storage), key.id, key.version, static_cast<u8>(key.type),
                  ctx.GetReadBufferSize());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Remove(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();

        std::erase_if(entries, [&key](const ContentMetaKey& entry) {
            return entry.id == key.id && entry.version == key.version && entry.type == key.type &&
                   entry.install_type == key.install_type;
        });

        LOG_DEBUG(Service_NCM, "called, storage_id={}, title_id={:016X}, version={}, type={}",
                  static_cast<u32>(storage), key.id, key.version, static_cast<u8>(key.type));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Has(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();

        const bool has_pending =
            std::find_if(entries.begin(), entries.end(), [&key](const ContentMetaKey& entry) {
                return entry.id == key.id && entry.version == key.version &&
                       entry.type == key.type && entry.install_type == key.install_type;
            }) != entries.end();

        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const bool has_registered =
            registered_cache != nullptr &&
            registered_cache->HasEntry(key.id, FileSys::ContentRecordType::Meta);

        LOG_DEBUG(Service_NCM, "called, storage_id={}, title_id={:016X}, version={}, type={}, has={}",
                  static_cast<u32>(storage), key.id, key.version, static_cast<u8>(key.type),
                  has_pending || has_registered);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(has_pending || has_registered);
    }

    void Commit(HLERequestContext& ctx) {
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        if (registered_cache != nullptr) {
            registered_cache->Refresh();
        }

        LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IContentMetaDatabase::Set, "Set"},
        FunctionInfo{2, &IContentMetaDatabase::Remove, "Remove"},
        FunctionInfo{8, &IContentMetaDatabase::Has, "Has"},
        FunctionInfo{15, &IContentMetaDatabase::Commit, "Commit"}
    );
    FileSys::StorageId storage;
    std::vector<ContentMetaKey> entries;
};

class LR final : public ServiceFramework<LR> {
public:
    explicit LR(Core::System& system_) : ServiceFramework{system_, "lr"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "OpenLocationResolver"},
            FunctionInfo{1, nullptr, "OpenRegisteredLocationResolver"},
            FunctionInfo{2, nullptr, "RefreshLocationResolver"},
            FunctionInfo{3, nullptr, "OpenAddOnContentLocationResolver"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class NCM final : public ServiceFramework<NCM> {
public:
    explicit NCM(Core::System& system_) : ServiceFramework{system_, "ncm"} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void OpenContentStorage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage_id));

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IContentStorage>(ctx, system, storage_id);
    }

    void OpenContentMetaDatabase(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage_id));

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IContentMetaDatabase>(ctx, system, storage_id);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "CreateContentStorage"},
        FunctionInfo{1, nullptr, "CreateContentMetaDatabase"},
        FunctionInfo{2, nullptr, "VerifyContentStorage"},
        FunctionInfo{3, nullptr, "VerifyContentMetaDatabase"},
        FunctionInfo{4, &NCM::OpenContentStorage, "OpenContentStorage"},
        FunctionInfo{5, &NCM::OpenContentMetaDatabase, "OpenContentMetaDatabase"},
        FunctionInfo{6, nullptr, "CloseContentStorageForcibly"},
        FunctionInfo{7, nullptr, "CloseContentMetaDatabaseForcibly"},
        FunctionInfo{8, nullptr, "CleanupContentMetaDatabase"},
        FunctionInfo{9, nullptr, "ActivateContentStorage"},
        FunctionInfo{10, nullptr, "InactivateContentStorage"},
        FunctionInfo{11, nullptr, "ActivateContentMetaDatabase"},
        FunctionInfo{12, nullptr, "InactivateContentMetaDatabase"},
        FunctionInfo{13, nullptr, "InvalidateRightsIdCache"},
        FunctionInfo{14, nullptr, "GetMemoryReport"},
        FunctionInfo{15, nullptr, "ActivateFsContentStorage"}
    );
};

class NCM_V final : public ServiceFramework<NCM_V> {
public:
    explicit NCM_V(Core::System& system_)
        : ServiceFramework{system_, "ncm:v"}
    {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetSystemVersion"}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("lr", std::make_shared<LR>(system));
    server_manager->RegisterNamedService("ncm", std::make_shared<NCM>(system));
    if (1 /* not retail */) {
        server_manager->RegisterNamedService("ncm:v", std::make_shared<NCM_V>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NCM
