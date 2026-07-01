// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "core/internal_network/network.h"

namespace Service::Sockets {

const char* Translate(Network::GetAddrInfoError error) {
    // https://android.googlesource.com/platform/bionic/+/085543106/libc/dns/net/getaddrinfo.c#254
    switch (error) {
    case Network::GetAddrInfoError::SUCCESS:
        return "Success";
    case Network::GetAddrInfoError::ADDRFAMILY:
        return "Address family for hostname not supported";
    case Network::GetAddrInfoError::AGAIN:
        return "Temporary failure in name resolution";
    case Network::GetAddrInfoError::BADFLAGS:
        return "Invalid value for ai_flags";
    case Network::GetAddrInfoError::FAIL:
        return "Non-recoverable failure in name resolution";
    case Network::GetAddrInfoError::FAMILY:
        return "ai_family not supported";
    case Network::GetAddrInfoError::MEMORY:
        return "Memory allocation failure";
    case Network::GetAddrInfoError::NODATA:
        return "No address associated with hostname";
    case Network::GetAddrInfoError::NONAME:
        return "hostname nor servname provided, or not known";
    case Network::GetAddrInfoError::SERVICE:
        return "servname not supported for ai_socktype";
    case Network::GetAddrInfoError::SOCKTYPE:
        return "ai_socktype not supported";
    case Network::GetAddrInfoError::SYSTEM:
        return "System error returned in errno";
    case Network::GetAddrInfoError::BADHINTS:
        return "Invalid value for hints";
    case Network::GetAddrInfoError::PROTOCOL:
        return "Resolved protocol is unknown";
    case Network::GetAddrInfoError::OVERFLOW_:
        return "Argument buffer overflow";
    default:
        return "Unknown error";
    }
}

} // namespace Service::Sockets
