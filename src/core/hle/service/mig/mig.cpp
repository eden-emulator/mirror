// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/mig/mig.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::Migration {

class MIG_USR final : public ServiceFramework<MIG_USR> {
public:
    explicit MIG_USR(Core::System& system_) : ServiceFramework{system_, "mig:usr"} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Unknown0", MakeVersionGate({19,0,0})},
        FunctionInfo{1, nullptr, "Unknown1", MakeVersionGate({20,0,0})},
        FunctionInfo{2, nullptr, "Unknown2", MakeVersionGate({20,0,0})},
        FunctionInfo{10, nullptr, "TryGetLastMigrationInfo"},
        FunctionInfo{11, nullptr, "Unknown11", MakeVersionGate({20,0,0})},
        FunctionInfo{100, nullptr, "CreateUserMigrationServer", MakeVersionGate({7,0,0})},
        FunctionInfo{101, nullptr, "ResumeUserMigrationServer", MakeVersionGate({7,0,0})},
        FunctionInfo{200, nullptr, "CreateUserMigrationClient", MakeVersionGate({7,0,0})},
        FunctionInfo{201, nullptr, "ResumeUserMigrationClient", MakeVersionGate({7,0,0})},
        FunctionInfo{1001, nullptr, "GetSaveDataMigrationPolicyInfoAsync", MakeVersionGate({8,0,0}, {20,5,0})},
        FunctionInfo{1010, nullptr, "TryGetLastSaveDataMigrationInfo", MakeVersionGate({7,0,0})},
        FunctionInfo{1100, nullptr, "CreateSaveDataMigrationServer", MakeVersionGate({7,0,0}, {19,0,1})},
        FunctionInfo{1101, nullptr, "ResumeSaveDataMigrationServer", MakeVersionGate({7,0,0})},
        FunctionInfo{1110, nullptr, "Unknown1101", MakeVersionGate({17,0,0})},
        FunctionInfo{1200, nullptr, "CreateSaveDataMigrationClient", MakeVersionGate({7,0,0})},
        FunctionInfo{1201, nullptr, "ResumeSaveDataMigrationClient", MakeVersionGate({7,0,0})},
        FunctionInfo{2001, nullptr, "Unknown2001", MakeVersionGate({20,0,0})},
        FunctionInfo{2010, nullptr, "Unknown2010", MakeVersionGate({20,0,0})},
        FunctionInfo{2100, nullptr, "Unknown2100", MakeVersionGate({20,0,0})},
        FunctionInfo{2110, nullptr, "Unknown2110", MakeVersionGate({20,0,0})},
        FunctionInfo{2200, nullptr, "Unknown2200", MakeVersionGate({20,0,0})},
        FunctionInfo{2210, nullptr, "Unknown2210", MakeVersionGate({20,0,0})},
        FunctionInfo{2220, nullptr, "Unknown2220", MakeVersionGate({20,0,0})},
        FunctionInfo{2230, nullptr, "Unknown2230", MakeVersionGate({20,0,0})},
        FunctionInfo{2231, nullptr, "Unknown2231", MakeVersionGate({20,0,0})},
        FunctionInfo{2232, nullptr, "Unknown2232", MakeVersionGate({20,0,0})},
        FunctionInfo{2233, nullptr, "Unknown2233", MakeVersionGate({20,0,0})},
        FunctionInfo{2234, nullptr, "Unknown2234", MakeVersionGate({20,0,0})},
        FunctionInfo{2250, nullptr, "Unknown2250", MakeVersionGate({20,0,0})},
        FunctionInfo{2260, nullptr, "Unknown2260", MakeVersionGate({20,0,0})},
        FunctionInfo{2270, nullptr, "Unknown2270", MakeVersionGate({20,0,0})},
        FunctionInfo{2280, nullptr, "Unknown2280", MakeVersionGate({20,0,0})},
        FunctionInfo{2300, nullptr, "Unknown2300", MakeVersionGate({20,0,0})},
        FunctionInfo{2310, nullptr, "Unknown2310", MakeVersionGate({20,0,0})},
        FunctionInfo{2400, nullptr, "Unknown2400", MakeVersionGate({20,0,0})},
        FunctionInfo{2420, nullptr, "Unknown2420", MakeVersionGate({20,0,0})}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("mig:user", std::make_shared<MIG_USR>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Migration
