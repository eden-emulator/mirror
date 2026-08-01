// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/loader/homebrew_nxlink.h"

#include <array>
#include <cctype>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

#include <boost/asio.hpp>

#include "common/logging.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"

namespace Loader::HomebrewNxlink {
namespace {

using boost::asio::ip::tcp;

constexpr u16 NxlinkClientPort = 28771;
constexpr size_t ReceiveBufferSize = 1024;
constexpr size_t MaxLogLineSize = 16 * 1024;

struct LogConnection {
    explicit LogConnection(boost::asio::io_context& io_context_) : socket{io_context_} {}

    tcp::socket socket;
    std::array<char, ReceiveBufferSize> buffer{};
    std::string line;
};

struct LogServerState {
    LogServerState() : acceptor{io_context} {}

    boost::asio::io_context io_context;
    tcp::acceptor acceptor;
};

std::mutex server_mutex;
std::shared_ptr<LogServerState> server_state;
std::optional<std::jthread> server_thread;

std::optional<std::string> GetLastArgvToken(std::string_view argv_string) {
    while (!argv_string.empty() && argv_string.back() == '\0') {
        argv_string.remove_suffix(1);
    }

    std::optional<std::string_view> last_token;
    bool in_token = false;
    bool quoted = false;
    size_t token_begin = 0;
    size_t token_size = 0;

    for (size_t i = 0; i <= argv_string.size(); i++) {
        const char c = i < argv_string.size() ? argv_string[i] : '\0';

        if (!in_token) {
            if (c == '\0' || std::isspace(static_cast<unsigned char>(c))) {
                continue;
            }

            in_token = true;
            token_size = 0;
            if (c == '"') {
                quoted = true;
                token_begin = i + 1;
            } else {
                quoted = false;
                token_begin = i;
                token_size = 1;
            }
            continue;
        }

        const bool token_end =
            quoted ? c == '"' || c == '\0'
                   : c == '\0' || std::isspace(static_cast<unsigned char>(c));
        if (token_end) {
            if (token_size != 0) {
                last_token = argv_string.substr(token_begin, token_size);
            }
            in_token = false;
            quoted = false;
            token_size = 0;
            continue;
        }

        token_size++;
    }

    if (!last_token) {
        return std::nullopt;
    }
    return std::string{*last_token};
}

void AppendArgvToken(std::string& argv_string, std::string_view token) {
    while (!argv_string.empty() && argv_string.back() == '\0') {
        argv_string.pop_back();
    }
    if (!argv_string.empty()) {
        argv_string.push_back(' ');
    }
    argv_string.append(token);
}

void FlushLogLine(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (!line.empty()) {
        LOG_INFO(Loader, "{}", line);
        line.clear();
    }
}

void ConsumeLogBytes(LogConnection& connection, std::string_view bytes) {
    for (const char byte : bytes) {
        if (byte == '\n') {
            FlushLogLine(connection.line);
            continue;
        }

        connection.line.push_back(byte);
        if (connection.line.size() >= MaxLogLineSize) {
            FlushLogLine(connection.line);
        }
    }
}

void StartRead(std::shared_ptr<LogConnection> connection) {
    connection->socket.async_read_some(
        boost::asio::buffer(connection->buffer),
        [connection](const boost::system::error_code& error, size_t bytes_read) {
            if (error.failed()) {
                FlushLogLine(connection->line);
                return;
            }

            ConsumeLogBytes(*connection, {connection->buffer.data(), bytes_read});
            StartRead(connection);
        });
}

void StartAccept(std::shared_ptr<LogServerState> state) {
    auto connection = std::make_shared<LogConnection>(state->io_context);
    state->acceptor.async_accept(
        connection->socket,
        [state, connection](const boost::system::error_code& error) {
            if (!error.failed()) {
                LOG_INFO(Loader, "Homebrew nxlink log client connected");
                StartRead(connection);
            }
            if (state->acceptor.is_open()) {
                StartAccept(state);
            }
        });
}

void StartLogServer() {
    std::scoped_lock lock{server_mutex};
    if (server_thread) {
        return;
    }

    auto state = std::make_shared<LogServerState>();
    const tcp::endpoint endpoint{boost::asio::ip::address_v4::loopback(), NxlinkClientPort};

    boost::system::error_code error;
    state->acceptor.open(endpoint.protocol(), error);
    if (!error.failed()) {
        state->acceptor.set_option(tcp::acceptor::reuse_address(true), error);
    }
    if (!error.failed()) {
        state->acceptor.bind(endpoint, error);
    }
    if (!error.failed()) {
        state->acceptor.listen(boost::asio::socket_base::max_listen_connections, error);
    }
    if (error.failed()) {
        LOG_WARNING(Loader, "Homebrew nxlink log server could not listen on 127.0.0.1:{}: {}",
                    NxlinkClientPort, error.message());
        return;
    }

    StartAccept(state);
    server_state = state;
    server_thread.emplace([state](std::stop_token stop_token) {
        Common::SetCurrentThreadName("HomebrewNxlink");
        std::stop_callback stop_callback{stop_token, [state] {
            boost::system::error_code ignored;
            state->acceptor.close(ignored);
            state->io_context.stop();
        }};

        LOG_INFO(Loader, "Homebrew nxlink log server listening on 127.0.0.1:{}",
                 NxlinkClientPort);
        state->io_context.run();
        LOG_INFO(Loader, "Homebrew nxlink log server stopped");
    });
}

} // namespace

bool IsArgvMarker(std::string_view token) {
    if (token.size() != ArgvMarkerSize || token.substr(8) != ArgvMarkerSuffix) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        if (!std::isxdigit(static_cast<unsigned char>(token[i]))) {
            return false;
        }
    }

    return true;
}

bool IsLoopbackArgvMarker(std::string_view token) {
    if (!IsArgvMarker(token)) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        if (std::tolower(static_cast<unsigned char>(token[i])) !=
            std::tolower(static_cast<unsigned char>(LoopbackArgvMarker[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> GetArgvMarker(std::string_view argv_string) {
    auto last_token = GetLastArgvToken(argv_string);
    if (!last_token || !IsArgvMarker(*last_token)) {
        return std::nullopt;
    }
    return last_token;
}

std::optional<std::string> PrepareArgv(std::string& argv_string,
                                       std::string_view inherited_marker,
                                       bool append_loopback_marker) {
    auto marker = GetArgvMarker(argv_string);
    if (!marker && IsArgvMarker(inherited_marker)) {
        AppendArgvToken(argv_string, inherited_marker);
        marker = std::string{inherited_marker};
    }
    if (!marker && append_loopback_marker) {
        AppendArgvToken(argv_string, LoopbackArgvMarker);
        marker = std::string{LoopbackArgvMarker};
    }
    return marker;
}

void ApplyServerMode(Settings::HomebrewNxlinkServerMode mode,
                     const std::optional<std::string>& active_marker) {
    switch (mode) {
    case Settings::HomebrewNxlinkServerMode::Disabled:
        StopServer();
        return;
    case Settings::HomebrewNxlinkServerMode::EdenLog:
        if (active_marker && IsLoopbackArgvMarker(*active_marker)) {
            StartLogServer();
            return;
        }
        StopServer();
        if (active_marker) {
            LOG_INFO(Loader,
                     "Homebrew nxlink server mode overridden by argv marker '{}'; not starting "
                     "local nxlink log server",
                     *active_marker);
        } else {
            LOG_WARNING(Loader,
                        "Homebrew nxlink log server requested, but no argv marker is active");
        }
        return;
    case Settings::HomebrewNxlinkServerMode::HostStdout:
    case Settings::HomebrewNxlinkServerMode::File:
        StopServer();
        LOG_WARNING(Loader, "Homebrew nxlink server mode {} is not implemented",
                    static_cast<int>(mode));
        return;
    }
}

void StopServer() {
    std::shared_ptr<LogServerState> state;
    std::optional<std::jthread> thread;
    {
        std::scoped_lock lock{server_mutex};
        state = std::move(server_state);
        thread = std::move(server_thread);
    }

    if (state) {
        boost::system::error_code ignored;
        state->acceptor.close(ignored);
        state->io_context.stop();
    }
    if (thread) {
        thread->request_stop();
    }
}

} // namespace Loader::HomebrewNxlink
