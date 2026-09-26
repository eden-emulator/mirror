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
#include "common/socket_types.h"
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

class BSD_USA final : public ServiceFramework<BSD_USA> {
public:
    explicit BSD_USA(Core::System& system_, const char* name, bool is_user);
    ~BSD_USA() override;

    // These methods are called from SSL; the first two are also called from
    // this class for the corresponding IPC methods.
    // On the real device, the SSL service makes IPC calls to this service.
    std::variant<s32, Errno> DuplicateSocketImpl(s32 fd);
    Errno CloseImpl(s32 fd);
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
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 nfds;
        s32 timeout;
        std::span<const u8> read_buffer;
        std::vector<u8> write_buffer;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct AcceptWork {
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        std::vector<u8> write_buffer;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct ConnectWork {
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        std::span<const u8> addr;
        Errno bsd_errno{};
    };

    struct RecvWork {
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::vector<u8> message;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct RecvFromWork {
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::vector<u8> message;
        std::vector<u8> addr;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct SendWork {
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::span<const u8> message;
        s32 ret{};
        Errno bsd_errno{};
    };

    struct SendToWork {
        void Execute(BSD_USA* bsd);
        void Response(HLERequestContext& ctx);

        s32 fd;
        u32 flags;
        std::span<const u8> message;
        std::span<const u8> addr;
        s32 ret{};
        Errno bsd_errno{};
    };

    void RegisterClient(HLERequestContext& ctx);
    void StartMonitoring(HLERequestContext& ctx);
    void Socket(HLERequestContext& ctx);
    void SocketExempt(HLERequestContext& ctx);
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

    std::pair<s32, Errno> SocketImpl(Domain domain, Type type, Protocol protocol);
    std::pair<s32, Errno> PollImpl(std::vector<u8>& write_buffer, std::span<const u8> read_buffer, s32 nfds, s32 timeout);
    std::pair<s32, Errno> AcceptImpl(s32 fd, std::vector<u8>& write_buffer);
    Errno BindImpl(s32 fd, std::span<const u8> addr);
    Errno ConnectImpl(s32 fd, std::span<const u8> addr);
    Errno GetPeerNameImpl(s32 fd, std::vector<u8>& write_buffer);
    Errno GetSockNameImpl(s32 fd, std::vector<u8>& write_buffer);
    Errno ListenImpl(s32 fd, s32 backlog);
    std::pair<s32, Errno> FcntlImpl(s32 fd, FcntlCmd cmd, s32 arg);
    Errno GetSockOptImpl(s32 fd, u32 level, OptName optname, std::vector<u8>& optval);
    Errno SetSockOptImpl(s32 fd, u32 level, OptName optname, std::span<const u8> optval);
    Errno ShutdownImpl(s32 fd, s32 how);
    std::pair<s32, Errno> RecvImpl(s32 fd, u32 flags, std::vector<u8>& message);
    std::pair<s32, Errno> RecvFromImpl(s32 fd, u32 flags, std::vector<u8>& message,
                                       std::vector<u8>& addr);
    std::pair<s32, Errno> SendImpl(s32 fd, u32 flags, std::span<const u8> message);
    std::pair<s32, Errno> SendToImpl(s32 fd, u32 flags, std::span<const u8> message,
                                     std::span<const u8> addr);

    s32 FindFreeFileDescriptorHandle() noexcept;
    bool IsFileDescriptorValid(s32 fd) const noexcept;

    void BuildErrnoResponse(HLERequestContext& ctx, Errno bsd_errno) const noexcept;

    static inline std::array<std::optional<FileDescriptor>, MAX_FD> file_descriptors{};

    /// Callback to parse and handle a received wifi packet.
    void OnProxyPacketReceived(const Network::ProxyPacket& packet);

    // Callback identifier for the OnProxyPacketReceived event.
    Network::RoomMember::CallbackHandle<Network::ProxyPacket> proxy_packet_received;

protected:
    std::unique_lock<std::mutex> LockService() noexcept override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, &BSD_USA::RegisterClient, "RegisterClient"},
        FunctionInfo{1, &BSD_USA::StartMonitoring, "StartMonitoring"},
        FunctionInfo{2, &BSD_USA::Socket, "Socket"},
        FunctionInfo{3, &BSD_USA::SocketExempt, "SocketExempt"},
        FunctionInfo{4, nullptr, "Open"},
        FunctionInfo{5, &BSD_USA::Select, "Select"},
        FunctionInfo{6, &BSD_USA::Poll, "Poll"},
        FunctionInfo{7, nullptr, "Sysctl"},
        FunctionInfo{8, &BSD_USA::Recv, "Recv"},
        FunctionInfo{9, &BSD_USA::RecvFrom, "RecvFrom"},
        FunctionInfo{10, &BSD_USA::Send, "Send"},
        FunctionInfo{11, &BSD_USA::SendTo, "SendTo"},
        FunctionInfo{12, &BSD_USA::Accept, "Accept"},
        FunctionInfo{13, &BSD_USA::Bind, "Bind"},
        FunctionInfo{14, &BSD_USA::Connect, "Connect"},
        FunctionInfo{15, &BSD_USA::GetPeerName, "GetPeerName"},
        FunctionInfo{16, &BSD_USA::GetSockName, "GetSockName"},
        FunctionInfo{17, &BSD_USA::GetSockOpt, "GetSockOpt"},
        FunctionInfo{18, &BSD_USA::Listen, "Listen"},
        FunctionInfo{19, nullptr, "Ioctl"},
        FunctionInfo{20, &BSD_USA::Fcntl, "Fcntl"},
        FunctionInfo{21, &BSD_USA::SetSockOpt, "SetSockOpt"},
        FunctionInfo{22, &BSD_USA::Shutdown, "Shutdown"},
        FunctionInfo{23, nullptr, "ShutdownAllSockets"},
        FunctionInfo{24, &BSD_USA::Write, "Write"},
        FunctionInfo{25, &BSD_USA::Read, "Read"},
        FunctionInfo{26, &BSD_USA::Close, "Close"},
        FunctionInfo{27, &BSD_USA::DuplicateSocket, "DuplicateSocket"},
        FunctionInfo{28, nullptr, "GetResourceStatistics"},
        FunctionInfo{29, nullptr, "RecvMMsg"}, //3.0.0+
        FunctionInfo{30, nullptr, "SendMMsg"}, //3.0.0+
        FunctionInfo{31, &BSD_USA::EventFd, "EventFd"}, //7.0.0+
        FunctionInfo{32, nullptr, "RegisterResourceStatisticsName"}, //7.0.0+
        FunctionInfo{33, nullptr, "RegisterClientShared"}, //10.0.0+
        FunctionInfo{34, nullptr, "GetSocketStatistics"}, //15.0.0+
        FunctionInfo{35, nullptr, "NifIoctl"}, //17.0.0+
        FunctionInfo{36, nullptr, "Unknown36"}, //18.0.0+
        FunctionInfo{37, nullptr, "Unknown37"}, //18.0.0+
        FunctionInfo{38, nullptr, "Unknown38"}, //18.0.0+
        FunctionInfo{39, nullptr, "Unknown39"}, //20.0.0+
        FunctionInfo{40, nullptr, "Unknown40"}, //20.0.0+
        FunctionInfo{41, nullptr, "Unknown41"}, //21.0.0+
        FunctionInfo{42, nullptr, "Unknown42"}, //21.0.0+
        FunctionInfo{43, nullptr, "Unknown43"}, //21.0.0+
        FunctionInfo{200, nullptr, "SetThreadCoreMask"}, //15.0.0+
        FunctionInfo{201, nullptr, "GetThreadCoreMask"} //15.0.0+
    );
    bool is_user = false;
};

class BSDCFG final : public ServiceFramework<BSDCFG> {
public:
    explicit BSDCFG(Core::System& system_, const char *name);
    ~BSDCFG() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "SetIfUp"},
        FunctionInfo{1, nullptr, "SetIfUpWithEvent"},
        FunctionInfo{2, nullptr, "CancelIf"},
        FunctionInfo{3, nullptr, "SetIfDown"},
        FunctionInfo{4, nullptr, "GetIfState"},
        FunctionInfo{5, nullptr, "DhcpRenew"},
        FunctionInfo{6, nullptr, "AddStaticArpEntry"},
        FunctionInfo{7, nullptr, "RemoveArpEntry"},
        FunctionInfo{8, nullptr, "LookupArpEntry"},
        FunctionInfo{9, nullptr, "LookupArpEntry2"},
        FunctionInfo{10, nullptr, "ClearArpEntries"},
        FunctionInfo{11, nullptr, "ClearArpEntries2"},
        FunctionInfo{12, nullptr, "PrintArpEntries"},
        FunctionInfo{13, nullptr, "Unknown13"},
        FunctionInfo{14, nullptr, "Unknown14"},
        FunctionInfo{15, nullptr, "Unknown15"}
    );
};

class BSD_NU final : public ServiceFramework<BSD_NU> {
public:
    explicit BSD_NU(Core::System& system_);
    ~BSD_NU() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "CreateUserService"}
    );
};

} // namespace Service::Sockets
