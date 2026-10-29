// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/result.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/ngc/ngc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "frontend_common/firmware_manager.h"

namespace Service::NGC {

class IService final : public ServiceFramework<IService> {
public:
    explicit IService(Core::System& system_) : ServiceFramework{system_, "ngct:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IService::Match, "Match"},
            {1, &IService::Filter, "Filter"},
        };
        // clang-format on

        RegisterHandlers(functions);
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
    explicit NgcServiceImpl(Core::System& system_) : ServiceFramework(system_, "ngc:u") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NgcServiceImpl::GetContentVersion, "GetContentVersion"},
            {1, &NgcServiceImpl::Check, "Check"},
            {2, &NgcServiceImpl::Mask, "Mask"},
            {3, &NgcServiceImpl::Reload, "Reload"},
            {4, &NgcServiceImpl::Check, "Check2"},
            {5, &NgcServiceImpl::Mask, "Mask2"},
        };
        // clang-format on

        RegisterHandlers(functions);
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
    explicit IServiceWithManagementApi(Core::System& system_) : ServiceFramework(system_, "ngct:s") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0 , nullptr, "Match"},
            {1 , nullptr, "Filter"},
            {100, nullptr, "ConfigureAutoUpdateSetting"},
            {101, nullptr, "RequestResourceUpdateCheck"},
            {110, nullptr, "Reload"},
            {111, nullptr, "IsReloadRequired"},
            {112, nullptr, "TryAcquireReloadRequestNotifier"},
            {120, nullptr, "CalculateContentFingerprint"},
            {130, nullptr, "TryEnableTemporalPassThrough"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
};

// Results returned by the stpl:u user shim object for out-of-range arguments.
constexpr Result ResultInvalidStreamPlayType{0x2efb1f};
constexpr Result ResultInvalidStreamPlayMode{0x2ee91f};
constexpr Result ResultInvalidStreamPlayGuestCount{0x2ee31f};
constexpr Result ResultInvalidStreamPlayVersion{0x32cf1f};

struct SaveDataHandle {
    u64 unk0;
};
static_assert(sizeof(SaveDataHandle) == 0x08);

class IUserShimScopedObject final : public ServiceFramework<IUserShimScopedObject> {
public:
    explicit IUserShimScopedObject(Core::System& system_) : ServiceFramework(system_, "IUserShimScopedObject") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, D<&IUserShimScopedObject::GetRequiredMemorySize>, "GetRequiredMemorySize"},
            {2, D<&IUserShimScopedObject::Initialize>, "Initialize"},
            {300, D<&IUserShimScopedObject::GetGuestHidSharedMemory>, "GetGuestHidSharedMemory"},
            {450, nullptr, "InitializeForSaveData"},
            {451, nullptr, "FinalizeForSaveData"},
            {452, D<&IUserShimScopedObject::OpenSaveData>, "OpenSaveData"},
            {453, nullptr, "CloseSaveData"},
            {454, D<&IUserShimScopedObject::ReadSaveSlot>, "ReadSaveSlot"},
            {455, D<&IUserShimScopedObject::WriteSaveSlot>, "WriteSaveSlot"},
            {456, nullptr, "FlushSaveSlot"},
            {457, nullptr, "CommitSaveData"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    ~IUserShimScopedObject() override {
        if (m_guest_hid_memory) {
            m_guest_hid_memory->Close(kernel);
        }
    }

    struct GuestHidMemoryConfig {
        u64 size;
        u64 offset;
    };

    Result GetRequiredMemorySize(Out<u32> out_size, u8 type, u8 mode, s32 guest_count) {
        LOG_INFO(Service_NGC, "GameShare work memory query type={}, mode={}, guests={}", type, mode, guest_count);
        // The SDK recovers the guest count in nn::streamplay::Initialize as
        // buffer_size / GetRequiredMemorySize(type, mode, 1), clamped to 3, so
        // the requirement has to scale linearly with the guest count. The service
        // computes base + guests * per_guest in 64 bits with no limit on the guest
        // count, and rejects only a type or mode above 1.
        R_UNLESS(type <= 1, ResultInvalidStreamPlayType);
        R_UNLESS(mode <= 1, ResultInvalidStreamPlayMode);
        // The disconnected implementation reserves one page per guest for its
        // session control state. Streaming/encoding work is not implemented.
        const s64 size = static_cast<s64>(guest_count) * 0x1000;
        *out_size = static_cast<u32>(size);
        R_SUCCEED();
    }

    Result Initialize(u8 type, u8 mode, s32 guest_count, u64 context, ClientProcessId pid) {
        LOG_INFO(Service_NGC, "GameShare initialize type={}, mode={}, guests={}, context={:#x}", type, mode,
                 guest_count, context);
        // The service checks the guest count (signed, upper bound 3) before the type and mode.
        R_UNLESS(guest_count <= 3, ResultInvalidStreamPlayGuestCount);
        R_UNLESS(type <= 1, ResultInvalidStreamPlayType);
        R_UNLESS(mode <= 1, ResultInvalidStreamPlayMode);
        m_stream_type = type;
        m_stream_mode = mode;
        m_guest_count = guest_count;
        R_SUCCEED();
    }

    Result GetGuestHidSharedMemory(Out<GuestHidMemoryConfig> out_config,
                                 OutCopyHandle<Kernel::KSharedMemory> out_memory) {
        // Three guest pad blocks, touch rings, and JCMS rings. Empty rings and
        // zero style sets describe no connected GameShare guests.
        constexpr u64 GuestHidMemorySize = 0x84000;
        if (!m_guest_hid_memory) {
            auto* memory = Kernel::KSharedMemory::Create(kernel);
            const auto result = memory->Initialize(kernel, system.DeviceMemory(), nullptr,
                Kernel::Svc::MemoryPermission::ReadWrite, Kernel::Svc::MemoryPermission::Read,
                GuestHidMemorySize);
            if (R_FAILED(result)) {
                memory->Close(kernel);
                R_RETURN(result);
            }
            Kernel::KSharedMemory::Register(kernel, memory);
            m_guest_hid_memory = memory;
        }
        *out_config = GuestHidMemoryConfig{GuestHidMemorySize, 0};
        *out_memory = m_guest_hid_memory;
        LOG_DEBUG(Service_NGC, "GameShare guest HID memory size={:#x}, no guests connected", GuestHidMemorySize);
        R_SUCCEED();
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

private:
    Kernel::KSharedMemory* m_guest_hid_memory{};
    u8 m_stream_type{};
    u8 m_stream_mode{};
    s32 m_guest_count{};
};

class IUserService final : public ServiceFramework<IUserService> {
public:
    explicit IUserService(Core::System& system_) : ServiceFramework(system_, "stpl:u") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0 , D<&IUserService::Cmd0>, "Cmd0"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    // The input is the client API version. The service accepts a zero minor/major
    // pair, major 1 with minor below 5, and majors 2 and 3 with minor 0; the legacy
    // encoding 0x100 means 1.0 and 0x10100 means 1.1.
    static bool IsSupportedVersion(u32 version) {
        u32 v = version;
        if ((version & 0xffff) == 0x100 && ((version >> 16) & 0xff) == 0 && (version >> 24) == 0) {
            v = 0x10000;
        } else if ((version & 0xffff) == 0x100 && ((version >> 16) & 0xff) == 1 && (version >> 24) == 0) {
            v = 0x10100;
        }
        const u32 mid = v & 0xffff00;
        if (mid == 0) {
            return true;
        }
        const u32 major = v & 0xff0000;
        if (major != 0 && (mid >> 8) < 0x105) {
            return true;
        }
        if (major >= 0x200000 && (mid >> 8) < 0x201) {
            return true;
        }
        return major >= 0x300000 && mid < 0x30100;
    }

    Result Cmd0(u32 version, OutInterface<IUserShimScopedObject> out_interface) {
        LOG_WARNING(Service_NGC, "stubbed, version={:#x}", version);
        R_UNLESS(IsSupportedVersion(version), ResultInvalidStreamPlayVersion);
        *out_interface = std::make_shared<IUserShimScopedObject>(system);
        R_SUCCEED();
    }
};

class ISystemShimScopedObject final : public ServiceFramework<ISystemShimScopedObject> {
public:
    explicit ISystemShimScopedObject(Core::System& system_) : ServiceFramework(system_, "ISystemShimScopedObject") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {106, nullptr, "Cmd106"},
            {107, nullptr, "Cmd107"},
            {108, D<&ISystemShimScopedObject::Cmd108>, "Cmd108"},
            {207, nullptr, "Cmd207"},
            {208, D<&ISystemShimScopedObject::Cmd208>, "Cmd208"},
            {209, nullptr, "Cmd209"},
            {210, nullptr, "Cmd210"},
            {211, nullptr, "Cmd211"},
            {212, nullptr, "Cmd212"},
        };
        // clang-format on
        RegisterHandlers(functions);
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
    explicit ISystemService(Core::System& system_) : ServiceFramework(system_, "stpl:sys") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0 , D<&ISystemService::Cmd0>, "Cmd0"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    Result Cmd0(OutInterface<ISystemShimScopedObject> out_interface) {
        LOG_WARNING(Service_NGC, "stubbed");
        *out_interface = std::make_shared<ISystemShimScopedObject>(system);
        R_SUCCEED();
    }
};

// The chat module (firmware 040000000000004a) serves "chat:u" (nn::chat::IUserService) next to stpl:u.
// MKW's nn::chat::Initialize (sdk+0x6bbd70) requests it from sm and then runs, all read from the module:
//  - IUserService cmd 0 (04a+0x2b12d0 -> 04a+0x1f7e40): no data input (the 0x100001 / 1 pair in the
//    request is the CMIF domain header), returns the IUserShimScopedObject.
//  - shim cmd 1 (04a+0x206250): returns the object's event once as a copy handle and fails with 0x4e2
//    (module 226, description 2) on any later call.
//  - IUserService cmd 3 (04a+0x2b1430 -> 04a+0x1f7fe0): client process id in, returns the
//    IVirtualCameraShimScopedObject; Initialize only keeps it (sdk+0x6c0670).
// The remaining commands are not implemented: unknown ones are logged with the port name.
class IChatUserShimScopedObject final : public ServiceFramework<IChatUserShimScopedObject> {
public:
    explicit IChatUserShimScopedObject(Core::System& system_)
        : ServiceFramework(system_, "nn::chat::IUserShimScopedObject"), m_context{system_, "chat:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, D<&IChatUserShimScopedObject::GetEvent0>, "GetEvent0"},
            {2, D<&IChatUserShimScopedObject::GetEvent1>, "GetEvent1"},
            {3, D<&IChatUserShimScopedObject::GetEvent2>, "GetEvent2"},
            {4, D<&IChatUserShimScopedObject::GetEvent3>, "GetEvent3"},
            {5, D<&IChatUserShimScopedObject::GetEvent4>, "GetEvent4"},
            {6, D<&IChatUserShimScopedObject::GetEvent5>, "GetEvent5"},
            {7, D<&IChatUserShimScopedObject::GetEvent6>, "GetEvent6"},
            {10, D<&IChatUserShimScopedObject::GetEvent7>, "GetEvent7"},
        };
        // clang-format on
        RegisterHandlers(functions);
        for (auto& event : m_events) {
            event = m_context.CreateEvent("ChatUserShimEvent");
        }
    }
    ~IChatUserShimScopedObject() override {
        for (auto* event : m_events) {
            m_context.CloseEvent(event);
        }
    }

    Result GetEvent0(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 0); }
    Result GetEvent1(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 1); }
    Result GetEvent2(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 2); }
    Result GetEvent3(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 3); }
    Result GetEvent4(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 4); }
    Result GetEvent5(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 5); }
    Result GetEvent6(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 6); }
    Result GetEvent7(OutCopyHandle<Kernel::KReadableEvent> out_event) { return GetEvent(out_event, 7); }

private:
    // Commands 1-7 and 10 (04a+0x206250, 0x2062e0, 0x206370, 0x206400, 0x206490, 0x206520, 0x2065b0,
    // 0x206640) share one body: hand out the command's own event once as a copy handle, then fail
    // with 0x4e2 (module 226, description 2).
    Result GetEvent(OutCopyHandle<Kernel::KReadableEvent>& out_event, size_t index) {
        LOG_DEBUG(Service_NGC, "called, event={}", index);
        R_UNLESS(!m_event_taken[index], Result(static_cast<ErrorModule>(226), 2));
        m_event_taken[index] = true;
        *out_event = &m_events[index]->GetReadableEvent();
        R_SUCCEED();
    }

    KernelHelpers::ServiceContext m_context;
    std::array<Kernel::KEvent*, 8> m_events{};
    std::array<bool, 8> m_event_taken{};
};

class IChatVirtualCameraShimScopedObject final
    : public ServiceFramework<IChatVirtualCameraShimScopedObject> {
public:
    explicit IChatVirtualCameraShimScopedObject(Core::System& system_)
        : ServiceFramework(system_, "nn::chat::IVirtualCameraShimScopedObject") {}
};

class IChatUserService final : public ServiceFramework<IChatUserService> {
public:
    explicit IChatUserService(Core::System& system_) : ServiceFramework(system_, "chat:u") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IChatUserService::GetUserShimScopedObject>, "GetUserShimScopedObject"},
            {3, D<&IChatUserService::GetVirtualCameraShimScopedObject>, "GetVirtualCameraShimScopedObject"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    Result GetUserShimScopedObject(OutInterface<IChatUserShimScopedObject> out_interface) {
        LOG_DEBUG(Service_NGC, "called");
        *out_interface = std::make_shared<IChatUserShimScopedObject>(system);
        R_SUCCEED();
    }

    Result GetVirtualCameraShimScopedObject(OutInterface<IChatVirtualCameraShimScopedObject> out_interface,
                                            ClientProcessId process_id) {
        LOG_DEBUG(Service_NGC, "called, pid={}", process_id.pid);
        *out_interface = std::make_shared<IChatVirtualCameraShimScopedObject>(system);
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
        server_manager->RegisterNamedService("chat:u", std::make_shared<IChatUserService>(system), 4);
    }

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NGC
