// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460 core

layout (push_constant) uniform PushConstants {
    vec4 clear_color;
};

layout(location = 0) out vec4 colors[8];

void main() {
    for (int index = 0; index < 8; ++index) {
        colors[index] = clear_color;
    }
}
