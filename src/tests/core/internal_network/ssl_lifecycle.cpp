// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <semaphore>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/sockets/socket_descriptor_table.h"
#include "core/hle/service/ssl/ssl_connection.h"
#include "tests/core/internal_network/socket_test_helpers.h"

namespace {

using Service::SSL::Connection;
using Service::SSL::CreateSSLConnectionBackend;
using Service::SSL::ResultInternalError;
using Service::SSL::ResultNoSocket;
using Service::SSL::ResultWouldBlock;

Service::SSL::SetOptionParameters WireOption(u8 value, Service::SSL::OptionType option) {
    const std::array<u8, 8> wire{value, 0, 0, 0, static_cast<u8>(option), 0, 0, 0};
    Service::SSL::SetOptionParameters parameters{};
    std::memcpy(&parameters, wire.data(), wire.size());
    return parameters;
}

struct TlsEnvironment {
    TlsEnvironment() {
#ifdef _WIN32
        _putenv_s("SSL_CERT_FILE", NETWORK_TEST_CERT_DIR "/cert.pem");
#else
        setenv("SSL_CERT_FILE", NETWORK_TEST_CERT_DIR "/cert.pem", 1);
#endif
    }
};

Result FinishHandshake(Connection& connection) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    for (int attempts = 0; attempts < 100; ++attempts) {
        const Result result = connection.DoHandshake();
        if (result != ResultWouldBlock) {
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::vector<Network::PollFD> entries{
            {connection.GetSocket().get(), Network::PollEvents::In, {}}};
        if (Network::Poll(entries, 30).second != Network::Errno::SUCCESS) {
            return ResultInternalError;
        }
    }
    return Service::SSL::ResultTimeout;
}

struct TlsServer {
    explicit TlsServer(std::shared_ptr<Network::SocketBase> socket, bool tls = true,
                       bool untrusted = false) {
        REQUIRE(socket->SetRcvTimeo(2000) == Network::Errno::SUCCESS);
        REQUIRE(socket->SetSndTimeo(2000) == Network::Errno::SUCCESS);
        SSL_CTX* context = SSL_CTX_new(TLS_server_method());
        REQUIRE(context);
        REQUIRE(SSL_CTX_use_certificate_file(context,
                                             untrusted ? NETWORK_TEST_CERT_DIR "/untrusted-cert.pem"
                                                       : NETWORK_TEST_CERT_DIR "/cert.pem",
                                             SSL_FILETYPE_PEM) == 1);
        REQUIRE(SSL_CTX_use_PrivateKey_file(context,
                                            untrusted ? NETWORK_TEST_CERT_DIR "/untrusted-key.pem"
                                                      : NETWORK_TEST_CERT_DIR "/key.pem",
                                            SSL_FILETYPE_PEM) == 1);
        worker = std::async(std::launch::async, [this, context, socket = std::move(socket), tls] {
            gate.acquire();
            if (!tls) {
                const std::array<u8, 5> invalid{1, 2, 3, 4, 5};
                socket->Send(invalid, 0);
                socket->Shutdown(Network::ShutdownHow::RDWR);
                SSL_CTX_free(context);
                return false;
            }
            ::SSL* ssl = SSL_new(context);
            bool success = ssl && SSL_set_fd(ssl, static_cast<int>(socket->GetFD())) == 1 &&
                           SSL_accept(ssl) == 1;
            if (success) {
                std::array<u8, 4> bytes{};
                size_t size{};
                success = SSL_read_ex(ssl, bytes.data(), bytes.size(), &size) == 1 &&
                          size == bytes.size() && SSL_write_ex(ssl, bytes.data(), size, &size) == 1;
            }
            SSL_free(ssl);
            SSL_CTX_free(context);
            return success;
        });
    }

    ~TlsServer() {
        Start();
        if (worker.valid()) {
            worker.wait();
        }
    }

    void Start() {
        if (!started) {
            started = true;
            gate.release();
        }
    }

    std::binary_semaphore gate{0};
    std::future<bool> worker;
    bool started = false;
};

std::unique_ptr<Connection> NewConnection() {
    static const TlsEnvironment environment;
    std::unique_ptr<Service::SSL::SSLConnectionBackend> backend;
    REQUIRE(CreateSSLConnectionBackend(&backend) == ResultSuccess);
    backend->SetVerifyOption(3);
    return std::make_unique<Connection>(std::move(backend));
}

} // namespace

TEST_CASE("SSL retains sockets through verified local TLS", "[network][ssl]") {
    Network::NetworkInstance instance;
    const bool nonblocking = GENERATE(false, true);
    const bool do_not_close = GENERATE(false, true);
    Network::Tests::TcpPair pair;
    TlsServer server{pair.server};
    Service::Sockets::SocketDescriptorTable table;
    const s32 fd = table.Insert(std::move(pair.client), true);
    const auto native_fd = table.Get(fd)->socket->GetFD();
    auto connection = NewConnection();
    REQUIRE(connection->DoHandshake() == ResultNoSocket);
    REQUIRE(connection->SetIoMode(nonblocking) == ResultNoSocket);
    REQUIRE(connection->SetOption(WireOption(
                do_not_close, Service::SSL::OptionType::DoNotCloseSocket)) == ResultSuccess);
    s32 returned_fd = -2;
    REQUIRE(connection->SetSocketDescriptor(fd, table.Get(fd)->socket, &returned_fd) ==
            ResultSuccess);
    REQUIRE(returned_fd == (do_not_close ? -1 : fd));
    REQUIRE(connection->GetSocketDescriptor() == fd);
    REQUIRE(connection->SetDoNotCloseSocket(!do_not_close) != ResultSuccess);
    REQUIRE(connection->SetSocketDescriptor(fd, table.Get(fd)->socket, &returned_fd) !=
            ResultSuccess);
    REQUIRE(connection->Backend().SetHostName("localhost") == ResultSuccess);
    REQUIRE(connection->SetOption(WireOption(0, Service::SSL::OptionType::GetServerCertChain)) ==
            ResultSuccess);
    REQUIRE(connection->SetOption(WireOption(0, Service::SSL::OptionType::SkipDefaultVerify)) ==
            ResultSuccess);
    REQUIRE(connection->SetOption(WireOption(0, Service::SSL::OptionType::EnableAlpn)) ==
            ResultSuccess);
    REQUIRE(connection->GetDoNotCloseSocket() == do_not_close);
    REQUIRE(connection->SetIoMode(nonblocking) == ResultSuccess);
    REQUIRE(connection->IsNonBlocking() == nonblocking);
    if (!do_not_close) {
        REQUIRE(table.Close(returned_fd) == Service::Sockets::Errno::SUCCESS);
    }
    if (nonblocking) {
        REQUIRE(connection->DoHandshake() == ResultWouldBlock);
        REQUIRE_FALSE(connection->DidHandshake());
    }
    server.Start();
    REQUIRE(FinishHandshake(*connection) == ResultSuccess);
    REQUIRE(connection->DidHandshake());
    REQUIRE(connection->SetIoMode(false) == ResultSuccess);
    REQUIRE(connection->GetSocket()->SetRcvTimeo(2000) == Network::Errno::SUCCESS);
    const std::array<u8, 4> request{1, 3, 5, 7};
    size_t size{};
    REQUIRE(connection->Backend().Write(&size, request) == ResultSuccess);
    REQUIRE(size == request.size());
    std::array<u8, 4> response{};
    REQUIRE(connection->Backend().Peek(&size, response) == ResultSuccess);
    REQUIRE(size == response.size());
    REQUIRE(response == request);
    REQUIRE(connection->Backend().Read(&size, response) == ResultSuccess);
    REQUIRE(size == response.size());
    REQUIRE(response == request);
    connection.reset();
    REQUIRE(server.worker.wait_for(std::chrono::seconds{3}) == std::future_status::ready);
    REQUIRE(server.worker.get());
    REQUIRE(Network::Tests::NativeSocketIsOpen(native_fd) == do_not_close);
    if (do_not_close) {
        REQUIRE(table.Get(fd)->socket->GetSockName().second == Network::Errno::SUCCESS);
        REQUIRE(table.Close(fd) == Service::Sockets::Errno::SUCCESS);
    }
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
}

TEST_CASE("SSL verification and failed handshakes release their socket", "[network][ssl]") {
    Network::NetworkInstance instance;
    const int failure = GENERATE(0, 1, 2);
    const bool use_tls = failure != 0;
    Network::Tests::TcpPair pair;
    TlsServer server{pair.server, use_tls, failure == 2};
    const auto native_fd = pair.client->GetFD();
    auto connection = NewConnection();
    s32 returned_fd{};
    REQUIRE(connection->SetSocketDescriptor(7, pair.client, &returned_fd) == ResultSuccess);
    pair.client.reset();
    connection->Backend().SetVerifyOption(0);
    REQUIRE(connection->Backend().SetHostName(failure == 2 ? "localhost" : "wrong-host.invalid") ==
            ResultSuccess);
    connection->Backend().SetVerifyOption(3);
    REQUIRE(connection->GetSocket()->SetRcvTimeo(2000) == Network::Errno::SUCCESS);
    server.Start();
    REQUIRE(FinishHandshake(*connection) == ResultInternalError);
    REQUIRE_FALSE(connection->DidHandshake());
    connection.reset();
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
    REQUIRE(server.worker.wait_for(std::chrono::seconds{3}) == std::future_status::ready);
    REQUIRE_FALSE(server.worker.get());
}

TEST_CASE("SSL rejects failed attachment without retaining resources", "[network][ssl]") {
    Network::NetworkInstance instance;
    auto connection = NewConnection();
    s32 output = -2;
    REQUIRE(connection->SetSocketDescriptor(128, nullptr, &output) ==
            Service::SSL::ResultInvalidSocket);
    REQUIRE(output == -2);
    REQUIRE(connection->DoHandshake() == ResultNoSocket);
    REQUIRE_FALSE(connection->GetSocket());
    auto socket = Network::Tests::NewSocket();
    const auto native_fd = socket->GetFD();
    REQUIRE(connection->SetSocketDescriptor(0, std::move(socket), &output) == ResultSuccess);
    connection.reset();
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
}

TEST_CASE("SSL SetOption decodes the guest IPC payload after socket attachment",
          "[network][ssl][ipc]") {
    Network::NetworkInstance instance;
    const u8 option = GENERATE(u8{1}, u8{2}, u8{3});
    const u8 value = GENERATE(u8{0}, u8{1});
    Network::Tests::TcpPair pair;
    auto connection = NewConnection();
    Service::AutoOut<s32> output{-2};
    REQUIRE(connection->SetSocketDescriptor(0, pair.client, Service::Out<s32>{output}) ==
            ResultSuccess);
    REQUIRE(output.raw == 0);
    REQUIRE(connection->Backend().SetHostName("server.opencourse.world") == ResultSuccess);
    const std::array<u8, 8> wire{value, 0, 0, 0, option, 0, 0, 0};
    Service::SSL::SetOptionParameters parameters{};
    std::memcpy(&parameters, wire.data(), wire.size());
    const Result result = connection->SetOption(parameters);
    CAPTURE(option, parameters.value, static_cast<u32>(parameters.option), result.raw);
    REQUIRE(result == ResultSuccess);
    REQUIRE(parameters.value == value);
    REQUIRE(static_cast<u32>(parameters.option) == option);
    REQUIRE(connection->GetOption(static_cast<Service::SSL::OptionType>(option)) == bool(value));
    REQUIRE_FALSE(connection->GetDoNotCloseSocket());
    REQUIRE_FALSE(connection->DidHandshake());
}

TEST_CASE("SSL ownership options preserve signed IPC descriptor outputs", "[network][ssl][ipc]") {
    Network::NetworkInstance instance;
    const bool do_not_close = GENERATE(false, true);
    Network::Tests::TcpPair pair;
    auto connection = NewConnection();
    auto parameters = WireOption(do_not_close, Service::SSL::OptionType::DoNotCloseSocket);
    parameters.padding = {0x5a, 0xa5, 0xff};
    REQUIRE(connection->SetOption(parameters) == ResultSuccess);
    REQUIRE(connection->GetDoNotCloseSocket() == do_not_close);
    Service::AutoOut<s32> output{-2};
    REQUIRE(connection->SetSocketDescriptor(0, pair.client, Service::Out<s32>{output}) ==
            ResultSuccess);
    REQUIRE(output.raw == (do_not_close ? -1 : 0));
    std::array<u8, 4> wire{};
    std::memcpy(wire.data(), &output.raw, wire.size());
    const u8 expected = do_not_close ? 0xff : 0;
    REQUIRE(wire == std::array<u8, 4>{expected, expected, expected, expected});
    REQUIRE(connection->GetSocketDescriptor() == 0);
    REQUIRE(connection->SetOption(WireOption(
                !do_not_close, Service::SSL::OptionType::DoNotCloseSocket)) == ResultInternalError);
    REQUIRE(connection->GetDoNotCloseSocket() == do_not_close);
    REQUIRE_FALSE(connection->DidHandshake());
}

TEST_CASE("SSL guest cleanup cannot close a reused guest descriptor", "[network][ssl][ipc]") {
    Network::NetworkInstance instance;
    Network::Tests::TcpPair pair;
    Service::Sockets::SocketDescriptorTable table;
    const s32 fd = table.Insert(std::move(pair.client), true);
    REQUIRE(fd == 0);
    const auto native_fd = table.Get(fd)->socket->GetFD();
    auto connection = NewConnection();
    Service::AutoOut<s32> output{-2};
    REQUIRE(connection->SetSocketDescriptor(fd, table.Get(fd)->socket, Service::Out<s32>{output}) ==
            ResultSuccess);
    REQUIRE(connection->Backend().SetHostName("server.opencourse.world") == ResultSuccess);
    REQUIRE(connection->SetOption(WireOption(0, Service::SSL::OptionType::GetServerCertChain)) ==
            ResultSuccess);
    REQUIRE(output.raw == fd);
    REQUIRE(table.Close(output.raw) == Service::Sockets::Errno::SUCCESS);
    REQUIRE(Network::Tests::NativeSocketIsOpen(native_fd));
    REQUIRE(connection->GetSocketDescriptor() == fd);
    const s32 reused_fd = table.Insert(Network::Tests::NewSocket(), true);
    REQUIRE(reused_fd == fd);
    const auto reused_native_fd = table.Get(reused_fd)->socket->GetFD();
    connection.reset();
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
    REQUIRE(Network::Tests::NativeSocketIsOpen(reused_native_fd));
    REQUIRE(table.Close(reused_fd) == Service::Sockets::Errno::SUCCESS);
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(reused_native_fd));
}
