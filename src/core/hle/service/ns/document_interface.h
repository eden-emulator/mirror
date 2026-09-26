// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IDocumentInterface final : public ServiceFramework<IDocumentInterface> {
public:
    explicit IDocumentInterface(Core::System& system_);
    ~IDocumentInterface() override;

private:
    Result ResolveApplicationContentPath(ContentPath content_path);
    Result GetRunningApplicationProgramId(Out<u64> out_program_id, u64 caller_program_id);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{21, nullptr, "GetApplicationContentPath"},
        FunctionInfo{23, D<&IDocumentInterface::ResolveApplicationContentPath>, "ResolveApplicationContentPath"},
        FunctionInfo{92, D<&IDocumentInterface::GetRunningApplicationProgramId>, "GetRunningApplicationProgramId"}
    );
};

} // namespace Service::NS
