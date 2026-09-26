// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/friend/friend.h"

namespace Service::Friend {

class Friend final : public Module::Interface {
public:
    explicit Friend(std::shared_ptr<Module> module_, Core::System& system_, const char* name);
    ~Friend() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        {0, &Friend::CreateFriendService, "CreateFriendService"},
        {1, &Friend::CreateNotificationService, "CreateNotificationService"},
        {2, nullptr, "CreateDaemonSuspendSessionService"}
    );
};

} // namespace Service::Friend
