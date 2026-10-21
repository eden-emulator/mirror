// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <future>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/hle/service/sockets/socket_descriptor_table.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "tests/core/internal_network/socket_test_helpers.h"

using namespace Service::Sockets;

TEST_CASE("Guest socket duplicates retain native ownership", "[network][socket]") {
    Network::NetworkInstance instance;
    const bool close_original_first = GENERATE(false, true);
    for (int repeat = 0; repeat < 128; ++repeat) {
        SocketDescriptorTable table;
        auto socket = Network::Tests::NewSocket();
        const auto native_fd = socket->GetFD();
        std::weak_ptr<Network::SocketBase> weak = socket;
        const auto original = table.Insert(std::move(socket), true);
        REQUIRE(original >= 0);
        const auto duplicate_result = table.Duplicate(original);
        REQUIRE(std::holds_alternative<s32>(duplicate_result));
        const auto duplicate = std::get<s32>(duplicate_result);
        const auto first = close_original_first ? original : duplicate;
        const auto last = close_original_first ? duplicate : original;
        table.Get(original)->flags = Network::FLAG_O_NONBLOCK;
        REQUIRE(table.Get(duplicate)->flags.load() == Network::FLAG_O_NONBLOCK);
        REQUIRE(table.Close(first) == Errno::SUCCESS);
        REQUIRE(table.Close(first) == Errno::BADF);
        REQUIRE_FALSE(table.Get(first));
        REQUIRE(std::get<Errno>(table.Duplicate(first)) == Errno::BADF);
        REQUIRE(Network::Tests::NativeSocketIsOpen(native_fd));
        REQUIRE(table.Get(last)->socket->Bind({Network::Domain::INET, {127, 0, 0, 1}, 0}) ==
                Network::Errno::SUCCESS);
        REQUIRE(table.Close(last) == Errno::SUCCESS);
        REQUIRE(weak.expired());
        REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
    }
}

TEST_CASE("Guest descriptors reject invalid and exhausted slots", "[network][socket]") {
    Network::NetworkInstance instance;
    SocketDescriptorTable table;
    REQUIRE(table.Insert(nullptr, true) == -1);
    for (s32 fd : {-1, static_cast<s32>(SocketDescriptorTable::Capacity), 10000}) {
        REQUIRE_FALSE(table.Get(fd));
        REQUIRE(table.Close(fd) == Errno::BADF);
        REQUIRE(std::get<Errno>(table.Duplicate(fd)) == Errno::BADF);
    }
    const auto original = table.Insert(Network::Tests::NewSocket(), true);
    for (size_t i = 1; i < SocketDescriptorTable::Capacity; ++i) {
        REQUIRE(std::holds_alternative<s32>(table.Duplicate(original)));
    }
    REQUIRE(std::get<Errno>(table.Duplicate(original)) == Errno::MFILE);
    REQUIRE_FALSE(table.HasFreeSlot());
    auto extra = Network::Tests::NewSocket();
    const auto native_fd = extra->GetFD();
    REQUIRE(table.Insert(std::move(extra), true) == -1);
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
}

TEST_CASE("Guest poll reports invalid entries and ignores negative descriptors",
          "[network][poll]") {
    Network::NetworkInstance instance;
    Network::Tests::TcpPair pair;
    SocketDescriptorTable table;
    const s32 fd = table.Insert(pair.client, true);
    const s32 dup = std::get<s32>(table.Duplicate(fd));
    REQUIRE(table.Close(fd) == Errno::SUCCESS);
    std::array<PollFD, 3> polls{{{dup, PollEvents::Out, PollEvents::Err},
                                 {fd, PollEvents::In, {}},
                                 {-1, PollEvents::In, PollEvents::Err}}};
    auto [count, error] = table.Poll(polls, -1);
    REQUIRE(error == Errno::SUCCESS);
    REQUIRE(count == 2);
    REQUIRE(True(polls[0].revents & PollEvents::Out));
    REQUIRE(polls[1].revents == PollEvents::Nval);
    REQUIRE(False(polls[2].revents));
    REQUIRE(table.Close(dup) == Errno::SUCCESS);
    polls[0].fd = static_cast<s32>(SocketDescriptorTable::Capacity);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 32; ++i) {
        REQUIRE(table.Poll(polls, -1).first == 2);
    }
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds{1});
    REQUIRE(table.Poll(polls, -2).second == Errno::INVAL);
}

TEST_CASE("Polling pins sockets while descriptors close concurrently", "[network][poll]") {
    Network::NetworkInstance instance;
    Network::Tests::TcpPair pair;
    SocketDescriptorTable table;
    const auto fd = table.Insert(std::move(pair.client), true);
    const auto native_fd = table.Get(fd)->socket->GetFD();
    std::promise<void> pinned;
    auto result = std::async(std::launch::async, [&] {
        auto snapshot = table.Get(fd);
        pinned.set_value();
        std::vector<Network::PollFD> entries{{snapshot->socket.get(), Network::PollEvents::In, {}}};
        return Network::Poll(entries, 1000);
    });
    pinned.get_future().wait();
    REQUIRE(table.Close(fd) == Errno::SUCCESS);
    REQUIRE(Network::Tests::NativeSocketIsOpen(native_fd));
    const std::array<u8, 1> byte{42};
    REQUIRE(pair.server->Send(byte, 0).first == 1);
    REQUIRE(result.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
    const auto [count, error] = result.get();
    REQUIRE(error == Network::Errno::SUCCESS);
    REQUIRE(count == 1);
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
    std::vector<Network::PollFD> invalid{{nullptr, Network::PollEvents::In, {}}};
    REQUIRE(Network::Poll(invalid, 0).first == 1);
    REQUIRE(invalid[0].revents == Network::PollEvents::Nval);
}

TEST_CASE("Socket errors remain errors at the guest boundary", "[network][socket]") {
    Network::NetworkInstance instance;
    const auto addresses = Network::GetAddressInfo("127.0.0.1", std::nullopt);
    REQUIRE(std::holds_alternative<std::vector<Network::AddrInfo>>(addresses));
    REQUIRE(Translate(Network::Errno::NOTSOCK) == Errno::NOTSOCK);
    REQUIRE(Translate(Network::Errno::OTHER) == Errno::IO);
    REQUIRE(Translate(Network::Errno::MSGSIZE) == Errno::MSGSIZE);
    REQUIRE(Translate(Network::Errno::HOSTUNREACH) == Errno::HOSTUNREACH);
    REQUIRE(Translate(Network::Errno::NETDOWN) == Errno::NETDOWN);
    REQUIRE(Translate(Network::Errno::NETUNREACH) == Errno::NETUNREACH);
    Network::Socket closed;
    REQUIRE(closed.Close() == Network::Errno::BADF);
#ifdef _WIN32
    REQUIRE(closed.GetPendingError().second == Network::Errno::NOTSOCK);
#else
    REQUIRE(closed.GetPendingError().second == Network::Errno::BADF);
#endif
    REQUIRE(closed.Initialize(Network::Domain::INET, Network::Type::STREAM,
                              Network::Protocol::UDP) == Network::Errno::PROTONOSUPPORT);
    REQUIRE_FALSE(closed.IsOpened());
    REQUIRE(closed.Initialize(Network::Domain::INET, Network::Type::STREAM,
                              Network::Protocol::Unspecified) == Network::Errno::SUCCESS);
    const auto native_fd = closed.GetFD();
    REQUIRE(closed.Initialize(Network::Domain::INET, Network::Type::STREAM,
                              Network::Protocol::TCP) == Network::Errno::INVAL);
    REQUIRE(closed.Close() == Network::Errno::SUCCESS);
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
}

TEST_CASE("Native polling preserves readiness, timeout and hangup events", "[network][poll]") {
    Network::NetworkInstance instance;
    Network::Tests::TcpPair pair;
    std::vector<Network::PollFD> entries{{pair.client.get(), Network::PollEvents::In, {}}};
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(Network::Poll(entries, 30).first == 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds{15});
    REQUIRE(elapsed < std::chrono::seconds{1});
    entries[0].events = Network::PollEvents::Out | Network::PollEvents::Err |
                        Network::PollEvents::Nval | Network::PollEvents::Hup;
    REQUIRE(Network::Poll(entries, 30).second == Network::Errno::SUCCESS);
    REQUIRE(True(entries[0].revents & Network::PollEvents::Out));
    char urgent = 19;
    REQUIRE(send(pair.server->GetFD(), &urgent, 1, MSG_OOB) == 1);
    entries[0].events = Network::PollEvents::Pri;
    REQUIRE(Network::Poll(entries, 500).first == 1);
    REQUIRE(True(entries[0].revents & Network::PollEvents::Pri));
    REQUIRE(recv(pair.client->GetFD(), &urgent, 1, MSG_OOB) == 1);
    const std::array<u8, 1> byte{21};
    REQUIRE(pair.server->Send(byte, 0).first == 1);
    entries[0].events = Network::PollEvents::In | Network::PollEvents::RdNorm;
    REQUIRE(Network::Poll(entries, 500).first == 1);
    REQUIRE(True(entries[0].revents & Network::PollEvents::In));
    REQUIRE(True(entries[0].revents & Network::PollEvents::RdNorm));
    std::array<u8, 1> received{};
    REQUIRE(pair.client->Recv(0, received).first == 1);
    REQUIRE(pair.server->Shutdown(Network::ShutdownHow::RDWR) == Network::Errno::SUCCESS);
    pair.server.reset();
    entries[0].events = Network::PollEvents::In;
    REQUIRE(Network::Poll(entries, 500).first == 1);
    REQUIRE(True(entries[0].revents &
                 (Network::PollEvents::In | Network::PollEvents::Hup | Network::PollEvents::Err)));
    REQUIRE(pair.client->Close() == Network::Errno::SUCCESS);
    REQUIRE(Network::Poll(entries, 0).first == 1);
    REQUIRE(entries[0].revents == Network::PollEvents::Nval);
}

TEST_CASE("Service teardown releases descriptors after pending operations finish",
          "[network][socket]") {
    Network::NetworkInstance instance;
    SocketDescriptorTable table;
    auto socket = Network::Tests::NewSocket();
    const auto native_fd = socket->GetFD();
    const auto fd = table.Insert(std::move(socket), true);
    const auto duplicate = std::get<s32>(table.Duplicate(fd));
    auto pending = table.Get(fd);
    table.Clear();
    REQUIRE_FALSE(table.Get(fd));
    REQUIRE_FALSE(table.Get(duplicate));
    REQUIRE(table.HasFreeSlot());
    REQUIRE(Network::Tests::NativeSocketIsOpen(native_fd));
    pending.reset();
    REQUIRE_FALSE(Network::Tests::NativeSocketIsOpen(native_fd));
    REQUIRE(table.Close(fd) == Errno::BADF);
    REQUIRE(table.Insert(Network::Tests::NewSocket(), true) == 0);
}
