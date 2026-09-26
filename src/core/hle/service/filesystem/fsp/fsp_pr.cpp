// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/filesystem/fsp/fsp_pr.h"

namespace Service::FileSystem {

FSP_PR::FSP_PR(Core::System& system_) : ServiceFramework{system_, "fsp:pr"} {
}

FSP_PR::~FSP_PR() = default;

} // namespace Service::FileSystem
