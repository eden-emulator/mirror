// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>
#include <vector>
#include <variant>

#include "common/common_types.h"
#include "core/internal_network/socket_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sockets/sockets.h"
#include "network/network.h"

namespace Core {
class System;
}

namespace Network {
class SocketBase;
class Socket;
} // namespace Network

namespace Service::Sockets {

class BSD final : public ServiceFramework<BSD> {
public:
    explicit BSD(Core::System& system_, const char* name);
    ~BSD() override;

    // These methods are called from SSL; the first two are also called from
    // this class for the corresponding IPC methods.
    // On the real device, the SSL service makes IPC calls to this service.
    std::variant<s32, Network::Errno> DuplicateSocketImpl(s32 fd);
    Network::Errno CloseImpl(s32 fd);
    std::optional<std::shared_ptr<Network::SocketBase>> GetSocket(s32 fd);

private:
    /// Maximum number of file descriptors
    static constexpr size_t MAX_FD = 128;

    struct FileDescriptor {
        std::shared_ptr<Network::SocketBase> socket;
        s32 flags = 0;
        bool is_connection_based = false;
    };

    struct PollWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 nfds;
        s32 timeout;
        std::span<const u8> read_buffer;
        std::vector<u8> write_buffer;
        s32 ret{};
        Network::Errno bsd_errno{};
    };

    struct AcceptWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        std::vector<u8> write_buffer;
        s32 ret{};
        Network::Errno bsd_errno{};
    };

    struct ConnectWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        std::span<const u8> addr;
        Network::Errno bsd_errno{};
    };

    struct RecvWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::vector<u8> message;
        s32 ret{};
        Network::Errno bsd_errno{};
    };

    struct RecvFromWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::vector<u8> message;
        std::vector<u8> addr;
        s32 ret{};
        Network::Errno bsd_errno{};
    };

    struct SendWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::span<const u8> message;
        s32 ret{};
        Network::Errno bsd_errno{};
    };

    struct SendToWork {
        void Execute(BSD* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::span<const u8> message;
        std::span<const u8> addr;
        s32 ret{};
        Network::Errno bsd_errno{};
    };

    void RegisterClient(HLERequestContext& ctx);
    void StartMonitoring(HLERequestContext& ctx);
    void Socket(HLERequestContext& ctx);
    void Select(HLERequestContext& ctx);
    void Poll(HLERequestContext& ctx);
    void Accept(HLERequestContext& ctx);
    void Bind(HLERequestContext& ctx);
    void Connect(HLERequestContext& ctx);
    void GetPeerName(HLERequestContext& ctx);
    void GetSockName(HLERequestContext& ctx);
    void GetSockOpt(HLERequestContext& ctx);
    void Listen(HLERequestContext& ctx);
    void Fcntl(HLERequestContext& ctx);
    void SetSockOpt(HLERequestContext& ctx);
    void Shutdown(HLERequestContext& ctx);
    void Recv(HLERequestContext& ctx);
    void RecvFrom(HLERequestContext& ctx);
    void Send(HLERequestContext& ctx);
    void SendTo(HLERequestContext& ctx);
    void Write(HLERequestContext& ctx);
    void Read(HLERequestContext& ctx);
    void Close(HLERequestContext& ctx);
    void DuplicateSocket(HLERequestContext& ctx);
    void EventFd(HLERequestContext& ctx);

    template <typename Work>
    void ExecuteWork(HLERequestContext& ctx, Work work);

    std::pair<s32, Network::Errno> SocketImpl(Network::Domain domain, Network::Type type, Network::Protocol protocol);
    std::pair<s32, Network::Errno> PollImpl(std::vector<u8>& write_buffer, std::span<const u8> read_buffer,
                                   s32 nfds, s32 timeout);
    std::pair<s32, Network::Errno> AcceptImpl(s32 fd, std::vector<u8>& write_buffer);
    Network::Errno BindImpl(s32 fd, std::span<const u8> addr);
    Network::Errno ConnectImpl(s32 fd, std::span<const u8> addr);
    Network::Errno GetPeerNameImpl(s32 fd, std::vector<u8>& write_buffer);
    Network::Errno GetSockNameImpl(s32 fd, std::vector<u8>& write_buffer);
    Network::Errno ListenImpl(s32 fd, s32 backlog);
    std::pair<s32, Network::Errno> FcntlImpl(s32 fd, Network::FcntlCmd cmd, s32 arg);
    Network::Errno GetSockOptImpl(s32 fd, u32 level, Network::OptName optname, std::vector<u8>& optval);
    Network::Errno SetSockOptImpl(s32 fd, u32 level, Network::OptName optname, std::span<const u8> optval);
    Network::Errno ShutdownImpl(s32 fd, s32 how);
    std::pair<s32, Network::Errno> RecvImpl(s32 fd, u32 flags, std::vector<u8>& message);
    std::pair<s32, Network::Errno> RecvFromImpl(s32 fd, u32 flags, std::vector<u8>& message,
                                       std::vector<u8>& addr);
    std::pair<s32, Network::Errno> SendImpl(s32 fd, u32 flags, std::span<const u8> message);
    std::pair<s32, Network::Errno> SendToImpl(s32 fd, u32 flags, std::span<const u8> message,
                                     std::span<const u8> addr);

    s32 FindFreeFileDescriptorHandle() noexcept;
    bool IsFileDescriptorValid(s32 fd) const noexcept;

    void BuildErrnoResponse(HLERequestContext& ctx, Network::Errno bsd_errno) const noexcept;

    static inline std::array<std::optional<FileDescriptor>, MAX_FD> file_descriptors{};

    /// Callback to parse and handle a received wifi packet.
    void OnProxyPacketReceived(const Network::ProxyPacket& packet);

    // Callback identifier for the OnProxyPacketReceived event.
    Network::RoomMember::CallbackHandle<Network::ProxyPacket> proxy_packet_received;

protected:
    std::unique_lock<std::mutex> LockService() noexcept override;
};

class BSDCFG final : public ServiceFramework<BSDCFG> {
public:
    explicit BSDCFG(Core::System& system_);
    ~BSDCFG() override;
};

} // namespace Service::Sockets
