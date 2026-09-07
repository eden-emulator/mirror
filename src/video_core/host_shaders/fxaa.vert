// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) out vec4 posPos;

#ifdef VULKAN
#define BINDING_COLOR_TEXTURE 0
#define VERTEX_ID gl_VertexIndex
#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv
#define BINDING_COLOR_TEXTURE 0
#define VERTEX_ID gl_VertexID
#endif

layout (binding = BINDING_COLOR_TEXTURE) uniform sampler2D input_texture;

const float FXAA_SUBPIX_SHIFT = 0;
void main() {
    // 0b00 -> (-1, -1) 0b01 -> (-1, 3) 0b10 -> (3, -1)
    // ((x * 4) - 1) => 3
    // :: (v + 1) / 2 => ((x * 4) + 1) / 2
    // => (x * 4 + 1) / 2 => x * (4 / 2) + 1 / 2
    // :: (v+1)/2 => v/2 + 1/2 => v*(1/2) + (1/2)
    gl_Position = vec4(vec2(
        float(((VERTEX_ID & 1) << 2) - 1),
        float(((VERTEX_ID & 2) << 1) - 1)
    ), 0.0, 1.0);
    posPos = vec2(
        float((VERTEX_ID & 1) << 1),
        float((VERTEX_ID & 2) << 0)
    ).xyxy - vec4(
        0.0,
        0.0,
        (0.5 + FXAA_SUBPIX_SHIFT) / textureSize(input_texture, 0)
    );
}
