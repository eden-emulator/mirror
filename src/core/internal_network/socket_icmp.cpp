// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <thread>
#ifdef __unix__
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "common/assert.h"
#include "common/logging.h"
#include "core/internal_network/socket_icmp.h"

extern "C" {
extern char **environ;
}

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
            return Errno::E_INVAL;
        std::memcpy(&rcv_timeo, optval.data(), sizeof(rcv_timeo));
    }
    return Errno::E_SUCCESS;
}

Errno IcmpSocket::Initialize(Domain domain, Type type, Protocol socket_protocol) {
    return Errno::E_SUCCESS;
}

std::pair<IcmpSocket::AcceptResult, Errno> IcmpSocket::Accept() {
    LOG_WARNING(Network, "(stubbed) called");
    return {AcceptResult{}, Errno::E_SUCCESS};
}

Errno IcmpSocket::Connect(Network::SockAddrIn addr_in) {
    LOG_WARNING(Network, "(stubbed) called");
    connected_addr = addr_in;
    return Errno::E_SUCCESS;
}

std::pair<Network::SockAddrIn, Errno> IcmpSocket::GetPeerName() {
    LOG_WARNING(Network, "(stubbed) called");
    return {Network::SockAddrIn{}, Errno::E_SUCCESS};
}

std::pair<Network::SockAddrIn, Errno> IcmpSocket::GetSockName() {
    LOG_WARNING(Network, "(stubbed) called");
    return {Network::SockAddrIn{}, Errno::E_SUCCESS};
}

Errno IcmpSocket::Bind(Network::SockAddrIn addr) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::E_SUCCESS;
}

Errno IcmpSocket::Listen(s32 backlog) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::E_SUCCESS;
}

Errno IcmpSocket::Shutdown(ShutdownHow how) {
    LOG_WARNING(Network, "(stubbed) called");
    return Errno::E_SUCCESS;
}

std::pair<s32, Errno> IcmpSocket::Recv(int flags, std::span<u8> message) {
    LOG_DEBUG(Network, "(stubbed) called");
    return connected_addr.has_value()
        ? RecvFrom(flags, message, nullptr)
        : std::make_pair(s32(0), Errno::E_NOTCONN);
}

std::pair<s32, Errno> IcmpSocket::RecvFrom(int flags, std::span<u8> message, Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "(stubbed) called");
    ASSERT(flags == 0);
    ASSERT(message.size() < std::size_t((std::numeric_limits<int>::max)()));

#if !defined(__OPENORBIS__) && (defined(__FreeBSD__) || defined(__linux__))
    const auto rcv_timeout_ms = (s64(rcv_timeo.tv_sec) * 1000) + (s64(rcv_timeo.tv_usec) / 1000);
    const auto timestamp = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard guard(pings_mutex);
            // find ping process that is finished running
            for (auto it = pings.begin(); it != pings.end();) {
                pid_t result = waitpid(it->ping_pid, &it->ping_status, WNOHANG);
                // ping process is still running, go to next
                if (result != it->ping_pid) {
                    ++it;
                    continue;
                }
                // ping process is finished, remove and handle it
                it = pings.erase(it);
                if (it->ping_status == 0) {
                    if (addr) {
                        addr->family = it->family;
                        addr->ip = it->ip;
                        addr->portno = it->portno;
                        addr->len = 16;
                        addr->zeroes = {};
                    }
                    std::array<u8, 8> data{
                        0,
                        0,
                        0, //checksum
                        0,
                        it->seq_ident[0],
                        it->seq_ident[1],
                        it->seq_ident[2],
                        it->seq_ident[3]
                    };
                    auto const csum = ComputeChecksum(std::span<const u8>{data.begin(), data.end()});
                    data[2] = u8(csum >> 8); //hi
                    data[3] = u8(csum); //lo
                    auto const n = std::min(data.size(), message.size());
                    std::copy(data.begin(), data.begin() + n, message.begin());
                    return {s32(n), Errno::E_SUCCESS};
                }
            }
        }

        if (!blocking)
            return {-1, Errno::E_AGAIN};

        const auto time_diff = std::chrono::steady_clock::now() - timestamp;
        const auto time_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_diff).count();
        if (time_diff_ms > rcv_timeout_ms)
            return {-1, Errno::E_TIMEDOUT};

        std::this_thread::yield();
    }
#endif
    return {-1, Errno::E_INVAL};
}

std::pair<s32, Errno> IcmpSocket::Send(std::span<const u8> message, int flags) {
    LOG_DEBUG(Network, "(stubbed) called");
    if (connected_addr.has_value())
        return SendTo(flags, message, std::addressof(connected_addr.value()));
    return {s32(0), Errno::E_NOTCONN};
}

std::pair<s32, Errno> IcmpSocket::SendTo(u32 flags, std::span<const u8> message, const Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "(stubbed) called");
    ASSERT(message.size() < size_t((std::numeric_limits<int>::max)()));
    // 0 -> 8 (IPv4), 128 (IPv6)
    // 1 -> 0
    // 2..4 -> checksum
    // 4..6 -> ident
    // 6..8 -> seq

    // PLEASE DON'T KILL ME, I SWEAR THIS IS LEGITIMATELY THE BEST WAY TO DO IT
    // IF YOU OPEN socket() GOOGLE WILL STRAIGHT UP IP BAN YOU AFTER 2 HOURS
#if !defined(__OPENORBIS__) && (defined(__FreeBSD__) || defined(__linux__))
    const auto rcv_timeout_ms = (s64(rcv_timeo.tv_sec) * 1000) + (s64(rcv_timeo.tv_usec) / 1000);
    if (!addr)
        return {-1, Errno::E_DESTADDRREQ};

    if (message.size() >= 8) {
        std::string ip_str = fmt::format(
            "{}.{}.{}.{}",
            addr->ip[0],
            addr->ip[1],
            addr->ip[2],
            addr->ip[3]
        );
#ifdef __FreeBSD__
        // ping -W option is a nonfractional int (milliseconds)
        std::string timeout_str = fmt::format("{}", rcv_timeout_ms);
        std::vector<char*> argv = {
            const_cast<char*>("ping"),
            const_cast<char*>("-c"),
            const_cast<char*>("1"),
            const_cast<char*>("-W"),
            timeout_str.data(),
            ip_str.data(),
            nullptr
        };
#elif defined(__linux__)
        // ping -W option is a fractional float (seconds)
        auto const rcv_timeout_s = f64(rcv_timeout_ms) / 1000.0;
        std::string timeout_str = fmt::format("{}", rcv_timeout_s);
        std::vector<char*> argv = {
            const_cast<char*>("ping"),
            const_cast<char*>("-c"),
            const_cast<char*>("1"),
            const_cast<char*>("-W"),
            timeout_str.data(),
            ip_str.data(),
            nullptr
        };
#endif
        pid_t ping_pid;
        // we should pass in attributes to stop stdout spam, but im too lazy to figure that out
        if (posix_spawnp(&ping_pid, "ping", nullptr, nullptr, argv.data(), environ) != 0) {
            LOG_ERROR(Network, "Unable to start ping process for emulated ICMP socket");
            return {-1, Errno::E_INVAL};
        }
        std::lock_guard guard(pings_mutex);
        if (pings.size() >= pings.max_size())
            pings.erase(pings.begin());
        pings.push_back(PingProcessData{
            .ip = addr->ip,
            .portno = addr->portno,
            .ping_pid = ping_pid,
            .ping_status = 0,
            .seq_ident = {
                message[4],
                message[5],
                message[6],
                message[7]
            },
            .family = addr->family,
        });
        return {s32(message.size()), Errno::E_SUCCESS};
    }
#endif
    return {-1, Errno::E_INVAL};
}

Errno IcmpSocket::Close() {
    LOG_DEBUG(Network, "called");
    fd = INVALID_SOCKET;
    return Errno::E_SUCCESS;
}

std::pair<Errno, Errno> IcmpSocket::GetPendingError() {
    LOG_DEBUG(Network, "called");
    return {Errno::E_SUCCESS, Errno::E_SUCCESS};
}

bool IcmpSocket::IsOpened() const {
    return fd != INVALID_SOCKET;
}

void IcmpSocket::HandleProxyPacket(const ProxyPacket& packet) {
    LOG_WARNING(Network, "(stubbed) called");
}
Errno IcmpSocket::SetNonBlock(bool enable) {
    blocking = !enable;
    return Errno::E_SUCCESS;
}

} // namespace Network
