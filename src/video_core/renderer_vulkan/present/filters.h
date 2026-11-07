// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 Torzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/present/window_adapt_pass.h"

namespace Vulkan {

class MemoryAllocator;

std::unique_ptr<WindowAdaptPass> MakeNearestNeighbor(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeBilinear(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeBicubic(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format, VkCubicFilterWeightsQCOM qcom_weights);
std::unique_ptr<WindowAdaptPass> MakeSpline1(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeGaussian(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeLanczos(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeScaleForce(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeArea(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);
std::unique_ptr<WindowAdaptPass> MakeMmpx(const Device& device, MemoryAllocator& memory_allocator, VkFormat frame_format);

} // namespace Vulkan
