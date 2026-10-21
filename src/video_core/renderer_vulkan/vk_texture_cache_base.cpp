// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/texture_cache/texture_cache.h"

namespace VideoCommon {
template class VideoCommon::TextureCache<Vulkan::TextureCacheParams>;
template void VideoCommon::TextureCache<Vulkan::TextureCacheParams>::UpdateImagesFromBuffer<VkBuffer>(
    DAddr, size_t, VkBuffer, u64);
}
