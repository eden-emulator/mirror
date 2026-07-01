// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <vector>
#include <variant>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/internal_network/socket_types.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

namespace Common {
template <typename T, typename E>
class Expected;
}

namespace Network {

class SocketBase;
class Socket;

struct HostPollFD {
    SocketBase* socket;
    Network::PollEvents events;
    Network::PollEvents revents;
};

class NetworkInstance {
public:
    explicit NetworkInstance();
    ~NetworkInstance();
};

void CancelPendingSocketOperations();
void RestartSocketOperations();

#ifdef _WIN32
constexpr IPv4Address TranslateIPv4(in_addr addr) {
    auto& bytes = addr.S_un.S_un_b;
    return IPv4Address{bytes.s_b1, bytes.s_b2, bytes.s_b3, bytes.s_b4};
}
#else
constexpr IPv4Address TranslateIPv4(in_addr addr) {
    const u32 bytes = addr.s_addr;
    return IPv4Address{static_cast<u8>(bytes), static_cast<u8>(bytes >> 8),
                       static_cast<u8>(bytes >> 16), static_cast<u8>(bytes >> 24)};
}
#endif

/// @brief Returns host's IPv4 address
/// @return human ordered IPv4 address (e.g. 192.168.0.1) as an array
std::optional<IPv4Address> GetHostIPv4Address();

std::string IPv4AddressToString(IPv4Address ip_addr);
u32 IPv4AddressToInteger(IPv4Address ip_addr);

// named to avoid name collision with Windows macro
std::variant<std::vector<AddrInfo>, GetAddrInfoError> GetAddressInfo(const std::string& host, const std::optional<std::string>& service);

} // namespace Network
