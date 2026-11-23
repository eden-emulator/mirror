// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core_timing.h"
#include "core/hle/service/hid/resources/applet_resource.h"
#include "core/hle/service/hid/resources/shared_memory_format.h"
#include "core/hle/service/hid/resources/unique_pad/unique_pad.h"

namespace Service::HID {

UniquePad::UniquePad(Core::HID::HIDCore& hid_core_) : ControllerBase{hid_core_} {}

UniquePad::~UniquePad() = default;

void UniquePad::OnInit() {}

void UniquePad::OnRelease() {}

void UniquePad::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    const u64 aruid = applet_resource->GetActiveAruid();
    auto* data = applet_resource->GetAruidData(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return;
    }

    auto& header = data->shared_memory_format->unique_pad.header;
    header.timestamp = core_timing.GetGlobalTimeNs().count();
    header.total_entry_count = 17;
    header.entry_count = 0;
    header.last_entry_index = 0;
}

} // namespace Service::HID
