// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Source code is adapted from
// https://www.geeks3d.com/20110405/fxaa-fast-approximate-anti-aliasing-demo-glsl-opengl-test-radeon-geforce/3/

#version 460

#ifdef VULKAN
#define BINDING_COLOR_TEXTURE 1
#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv
#define BINDING_COLOR_TEXTURE 0
#endif

layout (location = 0) in vec4 posPos;
layout (location = 0) out vec4 frag_color;
layout (binding = BINDING_COLOR_TEXTURE) uniform sampler2D input_texture;

const float FXAA_SPAN_MAX = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

vec3 FxaaPixelShader(vec4 posPos, sampler2D tex) {
    mat4x3 rgb_matrix = mat4x3(
        textureLod(tex, posPos.zw, 0.0).xyz,
        textureLodOffset(tex, posPos.zw, 0.0, ivec2(1, 0)).xyz,
        textureLodOffset(tex, posPos.zw, 0.0, ivec2(0, 1)).xyz,
        textureLodOffset(tex, posPos.zw, 0.0, ivec2(1, 1)).xyz
    );
    vec3 rgbM  = textureLod(tex, posPos.xy, 0.0).xyz;
    vec3 luma = vec3(0.299, 0.587, 0.114);
    // vec4(dot(m1, l), dot(m2, l), ...) => m * l
    vec4 lume_per_rgb = luma * rgb_matrix;
    float lumaM  = dot(rgbM,  luma);
    float lumaMin = min(lumaM, min(min(lume_per_rgb.x, lume_per_rgb.y), min(lume_per_rgb.z, lume_per_rgb.w)));
    float lumaMax = max(lumaM, max(max(lume_per_rgb.x, lume_per_rgb.y), max(lume_per_rgb.z, lume_per_rgb.w)));
    vec2 dir = vec2(
        -((lume_per_rgb.x + lume_per_rgb.y) - (lume_per_rgb.z + lume_per_rgb.w)),
        +((lume_per_rgb.x + lume_per_rgb.z) - (lume_per_rgb.y + lume_per_rgb.w))
    );
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + max(
        (lume_per_rgb.x + lume_per_rgb.y + lume_per_rgb.z + lume_per_rgb.w) * (0.25 * FXAA_REDUCE_MUL),
        FXAA_REDUCE_MIN
    ));
    dir = min(
        vec2(FXAA_SPAN_MAX),
        max(vec2(-FXAA_SPAN_MAX), dir * rcp_dir_min)
    );
    // Constants (calculating via division is faster)
    mat4x2 const_dir = mat4x2(
        (1.0 / 3.0 - 0.5) / textureSize(tex, 0),
        (2.0 / 3.0 - 0.5) / textureSize(tex, 0),
        (0.0 / 3.0 - 0.5) / textureSize(tex, 0),
        (3.0 / 3.0 - 0.5) / textureSize(tex, 0)
    );
    // dir * const_dir[i] => dir * const_dir => vec4
    vec3 rgbA = (1.0 / 2.0) * (
        textureLod(tex, fma(dir, const_dir[0], posPos.xy), 0.0).xyz +
        textureLod(tex, fma(dir, const_dir[1], posPos.xy), 0.0).xyz
    );
    vec3 rgbB = fma(rgbA, vec3(1.0 / 2.0), (1.0 / 4.0) * (
        textureLod(tex, fma(dir, const_dir[2], posPos.xy), 0.0).xyz +
        textureLod(tex, fma(dir, const_dir[3], posPos.xy), 0.0).xyz
    ));
    float lumaB = dot(rgbB, luma);
    return ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;
}

void main() {
    frag_color = vec4(FxaaPixelShader(posPos, input_texture), texture(input_texture, posPos.xy).a);
}
