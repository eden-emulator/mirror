// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/control_metadata.h"
#include "core/hle/api_version.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/button_poller.h"
#include "core/hle/service/am/event_observer.h"
#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/am/service/application_creator.h"
#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/debug_functions.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/am/service/home_menu_functions.h"
#include "core/hle/service/am/service/process_winding_controller.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/am/service/application_functions.h"
#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/am/process_creation.h"
#include "core/hle/service/am/service/library_applet_accessor.h"
#include "core/hle/service/am/service/overlay_functions.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/ns/language.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/service_getter_interface.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/frontend/applet_profile_select.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

struct Applet;
class IAppletCommonFunctions;
class IApplicationCreator;
class IAudioController;
class ICommonStateGetter;
class IDebugFunctions;
class IDisplayController;
class IHomeMenuFunctions;
class IGlobalStateController;
class ILibraryAppletCreator;
class IProcessWindingController;
class ISelfController;
class IWindowController;
class WindowSystem;

class IApplicationProxy final : public ServiceFramework<IApplicationProxy> {
public:
    explicit IApplicationProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                        Kernel::KProcess* process, WindowSystem& window_system)
        : ServiceFramework{system_, "IApplicationProxy"},
        m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    }

    ~IApplicationProxy() override = default;

    Result GetAudioController(
        Out<SharedPointer<IAudioController>> out_audio_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_audio_controller = std::make_shared<IAudioController>(system);
        R_SUCCEED();
    }

    Result GetDisplayController(
        Out<SharedPointer<IDisplayController>> out_display_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetDebugFunctions(
        Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_debug_functions = std::make_shared<IDebugFunctions>(system);
        R_SUCCEED();
    }

    Result GetWindowController(
        Out<SharedPointer<IWindowController>> out_window_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetSelfController(
        Out<SharedPointer<ISelfController>> out_self_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
        R_SUCCEED();
    }

    Result GetCommonStateGetter(
        Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
        LOG_DEBUG(Service_AM, "called");
        *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
        R_SUCCEED();
    }

    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
        LOG_DEBUG(Service_AM, "called");
        *out_library_applet_creator =
            std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetApplicationFunctions(
        Out<SharedPointer<IApplicationFunctions>> out_application_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_application_functions = std::make_shared<IApplicationFunctions>(system, m_applet);
        R_SUCCEED();
    }

private:
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IApplicationProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        FunctionInfo{1, D<&IApplicationProxy::GetSelfController>, "GetSelfController"},
        FunctionInfo{2, D<&IApplicationProxy::GetWindowController>, "GetWindowController"},
        FunctionInfo{3, D<&IApplicationProxy::GetAudioController>, "GetAudioController"},
        FunctionInfo{4, D<&IApplicationProxy::GetDisplayController>, "GetDisplayController"},
        FunctionInfo{10, D<&IApplicationProxy::GetProcessWindingController>, "GetProcessWindingController"},
        FunctionInfo{11, D<&IApplicationProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        FunctionInfo{20, D<&IApplicationProxy::GetApplicationFunctions>, "GetApplicationFunctions"},
        FunctionInfo{1000, D<&IApplicationProxy::GetDebugFunctions>, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

class IApplicationProxyService final : public ServiceFramework<IApplicationProxyService> {
public:
    explicit IApplicationProxyService(Core::System& system_,
                                                    WindowSystem& window_system)
        : ServiceFramework{system_, "appletOE"}, m_window_system{window_system} {
    }

    ~IApplicationProxyService() override = default;

    Result OpenApplicationProxy(
        Out<SharedPointer<IApplicationProxy>> out_application_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle) {
        LOG_DEBUG(Service_AM, "called");

        if (const auto applet = this->GetAppletFromProcessId(pid)) {
            *out_application_proxy = std::make_shared<IApplicationProxy>(
                system, applet, process_handle.Get(), m_window_system);
            R_SUCCEED();
        } else {
            UNIMPLEMENTED();
            R_THROW(ResultUnknown);
        }
    }

    std::shared_ptr<Applet> GetAppletFromProcessId(ProcessId process_id) {
        return m_window_system.GetByAppletResourceUserId(process_id.pid);
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IApplicationProxyService::OpenApplicationProxy>, "OpenApplicationProxy"}
    );
    WindowSystem& m_window_system;
};

class ISystemAppletProxy final : public ServiceFramework<ISystemAppletProxy> {
public:
    explicit ISystemAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet, Kernel::KProcess* process, WindowSystem& window_system)
        : ServiceFramework{system_, "ISystemAppletProxy"},
        m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    }

    ~ISystemAppletProxy() override = default;

    Result GetAudioController(
        Out<SharedPointer<IAudioController>> out_audio_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_audio_controller = std::make_shared<IAudioController>(system);
        R_SUCCEED();
    }

    Result GetDisplayController(
        Out<SharedPointer<IDisplayController>> out_display_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetDebugFunctions(
        Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_debug_functions = std::make_shared<IDebugFunctions>(system);
        R_SUCCEED();
    }

    Result GetWindowController(
        Out<SharedPointer<IWindowController>> out_window_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetSelfController(
        Out<SharedPointer<ISelfController>> out_self_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
        R_SUCCEED();
    }

    Result GetCommonStateGetter(
        Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
        LOG_DEBUG(Service_AM, "called");
        *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
        R_SUCCEED();
    }

    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
        LOG_DEBUG(Service_AM, "called");
        *out_library_applet_creator =
            std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetApplicationCreator(
        Out<SharedPointer<IApplicationCreator>> out_application_creator) {
        LOG_DEBUG(Service_AM, "called");
        *out_application_creator = std::make_shared<IApplicationCreator>(system, m_window_system);
        R_SUCCEED();
    }

    Result GetAppletCommonFunctions(
        Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_applet_common_functions = std::make_shared<IAppletCommonFunctions>(system, m_applet);
        R_SUCCEED();
    }

    Result GetHomeMenuFunctions(
        Out<SharedPointer<IHomeMenuFunctions>> out_home_menu_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_home_menu_functions =
            std::make_shared<IHomeMenuFunctions>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetGlobalStateController(
        Out<SharedPointer<IGlobalStateController>> out_global_state_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISystemAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        FunctionInfo{1, D<&ISystemAppletProxy::GetSelfController>, "GetSelfController"},
        FunctionInfo{2, D<&ISystemAppletProxy::GetWindowController>, "GetWindowController"},
        FunctionInfo{3, D<&ISystemAppletProxy::GetAudioController>, "GetAudioController"},
        FunctionInfo{4, D<&ISystemAppletProxy::GetDisplayController>, "GetDisplayController"},
        FunctionInfo{10, D<&ISystemAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        FunctionInfo{11, D<&ISystemAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        FunctionInfo{20, D<&ISystemAppletProxy::GetHomeMenuFunctions>, "GetHomeMenuFunctions"},
        FunctionInfo{21, D<&ISystemAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        FunctionInfo{22, D<&ISystemAppletProxy::GetApplicationCreator>, "GetApplicationCreator"},
        FunctionInfo{23, D<&ISystemAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        FunctionInfo{1000, D<&ISystemAppletProxy::GetDebugFunctions>, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

static AppletIdentityInfo GetCallerIdentity(Applet& applet) {
    if (const auto caller_applet = applet.caller_applet.lock(); caller_applet) {
        // TODO: is this actually the application ID?
        return {
            .applet_id = caller_applet->applet_id,
            .application_id = caller_applet->program_id,
        };
    } else {
        return {
            .applet_id = AppletId::QLaunch,
            .application_id = 0x0100000000001000ull,
        };
    }
}

class ILibraryAppletSelfAccessor final : public ServiceFramework<ILibraryAppletSelfAccessor> {
public:
    ILibraryAppletSelfAccessor(Core::System& system_,
                                                        std::shared_ptr<Applet> applet)
        : ServiceFramework{system_, "ILibraryAppletSelfAccessor"}, m_applet{std::move(applet)},
        m_broker{m_applet->caller_applet_broker} {
    }

    ~ILibraryAppletSelfAccessor() = default;

    Result PopInData(Out<SharedPointer<IStorage>> out_storage) {
        LOG_INFO(Service_AM, "called");
        R_RETURN(m_broker->GetInData().Pop(system.Kernel(), out_storage));
    }

    Result PushOutData(SharedPointer<IStorage> storage) {
        LOG_INFO(Service_AM, "called");
        m_broker->GetOutData().Push(system.Kernel(), storage);
        R_SUCCEED();
    }

    Result PopInteractiveInData(Out<SharedPointer<IStorage>> out_storage) {
        LOG_INFO(Service_AM, "called");
        R_RETURN(m_broker->GetInteractiveInData().Pop(system.Kernel(), out_storage));
    }

    Result PushInteractiveOutData(SharedPointer<IStorage> storage) {
        LOG_INFO(Service_AM, "called");
        m_broker->GetInteractiveOutData().Push(system.Kernel(), storage);
        R_SUCCEED();
    }

    Result GetPopInDataEvent(
        OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_INFO(Service_AM, "called");
        *out_event = m_broker->GetInData().GetEvent();
        R_SUCCEED();
    }

    Result GetPopInteractiveInDataEvent(
        OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_INFO(Service_AM, "called");
        *out_event = m_broker->GetInteractiveInData().GetEvent();
        R_SUCCEED();
    }

    Result GetLibraryAppletInfo(
        Out<LibraryAppletInfo> out_library_applet_info) {
        LOG_INFO(Service_AM, "called");
        *out_library_applet_info = {
            .applet_id = m_applet->applet_id,
            .library_applet_mode = m_applet->library_applet_mode,
        };
        R_SUCCEED();
    }

    Result GetMainAppletIdentityInfo(
        Out<AppletIdentityInfo> out_identity_info) {
        LOG_WARNING(Service_AM, "(STUBBED) called");
        *out_identity_info = {
            .applet_id = AppletId::QLaunch,
            .application_id = 0x0100000000001000ull,
        };
        R_SUCCEED();
    }

    Result CanUseApplicationCore(Out<bool> out_can_use_application_core) {
        // TODO: This appears to read the NPDM from state and check the core mask of the applet.
        LOG_WARNING(Service_AM, "(STUBBED) called");
        *out_can_use_application_core = false;
        R_SUCCEED();
    }

    Result GetMainAppletApplicationControlProperty(
        OutLargeData<std::array<u8, 0x4000>, BufferAttr_HipcMapAlias> out_nacp) {
        LOG_WARNING(Service_AM, "(STUBBED) called");

        // TODO: this should be the main applet, not the caller applet
        const auto application = GetCallerIdentity(*m_applet);
        std::vector<u8> nacp;
        const auto result =
            system.GetARPManager().GetControlProperty(&nacp, application.application_id);

        if (R_SUCCEEDED(result)) {
            std::memcpy(out_nacp->data(), nacp.data(), (std::min)(nacp.size(), out_nacp->size()));
        }

        R_RETURN(result);
    }

    Result GetMainAppletStorageId(Out<FileSys::StorageId> out_storage_id) {
        LOG_INFO(Service_AM, "(STUBBED) called");
        *out_storage_id = FileSys::StorageId::NandUser;
        R_SUCCEED();
    }

    Result ExitProcessAndReturn() {
        LOG_INFO(Service_AM, "called");

        if (const auto caller_applet = m_applet->caller_applet.lock(); caller_applet) {
            m_applet->process->Terminate();
        } else {
            system.GetUserChannel() = m_applet->user_channel_launch_parameter;
            system.ExecuteProgram(0);
        }

        R_SUCCEED();
    }

    Result GetCallerAppletIdentityInfo(
        Out<AppletIdentityInfo> out_identity_info) {
        LOG_INFO(Service_AM, "called");
        *out_identity_info = GetCallerIdentity(*m_applet);
        R_SUCCEED();
    }

    Result GetCallerAppletIdentityInfoStack(
        Out<s32> out_count, OutArray<AppletIdentityInfo, BufferAttr_HipcMapAlias> out_identity_info) {
        LOG_INFO(Service_AM, "called");

        std::shared_ptr<Applet> applet = m_applet;
        *out_count = 0;

        do {
            if (*out_count >= static_cast<s32>(out_identity_info.size())) {
                break;
            }
            out_identity_info[(*out_count)++] = GetCallerIdentity(*applet);
        } while ((applet = applet->caller_applet.lock()));

        R_SUCCEED();
    }

    Result GetDesirableKeyboardLayout(Out<u32> out_desirable_layout) {
        LOG_WARNING(Service_AM, "(STUBBED) called");
        *out_desirable_layout = 0;
        R_SUCCEED();
    }

    Result ReportVisibleError(ErrorCode error_code) {
        LOG_WARNING(Service_AM, "(STUBBED) called, error {}-{}", error_code.category,
                    error_code.number);
        R_SUCCEED();
    }

    Result ReportVisibleErrorWithErrorContext(
        ErrorCode error_code, InLargeData<ErrorContext, BufferAttr_HipcMapAlias> error_context) {
        LOG_WARNING(Service_AM, "(STUBBED) called, error {}-{}", error_code.category,
                    error_code.number);
        R_SUCCEED();
    }

    Result UnpopInData(SharedPointer<IStorage> storage) {
        LOG_INFO(Service_AM, "called");
        m_broker->GetInData().Unpop(system.Kernel(), storage);
        R_SUCCEED();
    }

    Result GetMainAppletApplicationDesiredLanguage(
        Out<u64> out_desired_language) {
        // FIXME: this is copied from IApplicationFunctions::GetDesiredLanguage
        // FIXME: all of this stuff belongs to ns
        auto identity = GetCallerIdentity(*m_applet);

        // TODO(bunnei): This should be configurable
        LOG_DEBUG(Service_AM, "called");

        // Get supported languages from NACP, if possible
        // Default to 0 (all languages supported)
        u32 supported_languages = 0;

        const auto res = FileSys::PatchManager::GetMetadataFromBaseOrUpdate(system, identity.application_id);

        if (res.first != nullptr) {
            supported_languages = res.first->GetSupportedLanguages();
        }

        // Call IApplicationManagerInterface implementation.
        auto& service_manager = system.ServiceManager();
        auto ns_am2 = service_manager.GetService<NS::IServiceGetterInterface>("ns:am2");

        std::shared_ptr<NS::IApplicationManagerInterface> app_man;
        R_TRY(ns_am2->GetApplicationManagerInterface(&app_man));

        // Get desired application language
        NS::ApplicationLanguage desired_language{};
        R_TRY(app_man->GetApplicationDesiredLanguage(&desired_language, supported_languages));

        // Convert to settings language code.
        u64 language_code{};
        R_TRY(app_man->ConvertApplicationLanguageToLanguageCode(&language_code, desired_language));

        LOG_DEBUG(Service_AM, "got desired_language={:016X}", language_code);

        *out_desired_language = language_code;
        R_SUCCEED();
    }

    Result GetCurrentApplicationId(Out<u64> out_application_id) {
        LOG_WARNING(Service_AM, "(STUBBED) called");

        // TODO: this should be the main applet, not the caller applet
        const auto main_applet = GetCallerIdentity(*m_applet);
        *out_application_id = main_applet.application_id;

        R_SUCCEED();
    }

    Result GetMainAppletAvailableUsers(
        Out<bool> out_can_select_any_user, Out<s32> out_users_count,
        OutArray<Common::UUID, BufferAttr_HipcMapAlias> out_users) {
        const Service::Account::ProfileManager manager{};

        *out_can_select_any_user = false;
        *out_users_count = -1;

        LOG_INFO(Service_AM, "called");

        if (manager.GetUserCount() > 0) {
            *out_can_select_any_user = true;
            *out_users_count = static_cast<s32>(manager.GetUserCount());

            const auto users = manager.GetAllUsers();
            for (size_t i = 0; i < users.size() && i < out_users.size(); i++) {
                out_users[i] = users[i];
            }
        }

        R_SUCCEED();
    }

    Result ShouldSetGpuTimeSliceManually(
        Out<bool> out_should_set_gpu_time_slice_manually) {
        LOG_INFO(Service_AM, "(STUBBED) called");
        *out_should_set_gpu_time_slice_manually = false;
        R_SUCCEED();
    }

    Result GetLibraryAppletInfoEx(
        Out<LibraryAppletInfo> out_library_applet_info) {
        LOG_INFO(Service_AM, "called");
        *out_library_applet_info = {
            .applet_id = m_applet->applet_id,
            .library_applet_mode = m_applet->library_applet_mode,
        };
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ILibraryAppletSelfAccessor::PopInData>, "PopInData"},
        FunctionInfo{1, D<&ILibraryAppletSelfAccessor::PushOutData>, "PushOutData"},
        FunctionInfo{2, D<&ILibraryAppletSelfAccessor::PopInteractiveInData>, "PopInteractiveInData"},
        FunctionInfo{3, D<&ILibraryAppletSelfAccessor::PushInteractiveOutData>, "PushInteractiveOutData"},
        FunctionInfo{5, D<&ILibraryAppletSelfAccessor::GetPopInDataEvent>, "GetPopInDataEvent"},
        FunctionInfo{6, D<&ILibraryAppletSelfAccessor::GetPopInteractiveInDataEvent>, "GetPopInteractiveInDataEvent"},
        FunctionInfo{10, D<&ILibraryAppletSelfAccessor::ExitProcessAndReturn>, "ExitProcessAndReturn"},
        FunctionInfo{11, D<&ILibraryAppletSelfAccessor::GetLibraryAppletInfo>, "GetLibraryAppletInfo"},
        FunctionInfo{12, D<&ILibraryAppletSelfAccessor::GetMainAppletIdentityInfo>, "GetMainAppletIdentityInfo"},
        FunctionInfo{13, D<&ILibraryAppletSelfAccessor::CanUseApplicationCore>, "CanUseApplicationCore"},
        FunctionInfo{14, D<&ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfo>, "GetCallerAppletIdentityInfo"},
        FunctionInfo{15, D<&ILibraryAppletSelfAccessor::GetMainAppletApplicationControlProperty>, "GetMainAppletApplicationControlProperty"},
        FunctionInfo{16, D<&ILibraryAppletSelfAccessor::GetMainAppletStorageId>, "GetMainAppletStorageId"},
        FunctionInfo{17, D<&ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfoStack>, "GetCallerAppletIdentityInfoStack"},
        FunctionInfo{18, nullptr, "GetNextReturnDestinationAppletIdentityInfo"},
        FunctionInfo{19, D<&ILibraryAppletSelfAccessor::GetDesirableKeyboardLayout>, "GetDesirableKeyboardLayout"},
        FunctionInfo{20, nullptr, "PopExtraStorage"},
        FunctionInfo{25, nullptr, "GetPopExtraStorageEvent"},
        FunctionInfo{30, D<&ILibraryAppletSelfAccessor::UnpopInData>, "UnpopInData"},
        FunctionInfo{31, nullptr, "UnpopExtraStorage"},
        FunctionInfo{40, nullptr, "GetIndirectLayerProducerHandle"},
        FunctionInfo{50, D<&ILibraryAppletSelfAccessor::ReportVisibleError>, "ReportVisibleError"},
        FunctionInfo{51, D<&ILibraryAppletSelfAccessor::ReportVisibleErrorWithErrorContext>, "ReportVisibleErrorWithErrorContext"},
        FunctionInfo{60, D<&ILibraryAppletSelfAccessor::GetMainAppletApplicationDesiredLanguage>, "GetMainAppletApplicationDesiredLanguage"},
        FunctionInfo{70, D<&ILibraryAppletSelfAccessor::GetCurrentApplicationId>, "GetCurrentApplicationId"},
        FunctionInfo{80, nullptr, "RequestExitToSelf"},
        FunctionInfo{90, nullptr, "CreateApplicationAndPushAndRequestToLaunch"},
        FunctionInfo{100, nullptr, "CreateGameMovieTrimmer"},
        FunctionInfo{101, nullptr, "ReserveResourceForMovieOperation"},
        FunctionInfo{102, nullptr, "UnreserveResourceForMovieOperation"},
        FunctionInfo{110, D<&ILibraryAppletSelfAccessor::GetMainAppletAvailableUsers>, "GetMainAppletAvailableUsers"},
        FunctionInfo{120, nullptr, "GetLaunchStorageInfoForDebug"},
        FunctionInfo{130, nullptr, "GetGpuErrorDetectedSystemEvent"},
        FunctionInfo{140, nullptr, "SetApplicationMemoryReservation"},
        FunctionInfo{150, D<&ILibraryAppletSelfAccessor::ShouldSetGpuTimeSliceManually>, "ShouldSetGpuTimeSliceManually"},
        FunctionInfo{160, D<&ILibraryAppletSelfAccessor::GetLibraryAppletInfoEx>, "GetLibraryAppletInfoEx"}
    );
    const std::shared_ptr<Applet> m_applet;
    const std::shared_ptr<AppletDataBroker> m_broker;
};

class ILibraryAppletProxy final : public ServiceFramework<ILibraryAppletProxy> {
public:
    explicit ILibraryAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                            Kernel::KProcess* process, WindowSystem& window_system)
        : ServiceFramework{system_, "ILibraryAppletProxy"},
        m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    }
    ~ILibraryAppletProxy() override = default;

    Result GetAudioController(
        Out<SharedPointer<IAudioController>> out_audio_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_audio_controller = std::make_shared<IAudioController>(system);
        R_SUCCEED();
    }

    Result GetDisplayController(
        Out<SharedPointer<IDisplayController>> out_display_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetDebugFunctions(
        Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_debug_functions = std::make_shared<IDebugFunctions>(system);
        R_SUCCEED();
    }

    Result GetWindowController(
        Out<SharedPointer<IWindowController>> out_window_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetSelfController(
        Out<SharedPointer<ISelfController>> out_self_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
        R_SUCCEED();
    }

    Result GetCommonStateGetter(
        Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
        LOG_DEBUG(Service_AM, "called");
        *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
        R_SUCCEED();
    }

    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
        LOG_DEBUG(Service_AM, "called");
        *out_library_applet_creator =
            std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result OpenLibraryAppletSelfAccessor(
        Out<SharedPointer<ILibraryAppletSelfAccessor>> out_library_applet_self_accessor) {
        LOG_DEBUG(Service_AM, "called");
        *out_library_applet_self_accessor =
            std::make_shared<ILibraryAppletSelfAccessor>(system, m_applet);
        R_SUCCEED();
    }

    Result GetAppletCommonFunctions(
        Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_applet_common_functions = std::make_shared<IAppletCommonFunctions>(system, m_applet);
        R_SUCCEED();
    }

    Result GetHomeMenuFunctions(
        Out<SharedPointer<IHomeMenuFunctions>> out_home_menu_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_home_menu_functions =
            std::make_shared<IHomeMenuFunctions>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetGlobalStateController(
        Out<SharedPointer<IGlobalStateController>> out_global_state_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ILibraryAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        FunctionInfo{1, D<&ILibraryAppletProxy::GetSelfController>, "GetSelfController"},
        FunctionInfo{2, D<&ILibraryAppletProxy::GetWindowController>, "GetWindowController"},
        FunctionInfo{3, D<&ILibraryAppletProxy::GetAudioController>, "GetAudioController"},
        FunctionInfo{4, D<&ILibraryAppletProxy::GetDisplayController>, "GetDisplayController"},
        FunctionInfo{10, D<&ILibraryAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        FunctionInfo{11, D<&ILibraryAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        FunctionInfo{20, D<&ILibraryAppletProxy::OpenLibraryAppletSelfAccessor>, "OpenLibraryAppletSelfAccessor"},
        FunctionInfo{21, D<&ILibraryAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        FunctionInfo{22, D<&ILibraryAppletProxy::GetHomeMenuFunctions>, "GetHomeMenuFunctions"},
        FunctionInfo{23, D<&ILibraryAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        FunctionInfo{1000, D<&ILibraryAppletProxy::GetDebugFunctions>, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

class IOverlayAppletProxy final : public ServiceFramework<IOverlayAppletProxy> {
public:
    explicit IOverlayAppletProxy(Core::System &system_, std::shared_ptr<Applet> applet,
                                             Kernel::KProcess *process, WindowSystem &window_system)
        : ServiceFramework{system_, "IOverlayAppletProxy"},
          m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    }

    ~IOverlayAppletProxy() override = default;

    Result GetCommonStateGetter(
        Out<SharedPointer<ICommonStateGetter> > out_common_state_getter) {
        LOG_DEBUG(Service_AM, "called");
        *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
        R_SUCCEED();
    }

    Result GetSelfController(
        Out<SharedPointer<ISelfController> > out_self_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
        R_SUCCEED();
    }

    Result GetWindowController(
        Out<SharedPointer<IWindowController> > out_window_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetAudioController(
        Out<SharedPointer<IAudioController> > out_audio_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_audio_controller = std::make_shared<IAudioController>(system);
        R_SUCCEED();
    }

    Result GetDisplayController(
        Out<SharedPointer<IDisplayController> > out_display_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController> > out_process_winding_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
        R_SUCCEED();
    }

    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator> > out_library_applet_creator) {
        LOG_DEBUG(Service_AM, "called");
        *out_library_applet_creator =
                std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
        R_SUCCEED();
    }

    Result GetOverlayFunctions(
        Out<SharedPointer<IOverlayFunctions> > out_overlay_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_overlay_functions = std::make_shared<IOverlayFunctions>(system, m_applet);
        R_SUCCEED();
    }

    Result GetAppletCommonFunctions(
        Out<SharedPointer<IAppletCommonFunctions> > out_applet_common_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_applet_common_functions = std::make_shared<IAppletCommonFunctions>(system, m_applet);
        R_SUCCEED();
    }

    Result GetGlobalStateController(
        Out<SharedPointer<IGlobalStateController> > out_global_state_controller) {
        LOG_DEBUG(Service_AM, "called");
        *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
        R_SUCCEED();
    }

    Result GetDebugFunctions(
        Out<SharedPointer<IDebugFunctions> > out_debug_functions) {
        LOG_DEBUG(Service_AM, "called");
        *out_debug_functions = std::make_shared<IDebugFunctions>(system);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IOverlayAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        FunctionInfo{1, D<&IOverlayAppletProxy::GetSelfController>, "GetSelfController"},
        FunctionInfo{2, D<&IOverlayAppletProxy::GetWindowController>, "GetWindowController"},
        FunctionInfo{3, D<&IOverlayAppletProxy::GetAudioController>, "GetAudioController"},
        FunctionInfo{4, D<&IOverlayAppletProxy::GetDisplayController>, "GetDisplayController"},
        FunctionInfo{10, D<&IOverlayAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        FunctionInfo{11, D<&IOverlayAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        FunctionInfo{20, D<&IOverlayAppletProxy::GetOverlayFunctions>, "GetOverlayFunctions"},
        FunctionInfo{21, D<&IOverlayAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        FunctionInfo{23, D<&IOverlayAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        FunctionInfo{1000, D<&IOverlayAppletProxy::GetDebugFunctions>, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

static bool ShouldCreateGuestApplet(AppletId applet_id) {
#define X(Name, name)                                                                              \
    if (applet_id == AppletId::Name &&                                                             \
        Settings::values.name##_applet_mode.GetValue() != Settings::AppletMode::LLE) {             \
        return false;                                                                              \
    }

    X(Cabinet, cabinet)
    X(Controller, controller)
    X(DataErase, data_erase)
    X(Error, error)
    X(NetConnect, net_connect)
    X(ProfileSelect, player_select)
    X(SoftwareKeyboard, swkbd)
    X(MiiEdit, mii_edit)
    X(Web, web)
    X(Shop, shop)
    X(PhotoViewer, photo_viewer)
    X(OfflineWeb, offline_web)
    X(LoginShare, login_share)
    X(WebAuth, wifi_web_auth)
    X(MyPage, my_page)

#undef X

    return true;
}

static AppletProgramId AppletIdToProgramId(AppletId applet_id) {
    switch (applet_id) {
    case AppletId::OverlayDisplay:
        return AppletProgramId::OverlayDisplay;
    case AppletId::QLaunch:
        return AppletProgramId::QLaunch;
    case AppletId::Starter:
        return AppletProgramId::Starter;
    case AppletId::Auth:
        return AppletProgramId::Auth;
    case AppletId::Cabinet:
        return AppletProgramId::Cabinet;
    case AppletId::Controller:
        return AppletProgramId::Controller;
    case AppletId::DataErase:
        return AppletProgramId::DataErase;
    case AppletId::Error:
        return AppletProgramId::Error;
    case AppletId::NetConnect:
        return AppletProgramId::NetConnect;
    case AppletId::ProfileSelect:
        return AppletProgramId::ProfileSelect;
    case AppletId::SoftwareKeyboard:
        return AppletProgramId::SoftwareKeyboard;
    case AppletId::MiiEdit:
        return AppletProgramId::MiiEdit;
    case AppletId::Web:
        return AppletProgramId::Web;
    case AppletId::Shop:
        return AppletProgramId::Shop;
    case AppletId::PhotoViewer:
        return AppletProgramId::PhotoViewer;
    case AppletId::Settings:
        return AppletProgramId::Settings;
    case AppletId::OfflineWeb:
        return AppletProgramId::OfflineWeb;
    case AppletId::LoginShare:
        return AppletProgramId::LoginShare;
    case AppletId::WebAuth:
        return AppletProgramId::WebAuth;
    case AppletId::MyPage:
        return AppletProgramId::MyPage;
    default:
        return static_cast<AppletProgramId>(0);
    }
}

static std::shared_ptr<ILibraryAppletAccessor> CreateGuestApplet(Core::System& system,
                                                          WindowSystem& window_system,
                                                          std::shared_ptr<Applet> caller_applet,
                                                          AppletId applet_id,
                                                          LibraryAppletMode mode) {
    const auto program_id = static_cast<u64>(AppletIdToProgramId(applet_id));
    if (program_id == 0) {
        // Unknown applet
        return {};
    }

    auto process = CreateProcess(system, program_id, 1, HLE::ApiVersion::HOS_VERSION_MAJOR);
    if (process) {
        const auto applet = std::make_shared<Applet>(system, std::move(process), false);
        applet->program_id = program_id;
        applet->applet_id = applet_id;
        applet->type = AppletType::LibraryApplet;
        applet->library_applet_mode = mode;
        applet->window_visible = mode != LibraryAppletMode::AllForegroundInitiallyHidden;

        auto broker = std::make_shared<AppletDataBroker>(system);
        applet->caller_applet = caller_applet;
        applet->caller_applet_broker = broker;
        {
            std::scoped_lock lk{caller_applet->lock};
            caller_applet->child_applets.push_back(applet);
        }
        window_system.TrackApplet(applet, false);
        return std::make_shared<ILibraryAppletAccessor>(system, broker, applet);
    }
    // Couldn't initialize the guest process
    return {};
}

static std::shared_ptr<ILibraryAppletAccessor> CreateFrontendApplet(Core::System& system,
                                                             WindowSystem& window_system,
                                                             std::shared_ptr<Applet> caller_applet,
                                                             AppletId applet_id,
                                                             LibraryAppletMode mode) {
    const auto program_id = static_cast<u64>(AppletIdToProgramId(applet_id));

    auto process = std::make_unique<Process>(system);
    auto applet = std::make_shared<Applet>(system, std::move(process), false);
    applet->program_id = program_id;
    applet->applet_id = applet_id;
    applet->type = AppletType::LibraryApplet;
    applet->library_applet_mode = mode;

    auto storage = std::make_shared<AppletDataBroker>(system);
    applet->caller_applet = caller_applet;
    applet->caller_applet_broker = storage;
    applet->frontend = system.GetFrontendAppletHolder().GetApplet(applet, applet_id, mode);
    {
        std::scoped_lock lk{caller_applet->lock};
        caller_applet->child_applets.push_back(applet);
    }
    return std::make_shared<ILibraryAppletAccessor>(system, storage, applet);
}

class ILibraryAppletCreator final : public ServiceFramework<ILibraryAppletCreator> {
public:
    explicit ILibraryAppletCreator(Core::System& system_, std::shared_ptr<Applet> applet, WindowSystem& window_system)
        : ServiceFramework{system_, "ILibraryAppletCreator"}
        , m_window_system{window_system}, m_applet{std::move(applet)} {
    }
    ~ILibraryAppletCreator() override = default;

    Result CreateLibraryApplet(
        Out<SharedPointer<ILibraryAppletAccessor>> out_library_applet_accessor, AppletId applet_id,
        LibraryAppletMode library_applet_mode) {
        LOG_DEBUG(Service_AM, "called with applet_id={} applet_mode={}", applet_id,
                library_applet_mode);

        std::shared_ptr<ILibraryAppletAccessor> library_applet;
        if (ShouldCreateGuestApplet(applet_id)) {
            library_applet =
                CreateGuestApplet(system, m_window_system, m_applet, applet_id, library_applet_mode);
        }
        if (!library_applet) {
            library_applet =
                CreateFrontendApplet(system, m_window_system, m_applet, applet_id, library_applet_mode);
        }
        if (!library_applet) {
            LOG_ERROR(Service_AM, "Applet doesn't exist! applet_id={}", applet_id);
            R_THROW(ResultUnknown);
        }

        // Applet is created, can now be launched.
        m_applet->library_applet_launchable_event.Signal(system.Kernel());
        *out_library_applet_accessor = library_applet;
        R_SUCCEED();
    }

    Result CreateLibraryAppletEx(
        Out<SharedPointer<ILibraryAppletAccessor>> out_library_applet_accessor, AppletId applet_id,
        LibraryAppletMode library_applet_mode, u64 thread_id) {
        LOG_DEBUG(Service_AM, "called with applet_id={} applet_mode={} thread_id={}", applet_id,
                library_applet_mode, thread_id);

        std::shared_ptr<ILibraryAppletAccessor> library_applet;
        if (ShouldCreateGuestApplet(applet_id)) {
            library_applet =
                CreateGuestApplet(system, m_window_system, m_applet, applet_id, library_applet_mode);
        }
        if (!library_applet) {
            library_applet =
                CreateFrontendApplet(system, m_window_system, m_applet, applet_id, library_applet_mode);
        }
        if (!library_applet) {
            LOG_ERROR(Service_AM, "Applet doesn't exist! applet_id={}", applet_id);
            R_THROW(ResultUnknown);
        }

        // Applet is created, can now be launched.
        m_applet->library_applet_launchable_event.Signal(system.Kernel());
        *out_library_applet_accessor = library_applet;
        R_SUCCEED();
    }

    Result CreateStorage(Out<SharedPointer<IStorage>> out_storage, s64 size) {
        LOG_DEBUG(Service_AM, "called, size={}", size);

        if (size <= 0) {
            LOG_ERROR(Service_AM, "size is less than or equal to 0");
            R_THROW(ResultUnknown);
        }

        *out_storage = std::make_shared<IStorage>(system, AM::CreateStorage(std::vector<u8>(size)));
        R_SUCCEED();
    }

    Result CreateTransferMemoryStorage(
        Out<SharedPointer<IStorage>> out_storage, bool is_writable, s64 size,
        InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
        LOG_DEBUG(Service_AM, "called, is_writable={} size={}", is_writable, size);

        if (size <= 0) {
            LOG_ERROR(Service_AM, "size is less than or equal to 0");
            R_THROW(ResultUnknown);
        }

        if (!transfer_memory_handle) {
            LOG_ERROR(Service_AM, "transfer_memory_handle is null");
            R_THROW(ResultUnknown);
        }

        *out_storage = std::make_shared<IStorage>(
            system, AM::CreateTransferMemoryStorage(system.Kernel(), transfer_memory_handle->GetOwner()->GetMemory(),
                                                    transfer_memory_handle.Get(), is_writable, size));
        R_SUCCEED();
    }

    Result CreateHandleStorage(
        Out<SharedPointer<IStorage>> out_storage, s64 size,
        InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
        LOG_DEBUG(Service_AM, "called, size={}", size);

        if (size <= 0) {
            LOG_ERROR(Service_AM, "size is less than or equal to 0");
            R_THROW(ResultUnknown);
        }

        if (!transfer_memory_handle) {
            LOG_ERROR(Service_AM, "transfer_memory_handle is null");
            R_THROW(ResultUnknown);
        }

        *out_storage = std::make_shared<IStorage>(
            system, AM::CreateHandleStorage(system.Kernel(), transfer_memory_handle->GetOwner()->GetMemory(),
                                            transfer_memory_handle.Get(), size));
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ILibraryAppletCreator::CreateLibraryApplet>, "CreateLibraryApplet"},
        FunctionInfo{1, nullptr, "TerminateAllLibraryApplets"},
        FunctionInfo{2, nullptr, "AreAnyLibraryAppletsLeft"},
        FunctionInfo{3, D<&ILibraryAppletCreator::CreateLibraryAppletEx>, "CreateLibraryAppletEx"},
        FunctionInfo{10, D<&ILibraryAppletCreator::CreateStorage>, "CreateStorage"},
        FunctionInfo{11, D<&ILibraryAppletCreator::CreateTransferMemoryStorage>, "CreateTransferMemoryStorage"},
        FunctionInfo{12, D<&ILibraryAppletCreator::CreateHandleStorage>, "CreateHandleStorage"}
    );
    WindowSystem& m_window_system;
    const std::shared_ptr<Applet> m_applet;
};

class IAllSystemAppletProxiesService final
    : public ServiceFramework<IAllSystemAppletProxiesService> {
public:
    explicit IAllSystemAppletProxiesService(Core::System& system_, WindowSystem& window_system)
        : ServiceFramework{system_, "appletAE"}, m_window_system{window_system} {
    }

    ~IAllSystemAppletProxiesService() override = default;

    Result OpenSystemAppletProxy(
        Out<SharedPointer<ISystemAppletProxy>> out_system_applet_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle) {
        LOG_DEBUG(Service_AM, "called");

        if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
            *out_system_applet_proxy = std::make_shared<ISystemAppletProxy>(
                system, applet, process_handle.Get(), m_window_system);
            R_SUCCEED();
        } else {
            UNIMPLEMENTED();
            R_THROW(ResultUnknown);
        }
    }

    Result OpenLibraryAppletProxy(
        Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle,
        InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute) {
        LOG_DEBUG(Service_AM, "called");

        if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
            *out_library_applet_proxy = std::make_shared<ILibraryAppletProxy>(
                system, applet, process_handle.Get(), m_window_system);
            R_SUCCEED();
        } else {
            UNIMPLEMENTED();
            R_THROW(ResultUnknown);
        }
    }

    Result OpenOverlayAppletProxy(
        Out<SharedPointer<IOverlayAppletProxy>> out_overlay_applet_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle) {
        LOG_WARNING(Service_AM, "called");

        if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
            *out_overlay_applet_proxy = std::make_shared<IOverlayAppletProxy>(
                system, applet, process_handle.Get(), m_window_system);
            R_SUCCEED();
        } else {
            UNIMPLEMENTED();
            R_THROW(ResultUnknown);
        }
    }

    Result OpenSystemApplicationProxy(
        Out<SharedPointer<IApplicationProxy>> out_system_application_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle) {
        LOG_DEBUG(Service_AM, "called");

        if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
            *out_system_application_proxy = std::make_shared<IApplicationProxy>(
                system, applet, process_handle.Get(), m_window_system);
            R_SUCCEED();
        } else {
            UNIMPLEMENTED();
            R_THROW(ResultUnknown);
        }
    }

    Result OpenLibraryAppletProxyOld(
        Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle) {
        LOG_DEBUG(Service_AM, "called");

        AppletAttribute attribute{};
        R_RETURN(this->OpenLibraryAppletProxy(out_library_applet_proxy, pid, process_handle, attribute));
    }

    Result GetSystemProcessCommonFunctions() {
        LOG_DEBUG(Service_AM, "(STUBBED) called.");
        // TODO (jarrodnorwell)
        R_SUCCEED();
    }

    Result GetAppletAlternativeFunctions() {
        LOG_DEBUG(Service_AM, "(STUBBED) called.");
        // TODO (maufeat)
        R_SUCCEED();
    }

    std::shared_ptr<Applet> GetAppletFromProcessId(ProcessId process_id) {
        return m_window_system.GetByAppletResourceUserId(process_id.pid);
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{100, D<&IAllSystemAppletProxiesService::OpenSystemAppletProxy>, "OpenSystemAppletProxy"},
        FunctionInfo{110, D<&IAllSystemAppletProxiesService::OpenSystemAppletProxy>, "OpenSystemAppletProxyEx"},
        FunctionInfo{200, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxyOld>, "OpenLibraryAppletProxyOld"},
        FunctionInfo{201, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxy>, "OpenLibraryAppletProxy"},
        FunctionInfo{300, D<&IAllSystemAppletProxiesService::OpenOverlayAppletProxy>, "OpenOverlayAppletProxy"},
        FunctionInfo{350, D<&IAllSystemAppletProxiesService::OpenSystemApplicationProxy>, "OpenSystemApplicationProxy"},
        FunctionInfo{400, nullptr, "CreateSelfLibraryAppletCreatorForDevelop"},
        FunctionInfo{410, nullptr, "GetSystemAppletControllerForDebug"},
        FunctionInfo{450, D<&IAllSystemAppletProxiesService::GetSystemProcessCommonFunctions>, "GetSystemProcessCommonFunctions"}, // 19.0.0+
        FunctionInfo{460, D<&IAllSystemAppletProxiesService::GetAppletAlternativeFunctions>, "GetAppletAlternativeFunctions"}, // 20.0.0+
        FunctionInfo{1000, nullptr, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
};

void LoopProcess(Core::System& system) {
    WindowSystem window_system(system);
    ButtonPoller button_poller(system, window_system);
    EventObserver event_observer(system, window_system);

    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("appletAE", std::make_shared<IAllSystemAppletProxiesService>(system, window_system));
    server_manager->RegisterNamedService("appletOE", std::make_shared<IApplicationProxyService>(system, window_system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::AM
