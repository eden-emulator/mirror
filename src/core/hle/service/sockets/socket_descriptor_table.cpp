// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/sockets/socket_descriptor_table.h"
#include "core/hle/service/sockets/sockets_translate.h"

namespace Service::Sockets {

std::pair<s32, Errno> SocketDescriptorTable::Poll(std::span<PollFD> fds, s32 timeout) const {
    if (timeout < -1) {
        return {-1, Errno::INVAL};
    }
    std::vector<std::shared_ptr<SocketDescriptor>> snapshots;
    std::vector<Network::PollFD> host_fds;
    std::vector<size_t> indices;
    bool invalid = false;
    for (size_t i = 0; i < fds.size(); ++i) {
        auto& entry = fds[i];
        entry.revents = {};
        if (entry.fd < 0) {
            continue;
        }
        auto descriptor = Get(entry.fd);
        if (!descriptor || !descriptor->socket) {
            entry.revents = PollEvents::Nval;
            invalid = true;
            continue;
        }
        host_fds.push_back({descriptor->socket.get(), Translate(entry.events), {}});
        snapshots.push_back(std::move(descriptor));
        indices.push_back(i);
    }

    // Snapshot ownership pins native handles through concurrent descriptor closure.
    const auto [count, error] = Network::Poll(host_fds, invalid ? 0 : timeout);
    if (error != Network::Errno::SUCCESS) {
        return {-1, Translate(error)};
    }
    for (size_t i = 0; i < indices.size(); ++i) {
        fds[indices[i]].revents = Translate(host_fds[i].revents);
    }
    const auto ready =
        std::count_if(fds.begin(), fds.end(), [](const auto& fd) { return True(fd.revents); });
    return {static_cast<s32>(ready), Errno::SUCCESS};
}

} // namespace Service::Sockets
