// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/vulkan_common/vulkan.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4189 )
#endif
// Same warning as MSVC 4189 above. VMA feeds results into VMA_ASSERT, which
// compiles away under NDEBUG and leaves the variables unused, so this only
// shows up in Release builds.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#include "vk_mem_alloc.h"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning( pop )
#endif
