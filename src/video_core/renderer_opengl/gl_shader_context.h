// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/container/stable_vector.hpp>
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"

namespace OpenGL::ShaderContext {

struct Context {
    explicit Context(Core::Frontend::EmuWindow& emu_window) : gl_context{emu_window.CreateSharedContext()}, scoped{*gl_context} {}
    std::unique_ptr<Core::Frontend::GraphicsContext> gl_context;
    Core::Frontend::GraphicsContext::Scoped scoped;
    Shader::Maxwell::ShaderPools pools;
};

} // namespace OpenGL::ShaderContext
