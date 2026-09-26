// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/pcv/pcv.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::PCV {

class PCV final : public ServiceFramework<PCV> {
public:
    explicit PCV(Core::System& system_) : ServiceFramework{system_, "pcv"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "SetPowerEnabled"},
            FunctionInfo{1, nullptr, "SetClockEnabled"},
            FunctionInfo{2, nullptr, "SetClockRate"},
            FunctionInfo{3, nullptr, "GetClockRate"},
            FunctionInfo{4, nullptr, "GetState"},
            FunctionInfo{5, nullptr, "GetPossibleClockRates"},
            FunctionInfo{6, nullptr, "SetMinVClockRate"},
            FunctionInfo{7, nullptr, "SetReset"},
            FunctionInfo{8, nullptr, "SetVoltageEnabled"},
            FunctionInfo{9, nullptr, "GetVoltageEnabled"},
            FunctionInfo{10, nullptr, "GetVoltageRange"},
            FunctionInfo{11, nullptr, "SetVoltageValue"},
            FunctionInfo{12, nullptr, "GetVoltageValue"},
            FunctionInfo{13, nullptr, "GetTemperatureThresholds"},
            FunctionInfo{14, nullptr, "SetTemperature"},
            FunctionInfo{15, nullptr, "Initialize"},
            FunctionInfo{16, nullptr, "IsInitialized"},
            FunctionInfo{17, nullptr, "Finalize"},
            FunctionInfo{18, nullptr, "PowerOn"},
            FunctionInfo{19, nullptr, "PowerOff"},
            FunctionInfo{20, nullptr, "ChangeVoltage"},
            FunctionInfo{21, nullptr, "GetPowerClockInfoEvent"},
            FunctionInfo{22, nullptr, "GetOscillatorClock"},
            FunctionInfo{23, nullptr, "GetDvfsTable"},
            FunctionInfo{24, nullptr, "GetModuleStateTable"},
            FunctionInfo{25, nullptr, "GetPowerDomainStateTable"},
            FunctionInfo{26, nullptr, "GetFuseInfo"},
            FunctionInfo{27, nullptr, "GetDramId"},
            FunctionInfo{28, nullptr, "IsPoweredOn"},
            FunctionInfo{29, nullptr, "GetVoltage"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class PCV_ARB final : public ServiceFramework<PCV_ARB> {
public:
    explicit PCV_ARB(Core::System& system_) : ServiceFramework{system_, "pcv:arb"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ReleaseControl"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class PCV_IMM final : public ServiceFramework<PCV_IMM> {
public:
    explicit PCV_IMM(Core::System& system_) : ServiceFramework{system_, "pcv:imm"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "SetClockRate"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IClkrstSession final : public ServiceFramework<IClkrstSession> {
public:
    explicit IClkrstSession(Core::System& system_, DeviceCode device_code_)
        : ServiceFramework{system_, "IClkrstSession"}, device_code(device_code_) {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void SetClockRate(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        clock_rate = rp.Pop<u32>();
        LOG_DEBUG(Service_PCV, "(STUBBED) called, clock_rate={}", clock_rate);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetClockRate(HLERequestContext& ctx) {
        LOG_DEBUG(Service_PCV, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(clock_rate);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "SetClockEnabled"},
        FunctionInfo{1, nullptr, "SetClockDisabled"},
        FunctionInfo{2, nullptr, "SetResetAsserted"},
        FunctionInfo{3, nullptr, "SetResetDeasserted"},
        FunctionInfo{4, nullptr, "SetPowerEnabled"},
        FunctionInfo{5, nullptr, "SetPowerDisabled"},
        FunctionInfo{6, nullptr, "GetState"},
        FunctionInfo{7, &IClkrstSession::SetClockRate, "SetClockRate"},
        FunctionInfo{8, &IClkrstSession::GetClockRate, "GetClockRate"},
        FunctionInfo{9, nullptr, "SetMinVClockRate"},
        FunctionInfo{10, nullptr, "GetPossibleClockRates"},
        FunctionInfo{11, nullptr, "GetDvfsTable"}
    );
    DeviceCode device_code;
    u32 clock_rate{};
};

class CLKRST final : public ServiceFramework<CLKRST> {
public:
    explicit CLKRST(Core::System& system_, const char* name) : ServiceFramework{system_, name} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void OpenSession(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto device_code = static_cast<DeviceCode>(rp.Pop<u32>());
        const auto unknown_input = rp.Pop<u32>();

        LOG_DEBUG(Service_PCV, "called, device_code={}, input={}", device_code, unknown_input);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IClkrstSession>(ctx, system, device_code);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &CLKRST::OpenSession, "OpenSession"},
        FunctionInfo{1, nullptr, "GetTemperatureThresholds"},
        FunctionInfo{2, nullptr, "SetTemperature"},
        FunctionInfo{3, nullptr, "GetModuleStateTable"},
        FunctionInfo{4, nullptr, "GetModuleStateTableEvent"},
        FunctionInfo{5, nullptr, "GetModuleStateTableMaxCount"}
    );
};

class CLKRST_A final : public ServiceFramework<CLKRST_A> {
public:
    explicit CLKRST_A(Core::System& system_) : ServiceFramework{system_, "clkrst:a"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ReleaseControl"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pcv", std::make_shared<PCV>(system));
    server_manager->RegisterNamedService("pcv:arb", std::make_shared<PCV_ARB>(system));
    server_manager->RegisterNamedService("pcv:imm", std::make_shared<PCV_IMM>(system));
    server_manager->RegisterNamedService("clkrst", std::make_shared<CLKRST>(system, "clkrst"));
    server_manager->RegisterNamedService("clkrst:i", std::make_shared<CLKRST>(system, "clkrst:i"));
    server_manager->RegisterNamedService("clkrst:a", std::make_shared<CLKRST_A>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PCV
