// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include "common/assert.h"
#include "common/logging.h"
#include "core/internal_network/socket_icmp.h"

#ifdef __unix__
#include <unistd.h>
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
    if (optname == Network::OptName::RCVTIMEO) {
        if (optval.size() < sizeof(Network::Timeval))
            return Errno::INVAL;
        std::memcpy(&rcv_timeo, optval.data(), sizeof(rcv_timeo));
    }
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
#if !defined(__OPENORBIS__) && (defined(__FreeBSD__) || defined(__linux__))
    if (addr) {
        if (seq_ident.empty())
            return {0, Errno::SUCCESS};
        // PLEASE DON'T KILL ME, I SWEAR THIS IS LEGITIMATELY THE BEST WAY TO DO IT
        // IF YOU OPEN socket() GOOGLE WILL STRAIGHT UP IP BAN YOU AFTER 2 HOURS
        auto const rcv_timeout = std::max<u64>(rcv_timeo.tv_sec, 0);
#ifdef __FreeBSD__
        auto const cmd = fmt::format("ping -t {} -o {}.{}.{}.{}", rcv_timeout, addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
#elif defined(__linux__)
        auto const cmd = fmt::format("ping -c 1 -W {} {}.{}.{}.{}", rcv_timeout, addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
#endif
        if (::system(cmd.c_str()) == 0) {
            std::vector<u8> data{
                8,
                0,
                0, //checksum
                0,
                u8(seq_ident.front() >> 24), //ident
                u8(seq_ident.front() >> 16),
                u8(seq_ident.front() >> 8), // seq
                u8(seq_ident.front() >> 0),
            };
            seq_ident.pop_back();
            auto const csum = ComputeChecksum(std::span<const u8>{data.begin(), data.end()});
            data[2] = u8(csum >> 8); //hi
            data[3] = u8(csum); //lo
            auto const n = (std::max)(data.size(), message.size());
            std::copy(data.begin(), data.begin() + n, message.begin());
            return {n, Errno::SUCCESS};
        }
        return {-1, Errno::TIMEDOUT};
    }
#endif
    return {-1, Errno::INVAL};
}

std::pair<s32, Errno> IcmpSocket::Send(std::span<const u8> message, int flags) {
    LOG_DEBUG(Network, "(stubbed) called");
    seq_ident.push_back(
        (u32(message[4]) << 24)
        | (u32(message[5]) << 16)
        | (u32(message[6]) << 8)
        | (u32(message[7]) << 0)
    );
    return {s32(0), Errno::NOTCONN};
}

std::pair<s32, Errno> IcmpSocket::SendTo(u32 flags, std::span<const u8> message, const Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "(stubbed) called");
    ASSERT(message.size() < size_t((std::numeric_limits<int>::max)()));
    // 0 -> 8 (IPv4), 128 (IPv6)
    // 1 -> 0
    // 2..4 -> checksum
    // 4..6 -> ident
    // 6..8 -> seq
    if (!message.empty())
        return {s32(message.size()), Errno::SUCCESS};
    return {-1, Errno::INVAL};
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
    return Errno::SUCCESS;
}

} // namespace Network
