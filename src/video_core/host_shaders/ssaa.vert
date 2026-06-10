// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#define FLIPY 1
#else // ^^^ Vulkan ^^^ // vvv OpenGL vvv
#define VERTEX_ID gl_VertexID
#define FLIPY -1
out gl_PerVertex {
    vec4 gl_Position;
};
#endif

void main() {
    float x = float((VERTEX_ID & 1) << 2);
    float y = float((VERTEX_ID & 2) << 1);
    gl_Position = vec4(x - 1.0, FLIPY * (y - 1.0), 0.0, 1.0);
}
