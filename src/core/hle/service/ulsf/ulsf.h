// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::ULSF {

struct ULauncherVersion {
    u8 major;
    u8 minor;
    u8 micro;
};

class ULSF_U final : public ServiceFramework<ULSF_U> {
public:
    explicit ULSF_U(Core::System& system_);
    ~ULSF_U();
    //Result GetVersion(Out<ULauncherVersion> out_version);
    void GetVersion(HLERequestContext& ctx);
};

void LoopProcess(Core::System& system);

}
