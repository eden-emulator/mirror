// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/grc/grc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::GRC {

class GRC final : public ServiceFramework<GRC> {
public:
    explicit GRC(Core::System& system_) : ServiceFramework{system_, "grc:c"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{1, nullptr, "OpenContinuousRecorder"},
            FunctionInfo{2, nullptr, "OpenGameMovieTrimmer"},
            FunctionInfo{3, nullptr, "OpenOffscreenRecorder"},
            FunctionInfo{101, nullptr, "CreateMovieMaker"},
            FunctionInfo{9903, nullptr, "SetOffscreenRecordingMarker"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class GRC_D final : public ServiceFramework<GRC_D> {
public:
    explicit GRC_D(Core::System& system_) : ServiceFramework{system_, "grc:d"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{1, nullptr, "Initialize"},
            FunctionInfo{2, nullptr, "Transfer"},
            FunctionInfo{3, nullptr, "Cmd3"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("grc:c", std::make_shared<GRC>(system), 4);
    server_manager->RegisterNamedService("grc:d", std::make_shared<GRC_D>(system), 4);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::GRC
