// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::LDR {

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor(Core::System& system_) : ServiceFramework{system_, "ldr:dmnt"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "SetProgramArgument"},
            FunctionInfo{1, nullptr, "FlushArguments"},
            FunctionInfo{2, nullptr, "GetProcessModuleInfo"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class ProcessManager final : public ServiceFramework<ProcessManager> {
public:
    explicit ProcessManager(Core::System& system_) : ServiceFramework{system_, "ldr:pm"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "CreateProcess"},
            FunctionInfo{1, nullptr, "GetProgramInfo"},
            FunctionInfo{2, nullptr, "PinProgram"},
            FunctionInfo{3, nullptr, "UnpinProgram"},
            FunctionInfo{4, nullptr, "SetEnabledProgramVerification"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class Shell final : public ServiceFramework<Shell> {
public:
    explicit Shell(Core::System& system_) : ServiceFramework{system_, "ldr:shel"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "SetProgramArgument"},
            FunctionInfo{1, nullptr, "FlushArguments"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ldr:dmnt", std::make_shared<DebugMonitor>(system), 3);
    server_manager->RegisterNamedService("ldr:pm", std::make_shared<ProcessManager>(system), 1);
    server_manager->RegisterNamedService("ldr:shel", std::make_shared<Shell>(system), 3);

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::LDR
