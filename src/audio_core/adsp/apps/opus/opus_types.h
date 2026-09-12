// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace AudioCore::ADSP {

static constexpr u32 DECODE_OBJECT_MAGIC = 0xDEADBEEF;
struct LibOpusDecoder {
    u32 magic;
    bool initialized;
    bool state_valid;
    LibOpusDecoder* self;
    u32 final_range;
    void* decoder;
};
static_assert(sizeof(LibOpusDecoder) == 32);

static constexpr u32 DECODE_MULTISTREAM_OBJECT_MAGIC = 0xDEADBEEF;
struct LibOpusMultistreamDecoder {
    u32 magic;
    bool initialized;
    bool state_valid;
    LibOpusMultistreamDecoder* self;
    u32 final_range;
    void* decoder;
};
static_assert(sizeof(LibOpusMultistreamDecoder) == 32);

}
