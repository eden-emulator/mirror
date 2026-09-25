// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/eupld/eupld.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::EUPLD {

class ErrorUploadContext final : public ServiceFramework<ErrorUploadContext> {
public:
    explicit ErrorUploadContext(Core::System& system_) : ServiceFramework{system_, "eupld:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "SetUrl"},
            FunctionInfo{1, nullptr, "ImportCrt"},
            FunctionInfo{2, nullptr, "ImportPki"},
            FunctionInfo{3, nullptr, "SetAutoUpload"},
            FunctionInfo{4, nullptr, "GetAutoUpload"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class ErrorUploadRequest final : public ServiceFramework<ErrorUploadRequest> {
public:
    explicit ErrorUploadRequest(Core::System& system_) : ServiceFramework{system_, "eupld:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "Initialize"},
            FunctionInfo{1, nullptr, "UploadAll"},
            FunctionInfo{2, nullptr, "UploadSelected"},
            FunctionInfo{3, nullptr, "GetUploadStatus"},
            FunctionInfo{4, nullptr, "CancelUpload"},
            FunctionInfo{5, nullptr, "GetResult"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("eupld:c", std::make_shared<ErrorUploadContext>(system));
    server_manager->RegisterNamedService("eupld:r", std::make_shared<ErrorUploadRequest>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::EUPLD
