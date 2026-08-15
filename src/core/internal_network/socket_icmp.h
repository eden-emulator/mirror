// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include "core/internal_network/sockets.h"

namespace Network {

class IcmpSocket : public Network::SocketBase {
public:
    explicit IcmpSocket() noexcept;
    ~IcmpSocket() override;
    Errno Initialize(Domain domain, Type type, Protocol socket_protocol) override;
    Errno Close() override;
    std::pair<AcceptResult, Errno> Accept() override;
    Errno Connect(Network::SockAddrIn addr_in) override;
    std::pair<Network::SockAddrIn, Errno> GetPeerName() override;
    std::pair<Network::SockAddrIn, Errno> GetSockName() override;
    Errno Bind(Network::SockAddrIn addr) override;
    Errno Listen(s32 backlog) override;
    Errno Shutdown(ShutdownHow how) override;
    std::pair<s32, Errno> Recv(int flags, std::span<u8> message) override;
    std::pair<s32, Errno> RecvFrom(int flags, std::span<u8> message, Network::SockAddrIn* addr) override;
    std::pair<s32, Errno> Send(std::span<const u8> message, int flags) override;
    std::pair<s32, Errno> SendTo(u32 flags, std::span<const u8> message, const Network::SockAddrIn* addr) override;
    Errno SetSockOpt(Network::SocketLevel level, Network::OptName option, std::span<const u8> value) override;
    std::pair<Errno, Errno> GetPendingError() override;
    bool IsOpened() const override;
    void HandleProxyPacket(const ProxyPacket& packet) override;
    Errno SetNonBlock(bool enable) override;
};

} // namespace Network
