// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <sysinfoapi.h>
// clang-format on
#else
#include <sys/types.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#else
#include <unistd.h>
#endif
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/memory_detect.h"

namespace Common {

// Detects the RAM and Swapfile sizes
static MemoryInfo Detect() {
    MemoryInfo mem_info{};

#ifdef _WIN32
    MEMORYSTATUSEX memorystatus;
    memorystatus.dwLength = sizeof(memorystatus);
    GlobalMemoryStatusEx(&memorystatus);
    mem_info.TotalPhysicalMemory = memorystatus.ullTotalPhys;
    mem_info.TotalSwapMemory = memorystatus.ullTotalPageFile - mem_info.TotalPhysicalMemory;
#elif defined(__APPLE__)
    u64 ramsize;
    struct xsw_usage vmusage;
    std::size_t sizeof_ramsize = sizeof(ramsize);
    std::size_t sizeof_vmusage = sizeof(vmusage);
    // hw and vm are defined in sysctl.h
    // https://github.com/apple/darwin-xnu/blob/master/bsd/sys/sysctl.h#L471
    // sysctlbyname(const char *, void *, size_t *, void *, size_t);
    sysctlbyname("hw.memsize", &ramsize, &sizeof_ramsize, nullptr, 0);
    sysctlbyname("vm.swapusage", &vmusage, &sizeof_vmusage, nullptr, 0);
    mem_info.TotalPhysicalMemory = ramsize;
    mem_info.TotalSwapMemory = vmusage.xsu_total;
#elif defined(__FreeBSD__)
    u_long physmem, swap_total;
    std::size_t sizeof_u_long = sizeof(u_long);
    // sysctlbyname(const char *, void *, size_t *, const void *, size_t);
    sysctlbyname("hw.physmem", &physmem, &sizeof_u_long, nullptr, 0);
    sysctlbyname("vm.swap_total", &swap_total, &sizeof_u_long, nullptr, 0);
    mem_info.TotalPhysicalMemory = physmem;
    mem_info.TotalSwapMemory = swap_total;
#elif defined(__linux__)
    struct sysinfo meminfo;
    sysinfo(&meminfo);
    mem_info.TotalPhysicalMemory = meminfo.totalram;
    mem_info.TotalSwapMemory = meminfo.totalswap;
#else
    mem_info.TotalPhysicalMemory = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE);
    mem_info.TotalSwapMemory = 0;
#endif

    return mem_info;
}

const MemoryInfo& GetMemInfo() {
    static MemoryInfo mem_info = Detect();
    return mem_info;
}

u64 GetAvailablePhysicalMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memorystatus;
    memorystatus.dwLength = sizeof(memorystatus);
    if (GlobalMemoryStatusEx(&memorystatus)) {
        return memorystatus.ullAvailPhys;
    }
    return 0;
#elif defined(__linux__)
    if (std::FILE* const file = std::fopen("/proc/meminfo", "re")) {
        char line[256];
        u64 available = 0;
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            if (std::strncmp(line, "MemAvailable:", 13) == 0) {
                available = std::strtoull(line + 13, nullptr, 10) * 1024ULL;
                break;
            }
        }
        std::fclose(file);
        if (available != 0) {
            return available;
        }
    }
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        const u64 unit = info.mem_unit != 0 ? info.mem_unit : 1ULL;
        return (static_cast<u64>(info.freeram) + static_cast<u64>(info.bufferram)) * unit;
    }
    return 0;
#else
    return 0;
#endif
}

u64 GetMaxMapCount() {
#ifdef __linux__
    if (std::FILE* const file = std::fopen("/proc/sys/vm/max_map_count", "re")) {
        char line[32];
        u64 count = 0;
        if (std::fgets(line, sizeof(line), file) != nullptr) {
            count = std::strtoull(line, nullptr, 10);
        }
        std::fclose(file);
        return count;
    }
    return 0;
#else
    return 0;
#endif
}

} // namespace Common
