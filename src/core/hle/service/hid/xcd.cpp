// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/hid/xcd.h"

namespace Service::HID {

XCD_SYS::XCD_SYS(Core::System& system_) : ServiceFramework{system_, "xcd:sys"} {
}

XCD_SYS::~XCD_SYS() = default;

} // namespace Service::HID
