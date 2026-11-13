// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// yuzu data directories

inline constexpr const char *EDEN_DIR = "eden";
inline constexpr const char *PORTABLE_DIR = "user";

// Sub-directories contained within a yuzu data directory
inline constexpr const char *AMIIBO_DIR = "amiibo";
inline constexpr const char *CACHE_DIR = "cache";
inline constexpr const char *CONFIG_DIR = "config";
inline constexpr const char *CRASH_DUMPS_DIR = "crash_dumps";
inline constexpr const char *DUMP_DIR = "dump";
inline constexpr const char *KEYS_DIR = "keys";
inline constexpr const char *LOAD_DIR = "load";
inline constexpr const char *LOG_DIR = "log";
inline constexpr const char *LOSSLESS_DIR = "lossless";
inline constexpr const char *NAND_DIR = "nand";
inline constexpr const char *PLAY_TIME_DIR = "play_time";
inline constexpr const char *POST_PRESET_DIR = "post_presets";
inline constexpr const char *POST_SHADER_DIR = "post_shaders";
inline constexpr const char *SCREENSHOTS_DIR = "screenshots";
inline constexpr const char *SDMC_DIR = "sdmc";
inline constexpr const char *SHADER_DIR = "shader";
inline constexpr const char *TAS_DIR = "tas";
inline constexpr const char *ICONS_DIR = "icons";

// Compatibility with other emulators
inline constexpr const char *CITRON_DIR = "citron";
inline constexpr const char *SUDACHI_DIR = "sudachi";
inline constexpr const char *YUZU_DIR = "yuzu";
inline constexpr const char *SUYU_DIR = "suyu";
inline constexpr const char *RYUJINX_DIR = "Ryujinx";

// yuzu-specific files
inline constexpr const char *LOG_FILE = "eden_log.txt";
inline constexpr const char *LOSSLESS_DLL_FILE = "Lossless.dll";
inline constexpr const char *LOSSLESS_CACHE_FILE = "lsfg_spirv.cache";
