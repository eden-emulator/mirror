// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/friend/friend_interface.h"

namespace Service::Friend {

Friend::Friend(std::shared_ptr<Module> module_, Core::System& system_, const char* name)
    : Interface(std::move(module_), system_, name) {
}

Friend::~Friend() = default;

} // namespace Service::Friend
