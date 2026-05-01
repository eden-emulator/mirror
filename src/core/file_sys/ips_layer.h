// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <vector>

#include "common/common_types.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

VirtualFile PatchIPS(const VirtualFile& in, const VirtualFile& ips);

class IPSwitchCompiler {
public:
    explicit IPSwitchCompiler(VirtualFile patch_text);
    ~IPSwitchCompiler();

    std::array<u8, 0x20> GetBuildID() const;
    VirtualFile Apply(const VirtualFile& in) const;

private:
    struct IPSwitchPatch;

    void ParseFlag(const std::string& flag);
    void Parse();

    VirtualFile patch_text;
    std::vector<IPSwitchPatch> patches;
    std::array<u8, 0x20> nso_build_id{};
};

} // namespace FileSys
