// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <span>
#include <utility>
#include <sys/types.h>
#include <mutex>
#include <boost/container/static_vector.hpp>
#include "core/internal_network/socket_types.h"
#include "core/internal_network/sockets.h"

namespace Network {

struct PingProcessData {
    IPv4Address ip;
    u16 portno;
    pid_t ping_pid;
    pid_t ping_status;
    std::array<u8, 4> seq_ident;
    u8 family;
};

class IcmpSocket : public Network::SocketBase {
public:
    explicit IcmpSocket() noexcept = default;
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

    boost::container::static_vector<PingProcessData, 128> pings;
    std::optional<SockAddrIn> connected_addr;
    std::mutex pings_mutex;
    Network::Timeval rcv_timeo;
    bool blocking = true;
};

} // namespace Network
