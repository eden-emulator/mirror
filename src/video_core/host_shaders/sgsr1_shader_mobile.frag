// SPDX-FileCopyrightText: Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#version 460 core

precision highp float;
precision highp int;

#define EDGE_THRESHOLD (8.0 / 255.0)
#define DIRECTION_EPSILON 6.5e-05
#define DEVIATION_FLOOR 6.0e-02

layout(push_constant) uniform constants {
    vec2 scale;
    vec2 size;
    vec2 resize_factor;
    vec2 crop_offset;
    float edge_sharpness;
};
layout(set = 0, binding = 0) uniform sampler2D sampler0;
layout(location=0) in vec2 texcoord;
layout(location=0) out vec4 frag_color;

mediump vec4 fastLanczos2(mediump vec4 x) {
    mediump vec4 wA = x - 4.0f;
    mediump vec4 wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

mediump vec2 edgeDirection(mediump vec4 left, mediump vec4 right) {
    mediump float RxLz = right.x - left.z;
    mediump float RwLy = right.w - left.y;
    mediump vec2 delta = vec2(RxLz + RwLy, RxLz - RwLy);
    mediump float length_inv =
        inversesqrt((delta.x * delta.x + DIRECTION_EPSILON) + delta.y * delta.y);
    return delta * length_inv;
}

mediump vec4 weightY(mediump vec4 dx, mediump vec4 dy, mediump vec4 c, mediump float std,
                     mediump vec2 dir) {
    mediump vec4 edge_dis = dx * dir.y + dy * dir.x;
    mediump vec4 x = (dx * dx + dy * dy) +
                     (edge_dis * edge_dis) * (clamp((c * c) * std, 0.0f, 1.0f) * 0.7f - 1.0f);
    return fastLanczos2(x);
}

void main() {
    mediump vec4 color = textureLod(sampler0, texcoord.xy, 0.0f);
    highp vec2 icoord = (texcoord * size + vec2(-0.5f, 0.5f));
    highp vec2 icoord_pixel = floor(icoord);
    highp vec2 coord = icoord_pixel * scale;
    mediump vec2 pl = icoord - icoord_pixel;
    mediump mat3x4 dg = mat3x4(
        textureGather(sampler0, coord, 1),
        textureGather(sampler0, coord + vec2(2.f * scale.x, 0.0f), 1),
        vec4(
            textureGather(sampler0, coord + vec2(scale.x, -scale.y), 1).wz,
            textureGather(sampler0, coord + vec2(scale.x, +scale.y), 1).yx
        )
    );
    mediump float edgeVote =
        abs(dg[0].z - dg[0].y) + abs(color.y - dg[0].y) + abs(color.y - dg[0].z);
    if (edgeVote > EDGE_THRESHOLD) {
        mediump float mean = (dg[0].y + dg[0].z + dg[1].x + dg[1].w) * 0.25f;
        dg = dg - mean;
        mediump float sum = dot(abs(dg[0]) + abs(dg[1]) + abs(dg[2]), vec4(1.0f));
        mediump float sum_mean = 1.014185e+01f / max(sum, DEVIATION_FLOOR);
        mediump float std = sum_mean * sum_mean;
        mediump vec2 dir = edgeDirection(dg[0], dg[1]);
        mediump vec4 w0 = weightY(
            pl.xxxx + vec4(+1.0f, +0.0f, +0.0f, +1.0f),
            pl.yyyy + vec4(-1.0f, -1.0f, +0.0f, +0.0f),
            dg[0], std, dir
        );
        mediump vec4 w1 = weightY(
            pl.xxxx + vec4(-1.0f, -2.0f, -2.0f, -1.0f),
            pl.yyyy + vec4(-1.0f, -1.0f, +0.0f, +0.0f),
            dg[1], std, dir
        );
        mediump vec4 w2 = weightY(
            pl.xxxx + vec4(+0.0f, -1.0f, -1.0f, +0.0f),
            pl.yyyy + vec4(+1.0f, +1.0f, -2.0f, -2.0f),
            dg[2], std, dir
        );
        mediump float sum_w = dot(w0 + w1 + w2, vec4(1.0f));
        mediump float sum_wc = dot(w0 * dg[0] + w1 * dg[1] + w2 * dg[2], vec4(1.0f));
        mediump vec2 yb = vec2(
            min(min(dg[0].y, dg[0].z), min(dg[1].x, dg[1].w)),
            max(max(dg[0].y, dg[0].z), max(dg[1].x, dg[1].w))
        );
        mediump float fy = clamp((sum_wc / sum_w) * edge_sharpness, yb[0], yb[1]);
        mediump float dy = clamp(fy - color.y + mean, -23.0f / 255.0f, 23.0f / 255.0f);
        color = clamp(color + dy, 0.0f, 1.0f);
    }
    color.w = 1.0f;
    frag_color.xyzw = color;
}
