// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <catch2/catch_test_macros.hpp>
#include "core/internal_network/sockets.h"

namespace Network::Tests {

inline std::shared_ptr<Socket> NewSocket() {
    auto socket = std::make_shared<Socket>();
    REQUIRE(socket->Initialize(Domain::INET, Type::STREAM, Protocol::IP) == Errno::SUCCESS);
    return socket;
}

struct TcpPair {
    TcpPair() {
        auto listener = NewSocket();
        REQUIRE(listener->Bind({Domain::INET, {127, 0, 0, 1}, 0}) == Errno::SUCCESS);
        REQUIRE(listener->Listen(1) == Errno::SUCCESS);
        const auto [address, address_error] = listener->GetSockName();
        REQUIRE(address_error == Errno::SUCCESS);
        client = NewSocket();
        REQUIRE(client->Connect(address) == Errno::SUCCESS);
        auto [accepted, error] = listener->Accept();
        REQUIRE(error == Errno::SUCCESS);
        server = std::move(accepted.socket);
    }

    std::shared_ptr<Socket> client;
    std::shared_ptr<SocketBase> server;
};

inline bool NativeSocketIsOpen(decltype(Socket{}.GetFD()) fd) {
    int value{};
#ifdef _WIN32
    int size = sizeof(value);
#else
    socklen_t size = sizeof(value);
#endif
    return getsockopt(fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&value), &size) == 0;
}

} // namespace Network::Tests
