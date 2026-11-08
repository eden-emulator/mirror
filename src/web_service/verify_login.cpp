// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "web_service/verify_login.h"
#include "web_service/web_backend.h"
#include "web_service/web_result.h"

import jacinth;

namespace WebService {

struct Reply {
    std::optional<std::string> username;
};

bool VerifyLogin(const std::string& host, const std::string& username, const std::string& token) {
    Client client(host, username, token);
    auto reply = client.GetJson("/profile", false).returned_data;
    if (reply.empty()) {
        return false;
    }

    auto reply_s = jacinth::doc::try_read<Reply>(reply);

    if (!reply_s) {
        LOG_WARNING(WebService, "Failed to parse verification profile");
        return false;
    }

    if (!reply_s->username) return username.empty();

    return reply_s->username == username;
}

} // namespace WebService
