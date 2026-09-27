// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <thread>

#include "common/assert.h"
#include "common/logging.h"
#include "common/zstd_compression.h"
#include "core/internal_network/network.h"
#include "core/internal_network/network_interface.h"
#include "core/internal_network/socket_proxy.h"
#include "network/network.h"

#ifdef __unix__
#include <sys/socket.h>
#endif

namespace Network {

ProxySocket::ProxySocket() noexcept {}

ProxySocket::~ProxySocket() {
    if (fd == INVALID_SOCKET) {
        return;
    }
    fd = INVALID_SOCKET;
}

void ProxySocket::HandleProxyPacket(const ProxyPacket& packet) {
    if (protocol != packet.protocol || local_endpoint.portno != packet.remote_endpoint.portno ||
        closed) {
        return;
    }

    if (!broadcast && packet.broadcast) {
        LOG_INFO(Network, "Received broadcast packet, but not configured for broadcast mode");
        return;
    }

    auto decompressed = packet;
    decompressed.data = Common::Compression::DecompressDataZSTD(packet.data);

    std::lock_guard guard(packets_mutex);
    received_packets.push(decompressed);
}

bool ProxySocket::GetNonBlock() {
    return blocking;
}

Errno ProxySocket::SetNonBlock(bool enable) {
    blocking = !enable;
    return Errno::E_SUCCESS;
}

Errno ProxySocket::SetSockOpt(Network::SocketLevel level, Network::OptName optname, std::span<const u8> optval) {
    LOG_DEBUG(Network, "level={},optname={},optval={}", level, optname, optval.size());
    // numeric values?
    if (optval.size() >= sizeof(u32)) {
        u32 value;
        std::memcpy(&value, optval.data(), sizeof(value));
        if (optname == Network::OptName::BROADCAST)
            broadcast = bool(value);
        if (optname == Network::OptName::SNDTIMEO)
            send_timeout = value;
        if (optname == Network::OptName::RCVTIMEO)
            receive_timeout = value;
    }
    return Errno::E_SUCCESS;
}

Errno ProxySocket::Initialize(Domain domain, Type type, Protocol socket_protocol) {
    protocol = socket_protocol;
    return Errno::E_SUCCESS;
}

std::pair<ProxySocket::AcceptResult, Errno> ProxySocket::Accept() {
    LOG_WARNING(Network, "(stubbed) called");
    return {AcceptResult{}, Errno::E_SUCCESS};
}

Errno ProxySocket::Connect(Network::SockAddrIn addr_in) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::E_SUCCESS;
}

std::pair<Network::SockAddrIn, Errno> ProxySocket::GetPeerName() {
    LOG_WARNING(Network, "(stubbed) called");
    return {Network::SockAddrIn{}, Errno::E_SUCCESS};
}

std::pair<Network::SockAddrIn, Errno> ProxySocket::GetSockName() {
    LOG_WARNING(Network, "(stubbed) called");
    return {Network::SockAddrIn{}, Errno::E_SUCCESS};
}

Errno ProxySocket::Bind(Network::SockAddrIn addr) {
    if (is_bound) {
        LOG_WARNING(Network, "Rebinding Socket is unimplemented!");
        return Errno::E_SUCCESS;
    }
    local_endpoint = addr;
    is_bound = true;
    return Errno::E_SUCCESS;
}

Errno ProxySocket::Listen(s32 backlog) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::E_SUCCESS;
}

Errno ProxySocket::Shutdown(ShutdownHow how) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::E_SUCCESS;
}

std::pair<s32, Errno> ProxySocket::Recv(int flags, std::span<u8> message) {
    LOG_WARNING(Network, "(stubbed) called");
    ASSERT(flags == 0 && message.size() < std::size_t((std::numeric_limits<int>::max)()));
    return {s32(0), Errno::E_SUCCESS};
}

std::pair<s32, Errno> ProxySocket::RecvFrom(int flags, std::span<u8> message, Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "called");
    ASSERT(flags == 0 && message.size() < std::size_t((std::numeric_limits<int>::max)()));
    do {
        std::unique_lock lk{packets_mutex};
        if (received_packets.size() > 0)
            return ReceivePacket(flags, message, addr, message.size());
    } while (blocking);
    return {-1, Errno::E_AGAIN};
}

std::pair<s32, Errno> ProxySocket::ReceivePacket(int flags, std::span<u8> message, Network::SockAddrIn* addr, std::size_t max_length) {
    LOG_DEBUG(Network, "called");
    ProxyPacket& packet = received_packets.front();
    if (addr) {
        addr->len = 16;
        addr->family = u8(Network::Domain::INET);
        addr->ip = packet.local_endpoint.ip;         // The senders ip address
        addr->portno = packet.local_endpoint.portno; // The senders port number
        addr->zeroes = {};
    }

    bool peek = (flags & u32(Network::MsgOpt::PEEK)) != 0;
    std::size_t read_bytes = (std::min)(max_length, packet.data.size());
    std::memcpy(message.data(), packet.data.data(), read_bytes);
    if (!peek) {
        packet.data.erase(packet.data.begin(), packet.data.begin() + read_bytes);
        if (packet.data.empty())
            received_packets.pop();
    }
    if (packet.data.size() > max_length && protocol == Protocol::UDP) {
        LOG_ERROR(Network, "Packet size");
        return {-1, Errno::E_MSGSIZE};
    }
    return {u32(read_bytes), Errno::E_SUCCESS};
}

std::pair<s32, Errno> ProxySocket::Send(std::span<const u8> message, int flags) {
    LOG_WARNING(Network, "(stubbed) called");
    ASSERT(message.size() < size_t((std::numeric_limits<int>::max)()));
    ASSERT(flags == 0);
    return {s32(0), Errno::E_SUCCESS};
}

void ProxySocket::SendPacket(ProxyPacket& packet) {
    if (auto room_member = Network::GetRoomMember().lock()) {
        if (room_member->IsConnected()) {
            packet.data = Common::Compression::CompressDataZSTDDefault(packet.data.data(), packet.data.size());
            room_member->SendProxyPacket(packet);
        }
    }
}

std::pair<s32, Errno> ProxySocket::SendTo(u32 flags, std::span<const u8> message, const Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "called");
    ASSERT(flags == 0);

    if (!is_bound) {
        LOG_ERROR(Network, "ProxySocket is not bound!");
        return {s32(message.size()), Errno::E_SUCCESS};
    }

    if (auto room_member = Network::GetRoomMember().lock()) {
        if (!room_member->IsConnected()) {
            return {s32(message.size()), Errno::E_SUCCESS};
        }
    }

    ProxyPacket packet;
    packet.local_endpoint = local_endpoint;
    packet.remote_endpoint = *addr;
    packet.protocol = protocol;
    packet.broadcast = broadcast && packet.remote_endpoint.ip[3] == 255;

    auto& ip = local_endpoint.ip;
    auto ipv4 = Network::GetHostIPv4Address();
    // If the ip is all zeroes (INADDR_ANY) or if it matches the hosts ip address,
    // replace it with a "fake" routing address
    if (std::all_of(ip.begin(), ip.end(), [](u8 i) { return i == 0; }) || (ipv4 && ipv4 == ip)) {
        if (auto room_member = Network::GetRoomMember().lock()) {
            packet.local_endpoint.ip = room_member->GetFakeIpAddress();
        }
    }

    packet.data.clear();
    std::copy(message.begin(), message.end(), std::back_inserter(packet.data));

    SendPacket(packet);

    return {s32(message.size()), Errno::E_SUCCESS};
}

Errno ProxySocket::Close() {
    LOG_DEBUG(Network, "called");
    fd = INVALID_SOCKET;
    closed = true;

    return Errno::E_SUCCESS;
}

std::pair<Errno, Errno> ProxySocket::GetPendingError() {
    LOG_DEBUG(Network, "called");
    return {Errno::E_SUCCESS, Errno::E_SUCCESS};
}

bool ProxySocket::IsOpened() const {
    return fd != INVALID_SOCKET;
}

} // namespace Network
