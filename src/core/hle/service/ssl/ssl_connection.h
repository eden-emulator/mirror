// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

#include "core/hle/service/ssl/ssl_backend.h"
#include "core/internal_network/sockets.h"

namespace Service::SSL {

enum class OptionType : u32 {
    DoNotCloseSocket = 0,
    GetServerCertChain = 1,
    SkipDefaultVerify = 2,
    EnableAlpn = 3,
};

// CMIF places the flag byte before the aligned option, rather than in public API argument order.
struct SetOptionParameters {
    u8 value;
    std::array<u8, 3> padding;
    OptionType option;
};
static_assert(sizeof(SetOptionParameters) == 8);
static_assert(offsetof(SetOptionParameters, value) == 0);
static_assert(offsetof(SetOptionParameters, option) == 4);

class Connection {
public:
    explicit Connection(std::unique_ptr<SSLConnectionBackend> backend_)
        : backend{std::move(backend_)} {}

    Result SetSocketDescriptor(s32 fd, std::shared_ptr<Network::SocketBase> socket_, s32* out_fd) {
        if (socket || !socket_) {
            return ResultInvalidSocket;
        }
        socket = std::move(socket_);
        input_fd = fd;
        // The shared reference is SSL's private duplicate; it needs no guest descriptor slot.
        backend->SetSocket(socket);
        *out_fd = do_not_close_socket ? -1 : fd;
        return ResultSuccess;
    }

    Result SetDoNotCloseSocket(bool value) {
        if (socket) {
            return ResultInternalError;
        }
        do_not_close_socket = value;
        return ResultSuccess;
    }

    bool GetDoNotCloseSocket() const {
        return do_not_close_socket;
    }

    Result SetOption(SetOptionParameters parameters) {
        const bool value = parameters.value != 0;
        switch (parameters.option) {
        case OptionType::DoNotCloseSocket:
            return SetDoNotCloseSocket(value);
        case OptionType::GetServerCertChain:
            get_server_cert_chain = value;
            break;
        case OptionType::SkipDefaultVerify:
            skip_default_verify = value;
            break;
        case OptionType::EnableAlpn:
            enable_alpn = value;
            break;
        }
        return ResultSuccess;
    }

    bool GetOption(OptionType option) const {
        switch (option) {
        case OptionType::DoNotCloseSocket:
            return do_not_close_socket;
        case OptionType::GetServerCertChain:
            return get_server_cert_chain;
        case OptionType::SkipDefaultVerify:
            return skip_default_verify;
        case OptionType::EnableAlpn:
            return enable_alpn;
        }
        return false;
    }

    Result SetIoMode(bool nonblocking) {
        if (!socket) {
            return ResultNoSocket;
        }
        if (socket->SetNonBlock(nonblocking) != Network::Errno::SUCCESS) {
            return ResultInternalError;
        }
        is_nonblocking = nonblocking;
        return ResultSuccess;
    }

    bool IsNonBlocking() const {
        return is_nonblocking;
    }

    Result DoHandshake() {
        if (!socket) {
            return ResultNoSocket;
        }
        if (did_handshake) {
            return ResultInternalError;
        }
        const Result result = backend->DoHandshake();
        did_handshake = result.IsSuccess();
        return result;
    }

    bool DidHandshake() const {
        return did_handshake;
    }

    s32 GetSocketDescriptor() const {
        return input_fd;
    }

    const std::shared_ptr<Network::SocketBase>& GetSocket() const {
        return socket;
    }

    SSLConnectionBackend& Backend() {
        return *backend;
    }

private:
    std::shared_ptr<Network::SocketBase> socket;
    std::unique_ptr<SSLConnectionBackend> backend;
    s32 input_fd = -1;
    bool do_not_close_socket = false;
    bool did_handshake = false;
    bool is_nonblocking = false;
    bool get_server_cert_chain = false;
    bool skip_default_verify = false;
    bool enable_alpn = false;
};

} // namespace Service::SSL
