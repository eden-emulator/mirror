// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>

#define STUB_WEAK(name) \
    extern "C" void name() { \
        printf("called " #name); \
        asm volatile("ud2"); \
    }

extern "C" void __cxa_thread_atexit_impl() {}

STUB_WEAK(__assert)
STUB_WEAK(ZSTD_trace_compress_begin)
STUB_WEAK(ZSTD_trace_compress_end)
STUB_WEAK(ZSTD_trace_decompress_begin)
STUB_WEAK(ZSTD_trace_decompress_end)

FILE* __stderrp = stdout;
FILE* __stdinp = stdin;

extern "C" {
struct _RuneLocale;
thread_local const _RuneLocale *_ThreadRuneLocale = NULL;
const _RuneLocale *_CurrentRuneLocale = NULL;
int __isthreaded = 0;
int __mb_sb_limit = 0;
}

#undef STUB_WEAK

// THIS MAKES STD::COUT AND SUCH WORK :)
#include <iostream>
std::ios_base::Init init;
