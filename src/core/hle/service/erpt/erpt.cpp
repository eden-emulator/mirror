// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/erpt/erpt.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::ERPT {

class ErrorReportContext final : public ServiceFramework<ErrorReportContext> {
public:
    explicit ErrorReportContext(Core::System& system_) : ServiceFramework{system_, "erpt:c"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    Result SubmitContext(InBuffer<BufferAttr_HipcMapAlias> context_entry,
                         InBuffer<BufferAttr_HipcMapAlias> field_list) {
        LOG_WARNING(Service_SET, "(STUBBED) called, context_entry_size={}, field_list_size={}",
                    context_entry.size(), field_list.size());
        R_SUCCEED();
    }

    Result CreateReportV0(u32 report_type, InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(Service_SET, "(STUBBED) called, report_type={:#x}", report_type);
        R_SUCCEED();
    }

    Result CreateReportV1(u32 report_type, u32 unknown,
                          InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(Service_SET, "(STUBBED) called, report_type={:#x}, unknown={:#x}", report_type,
                    unknown);
        R_SUCCEED();
    }

    Result CreateReport(u32 report_type, u32 unknown, u32 create_report_option_flag,
                        InBuffer<BufferAttr_HipcMapAlias> context_entry,
                        InBuffer<BufferAttr_HipcMapAlias> report_list,
                        InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_WARNING(
            Service_SET,
            "(STUBBED) called, report_type={:#x}, unknown={:#x}, create_report_option_flag={:#x}",
            report_type, unknown, create_report_option_flag);
        R_SUCCEED();
    }

    Result UpdateAwakeTime(InBuffer<BufferAttr_HipcMapAlias> data_a,
                           InBuffer<BufferAttr_HipcMapAlias> data_b, u32 flag_a, u32 flag_b) {
        LOG_WARNING(Service_SET,
                    "(STUBBED) called, data_a_size={}, data_b_size={}, flag_a={}, flag_b={}",
                    data_a.size(), data_b.size(), flag_a, flag_b);
        R_SUCCEED();
    }

    static const auto functions = CreateStaticMap(
        FunctionInfo{0, C<&ErrorReportContext::SubmitContext>, "SubmitContext"},
        FunctionInfo{1, C<&ErrorReportContext::CreateReportV0>, "CreateReportV0"},
        FunctionInfo{2, nullptr, "SetInitialLaunchSettingsCompletionTime"},
        FunctionInfo{3, nullptr, "ClearInitialLaunchSettingsCompletionTime"},
        FunctionInfo{4, nullptr, "UpdatePowerOnTime"},
        FunctionInfo{5, D<&ErrorReportContext::UpdateAwakeTime>, "UpdateAwakeTime"},
        FunctionInfo{6, nullptr, "SubmitMultipleCategoryContext"},
        FunctionInfo{7, nullptr, "UpdateApplicationLaunchTime"},
        FunctionInfo{8, nullptr, "ClearApplicationLaunchTime"},
        FunctionInfo{9, nullptr, "SubmitAttachment"},
        FunctionInfo{10, nullptr, "CreateReportWithAttachments"},
        FunctionInfo{11, C<&ErrorReportContext::CreateReportV1>, "CreateReportV1"},
        FunctionInfo{12, C<&ErrorReportContext::CreateReport>, "CreateReport"},
        FunctionInfo{20, nullptr, "RegisterRunningApplet"},
        FunctionInfo{21, nullptr, "UnregisterRunningApplet"},
        FunctionInfo{22, nullptr, "UpdateAppletSuspendedDuration"},
        FunctionInfo{30, nullptr, "InvalidateForcedShutdownDetection"}
    );
};

class ErrorReportSession final : public ServiceFramework<ErrorReportSession> {
public:
    explicit ErrorReportSession(Core::System& system_) : ServiceFramework{system_, "erpt:r"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "OpenReport"},
        FunctionInfo{1, nullptr, "OpenManager"},
        FunctionInfo{2, nullptr, "OpenAttachment"}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("erpt:c", std::make_shared<ErrorReportContext>(system));
    server_manager->RegisterNamedService("erpt:r", std::make_shared<ErrorReportSession>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::ERPT
