// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/final_output_recorder_manager.h"

namespace Service::Audio {

class IFinalOutputRecorder final : public ServiceFramework<IFinalOutputRecorder> {
public:
    explicit IFinalOutputRecorder(Core::System& system_)
        : ServiceFramework{system_, "IFinalOutputRecorder"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetFinalOutputRecorderState"},
            FunctionInfo{1, nullptr, "Start"},
            FunctionInfo{2, nullptr, "Stop"},
            FunctionInfo{3, nullptr, "AppendFinalOutputRecorderBuffer"},
            FunctionInfo{4, nullptr, "RegisterBufferEvent"},
            FunctionInfo{5, nullptr, "GetReleasedFinalOutputRecorderBuffers"},
            FunctionInfo{6, nullptr, "ContainsFinalOutputRecorderBuffer"},
            FunctionInfo{7, nullptr, "GetFinalOutputRecorderBufferEndTime"},
            FunctionInfo{8, nullptr, "AppendFinalOutputRecorderBufferAuto"},
            FunctionInfo{9, nullptr, "GetReleasedFinalOutputRecorderBufferAuto"},
            FunctionInfo{10, nullptr, "FlushFinalOutputRecorderBuffers"},
            FunctionInfo{11, nullptr, "AttachWorkBuffer"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

IFinalOutputRecorderManager::IFinalOutputRecorderManager(Core::System& system_)
    : ServiceFramework{system_, "audrec:u"} {
}

IFinalOutputRecorderManager::~IFinalOutputRecorderManager() = default;

} // namespace Service::Audio
