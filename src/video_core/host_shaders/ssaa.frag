// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460
#extension GL_ARB_sample_shading : enable

#ifdef VULKAN
#define BINDING_COLOR_TEXTURE 1
#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv
#define BINDING_COLOR_TEXTURE 0
#endif

layout (location = 0) in vec4 posPos;
layout (location = 0) out vec4 frag_color;
layout (binding = BINDING_COLOR_TEXTURE) uniform sampler2DMS input_texture;

void main() {
    frag_color = texelFetch(input_texture, ivec2(gl_FragCoord.xy), gl_SampleID);
}
