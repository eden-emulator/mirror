// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include "common/common_types.h"

namespace VideoCommon {
namespace NativeQueryDiagnostics {
struct State {
    State() : file{"native-query-trace.csv", std::ios::trunc} {
        file << "event,asid,gpu,cpu,type,value,flags\n";
    }
    std::mutex mutex;
    std::ofstream file;
    size_t records{};
};
inline State& GetState() {
    static State state;
    return state;
}
} // namespace NativeQueryDiagnostics

// Events: 0=request, 1=written, 2=invalidated, 3=unsynchronized,
// 4=channel suspended, 5=channel resumed, 6=buffer readback,
// 7/8=QMD release slots observed, 9=PCAS request,
// 10=DMA destination (value=source, flags=line bytes), 11=inline write.
// Keep this opt-in and bounded.
inline void TraceNativeQuery(u32 event, u64 asid, u64 gpu, u64 cpu, u32 type, u64 value, u32 flags) {
    if (!std::getenv("EDEN_NATIVE_DRAW_DIAGNOSTICS")) return;
    auto& state = NativeQueryDiagnostics::GetState();
    const std::scoped_lock lock{state.mutex};
    if (state.records++ < 1000000 || event == 4 || event == 5) {
        state.file << event << ',' << asid << ',' << std::hex << gpu << ',' << cpu << ','
                   << type << ',' << value << ',' << flags << std::dec << '\n';
    }
    if (event == 4 || event == 5) state.file.flush();
}
} // namespace VideoCommon
