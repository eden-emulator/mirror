// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

class IReceiver;

class IReceiverService final : public ServiceFramework<IReceiverService> {
public:
    explicit IReceiverService(Core::System& system_);
    ~IReceiverService() override;

private:
    Result OpenReceiver(Out<SharedPointer<IReceiver>> out_receiver);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IReceiverService::OpenReceiver>, "OpenReceiver"}
    );
};

} // namespace Service::PSC
