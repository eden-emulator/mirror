// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::SM {

class Controller final : public ServiceFramework<Controller> {
public:
    explicit Controller(Core::System& system_);
    ~Controller() override;

private:
    void ConvertCurrentObjectToDomain(HLERequestContext& ctx);
    void CloneCurrentObject(HLERequestContext& ctx);
    void CloneCurrentObjectEx(HLERequestContext& ctx);
    void QueryPointerBufferSize(HLERequestContext& ctx);
    void SetPointerBufferSize(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &Controller::ConvertCurrentObjectToDomain, "ConvertCurrentObjectToDomain"},
        FunctionInfo{1, nullptr, "CopyFromCurrentDomain"},
        FunctionInfo{2, &Controller::CloneCurrentObject, "CloneCurrentObject"},
        FunctionInfo{3, &Controller::QueryPointerBufferSize, "QueryPointerBufferSize"},
        FunctionInfo{4, &Controller::CloneCurrentObjectEx, "CloneCurrentObjectEx"},
        FunctionInfo{5, &Controller::SetPointerBufferSize, "SetPointerBufferSize"} //TODO: where does this come from
    );
};

} // namespace Service::SM
