// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/i2c/i2c.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/server_manager.h"

namespace Service::I2C {

class I2CSession final : public ServiceFramework<I2CSession> {
public:
    explicit I2CSession(Core::System& system_)
        : ServiceFramework{system_, "I2CSession"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "SendOld"},
            {1, nullptr, "ReceiveOld"},
            {2, nullptr, "ExecuteCommandListOld"},
            {10, C<&I2CSession::Send>, "Send"},
            {11, nullptr, "Receive"},
            {12, nullptr, "ExecuteCommandList"},
            {13, nullptr, "SetRetryPolicy"},
        };
        RegisterHandlers(functions);
    }
    ~I2CSession() override = default;
    Result Send(InBuffer<BufferAttr_HipcMapAlias> in_data, u32 transaction_option) {
        LOG_WARNING(Service, "(stubbed) topt={}", transaction_option);
        R_THROW(ResultUnknown);
    }
};

enum class I2CDevice : u32 {
    ClassicController,
    Ftm3bd56,
};

class I2C final : public ServiceFramework<I2C> {
public:
    explicit I2C(Core::System& system_)
        : ServiceFramework{system_, "i2c"}
    {
        static const FunctionInfo functions[] = {
            {0, C<&I2C::OpenSessionForDev>, "OpenSessionForDev"},
            {1, C<&I2C::OpenSession>, "OpenSession"},
            {2, C<&I2C::HasDevice>, "HasDevice"},
            {3, C<&I2C::HasDeviceForDev>, "HasDeviceForDev"},
            {4, C<&I2C::OpenSession2>, "OpenSession2"},
        };
        RegisterHandlers(functions);
    }
    ~I2C() override = default;
    Result OpenSessionForDev(OutInterface<I2CSession> out_session, s32 bus_idx) {
        LOG_DEBUG(Service, "(stubbed)");
        *out_session = std::make_shared<I2CSession>(system);
        R_SUCCEED();
    }
    Result OpenSession(OutInterface<I2CSession> out_session, I2CDevice device) {
        LOG_DEBUG(Service, "(stubbed)");
        *out_session = std::make_shared<I2CSession>(system);
        R_SUCCEED();
    }
    Result HasDevice(Out<bool> out_has_device, I2CDevice device) {
        LOG_DEBUG(Service, "(stubbed)");
        *out_has_device = false;
        R_SUCCEED();
    }
    Result HasDeviceForDev(Out<bool> out_has_device, I2CDevice device) {
        LOG_DEBUG(Service, "(stubbed)");
        *out_has_device = false;
        R_SUCCEED();
    }
    Result OpenSession2(OutInterface<I2CSession> out_session, u32 device_code) {
        LOG_DEBUG(Service, "(stubbed) device_code={}", device_code);
        *out_session = std::make_shared<I2CSession>(system);
        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("i2c", std::make_shared<I2C>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::I2C
