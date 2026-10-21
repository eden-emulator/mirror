// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <variant>
#include <vector>

#include "common/common_types.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/internal_network/sockets.h"

namespace Service::Sockets {

struct SocketDescriptor {
    std::shared_ptr<Network::SocketBase> socket;
    std::atomic<s32> flags{0};
    bool is_connection_based = false;
};

class SocketDescriptorTable {
public:
    static constexpr size_t Capacity = 128;

    std::pair<s32, Errno> Poll(std::span<PollFD> fds, s32 timeout) const;

    s32 Insert(std::shared_ptr<Network::SocketBase> socket, bool is_connection_based) {
        if (!socket) {
            return -1;
        }
        std::scoped_lock lock{mutex};
        for (size_t i = 0; i < descriptors.size(); ++i) {
            if (!descriptors[i]) {
                auto descriptor = std::make_shared<SocketDescriptor>();
                descriptor->socket = std::move(socket);
                descriptor->is_connection_based = is_connection_based;
                descriptors[i] = std::move(descriptor);
                return static_cast<s32>(i);
            }
        }
        return -1;
    }

    std::shared_ptr<SocketDescriptor> Get(s32 fd) const {
        std::scoped_lock lock{mutex};
        return IsValid(fd) ? descriptors[fd] : nullptr;
    }

    bool HasFreeSlot() const {
        std::scoped_lock lock{mutex};
        return std::find(descriptors.begin(), descriptors.end(), nullptr) != descriptors.end();
    }

    std::variant<s32, Errno> Duplicate(s32 fd) {
        std::scoped_lock lock{mutex};
        if (!IsValid(fd) || !descriptors[fd]) {
            return Errno::BADF;
        }
        for (size_t i = 0; i < descriptors.size(); ++i) {
            if (!descriptors[i]) {
                descriptors[i] = descriptors[fd];
                return static_cast<s32>(i);
            }
        }
        return Errno::MFILE;
    }

    Errno Close(s32 fd) {
        std::shared_ptr<SocketDescriptor> removed;
        {
            std::scoped_lock lock{mutex};
            if (!IsValid(fd) || !descriptors[fd]) {
                return Errno::BADF;
            }
            removed = std::move(descriptors[fd]);
        }
        // Operations retain a descriptor snapshot. The socket destructor closes the native handle
        // after the final descriptor, SSL connection, and pending operation release their
        // references.
        return Errno::SUCCESS;
    }

    void Clear() {
        decltype(descriptors) removed;
        {
            std::scoped_lock lock{mutex};
            removed.swap(descriptors);
        }
    }

    std::vector<std::shared_ptr<SocketDescriptor>> Snapshot() const {
        std::scoped_lock lock{mutex};
        std::vector<std::shared_ptr<SocketDescriptor>> result;
        for (const auto& descriptor : descriptors) {
            if (descriptor && std::find(result.begin(), result.end(), descriptor) == result.end()) {
                result.push_back(descriptor);
            }
        }
        return result;
    }

private:
    static bool IsValid(s32 fd) {
        return fd >= 0 && static_cast<size_t>(fd) < Capacity;
    }

    mutable std::mutex mutex;
    std::array<std::shared_ptr<SocketDescriptor>, Capacity> descriptors{};
};

} // namespace Service::Sockets
