// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

layout(binding = 0) uniform sampler2D depth_tex;
layout(binding = 1) uniform usampler2D stencil_tex;

layout(location = 0) out vec4 color;

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    uint depth_val =
        uint(roundEven(clamp(textureLod(depth_tex, coord, 0).r, 0.0, 1.0) * (exp2(24.0) - 1.0)));
    uint stencil_val = textureLod(stencil_tex, coord, 0).r;
    uvec4 components =
        uvec4(stencil_val, (uvec3(depth_val) >> uvec3(16u, 8u, 0u)) & 0x000000FFu);
    color.abgr = vec4(components) / (exp2(8.0) - 1.0);
}
