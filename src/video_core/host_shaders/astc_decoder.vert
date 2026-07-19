// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#else
#define VERTEX_ID gl_VertexID
out gl_PerVertex {
    vec4 gl_Position;
};
#endif

void main() {
    float x = float((VERTEX_ID & 1) << 2);
    float y = float((VERTEX_ID & 2) << 1);
    gl_Position = vec4(x - 1.0, y - 1.0, 0.0, 1.0);
}
