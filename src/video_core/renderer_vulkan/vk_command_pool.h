// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class MasterSemaphore;

class CommandPool final {
public:
    explicit CommandPool(MasterSemaphore& master_semaphore_, const Device& device_);
    ~CommandPool();

    VkCommandBuffer Commit();

private:
    struct Pool;

    void AllocatePool();
    void AcquirePool();

    MasterSemaphore& master_semaphore;
    const Device& device;
    std::vector<Pool> pools;
    size_t current_pool = 0;
    size_t current_index = 0;
};

} // namespace Vulkan
