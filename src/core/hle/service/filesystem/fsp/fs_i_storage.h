// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class IStorage final : public ServiceFramework<IStorage> {
public:
    explicit IStorage(Core::System& system_, FileSys::VirtualFile backend_);

private:
    FileSys::VirtualFile backend;

    static constexpr s64 ReadBytesPerSecond = 40'000'000;
    static constexpr s64 MaxChargedReadSize = 64 * 1024;
    std::chrono::steady_clock::time_point next_read_tick{};

    Result Read(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_bytes,
        s64 offset, s64 length);
    Result GetSize(Out<u64> out_size);
};

} // namespace Service::FileSystem
