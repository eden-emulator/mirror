// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/service.h"

namespace Service::NS {

class IAsyncResult final : public ServiceFramework<IAsyncResult> {
public:
    explicit IAsyncResult(Core::System& system_, Service::Event* event_);
    ~IAsyncResult() override;

private:
    Result Cancel();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Get"},
        FunctionInfo{1, D<&IAsyncResult::Cancel>, "Cancel"},
        FunctionInfo{2, nullptr, "GetErrorContext"}, // 4.0.0+
    );
    Service::Event* event{};
};

} // namespace Service::NS