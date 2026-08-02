// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <limits>
#include <string>
#include <thread>

#include "common/error.h"
#include "common/logging.h"
#include "common/assert.h"
#include "common/thread.h"
#ifdef __APPLE__
#include <mach/mach.h>
#elif defined(__HAIKU__)
#include <kernel/OS.h>
#elif defined(_WIN32)
#include <windows.h>
#include "common/string_util.h"
#include "common/windows/timer_resolution.h"
#else
#if defined(__FreeBSD__)
#include <sys/cpuset.h>
#include <sys/_cpuset.h>
#include <pthread_np.h>
// Compatibility with CPUset
#define cpu_set_t cpuset_t
#elif defined(__DragonFly__) || defined(__OpenBSD__) || defined(__Bitrig__)
#include <pthread_np.h>
#endif
#include <pthread.h>
#include <sched.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include <sys/resource.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

namespace {
constexpr int ANDROID_THREAD_PRIORITY_AUDIO = -16;
constexpr int ANDROID_THREAD_PRIORITY_URGENT_DISPLAY = -8;
constexpr int ANDROID_THREAD_PRIORITY_DISPLAY = -4;
constexpr int ANDROID_THREAD_PRIORITY_DEFAULT = 0;
constexpr int ANDROID_THREAD_PRIORITY_BACKGROUND = 10;

constexpr size_t ANDROID_MINIMUM_PERFORMANCE_CORES = 4;

enum class CoreGroup {
    Unrestricted,
    Performance,
    Efficiency,
};

struct CoreTopology {
    cpu_set_t allowed;
    cpu_set_t performance;
    cpu_set_t efficiency;
    bool separated;
    bool initialized;
};

struct ThreadPolicy {
    pid_t tid;
    CoreGroup group;
    int nice_value;
    bool has_nice;
};

std::mutex g_topology_mutex;
CoreTopology g_topology{};

std::mutex g_policy_mutex;

std::vector<ThreadPolicy>& Policies() {
    static auto* const policies = new std::vector<ThreadPolicy>();
    return *policies;
}

struct PolicyRegistration {
    ~PolicyRegistration() {
        const pid_t tid = gettid();
        std::scoped_lock lock{g_policy_mutex};
        std::erase_if(Policies(), [tid](const ThreadPolicy& policy) { return policy.tid == tid; });
    }
};

thread_local PolicyRegistration t_policy_registration;

int PossibleCpuCount() {
    std::ifstream file("/sys/devices/system/cpu/possible");
    std::string list;
    if (file && std::getline(file, list) && !list.empty()) {
        int highest = -1;
        const char* cursor = list.c_str();
        while (*cursor != '\0') {
            char* end = nullptr;
            const long value = std::strtol(cursor, &end, 10);
            if (end == cursor) {
                break;
            }
            highest = (std::max)(highest, static_cast<int>(value));
            cursor = end;
            while (*cursor == '-' || *cursor == ',') {
                ++cursor;
            }
        }
        if (highest >= 0) {
            return (std::min)(highest + 1, CPU_SETSIZE);
        }
    }
    const long configured = sysconf(_SC_NPROCESSORS_CONF);
    if (configured > 0) {
        return static_cast<int>((std::min<long>)(configured, CPU_SETSIZE));
    }
    return static_cast<int>((std::min<unsigned>)(std::thread::hardware_concurrency(), CPU_SETSIZE));
}

long ReadCpuScalar(int cpu, const char* node) {
    long value = 0;
    std::ifstream file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/" + node);
    if (!file || !(file >> value) || value <= 0) {
        return 0;
    }
    return value;
}

std::vector<std::pair<long, int>> CollectCoreWeights(const cpu_set_t& allowed, int total,
                                                     const char* node, bool require_all) {
    std::vector<std::pair<long, int>> cores;
    for (int cpu = 0; cpu < total; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        const long weight = ReadCpuScalar(cpu, node);
        if (weight <= 0) {
            if (require_all) {
                return {};
            }
            LOG_WARNING(Common, "Could not read {} for CPU {}, treating it as an efficiency core",
                        node, cpu);
            continue;
        }
        cores.emplace_back(weight, cpu);
    }
    return cores;
}

void ComputeTopologyLocked() {
    g_topology.initialized = true;
    g_topology.separated = false;
    CPU_ZERO(&g_topology.allowed);
    CPU_ZERO(&g_topology.performance);
    CPU_ZERO(&g_topology.efficiency);

    if (sched_getaffinity(getpid(), sizeof(g_topology.allowed), &g_topology.allowed) != 0) {
        LOG_WARNING(Common, "Could not query process CPU affinity: {}",
                    ::Common::GetLastErrorMsg());
        return;
    }

    const int total = PossibleCpuCount();
    auto cores = CollectCoreWeights(g_topology.allowed, total, "cpu_capacity", true);
    if (cores.empty()) {
        cores = CollectCoreWeights(g_topology.allowed, total, "cpufreq/cpuinfo_max_freq", false);
    }
    if (cores.empty()) {
        LOG_WARNING(Common, "Could not determine CPU topology, thread placement is disabled");
        return;
    }

    std::sort(cores.begin(), cores.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

    const size_t allowed_count = static_cast<size_t>(CPU_COUNT(&g_topology.allowed));
    const size_t maximum =
        allowed_count > 2 * ANDROID_MINIMUM_PERFORMANCE_CORES
            ? allowed_count - ANDROID_MINIMUM_PERFORMANCE_CORES
            : ANDROID_MINIMUM_PERFORMANCE_CORES;

    size_t taken = 0;
    long cluster_weight = cores.front().first;
    for (const auto& [weight, cpu] : cores) {
        if (weight != cluster_weight) {
            if (taken >= ANDROID_MINIMUM_PERFORMANCE_CORES) {
                break;
            }
            cluster_weight = weight;
        }
        if (taken >= maximum) {
            break;
        }
        CPU_SET(cpu, &g_topology.performance);
        ++taken;
    }
    if (taken == 0) {
        return;
    }

    for (int cpu = 0; cpu < total; ++cpu) {
        if (CPU_ISSET(cpu, &g_topology.allowed) && !CPU_ISSET(cpu, &g_topology.performance)) {
            CPU_SET(cpu, &g_topology.efficiency);
        }
    }

    g_topology.separated = CPU_COUNT(&g_topology.efficiency) > 0;
    LOG_INFO(Common, "CPU topology: {} performance cores, {} efficiency cores, separation {}",
             CPU_COUNT(&g_topology.performance), CPU_COUNT(&g_topology.efficiency),
             g_topology.separated ? "enabled" : "unavailable");
}

void EnsureTopologyLocked() {
    if (!g_topology.initialized) {
        ComputeTopologyLocked();
    }
}

void RefreshTopologyLocked() {
    if (!g_topology.initialized) {
        ComputeTopologyLocked();
        return;
    }
    cpu_set_t current;
    CPU_ZERO(&current);
    if (sched_getaffinity(getpid(), sizeof(current), &current) != 0) {
        return;
    }
    if (std::memcmp(&current, &g_topology.allowed, sizeof(current)) != 0) {
        ComputeTopologyLocked();
    }
}

bool ApplyCoreGroupLocked(pid_t tid, CoreGroup group) {
    if (!g_topology.separated || group == CoreGroup::Unrestricted) {
        return false;
    }
    const cpu_set_t& mask =
        group == CoreGroup::Performance ? g_topology.performance : g_topology.efficiency;
    if (CPU_COUNT(&mask) == 0) {
        return false;
    }
    if (sched_setaffinity(tid, sizeof(mask), &mask) != 0) {
        LOG_WARNING(Common, "Could not restrict thread {} to its core group: {}", tid,
                    ::Common::GetLastErrorMsg());
        return false;
    }
    return true;
}

ThreadPolicy& AcquirePolicyLocked(pid_t tid) {
    auto& policies = Policies();
    for (auto& policy : policies) {
        if (policy.tid == tid) {
            return policy;
        }
    }
    return policies.emplace_back(ThreadPolicy{tid, CoreGroup::Unrestricted, 0, false});
}

void SetCurrentThreadCoreGroup(CoreGroup group) {
    const pid_t tid = gettid();
    {
        std::scoped_lock lock{g_topology_mutex};
        EnsureTopologyLocked();
        ApplyCoreGroupLocked(tid, group);
    }
    (void)&t_policy_registration;
    std::scoped_lock lock{g_policy_mutex};
    AcquirePolicyLocked(tid).group = group;
}

void RememberCurrentThreadNice(pid_t tid, int nice_value) {
    (void)&t_policy_registration;
    std::scoped_lock lock{g_policy_mutex};
    ThreadPolicy& policy = AcquirePolicyLocked(tid);
    policy.nice_value = nice_value;
    policy.has_nice = true;
}
} // Anonymous namespace
#endif

#include "common/cpu_features.h"
#ifdef ARCHITECTURE_x86_64
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include "common/x64/rdtsc.h"
#endif

namespace Common {

void SetCurrentThreadPriority(ThreadPriority new_priority) {
#ifdef _WIN32
    int windows_priority = [&]() {
        switch (new_priority) {
        case ThreadPriority::Low: return THREAD_PRIORITY_BELOW_NORMAL;
        case ThreadPriority::Normal: return THREAD_PRIORITY_NORMAL;
        case ThreadPriority::High: return THREAD_PRIORITY_ABOVE_NORMAL;
        case ThreadPriority::VeryHigh: return THREAD_PRIORITY_HIGHEST;
        case ThreadPriority::Critical: return THREAD_PRIORITY_TIME_CRITICAL;
        default: return THREAD_PRIORITY_NORMAL;
        }
    }();
    SetThreadPriority(GetCurrentThread(), windows_priority);
#elif defined(__HAIKU__)
    // TODO: We have priorities for 3D rendering applications - may help lavapipe?
    int priority = [&]() {
        switch (new_priority) {
        case ThreadPriority::Low: return B_LOW_PRIORITY;
        case ThreadPriority::Normal: return B_NORMAL_PRIORITY;
        case ThreadPriority::High: return B_DISPLAY_PRIORITY;
        case ThreadPriority::VeryHigh: return B_URGENT_DISPLAY_PRIORITY;
        case ThreadPriority::Critical: return B_URGENT_PRIORITY;
        default: return B_NORMAL_PRIORITY;
        }
    }();
    set_thread_priority(find_thread(NULL), priority);
#elif defined(__ANDROID__)
    const int nice_value = [&]() {
        switch (new_priority) {
        case ThreadPriority::Low: return ANDROID_THREAD_PRIORITY_BACKGROUND;
        case ThreadPriority::Normal: return ANDROID_THREAD_PRIORITY_DEFAULT;
        case ThreadPriority::High: return ANDROID_THREAD_PRIORITY_DISPLAY;
        case ThreadPriority::VeryHigh: return ANDROID_THREAD_PRIORITY_URGENT_DISPLAY;
        case ThreadPriority::Critical: return ANDROID_THREAD_PRIORITY_AUDIO;
        default: return ANDROID_THREAD_PRIORITY_DEFAULT;
        }
    }();
    const pid_t tid = gettid();
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice_value) != 0) {
        LOG_WARNING(Common, "Could not set thread nice value to {}: {}", nice_value,
                    GetLastErrorMsg());
        return;
    }
    RememberCurrentThreadNice(tid, nice_value);
#else
    pthread_t this_thread = pthread_self();
    const auto scheduling_type = SCHED_OTHER;
    s32 max_prio = sched_get_priority_max(scheduling_type);
    s32 min_prio = sched_get_priority_min(scheduling_type);
    u32 level = (std::max)(u32(new_priority) + 1, 4U);

    struct sched_param params;
    if (max_prio > min_prio) {
        params.sched_priority = min_prio + ((max_prio - min_prio) * level) / 4;
    } else {
        params.sched_priority = min_prio - ((min_prio - max_prio) * level) / 4;
    }

    pthread_setschedparam(this_thread, scheduling_type, &params);
#endif
}

void SetCurrentThreadName(const char* name) {
#ifdef _MSC_VER
    // Sets the debugger-visible name of the current thread.
    if (auto pf = (decltype(&SetThreadDescription))(void*)GetProcAddress(GetModuleHandle(TEXT("KernelBase.dll")), "SetThreadDescription"); pf)
        pf(GetCurrentThread(), UTF8ToUTF16W(name).data()); // Windows 10+
    else
        ; // No-op
#elif  defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__HAIKU__)
    rename_thread(find_thread(NULL), name);
#elif defined(__Bitrig__) || defined(__DragonFly__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    pthread_set_name_np(pthread_self(), name);
#elif defined(__NetBSD__)
    pthread_setname_np(pthread_self(), "%s", (void*)name);
#elif defined(__linux__) || defined(__CYGWIN__) || defined(__sun__) || defined(__glibc__) || defined(__managarm__)
    int ret = pthread_setname_np(pthread_self(), name);
    if (ret == ERANGE) {
        // Linux limits thread names to 15 characters and will outright reject any
        // attempt to set a longer name with ERANGE.
        char buf[16];
        size_t const len = std::min<size_t>(std::strlen(name), sizeof(buf) - 1);
        std::memcpy(buf, name, len);
        buf[len] = '\0';
        pthread_setname_np(pthread_self(), buf);
    }
#elif defined(_WIN32)
    // MinGW with the POSIX threading model does not support pthread_setname_np
    // See for reference
    // https://gitlab.freedesktop.org/mesa/mesa/-/blame/main/src/util/u_thread.c?ref_type=heads#L75
    (void)name;
#else
    pthread_setname_np(pthread_self(), name);
#endif
}

void SetCurrentThreadToPerformanceCores() {
#if defined(__ANDROID__)
    SetCurrentThreadCoreGroup(CoreGroup::Performance);
#endif
}

void SetCurrentThreadToEfficiencyCores() {
#if defined(__ANDROID__)
    SetCurrentThreadCoreGroup(CoreGroup::Efficiency);
#endif
}

void RefreshThreadPolicies() {
#if defined(__ANDROID__)
    std::scoped_lock topology_lock{g_topology_mutex};
    RefreshTopologyLocked();

    std::scoped_lock policy_lock{g_policy_mutex};
    for (const auto& policy : Policies()) {
        if (policy.has_nice) {
            setpriority(PRIO_PROCESS, static_cast<id_t>(policy.tid), policy.nice_value);
        }
        ApplyCoreGroupLocked(policy.tid, policy.group);
    }
#endif
}

#ifdef ARCHITECTURE_x86_64
// On Linux and UNIX systems, a futex would nominally be used to cover the costs
// the idea is that it's intuitivelly cheaper to use a direct instruction as opposed to a full futex call
// the underlying libc++ implementation uses pthread_cond_timedwait which MAY invoke a futex
// Let's pretend the OS is too expensive to jump into, and avoid ANY context switches
// this should *IN THEORY* lower CPU usage while just waiting for stuff effectively
// For windows the minimal quanta resolution is about 500us, and normal CRT cond var is 1.5ms(?)
// so may as well avoid that too
// Let's just give ALL platforms the same mechanisms (almost) for when they have umonitor OR waitpkg
#ifdef __clang__
__attribute__((target("waitpkg,mwaitx")))
#elif defined(__GNUC__)
#pragma GCC target("waitpkg")
#pragma GCC target("mwaitx")
#endif
bool Event::WaitFor(const std::chrono::nanoseconds time) {
#ifdef _WIN32
    auto const start = Common::X64::FencedRDTSC();
    auto const& caps = Common::g_cpu_caps;
    [[maybe_unused]] auto const end = start + Common::g_wall_clock.NsToTicks(time);
    if (caps.monitorx) {
        while (true) {
            // Armed monitor, as per manual, MWAITX must be conditional if the condition isn't satisfied
            // to prevent a race condition.
            _mm_monitorx(reinterpret_cast<u64*>(std::addressof(is_set)), 0, 0);
            if (!is_set.load()) {
                // RDTSC may be fenced here due to atomic load
#ifdef _MSC_VER
                auto const now = __rdtsc();
#else
                auto const now = _rdtsc();
#endif
                if (end > now) {
                    u32 const cycles = std::min<u32>((std::numeric_limits<u32>::max)(), s64(end) - s64(now));
                    // See here: https://github.com/torvalds/linux/blob/948a64995aca6820abefd17f1a4258f5835c5ad9/arch/x86/lib/delay.c#L93
                    // MWAITX accepts a 32-bit input timer which determines the total number of cycles to wait for
                    // NOT THE TOTAL ABSOLUTE TSC VALUE, it's just a delta
                    // BIT[1] = use a timer
                    // Hint = 0: Use C1 state when sleepy (means slower wakeup but better savings)
                    _mm_mwaitx(1 << 1, 0u, cycles);
                    if (!is_set.load())
                        return false;
                } else
                    return false; //timeout
            }
            bool expected = true;
            if (is_set.compare_exchange_weak(expected, false, std::memory_order_release))
                return true;
        }
    } else if (caps.waitpkg) {
        // #UD If CPUID.7.0:ECX.WAITPKG[bit 5]=0.
        while (true) {
            _umonitor(std::addressof(is_set));
            if (!is_set.load() && !_umwait(0, end)) //umwait is absolute time!!!
                return false;
            bool expected = true;
            if (is_set.compare_exchange_weak(expected, false, std::memory_order_release))
                return true;
        }
    } else {
#ifdef _MSC_VER
        while (!is_set.load() && end > __rdtsc())
            Common::Windows::SleepForOneTick();
#else
        while (!is_set.load() && end > _rdtsc())
            Common::Windows::SleepForOneTick();
#endif
        if (is_set.load())
            Reset();
        return true;
    }
#else
    std::unique_lock lk{mutex};
    if (!condvar.wait_for(lk, time, [this] { return is_set.load(); }))
        return false;
    is_set = false;
    return true;
#endif
}
#else
bool Event::WaitFor(const std::chrono::nanoseconds time) {
#ifdef _WIN32
    auto const end = Common::g_wall_clock.GetTimeNS() + time;
    while (!is_set.load() && end > Common::g_wall_clock.GetTimeNS())
        Common::Windows::SleepForOneTick();
    if (is_set.load())
        Reset();
    return true;
#else
    std::unique_lock lk{mutex};
    if (!condvar.wait_for(lk, time, [this] { return is_set.load(); }))
        return false;
    is_set = false;
    return true;
#endif
}
#endif

} // namespace Common
