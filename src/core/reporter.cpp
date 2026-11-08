// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ctime>
#include <format>
#include <fstream>
#include <iomanip>
#include <map>
#include <ranges>
#include <vector>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/hex_util.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/result.h"
#include "core/hle/service/hle_ipc.h"
#include "core/memory.h"
#include "core/reporter.h"

import jacinth;

struct YuzuVersionData {
    std::string scm_rev;
    std::string scm_branch;
    std::string scm_desc;
    std::string build_name;
    std::string build_date;
    std::string build_fullname;
    std::string build_version;
};

struct ReportCommonData {
    std::string title_id;
    std::string result_raw;
    std::string result_module;
    std::string result_description;
    std::string timestamp;
    std::optional<std::string> user_id;
};

struct ProcessorStateData {
    std::string entry_point;
    std::string sp;
    std::string pc;
    std::string pstate;
    std::string architecture;
    std::map<std::string, std::string> registers;
    std::optional<std::vector<std::string>> backtrace;
};

struct CrashReport {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
    ProcessorStateData processor_state;
};

struct SvcBreakData {
    std::string type;
    std::string signal_debugger;
    std::string info1;
    std::string info2;
    std::optional<std::string> debug_buffer;
};

struct SvcBreakReport {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
    SvcBreakData svc_break;
};

struct BufferDescriptorEntry {
    std::string address;
    std::string size;
    std::optional<std::string> data;
};

struct HLERequestContextData {
    std::vector<std::string> command_buffer;
    std::vector<BufferDescriptorEntry> buffer_descriptor_a;
    std::vector<BufferDescriptorEntry> buffer_descriptor_b;
    std::vector<BufferDescriptorEntry> buffer_descriptor_c;
    std::vector<BufferDescriptorEntry> buffer_descriptor_x;
    std::optional<u32> command_id;
    std::optional<std::string> function_name;
    std::optional<std::string> service_name;
};

struct UnimplementedFunctionReport {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
    HLERequestContextData function;
};

struct AppletCommonArgs {
    std::string applet_id;
    std::string common_args_version;
    std::string library_version;
    std::string theme_color;
    std::string startup_sound;
    std::string system_tick;
};

struct UnimplementedAppletReport {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
    AppletCommonArgs applet_common_args;
    std::vector<std::string> applet_normal_data;
    std::vector<std::string> applet_interactive_data;
};

struct PlayReport {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
    std::optional<std::string> play_report_process_id;
    std::string play_report_type;
    std::vector<std::string> play_report_data;
};

struct ErrorCustomText {
    std::string main;
    std::string detail;
};

struct ErrorReport {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
    ErrorCustomText error_custom_text;
};

struct FullDataAuto {
    YuzuVersionData yuzu_version;
    ReportCommonData report_common;
};

namespace {

std::filesystem::path GetPath(std::string_view type, u64 title_id, std::string_view timestamp) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir) / type /
           std::format("{:016X}_{}.json", title_id, timestamp);
}

std::string GetTimestamp() {
    const auto time = std::time(nullptr);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%FT%H-%M-%S");
    return oss.str();
}

template <typename T>
void SaveToFile(const T& data, const std::filesystem::path& filename) {
    if (!Common::FS::CreateParentDirs(filename)) {
        LOG_ERROR(Core, "Failed to create path for '{}' to save report!",
                  Common::FS::PathToUTF8String(filename));
        return;
    }

    const auto buffer = jacinth::json::dump(data, {.pretty = true});

    std::ofstream file;
    Common::FS::OpenFileStream(file, filename, std::ios_base::out | std::ios_base::trunc);
    file << buffer << std::endl;
}

YuzuVersionData GetYuzuVersionData() {
    return {
        .scm_rev = std::string(Common::g_scm_rev),
        .scm_branch = std::string(Common::g_scm_branch),
        .scm_desc = std::string(Common::g_scm_desc),
        .build_name = std::string(Common::g_build_name),
        .build_date = std::string(Common::g_build_date),
        .build_fullname = std::string(Common::g_build_fullname),
        .build_version = std::string(Common::g_build_version),
    };
}

ReportCommonData GetReportCommonData(u64 title_id, Result result, const std::string& timestamp,
                                     std::optional<u128> user_id = {}) {
    auto out = ReportCommonData{
        .title_id = std::format("{:016X}", title_id),
        .result_raw = std::format("{:08X}", result.raw),
        .result_module = std::format("{:08X}", static_cast<u32>(result.GetModule())),
        .result_description = std::format("{:08X}", result.GetDescription()),
        .timestamp = timestamp,
    };

    if (user_id.has_value()) {
        out.user_id = std::format("{:016X}{:016X}", (*user_id)[1], (*user_id)[0]);
    }

    return out;
}

ProcessorStateData GetProcessorStateData(const std::string& architecture, u64 entry_point, u64 sp,
                                         u64 pc, u64 pstate,
                                         const std::array<u64, 31>& registers,
                                         const std::optional<std::array<u64, 32>>& backtrace = {}) {
    std::map<std::string, std::string> registers_out;
    for (std::size_t i = 0; i < registers.size(); ++i) {
        registers_out[std::format("X{:02d}", i)] = std::format("{:016X}", registers[i]);
    }

    std::optional<std::vector<std::string>> backtrace_out;
    if (backtrace.has_value()) {
        backtrace_out = (*backtrace) | std::ranges::views::transform([](u64 entry) {
                            return std::format("{:016X}", entry);
                        }) |
                        std::ranges::to<std::vector<std::string>>();
    }

    return {
        .entry_point = std::format("{:016X}", entry_point),
        .sp = std::format("{:016X}", sp),
        .pc = std::format("{:016X}", pc),
        .pstate = std::format("{:016X}", pstate),
        .architecture = architecture,
        .registers = std::move(registers_out),
        .backtrace = std::move(backtrace_out),
    };
}

FullDataAuto GetFullDataAuto(const std::string& timestamp, u64 title_id, Core::System& system) {
    return {
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, ResultSuccess, timestamp),
    };
}

template <bool read_value, typename DescriptorType>
std::vector<BufferDescriptorEntry> GetHLEBufferDescriptorData(
    const boost::container::static_vector<DescriptorType, IPC::MAX_BUFFER_DESCRIPTORS>& buffer,
    Core::Memory::Memory& memory) {
    std::vector<BufferDescriptorEntry> out;
    out.reserve(buffer.size());
    for (const auto& desc : buffer) {
        BufferDescriptorEntry entry{
            .address = std::format("{:016X}", desc.Address()),
            .size = std::format("{:016X}", desc.Size()),
        };

        if constexpr (read_value) {
            std::vector<u8> data(desc.Size());
            memory.ReadBlock(desc.Address(), data.data(), desc.Size());
            entry.data = Common::HexToString(data);
        }

        out.push_back(std::move(entry));
    }
    return out;
}

HLERequestContextData GetHLERequestContextData(Service::HLERequestContext& ctx,
                                               Core::Memory::Memory& memory) {
    std::vector<std::string> cmd_buf;
    cmd_buf.reserve(IPC::COMMAND_BUFFER_LENGTH);
    for (std::size_t i = 0; i < IPC::COMMAND_BUFFER_LENGTH; ++i) {
        cmd_buf.push_back(std::format("{:08X}", ctx.CommandBuffer()[i]));
    }

    return {
        .command_buffer = std::move(cmd_buf),
        .buffer_descriptor_a = GetHLEBufferDescriptorData<true>(ctx.BufferDescriptorA(), memory),
        .buffer_descriptor_b = GetHLEBufferDescriptorData<false>(ctx.BufferDescriptorB(), memory),
        .buffer_descriptor_c = GetHLEBufferDescriptorData<false>(ctx.BufferDescriptorC(), memory),
        .buffer_descriptor_x = GetHLEBufferDescriptorData<true>(ctx.BufferDescriptorX(), memory),
    };
}

} // Anonymous namespace

namespace Core {

Reporter::Reporter(System& system_) : system(system_) {
    ClearFSAccessLog();
}

Reporter::~Reporter() = default;

void Reporter::SaveCrashReport(u64 title_id, Result result, u64 set_flags, u64 entry_point, u64 sp,
                               u64 pc, u64 pstate, u64 afsr0, u64 afsr1, u64 esr, u64 far,
                               const std::array<u64, 31>& registers,
                               const std::array<u64, 32>& backtrace, u32 backtrace_size,
                               const std::string& arch, u32 unk10) const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();

    CrashReport out{
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, result, timestamp),
        .processor_state = GetProcessorStateData(arch, entry_point, sp, pc, pstate, registers,
                                                 backtrace),
    };

    out.processor_state.registers["set_flags"] = std::format("{:016X}", set_flags);
    out.processor_state.registers["afsr0"] = std::format("{:016X}", afsr0);
    out.processor_state.registers["afsr1"] = std::format("{:016X}", afsr1);
    out.processor_state.registers["esr"] = std::format("{:016X}", esr);
    out.processor_state.registers["far"] = std::format("{:016X}", far);
    out.processor_state.registers["backtrace_size"] = std::format("{:08X}", backtrace_size);
    out.processor_state.registers["unknown_10"] = std::format("{:08X}", unk10);

    SaveToFile(out, GetPath("crash_report", title_id, timestamp));
}

void Reporter::SaveSvcBreakReport(u32 type, bool signal_debugger, u64 info1, u64 info2,
                                  const std::optional<std::vector<u8>>& resolved_buffer) const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();
    const auto title_id = system.GetApplicationProcessProgramID();

    SvcBreakReport out{
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, ResultSuccess, timestamp),
        .svc_break = SvcBreakData{
            .type = std::format("{:08X}", type),
            .signal_debugger = std::format("{}", signal_debugger),
            .info1 = std::format("{:016X}", info1),
            .info2 = std::format("{:016X}", info2),
        },
    };

    if (resolved_buffer.has_value()) {
        out.svc_break.debug_buffer = Common::HexToString(*resolved_buffer);
    }

    SaveToFile(out, GetPath("svc_break_report", title_id, timestamp));
}

void Reporter::SaveUnimplementedFunctionReport(Service::HLERequestContext& ctx, u32 command_id,
                                               const std::string& name,
                                               const std::string& service_name) const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();
    const auto title_id = system.GetApplicationProcessProgramID();

    UnimplementedFunctionReport out{
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, ResultSuccess, timestamp),
        .function = GetHLERequestContextData(ctx, ctx.GetMemory()),
    };

    out.function.command_id = command_id;
    out.function.function_name = name;
    out.function.service_name = service_name;

    SaveToFile(out, GetPath("unimpl_func_report", title_id, timestamp));
}

void Reporter::SaveUnimplementedAppletReport(
    u32 applet_id, u32 common_args_version, u32 library_version, u32 theme_color,
    bool startup_sound, u64 system_tick, const std::vector<std::vector<u8>>& normal_channel,
    const std::vector<std::vector<u8>>& interactive_channel) const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();
    const auto title_id = system.GetApplicationProcessProgramID();

    UnimplementedAppletReport out{
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, ResultSuccess, timestamp),
        .applet_common_args = AppletCommonArgs{
            .applet_id = std::format("{:02X}", applet_id),
            .common_args_version = std::format("{:08X}", common_args_version),
            .library_version = std::format("{:08X}", library_version),
            .theme_color = std::format("{:08X}", theme_color),
            .startup_sound = std::format("{}", startup_sound),
            .system_tick = std::format("{:016X}", system_tick),
        },
    };

    out.applet_normal_data = normal_channel | std::ranges::views::transform([](const auto& data) {
                                 return Common::HexToString(data);
                             }) |
                             std::ranges::to<std::vector<std::string>>();

    out.applet_interactive_data =
        interactive_channel | std::ranges::views::transform([](const auto& data) {
            return Common::HexToString(data);
        }) |
        std::ranges::to<std::vector<std::string>>();

    SaveToFile(out, GetPath("unimpl_applet_report", title_id, timestamp));
}

void Reporter::SavePlayReport(PlayReportType type, u64 title_id,
                              const std::vector<std::span<const u8>>& data,
                              std::optional<u64> process_id, std::optional<u128> user_id) const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();

    PlayReport out{
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, ResultSuccess, timestamp, user_id),
    };

    out.play_report_data = data | std::ranges::views::transform([](const auto& d) {
                               return Common::HexToString(d);
                           }) |
                           std::ranges::to<std::vector<std::string>>();

    if (process_id.has_value()) {
        out.play_report_process_id = std::format("{:016X}", *process_id);
    }

    out.play_report_type = std::format("{:02}", static_cast<u8>(type));

    SaveToFile(out, GetPath("play_report", title_id, timestamp));
}

void Reporter::SaveErrorReport(u64 title_id, Result result,
                               const std::optional<std::string>& custom_text_main,
                               const std::optional<std::string>& custom_text_detail) const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();

    ErrorReport out{
        .yuzu_version = GetYuzuVersionData(),
        .report_common = GetReportCommonData(title_id, result, timestamp),
        .error_custom_text = ErrorCustomText{
            .main = custom_text_main.value_or(""),
            .detail = custom_text_detail.value_or(""),
        },
    };

    SaveToFile(out, GetPath("error_report", title_id, timestamp));
}

void Reporter::SaveFSAccessLog(std::string_view log_message) const {
    const auto access_log_path =
        Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "FsAccessLog.txt";

    void(Common::FS::AppendStringToFile(access_log_path, Common::FS::FileType::TextFile,
                                        log_message));
}

void Reporter::SaveUserReport() const {
    if (!IsReportingEnabled()) {
        return;
    }

    const auto timestamp = GetTimestamp();
    const auto title_id = system.GetApplicationProcessProgramID();

    SaveToFile(GetFullDataAuto(timestamp, title_id, system),
               GetPath("user_report", title_id, timestamp));
}

void Reporter::ClearFSAccessLog() const {
    const auto access_log_path =
        Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "FsAccessLog.txt";

    Common::FS::IOFile access_log_file{access_log_path, Common::FS::FileAccessMode::Write,
                                       Common::FS::FileType::TextFile};

    if (!access_log_file.IsOpen()) {
        LOG_ERROR(Common_Filesystem, "Failed to clear the filesystem access log.");
    }
}

bool Reporter::IsReportingEnabled() const {
    return Settings::values.reporting_services.GetValue();
}

} // namespace Core
