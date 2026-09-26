// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Glue {

class ECTX_W final : public ServiceFramework<ECTX_W> {
public:
    explicit ECTX_W(Core::System& system_);
    ~ECTX_W() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "CreateContextRegistrar"},
        FunctionInfo{1, nullptr, "CommitContext"},
        FunctionInfo{2, nullptr, "RemoveContext"}
    );
};

class ECTX_R final : public ServiceFramework<ECTX_R> {
public:
    explicit ECTX_R(Core::System& system_);
    ~ECTX_R() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetContextInfo"},
        FunctionInfo{1, nullptr, "PullContext"},
        FunctionInfo{2, nullptr, "ListContextDescriptorWithResultForDebug"}
    );
};

class ECTX_AW final : public ServiceFramework<ECTX_AW> {
public:
    explicit ECTX_AW(Core::System& system_);
    ~ECTX_AW() override;

private:
    void CreateContextRegistrar(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, &ECTX_AW::CreateContextRegistrar, "CreateContextRegistrar"},
        FunctionInfo{1, nullptr, "CommitContext"}
    );
};

} // namespace Service::Glue
