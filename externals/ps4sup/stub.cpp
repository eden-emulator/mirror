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

#undef STUB_WEAK
