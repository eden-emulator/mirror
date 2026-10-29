// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Shader::Maxwell::Flow {
class CFG;
}

namespace Shader::Ampere {

// Fills a Maxwell::Flow::CFG with the blocks of a native Ampere program so the existing structured
// control flow builder can be reused unchanged. Friend of Flow::CFG.
struct CfgBuilder {
    static void Build(Maxwell::Flow::CFG& cfg);
};

} // namespace Shader::Ampere
