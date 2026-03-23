// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/abstract_syntax_list.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"

namespace Shader {
struct HostTranslateInfo;
namespace Maxwell {

struct ShaderPools;
[[nodiscard]] IR::AbstractSyntaxList BuildASL(ShaderPools& pools, Environment& env, Flow::CFG& cfg, const HostTranslateInfo& host_info);

} // namespace Maxwell
} // namespace Shader
