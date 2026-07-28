// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/random.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/hardware_properties.h"
#include "core/hle/api_version.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nro.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {

struct NroSegmentHeader {
    u32_le offset;
    u32_le size;
};
static_assert(sizeof(NroSegmentHeader) == 0x8, "NroSegmentHeader has incorrect size.");

struct NroHeader {
    INSERT_PADDING_BYTES(0x4);
    u32_le module_header_offset;
    u32 magic_ext1;
    u32 magic_ext2;
    u32_le magic;
    INSERT_PADDING_BYTES(0x4);
    u32_le file_size;
    INSERT_PADDING_BYTES(0x4);
    std::array<NroSegmentHeader, 3> segments; // Text, RoData, Data (in that order)
    u32_le bss_size;
    INSERT_PADDING_BYTES(0x44);
};
static_assert(sizeof(NroHeader) == 0x80, "NroHeader has incorrect size.");

struct ModHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le unwind_start_offset;
    u32_le unwind_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(ModHeader) == 0x1c, "ModHeader has incorrect size.");

struct AssetSection {
    u64_le offset;
    u64_le size;
};
static_assert(sizeof(AssetSection) == 0x10, "AssetSection has incorrect size.");

struct AssetHeader {
    u32_le magic;
    u32_le format_version;
    AssetSection icon;
    AssetSection nacp;
    AssetSection romfs;
};
static_assert(sizeof(AssetHeader) == 0x38, "AssetHeader has incorrect size.");

AppLoader_NRO::AppLoader_NRO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {
    NroHeader nro_header{};
    if (file->ReadObject(&nro_header) != sizeof(NroHeader)) {
        return;
    }

    if (file->GetSize() >= nro_header.file_size + sizeof(AssetHeader)) {
        const u64 offset = nro_header.file_size;
        AssetHeader asset_header{};
        if (file->ReadObject(&asset_header, offset) != sizeof(AssetHeader)) {
            return;
        }

        if (asset_header.format_version != 0) {
            LOG_WARNING(Loader,
                        "NRO Asset Header has format {}, currently supported format is 0. If "
                        "strange glitches occur with metadata, check NRO assets.",
                        asset_header.format_version);
        }

        if (asset_header.magic != Common::MakeMagic('A', 'S', 'E', 'T')) {
            return;
        }

        if (asset_header.nacp.size > 0) {
            nacp = std::make_unique<FileSys::NACP>(std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.nacp.size, offset + asset_header.nacp.offset, "Control.nacp"));
        }

        if (asset_header.romfs.size > 0) {
            romfs = std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.romfs.size, offset + asset_header.romfs.offset, "game.romfs");
        }

        if (asset_header.icon.size > 0) {
            icon_data = file->ReadBytes(asset_header.icon.size, offset + asset_header.icon.offset);
        }
    }
}

AppLoader_NRO::~AppLoader_NRO() = default;

FileType AppLoader_NRO::IdentifyType(const FileSys::VirtualFile& nro_file) {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != nro_file->ReadObject(&nro_header)) {
        return FileType::Error;
    }
    if (nro_header.magic == Common::MakeMagic('N', 'R', 'O', '0')) {
        return FileType::NRO;
    }
    return FileType::Error;
}

bool AppLoader_NRO::IsHomebrew() {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != file->ReadObject(&nro_header)) {
        return false;
    }
    return nro_header.magic_ext1 == Common::MakeMagic('H', 'O', 'M', 'E') &&
           nro_header.magic_ext2 == Common::MakeMagic('B', 'R', 'E', 'W');
}

static constexpr u32 PageAlignSize(u32 size) {
    return static_cast<u32>((size + Core::Memory::YUZU_PAGEMASK) & ~Core::Memory::YUZU_PAGEMASK);
}

static std::string MakeHomebrewSdmcPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    return "sdmc:/" + path;
}

static std::optional<std::string> TryMakeHomebrewSdmcPathFromHostPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');

    std::string sdmc_root =
        Common::FS::PathToUTF8String(Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir));
    std::replace(sdmc_root.begin(), sdmc_root.end(), '\\', '/');
    while (!sdmc_root.empty() && sdmc_root.back() == '/') {
        sdmc_root.pop_back();
    }

    if (!sdmc_root.empty() && path.size() > sdmc_root.size() &&
        path.compare(0, sdmc_root.size(), sdmc_root) == 0 && path[sdmc_root.size()] == '/') {
        return MakeHomebrewSdmcPath(path.substr(sdmc_root.size() + 1));
    }

    constexpr std::string_view SdmcMarker{"/sdmc/"};
    if (const auto pos = path.rfind(SdmcMarker); pos != std::string::npos) {
        return MakeHomebrewSdmcPath(path.substr(pos + SdmcMarker.size()));
    }

    return std::nullopt;
}

static std::string MakeHomebrewArgv0(std::string nro_path, std::string file_name) {
    if (nro_path.empty()) {
        nro_path = std::move(file_name);
    }

    std::replace(nro_path.begin(), nro_path.end(), '\\', '/');

    if (nro_path.rfind("sdmc:/", 0) == 0) {
        return nro_path;
    }

    if (auto sdmc_path = TryMakeHomebrewSdmcPathFromHostPath(nro_path)) {
        return *sdmc_path;
    }

#ifdef __ANDROID__
    if (nro_path.find('%') != std::string::npos) {
        const auto percent_decode = [](std::string value) {
            const auto hex_value = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };

            std::string decoded;
            decoded.reserve(value.size());
            for (size_t i = 0; i < value.size(); i++) {
                if (value[i] == '%' && i + 2 < value.size()) {
                    const int high = hex_value(value[i + 1]);
                    const int low = hex_value(value[i + 2]);
                    if (high >= 0 && low >= 0) {
                        decoded.push_back(static_cast<char>((high << 4) | low));
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(value[i]);
            }
            return decoded;
        };

        if (auto sdmc_path = TryMakeHomebrewSdmcPathFromHostPath(percent_decode(nro_path))) {
            return *sdmc_path;
        }
    }
#endif

    if (!nro_path.empty() && nro_path.front() == '/') {
        return "sdmc:" + nro_path;
    }

    return nro_path.empty() ? "homebrew" : nro_path;
}

static std::string QuoteHomebrewArgvComponent(const std::string& argument) {
    if (argument.find_first_of(" \t\r\n") == std::string::npos) {
        return argument;
    }

    std::string quoted{"\""};
    quoted += argument;
    quoted.push_back('"');
    return quoted;
}

static std::string GetHomebrewInitialCwd(const std::string& argv0) {
    constexpr std::string_view SdmcPrefix = "sdmc:";
    if (argv0.substr(0, SdmcPrefix.size()) != SdmcPrefix) {
        return {};
    }

    const auto last_slash = argv0.find_last_of('/');
    if (last_slash == std::string_view::npos || last_slash < SdmcPrefix.size()) {
        return {};
    }

    std::string cwd = argv0.substr(SdmcPrefix.size(), last_slash - SdmcPrefix.size());
    if (cwd.empty()) {
        cwd = "/";
    }
    while (cwd.size() > 1 && cwd.back() == '/') {
        cwd.pop_back();
    }
    return cwd;
}

constexpr size_t HomebrewNextLoadPathSize = 0x200;
constexpr size_t HomebrewNextLoadArgvSize = 0x800;
constexpr u32 HomebrewSvcExitProcessInstruction = 0xD40000E1;
constexpr u32 HomebrewEntryEndOfList = 0;
constexpr u32 HomebrewEntryMainThreadHandle = 1;
constexpr u32 HomebrewEntryNextLoadPath = 2;
constexpr u32 HomebrewEntryOverrideHeap = 3;
constexpr u32 HomebrewEntryArgv = 5;
constexpr u32 HomebrewEntrySyscallAvailableHint = 6;
constexpr u32 HomebrewEntryAppletType = 7;
constexpr u32 HomebrewEntryProcessHandle = 10;
constexpr u32 HomebrewEntryRandomSeed = 14;
constexpr u32 HomebrewEntryHosVersion = 16;
constexpr u32 HomebrewEntrySyscallAvailableHint2 = 17;
constexpr u32 HomebrewAppletTypeApplication = 0;
constexpr u64 HomebrewAllSvcHints = ~u64{0};
constexpr u32 HomebrewHosVersion = (u32{HLE::ApiVersion::HOS_VERSION_MAJOR} << 16) |
                                   (u32{HLE::ApiVersion::HOS_VERSION_MINOR} << 8) |
                                   u32{HLE::ApiVersion::HOS_VERSION_MICRO};

struct HomebrewConfigEntry {
    u32_le key;
    u32_le flags;
    u64_le value[2];
};
static_assert(sizeof(HomebrewConfigEntry) == 0x18);
constexpr size_t HomebrewBaseConfigEntryCount = 10;
constexpr size_t HomebrewInPlaceConfigEntryCount = 11;
constexpr size_t HomebrewBaseConfigTableSize =
    HomebrewBaseConfigEntryCount * sizeof(HomebrewConfigEntry);
constexpr size_t HomebrewInPlaceConfigTableSize =
    HomebrewInPlaceConfigEntryCount * sizeof(HomebrewConfigEntry);

struct HomebrewNroImage {
    Kernel::CodeSet codeset;
    size_t image_size{};
    size_t args_offset{};
    std::optional<size_t> exit_process_offset;
    std::string argv_string;
};

static void SetHomebrewConfigPointers(Kernel::KProcess& process, u64 config_addr,
                                      u64 next_load_path_addr, u64 next_load_argv_addr) {
    constexpr size_t MainThreadHandleEntryIndex = 0;
    constexpr size_t ProcessHandleEntryIndex = 1;
    constexpr size_t EntryValueOffset = offsetof(HomebrewConfigEntry, value);

    process.SetArgPointer(Kernel::KProcessAddress{config_addr});
    process.SetMainThreadHandleAddr(Kernel::KProcessAddress{
        config_addr + MainThreadHandleEntryIndex * sizeof(HomebrewConfigEntry) + EntryValueOffset});
    process.SetProcessHandleAddr(Kernel::KProcessAddress{
        config_addr + ProcessHandleEntryIndex * sizeof(HomebrewConfigEntry) + EntryValueOffset});
    process.SetHomebrewNextLoadBufferAddrs(Kernel::KProcessAddress{next_load_path_addr},
                                           Kernel::KProcessAddress{next_load_argv_addr});
}

static std::optional<HomebrewNroImage> BuildHomebrewNroImage(const std::vector<u8>& data,
                                                             std::string nro_path,
                                                             std::string file_name,
                                                             std::string launch_argv) {
    if (data.size() < sizeof(NroHeader)) {
        return std::nullopt;
    }

    NroHeader nro_header{};
    std::memcpy(&nro_header, data.data(), sizeof(NroHeader));
    if (nro_header.magic != Common::MakeMagic('N', 'R', 'O', '0')) {
        return std::nullopt;
    }
    if (data.size() < nro_header.file_size ||
        nro_header.module_header_offset + sizeof(ModHeader) > PageAlignSize(nro_header.file_size)) {
        return std::nullopt;
    }

    std::vector<u8> program_image(PageAlignSize(nro_header.file_size));
    std::memcpy(program_image.data(), data.data(), nro_header.file_size);

    Kernel::CodeSet codeset;
    for (std::size_t i = 0; i < nro_header.segments.size(); ++i) {
        codeset.segments[i].addr = nro_header.segments[i].offset;
        codeset.segments[i].offset = nro_header.segments[i].offset;
        codeset.segments[i].size = PageAlignSize(nro_header.segments[i].size);
    }

    u32 bss_size{PageAlignSize(nro_header.bss_size)};
    ModHeader mod_header{};
    std::memcpy(&mod_header, program_image.data() + nro_header.module_header_offset,
                sizeof(ModHeader));

    if (mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')) {
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }

    codeset.DataSegment().size += bss_size;
    program_image.resize(static_cast<u32>(program_image.size()) + bss_size);

    HomebrewNroImage image{.codeset = std::move(codeset)};
    const auto argv0 = MakeHomebrewArgv0(std::move(nro_path), std::move(file_name));

    if (!launch_argv.empty()) {
        image.argv_string = std::move(launch_argv);
    } else {
        image.argv_string = QuoteHomebrewArgvComponent(argv0);
    }
    if (image.argv_string.empty() || image.argv_string.back() != '\0') {
        image.argv_string.push_back('\0');
    }

    const auto& code = image.codeset.CodeSegment();
    const size_t code_end = (std::min)(program_image.size(), code.offset + code.size);
    for (size_t offset = code.offset; offset + sizeof(u32) <= code_end; offset += sizeof(u32)) {
        u32 instruction{};
        std::memcpy(&instruction, program_image.data() + offset, sizeof(instruction));
        if (instruction == HomebrewSvcExitProcessInstruction) {
            image.exit_process_offset = offset;
            break;
        }
    }

    const size_t entries_and_buffers =
        Common::AlignUp(HomebrewInPlaceConfigTableSize + HomebrewNextLoadPathSize +
                            HomebrewNextLoadArgvSize + image.argv_string.size(),
                        Core::Memory::YUZU_PAGESIZE);

    image.args_offset = program_image.size();
    image.codeset.DataSegment().size += static_cast<u32>(entries_and_buffers);
    program_image.resize(image.args_offset + entries_and_buffers);
    image.image_size = program_image.size();
    image.codeset.memory = std::move(program_image);

    return image;
}

bool LoadNroInPlace(Core::System& system, Kernel::KProcess& process, Kernel::KThread& thread,
                    const FileSys::VirtualFile& nro_file, const std::string& nro_path,
                    const std::string& launch_argv) {
    if (!nro_file) {
        return false;
    }

#ifdef HAS_NCE
    if (Settings::IsNceEnabled()) {
        LOG_WARNING(Loader,
                    "Homebrew next-load: in-place handoff unavailable because NCE is enabled");
        return false;
    }
#endif

    size_t live_threads = 0;
    for (auto& candidate : process.GetThreadList()) {
        if (candidate.GetState() != Kernel::ThreadState::Terminated) {
            live_threads++;
        }
    }
    if (live_threads != 1) {
        LOG_WARNING(Loader, "NextLoad: in-place handoff failed because live_threads={}",
                    live_threads);
        return false;
    }

    const auto stack_top = process.GetMainThreadStackTop();
    if (GetInteger(stack_top) == 0) {
        LOG_WARNING(Loader, "NextLoad: in-place handoff failed because stack_top=0");
        return false;
    }

    auto image =
        BuildHomebrewNroImage(nro_file->ReadAllBytes(), nro_path, nro_file->GetName(), launch_argv);
    if (!image) {
        LOG_WARNING(Loader, "NextLoad: in-place handoff failed because '{}' is invalid",
                    nro_path);
        return false;
    }

    const auto capacity = process.GetCodeSize();
    if (image->image_size > capacity) {
        LOG_WARNING(Loader,
                    "NextLoad: in-place handoff failed because image_size=0x{:X} exceeds "
                    "capacity=0x{:X}",
                    image->image_size, capacity);
        return false;
    }

    const u64 base = GetInteger(process.GetEntryPoint());
    const u64 heap_addr = GetInteger(process.GetPageTable().GetHeapRegionStart());
    const size_t heap_size = process.GetPageTable().GetBasePageTable().GetCurrentHeapSize();
    if (heap_addr == 0 || heap_size == 0) {
        LOG_WARNING(Loader,
                    "NextLoad: in-place handoff failed because heap override is "
                    "unavailable (addr=0x{:016X}, size=0x{:X})",
                    heap_addr, heap_size);
        return false;
    }

    const u64 config_addr = base + image->args_offset;
    const u64 next_load_path_addr = config_addr + HomebrewInPlaceConfigTableSize;
    const u64 next_load_argv_addr = next_load_path_addr + HomebrewNextLoadPathSize;
    const u64 argv_addr = next_load_argv_addr + HomebrewNextLoadArgvSize;
    const u64 argv_entry_addr = image->argv_string.empty() ? 0 : argv_addr;
    const std::string argv0 = MakeHomebrewArgv0(nro_path, nro_file->GetName());
    const std::string homebrew_initial_cwd = GetHomebrewInitialCwd(argv0);
    u64 program_id{};
    AppLoader_NRO loader{nro_file};
    if (loader.ReadProgramId(program_id) != Loader::ResultStatus::Success) {
        LOG_WARNING(Loader, "NextLoad: in-place handoff could not read NRO program id");
    }

    Kernel::Handle main_thread_handle{};
    if (process.GetHandleTable().Add(system.Kernel(), std::addressof(main_thread_handle), &thread)
            .IsError()) {
        LOG_WARNING(Loader,
                    "NextLoad: in-place handoff failed because main thread handle failed");
        return false;
    }

    Kernel::Handle process_handle{};
    if (process.GetHandleTable().Add(system.Kernel(), std::addressof(process_handle), &process)
            .IsError()) {
        LOG_WARNING(Loader,
                    "NextLoad: in-place handoff failed because process handle failed");
        return false;
    }

    if (!process.GetMemory().ZeroBlock(Common::ProcessAddress{heap_addr}, heap_size)) {
        LOG_WARNING(Loader,
                    "NextLoad: in-place handoff failed because heap clear failed "
                    "(addr=0x{:016X}, size=0x{:X})",
                    heap_addr, heap_size);
        return false;
    }

    process.LoadModule(system.Kernel(), std::move(image->codeset), process.GetEntryPoint());

    const HomebrewConfigEntry entries[HomebrewInPlaceConfigEntryCount] = {
        {HomebrewEntryMainThreadHandle, 0, {main_thread_handle, 0}},
        {HomebrewEntryProcessHandle, 0, {process_handle, 0}},
        {HomebrewEntryNextLoadPath, 0, {next_load_path_addr, next_load_argv_addr}},
        {HomebrewEntryOverrideHeap, 0, {heap_addr, heap_size}},
        {HomebrewEntryAppletType, 0, {HomebrewAppletTypeApplication, 0}},
        {HomebrewEntryArgv, 0, {0, argv_entry_addr}},
        {HomebrewEntrySyscallAvailableHint, 0, {HomebrewAllSvcHints, HomebrewAllSvcHints}},
        {HomebrewEntryRandomSeed, 0,
         {process.GetRandomEntropy(0), process.GetRandomEntropy(1)}},
        {HomebrewEntryHosVersion, 0, {HomebrewHosVersion, 0}},
        {HomebrewEntrySyscallAvailableHint2, 0, {HomebrewAllSvcHints, 0}},
        {HomebrewEntryEndOfList, 0, {0, 0}},
    };

    process.GetMemory().WriteBlock(Common::ProcessAddress{config_addr}, entries, sizeof(entries));
    process.GetMemory().WriteBlock(Common::ProcessAddress{argv_addr}, image->argv_string.data(),
                                   image->argv_string.size());
    process.GetMemory().Write32(Common::ProcessAddress{GetInteger(thread.GetTlsAddress()) + 0x110},
                                main_thread_handle);

    process.SetArgReturnAddress(Kernel::KProcessAddress{
        image->exit_process_offset ? base + *image->exit_process_offset : 0});
    SetHomebrewConfigPointers(process, config_addr, next_load_path_addr, next_load_argv_addr);
    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), program_id,
        std::make_unique<FileSys::RomFSFactory>(loader, system.GetContentProvider(),
                                                system.GetFileSystemController()),
        homebrew_initial_cwd);
    process.SetHomebrewInPlaceNextLoad(true);

    auto& context = thread.GetContext();
    context = {};
    context.r[0] = config_addr;
    context.r[1] = UINT64_MAX;
    context.r[18] = Common::Random::Random64(0) | 1;
    context.lr = image->exit_process_offset ? base + *image->exit_process_offset : 0;
    context.pc = base;
    context.sp = GetInteger(stack_top);
    context.fpcr = 0;
    context.fpsr = 0;

    for (std::size_t core = 0; core < Core::Hardware::NUM_CPU_CORES; core++) {
        if (auto* arm = process.GetArmInterface(core); arm != nullptr) {
            arm->ClearInstructionCache();
        }
    }

    return true;
}

static bool LoadNroImpl(Core::System& system, Kernel::KProcess& process,
                        const std::vector<u8>& data, std::string nro_path,
                        std::string file_name) {
    if (data.size() < sizeof(NroHeader)) {
        return {};
    }

    // Read NSO header
    NroHeader nro_header{};
    std::memcpy(&nro_header, data.data(), sizeof(NroHeader));
    if (nro_header.magic != Common::MakeMagic('N', 'R', 'O', '0')) {
        return {};
    }

    // Build program image
    std::vector<u8> program_image(PageAlignSize(nro_header.file_size));
    std::memcpy(program_image.data(), data.data(), program_image.size());
    if (program_image.size() != PageAlignSize(nro_header.file_size)) {
        return {};
    }

    Kernel::CodeSet codeset;
    for (std::size_t i = 0; i < nro_header.segments.size(); ++i) {
        codeset.segments[i].addr = nro_header.segments[i].offset;
        codeset.segments[i].offset = nro_header.segments[i].offset;
        codeset.segments[i].size = PageAlignSize(nro_header.segments[i].size);
    }

    // Default .bss to NRO header bss size if MOD0 section doesn't exist
    u32 bss_size{PageAlignSize(nro_header.bss_size)};

    // Read MOD header
    ModHeader mod_header{};
    std::memcpy(&mod_header, program_image.data() + nro_header.module_header_offset,
                sizeof(ModHeader));

    const bool has_mod_header{mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')};
    if (has_mod_header) {
        // Resize program image to include .bss section and page align each section
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }

    codeset.DataSegment().size += bss_size;
    program_image.resize(static_cast<u32>(program_image.size()) + bss_size);
    std::string argv_string;
    size_t args_offset_in_image = 0;
    std::optional<size_t> exit_process_offset_in_image;
    const auto& program_args = Settings::values.program_args.GetValue();
    const std::string argv0 = MakeHomebrewArgv0(std::move(nro_path), std::move(file_name));
    argv_string = QuoteHomebrewArgvComponent(argv0);
    if (!program_args.empty()) {
        argv_string.push_back(' ');
        argv_string += program_args;
    }
    if (argv_string.empty() || argv_string.back() != '\0') {
        argv_string.push_back('\0');
    }

    const auto& code_segment = codeset.CodeSegment();
    const size_t code_end =
        (std::min)(program_image.size(), code_segment.offset + code_segment.size);
    for (size_t offset = code_segment.offset; offset + sizeof(u32) <= code_end;
         offset += sizeof(u32)) {
        u32 instruction{};
        std::memcpy(&instruction, program_image.data() + offset, sizeof(instruction));
        if (instruction == HomebrewSvcExitProcessInstruction) {
            exit_process_offset_in_image = offset;
            break;
        }
    }
    if (!exit_process_offset_in_image) {
        LOG_WARNING(Loader, "Unable to find svcExitProcess in NRO; returning from main may fault");
    }

    const size_t entries_and_buffers =
        Common::AlignUp(HomebrewBaseConfigTableSize + HomebrewNextLoadPathSize +
                            HomebrewNextLoadArgvSize + argv_string.size(),
                        Core::Memory::YUZU_PAGESIZE);

    args_offset_in_image = program_image.size();
    codeset.DataSegment().size += static_cast<u32>(entries_and_buffers);
    program_image.resize(args_offset_in_image + entries_and_buffers);
    size_t image_size = program_image.size();

#ifdef HAS_NCE
    const auto& code = codeset.CodeSegment();

    // NROs always have a 39-bit address space.
    Settings::SetNceEnabled(true);

    // Create NCE patcher
    Core::NCE::Patcher patch{};

    if (Settings::IsNceEnabled()) {
        // Patch SVCs and MRS calls in the guest code
        patch.PatchText(program_image, code);

        // We only support PostData patching for NROs.
        ASSERT(patch.GetPatchMode() == Core::NCE::PatchMode::PostData);

        // Update patch section.
        auto& patch_segment = codeset.PatchSegment();
        patch_segment.addr = image_size;
        patch_segment.size = static_cast<u32>(patch.GetSectionSize());

        // Add patch section size to the module size.
        image_size += patch_segment.size;
    }
#endif
    // In-place NextLoad reuses the original code mapping; leave room for larger homebrew cores.
    constexpr size_t HomebrewCodeArenaSize = 192 * 1024 * 1024;
    image_size = (std::max)(image_size, HomebrewCodeArenaSize);

    // Enable direct memory mapping in case of NCE.
    const u64 fastmem_base = [&]() -> size_t {
        if (Settings::IsNceEnabled()) {
            auto& buffer = system.DeviceMemory().buffer;
            buffer.EnableDirectMappedAddress();
            return reinterpret_cast<u64>(buffer.VirtualBasePointer());
        }
        return 0;
    }();

    // TODO: this is bad form of ASLR, it sucks
    std::uintptr_t aslr_offset = ((::Settings::values.rng_seed_enabled.GetValue()
        ? ::Settings::values.rng_seed.GetValue() : Common::Random::Random64(0)) << 12) & 0xfff000;

    // Setup the process code layout
    if (process
            .LoadFromMetadata(system.Kernel(), FileSys::ProgramMetadata::GetDefault(), image_size, fastmem_base, aslr_offset)
            .IsError()) {
        return false;
    }

    // Relocate code patch and copy to the program_image if running under NCE.
    // This needs to be after LoadFromMetadata so we can use the process entry point.
#ifdef HAS_NCE
    if (Settings::IsNceEnabled()) {
        patch.RelocateAndCopy(process.GetEntryPoint(), code, program_image,
                              &process.GetPostHandlers());
    }
#endif

    // Load codeset for current process
    codeset.memory = std::move(program_image);
    process.LoadModule(system.Kernel(), std::move(codeset), process.GetEntryPoint());
    process.SetHomebrewInPlaceNextLoad(false);
    {
        const u64 base = GetInteger(process.GetEntryPoint());
        const u64 config_addr = base + args_offset_in_image;
        const u64 next_load_path_addr = config_addr + HomebrewBaseConfigTableSize;
        const u64 next_load_argv_addr = next_load_path_addr + HomebrewNextLoadPathSize;
        const u64 argv_addr = next_load_argv_addr + HomebrewNextLoadArgvSize;
        const u64 argv_entry_addr = argv_string.empty() ? 0 : argv_addr;

        const HomebrewConfigEntry entries[HomebrewBaseConfigEntryCount] = {
            {HomebrewEntryMainThreadHandle, 0, {0, 0}}, // Value[0] patched in Run()
            {HomebrewEntryProcessHandle, 0, {0, 0}},    // Value[0] patched in Run()
            {HomebrewEntryNextLoadPath, 0, {next_load_path_addr, next_load_argv_addr}},
            {HomebrewEntryAppletType, 0, {HomebrewAppletTypeApplication, 0}},
            {HomebrewEntryArgv, 0, {0, argv_entry_addr}},
            {HomebrewEntrySyscallAvailableHint, 0, {HomebrewAllSvcHints, HomebrewAllSvcHints}},
            {HomebrewEntryRandomSeed, 0,
             {process.GetRandomEntropy(0), process.GetRandomEntropy(1)}},
            {HomebrewEntryHosVersion, 0, {HomebrewHosVersion, 0}},
            {HomebrewEntrySyscallAvailableHint2, 0, {HomebrewAllSvcHints, 0}},
            {HomebrewEntryEndOfList, 0, {0, 0}},
        };
        process.GetMemory().WriteBlock(Common::ProcessAddress{config_addr}, entries,
                                       sizeof(entries));
        if (!argv_string.empty()) {
            process.GetMemory().WriteBlock(Common::ProcessAddress{argv_addr}, argv_string.data(),
                                           argv_string.size());
        }
        if (exit_process_offset_in_image) {
            process.SetArgReturnAddress(Kernel::KProcessAddress{base + *exit_process_offset_in_image});
        }
        SetHomebrewConfigPointers(process, config_addr, next_load_path_addr, next_load_argv_addr);
    }

    return true;
}

bool AppLoader_NRO::LoadNro(Core::System& system, Kernel::KProcess& process,
                            const FileSys::VfsFile& nro_file) {
    return LoadNroImpl(system, process, nro_file.ReadAllBytes(), nro_file.GetFullPath(),
                       nro_file.GetName());
}

AppLoader_NRO::LoadResult AppLoader_NRO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (!LoadNro(system, process, *file)) {
        return {ResultStatus::ErrorLoadingNRO, {}};
    }

    u64 program_id{};
    ReadProgramId(program_id);
    const std::string argv0 = MakeHomebrewArgv0(file->GetFullPath(), file->GetName());
    const std::string homebrew_initial_cwd = GetHomebrewInitialCwd(argv0);
    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), program_id,
        std::make_unique<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                system.GetFileSystemController()),
        homebrew_initial_cwd);

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NRO::ReadIcon(std::vector<u8>& buffer) {
    if (icon_data.empty()) {
        return ResultStatus::ErrorNoIcon;
    }

    buffer = icon_data;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadProgramId(u64& out_program_id) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    out_program_id = nacp->GetTitleId();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadRomFS(FileSys::VirtualFile& dir) {
    if (romfs == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    dir = romfs;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadTitle(std::string& title) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    title = nacp->GetApplicationName();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadControlData(FileSys::NACP& control) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    control = *nacp;
    return ResultStatus::Success;
}

bool AppLoader_NRO::IsRomFSUpdatable() const {
    return false;
}

} // namespace Loader
