// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#ifdef HAS_NCE
constexpr size_t VirtualReserveSize = 1ULL << 38;
#else
constexpr size_t VirtualReserveSize = 1ULL << 39;
#endif

namespace {
size_t ApplicationPoolOffset() {
    using Init = Kernel::Board::Nintendo::Nx::KSystemControl::Init;
    const size_t dram_size = Init::GetIntendedMemorySize();
    const size_t application_pool_size = Init::GetApplicationPoolSize();
    return dram_size > application_pool_size ? dram_size - application_pool_size : 0;
}
}

DeviceMemory::DeviceMemory()
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
             VirtualReserveSize, ApplicationPoolOffset()} {}

DeviceMemory::~DeviceMemory() = default;

} // namespace Core
