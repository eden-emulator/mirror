// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/friend/friend.h"

namespace Service::Friend {

class Friend final : public Module::Interface {
public:
    explicit Friend(std::shared_ptr<Module> module_, Core::System& system_, const char* name);
    ~Friend() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &Friend::CreateFriendService, "CreateFriendService"},
        FunctionInfo{1, &Friend::CreateNotificationService, "CreateNotificationService"},
        FunctionInfo{2, nullptr, "CreateDaemonSuspendSessionService"}
    );
};

} // namespace Service::Friend
