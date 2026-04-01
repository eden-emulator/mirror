// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/vulkan_common/vulkan.h"

// #if !defined(VMA_IMPLEMENTATION) && !defined(VMA_IMPLEMENTED)
// #define VMA_IMPLEMENTED
// #define VMA_IMPLEMENTATION
// #endif

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4189 )
#endif
#include "vk_mem_alloc.h"

#ifdef _MSC_VER
#pragma warning( pop )
#endif

// #undef VMA_IMPLEMENTATION
