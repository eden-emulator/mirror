// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/cradle_firmware_updater.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

    ServiceFrameworkBase::FunctionInfoBase const* ICradleFirmwareUpdater::FindRequest(u32 key) {
        static const auto functions = CreateStaticMap(
            FunctionInfo{0, D<&ICradleFirmwareUpdater::StartUpdate>, "StartUpdate"},
            FunctionInfo{1, D<&ICradleFirmwareUpdater::FinishUpdate>, "FinishUpdate"},
            FunctionInfo{2, D<&ICradleFirmwareUpdater::GetCradleDeviceInfo>, "GetCradleDeviceInfo"},
            FunctionInfo{3, D<&ICradleFirmwareUpdater::GetCradleDeviceInfoChangeEvent>, "GetCradleDeviceInfoChangeEvent"},
            FunctionInfo{4, nullptr, "GetUpdateProgressInfo"},
            FunctionInfo{5, nullptr, "GetLastInternalResult"}
        );
        return HandlerTableGenerateWithFind(key, functions);
    }

ICradleFirmwareUpdater::ICradleFirmwareUpdater(Core::System& system_)
    : ServiceFramework{system_, "ICradleFirmwareUpdater"},
      m_context{system, "ICradleFirmwareUpdater"}, m_cradle_device_info_event{m_context} {
}

ICradleFirmwareUpdater::~ICradleFirmwareUpdater() = default;

Result ICradleFirmwareUpdater::StartUpdate() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICradleFirmwareUpdater::FinishUpdate() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICradleFirmwareUpdater::GetCradleDeviceInfo(Out<CradleDeviceInfo> out_cradle_device_info) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_cradle_device_info = {};
    R_SUCCEED();
}

Result ICradleFirmwareUpdater::GetCradleDeviceInfoChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_event = m_cradle_device_info_event.GetHandle();
    R_SUCCEED();
}

} // namespace Service::AM
