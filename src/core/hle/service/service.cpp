// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <fmt/ranges.h>
#include <string_view>
#include <thread>
#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/reporter.h"
#include "frontend_common/firmware_manager.h"

namespace Service {

/// @brief Creates a function string for logging, complete with the name (or header code, depending
/// on what's passed in) the port name, and all the cmd_buf arguments.
[[maybe_unused]] static std::string MakeFunctionString(std::string_view name, std::string_view port_name, const u32* cmd_buf) {
    // Number of params == bits 0-5 + bits 6-11
    int num_params = (cmd_buf[0] & 0x3F) + ((cmd_buf[0] >> 6) & 0x3F);
    std::string function_string = fmt::format("fn '{}': port={}", name, port_name);
    for (int i = 1; i <= num_params; ++i)
        function_string += fmt::format(", cmd_buf[{}]={:#x}", i, cmd_buf[i]);
    return function_string;
}

ServiceFrameworkBase::ServiceFrameworkBase(Core::System& system_, const char* service_name_, u32 max_sessions_, InvokerFn* handler_invoker_)
    : SessionRequestHandler(system_.Kernel(), service_name_)
    , system{system_}
    , service_name{service_name_}
    , is_i_storage{std::string_view{service_name_} == "IStorage"}
    , handler_invoker{handler_invoker_}
    , max_sessions{max_sessions_}
{}

ServiceFrameworkBase::~ServiceFrameworkBase() {
    // Wait for other threads to release access before destroying
    const auto guard = ServiceFrameworkBase::LockService();
}

void ServiceFrameworkBase::ReportUnimplementedFunction(HLERequestContext& ctx, const FunctionInfoBase* info, bool gated) {
    auto cmd_buf = ctx.CommandBuffer();
    std::string function_name = bool(info) ? info->name : "<unknown>";

    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "function '{}({})': port='{}' cmd_buf={{[0]={:#x}", ctx.GetCommand(), function_name, service_name, cmd_buf[0]);
    for (int i = 1; i <= 8; ++i)
        fmt::format_to(std::back_inserter(buf), ", [{}]={:#x}", i, cmd_buf[i]);
    buf.push_back('}');

    system.GetReporter().SaveUnimplementedFunctionReport(ctx, ctx.GetCommand(), function_name, service_name);
    if (gated) {
        LOG_ERROR(Service, "Version gated {}", fmt::to_string(buf));
    } else {
        UNIMPLEMENTED_MSG("Unknown / unimplemented {}", fmt::to_string(buf));
    }
    if (Settings::values.use_auto_stub) {
        LOG_WARNING(Service, "Using auto stub fallback!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
}

[[nodiscard]] static bool VersionGateCheck(Core::System& system, u32 vg) {
    u32 const c_maj = FirmwareManager::GetFirmwareVersion(system).first.major;
    u32 const c_min = FirmwareManager::GetFirmwareVersion(system).first.minor;
    u32 const c_pat = FirmwareManager::GetFirmwareVersion(system).first.micro;
    u32 const cg = (c_pat << 0) | (c_min << 4) | (c_maj << 8);
    u32 const sg = (vg >> 16) & 0xffff;
    u32 const ug = vg & 0xffff;
    LOG_ERROR(Service, "sg: {}, cg: {}, ug: {}", sg, cg, ug);
    // feature available after current
    if (sg && cg < sg)
        return false;
    // feature until xxx (0 = up to current)
    return ug ? ug >= cg : true;
}

void ServiceFrameworkBase::InvokeRequest(HLERequestContext& ctx) {
    const bool is_cmd_read = ctx.GetCommand() == 0;
    auto const info = FindRequest(ctx.GetCommand());
    if (!info.has_value() || info->handler_callback == nullptr)
        return ReportUnimplementedFunction(ctx, &*info, false);

    LOG_ERROR(Service, "check for {} version_gating: {}", info->name, info->version_gating);
    if (!VersionGateCheck(system, info->version_gating))
    {
        return ReportUnimplementedFunction(ctx, &*info, true);
    }

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName(), ctx.CommandBuffer()));
    handler_invoker(this, info->handler_callback, ctx);
    if (is_i_storage && is_cmd_read) {
        const auto* const process = ctx.GetThread().GetOwnerProcess();
        if (process != nullptr && system.IsNVDECActiveForProcess(process->GetId())) {
            std::this_thread::sleep_for(std::chrono::microseconds{600});
        }
    }
}

void ServiceFrameworkBase::InvokeRequestTipc(HLERequestContext& ctx) {
    auto const info = FindRequestTipc(ctx.GetCommand());
    if (!info.has_value() || info->handler_callback == nullptr)
        return ReportUnimplementedFunction(ctx, &*info, false);

    LOG_ERROR(Service, "check for {} version_gating: {}", info->name, info->version_gating);
    if (!VersionGateCheck(system, info->version_gating))
    {
        return ReportUnimplementedFunction(ctx, &*info, true);
    }

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName(), ctx.CommandBuffer()));
    handler_invoker(this, info->handler_callback, ctx);
}

Result ServiceFrameworkBase::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
    const auto guard = LockService();

    Result result = ResultSuccess;

    switch (ctx.GetCommandType()) {
    case IPC::CommandType::Close:
    case IPC::CommandType::TIPC_Close: {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        result = IPC::ResultSessionClosed;
        break;
    }
    case IPC::CommandType::ControlWithContext:
    case IPC::CommandType::Control: {
        system.ServiceManager().InvokeControlRequest(ctx);
        break;
    }
    case IPC::CommandType::RequestWithContext:
    case IPC::CommandType::Request: {
        InvokeRequest(ctx);
        break;
    }
    default:
        if (ctx.IsTipc()) {
            InvokeRequestTipc(ctx);
            break;
        }

        UNIMPLEMENTED_MSG("command_type={}", ctx.GetCommandType());
        break;
    }

    // If emulation was shutdown, we are closing service threads, do not write the response back to
    // memory that may be shutting down as well.
    if (system.IsPoweredOn()) {
        ctx.WriteToOutgoingCommandBuffer();
    }

    return result;
}

} // namespace Service
