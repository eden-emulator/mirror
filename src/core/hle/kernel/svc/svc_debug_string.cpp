// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "core/core.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/svc.h"
#include "core/memory.h"

namespace Kernel::Svc {

/// Used to output a message on a debug hardware unit - does nothing on a retail unit
Result OutputDebugString(Core::System& system, u64 address, u64 len) {
    R_SUCCEED_IF(len == 0);
    std::string msg_buffer(len, 0);
    GetCurrentMemory(system.Kernel()).ReadBlock(address, msg_buffer.data(), len);
    LOG_INFO(Debug_Emulated, "{}", msg_buffer);
    R_SUCCEED();
}

Result OutputDebugString64(Core::System& system, uint64_t debug_str, uint64_t len) {
    R_RETURN(OutputDebugString(system, debug_str, len));
}

Result OutputDebugString64From32(Core::System& system, uint32_t debug_str, uint32_t len) {
    R_RETURN(OutputDebugString(system, debug_str, len));
}

} // namespace Kernel::Svc
