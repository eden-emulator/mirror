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

    std::optional<FunctionInfoBase> FindRequest(u32 key) override;
    Service::Event* event{};
};

} // namespace Service::NS