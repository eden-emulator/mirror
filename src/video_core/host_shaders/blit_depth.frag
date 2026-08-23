// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450 core

layout(binding = 0) uniform sampler2D depth_tex;

layout(location = 0) in vec2 texcoord;

void main() {
    gl_FragDepth = textureLod(depth_tex, texcoord, 0).r;
}
