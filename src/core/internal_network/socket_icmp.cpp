// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/assert.h"
#include "common/logging.h"
#include "core/internal_network/socket_icmp.h"

#ifdef __unix__
#include <sys/socket.h>
#endif

namespace Network {

namespace {

u16 ComputeChecksum(std::span<const u8> data) {
    u32 sum = 0;
    for (size_t i = 0; i < data.size(); i += 2) {
        u32 value = (u32(data[i + 0]) << 8ull) | u32(data[i + 1]); //big endian
        sum += value;
    }
    if (data.size() % 2 != 0){
        sum += u16(data[data.size() - 1]) << 8;
    }
    while ((sum >> 16) != 0)
        sum = (sum & 0xffff) + (sum >> 16);
    return (~sum) & 0xffff;
}

}

IcmpSocket::IcmpSocket() noexcept {}

IcmpSocket::~IcmpSocket() {
    if (fd == INVALID_SOCKET) {
        return;
    }
    fd = INVALID_SOCKET;
}

Errno IcmpSocket::SetSockOpt(Network::SocketLevel level, Network::OptName optname, std::span<const u8> optval) {
    LOG_WARNING(Network, "(stubbed) level={},optname={},optval={}", level, optname, optval.size());
    return Errno::SUCCESS;
}

Errno IcmpSocket::Initialize(Domain domain, Type type, Protocol socket_protocol) {
    return Errno::SUCCESS;
}

std::pair<IcmpSocket::AcceptResult, Errno> IcmpSocket::Accept() {
    LOG_WARNING(Network, "(stubbed) called");
    return {AcceptResult{}, Errno::SUCCESS};
}

Errno IcmpSocket::Connect(Network::SockAddrIn addr_in) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::SUCCESS;
}

std::pair<Network::SockAddrIn, Errno> IcmpSocket::GetPeerName() {
    LOG_WARNING(Network, "(stubbed) called");
    return {Network::SockAddrIn{}, Errno::SUCCESS};
}

std::pair<Network::SockAddrIn, Errno> IcmpSocket::GetSockName() {
    LOG_WARNING(Network, "(stubbed) called");
    return {Network::SockAddrIn{}, Errno::SUCCESS};
}

Errno IcmpSocket::Bind(Network::SockAddrIn addr) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::SUCCESS;
}

Errno IcmpSocket::Listen(s32 backlog) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::SUCCESS;
}

Errno IcmpSocket::Shutdown(ShutdownHow how) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::SUCCESS;
}

std::pair<s32, Errno> IcmpSocket::Recv(int flags, std::span<u8> message) {
    LOG_DEBUG(Network, "(stubbed) called");
    return {s32(0), Errno::NOTCONN};
}

std::pair<s32, Errno> IcmpSocket::RecvFrom(int flags, std::span<u8> message, Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "(stubbed) called");
    ASSERT(flags == 0);
    ASSERT(message.size() < std::size_t((std::numeric_limits<int>::max)()));
    if (!data.empty()) {
        auto const n = (std::max)(data.size(), message.size());
        std::copy(data.begin(), data.begin() + n, message.begin());
        return {n, Errno::SUCCESS};
    }
    return {-1, Errno::TIMEDOUT};
}

std::pair<s32, Errno> IcmpSocket::Send(std::span<const u8> message, int flags) {
    LOG_DEBUG(Network, "(stubbed) called");
    return {s32(0), Errno::NOTCONN};
}

std::pair<s32, Errno> IcmpSocket::SendTo(u32 flags, std::span<const u8> message, const Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "(stubbed) called");
    // 0 -> 8 (IPv4), 128 (IPv6)
    // 1 -> 0
    // 2..4 -> checksum
    // 4..6 -> ident
    // 6..8 -> seq
    if (message.size() >= 8) {
        ASSERT(message[0] == 0);
        auto const csum_pos = data.size();
        data.push_back(8);
        data.push_back(0);
        data.push_back(0); //checksum (placeholder 0)
        data.push_back(0);
        data.push_back(message[4]); //ident
        data.push_back(message[5]);
        data.push_back(message[6]); //seq
        data.push_back(message[7]);
        auto const csum = ComputeChecksum(std::span<const u8>{data.begin() + csum_pos, data.end()});
        data[csum_pos + 2] = u8(csum >> 8); //hi
        data[csum_pos + 3] = u8(csum); //lo
    }
    return {s32(message.size()), Errno::SUCCESS};
}

Errno IcmpSocket::Close() {
    LOG_DEBUG(Network, "called");
    fd = INVALID_SOCKET;
    return Errno::SUCCESS;
}

std::pair<Errno, Errno> IcmpSocket::GetPendingError() {
    LOG_DEBUG(Network, "called");
    return {Errno::SUCCESS, Errno::SUCCESS};
}

bool IcmpSocket::IsOpened() const {
    return fd != INVALID_SOCKET;
}

void IcmpSocket::HandleProxyPacket(const ProxyPacket& packet) {
    LOG_WARNING(Network, "(stubbed) called");
}
Errno IcmpSocket::SetNonBlock(bool enable) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::SUCCESS;
}

} // namespace Network
