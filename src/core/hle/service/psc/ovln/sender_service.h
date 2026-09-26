// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

class ISender;

class ISenderService final : public ServiceFramework<ISenderService> {
public:
    explicit ISenderService(Core::System& system_);
    ~ISenderService() override;

private:
    Result OpenSender(Out<SharedPointer<ISender>> out_sender, u32 sender_id, std::array<u64, 2> data);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISenderService::OpenSender>, "OpenSender"}
    );
};

} // namespace Service::PSC
