// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450 core
#extension GL_ARB_shader_stencil_export : require

layout(binding = 0) uniform sampler2D depth_tex;
layout(binding = 1) uniform usampler2D stencil_tex;

layout(push_constant) uniform PushConstants {
    ivec2 dst_offset;
    ivec2 src_offset;
    ivec2 scale;
};

void main() {
    const ivec2 msaa_coord = ivec2(gl_FragCoord.xy) - dst_offset;
    const ivec2 sample_offset = ivec2(gl_SampleID % scale.x, gl_SampleID / scale.x);
    const ivec2 coord = msaa_coord * scale + sample_offset + src_offset;
    gl_FragDepth = texelFetch(depth_tex, coord, 0).r;
    gl_FragStencilRefARB = int(texelFetch(stencil_tex, coord, 0).r);
}
