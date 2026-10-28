// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/swap.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/loader.h"

namespace {
template <typename T>
void Put(std::vector<u8>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<u8> MakeMetadata(bool extended) {
    std::vector<u8> bytes(0x600);
    std::memcpy(bytes.data(), "META", 4);
    bytes[0xC] = 7;
    Put(bytes, 0x1C, u32_le{0x100000});
    Put(bytes, 0x70, u32_le{0x500});
    Put(bytes, 0x74, u32_le{0x100});
    Put(bytes, 0x78, u32_le{0x80});
    Put(bytes, 0x7C, u32_le{0x380});
    const std::size_t acid = 0x80 + (extended ? 0x300 : 0x200);
    const u32 fac = extended ? 0x340 : 0x240;
    std::memcpy(bytes.data() + acid, "ACID", 4);
    Put(bytes, acid + 0x20, u32_le{fac});
    Put(bytes, acid + 0x24, u32_le{0x2C});
    Put(bytes, 0x80 + fac + 4, u64_le{0x1234});
    std::memcpy(bytes.data() + 0x500, "ACI0", 4);
    Put(bytes, 0x510, u64_le{0x0100123456780000});
    Put(bytes, 0x520, u32_le{0x40});
    Put(bytes, 0x524, u32_le{0x1C});
    Put(bytes, 0x530, u32_le{0x80});
    Put(bytes, 0x534, u32_le{4});
    Put(bytes, 0x544, u64_le{0x5678});
    Put(bytes, 0x580, u32_le{0x030043F7});
    return bytes;
}
} // namespace

TEST_CASE("Program metadata reads both ACID layouts and relative access offsets", "[loader][npdm]") {
    const bool extended = GENERATE(false, true);
    FileSys::ProgramMetadata metadata;
    REQUIRE(metadata.Load(std::make_shared<FileSys::VectorVfsFile>(MakeMetadata(extended))) ==
            Loader::ResultStatus::Success);
    REQUIRE(metadata.UsesExtendedAcid() == extended);
    REQUIRE(metadata.GetTitleID() == 0x0100123456780000);
    REQUIRE(metadata.GetFilesystemPermissions() == 0x5678);
    REQUIRE(metadata.GetMainThreadStackSize() == 0x100000);
    REQUIRE(metadata.GetKernelCapabilities() == std::vector<u32>{0x030043F7});
}

TEST_CASE("Program metadata rejects invalid sections", "[loader][npdm]") {
    const bool extended = GENERATE(false, true);
    auto bytes = MakeMetadata(extended);
    const std::size_t acid = 0x80 + (extended ? 0x300 : 0x200);
    Loader::ResultStatus expected{};
    SECTION("NPDM magic") {
        bytes[0] = 0;
        expected = Loader::ResultStatus::ErrorBadNPDMHeader;
    }
    SECTION("ACID past EOF") {
        Put(bytes, 0x78, u32_le{0xFFFFFFF0});
        expected = Loader::ResultStatus::ErrorBadACIDHeader;
    }
    SECTION("ACID magic") {
        bytes[acid] = 0;
        expected = Loader::ResultStatus::ErrorBadACIDHeader;
    }
    SECTION("ACI magic") {
        bytes[0x500] = 0;
        expected = Loader::ResultStatus::ErrorBadACIHeader;
    }
    SECTION("FAH outside its section") {
        Put(bytes, 0x520, u32_le{0xF0});
        expected = Loader::ResultStatus::ErrorBadFileAccessHeader;
    }
    SECTION("FAC outside its section") {
        Put(bytes, acid + 0x20, u32_le{0xFFFFFFF0});
        expected = Loader::ResultStatus::ErrorBadFileAccessControl;
    }
    SECTION("Truncated FAH") {
        Put(bytes, 0x524, u32_le{0x10});
        expected = Loader::ResultStatus::ErrorBadFileAccessHeader;
    }
    SECTION("Capabilities outside their section") {
        Put(bytes, 0x530, u32_le{0x100});
        expected = Loader::ResultStatus::ErrorBadKernelCapabilityDescriptors;
    }
    SECTION("Unaligned capability count") {
        Put(bytes, 0x534, u32_le{3});
        expected = Loader::ResultStatus::ErrorBadKernelCapabilityDescriptors;
    }
    FileSys::ProgramMetadata metadata;
    REQUIRE(metadata.Load(std::make_shared<FileSys::VectorVfsFile>(std::move(bytes))) == expected);
}

TEST_CASE("Reloading legacy metadata clears extended ACID state", "[loader][npdm]") {
    FileSys::ProgramMetadata metadata;
    REQUIRE(metadata.Load(std::make_shared<FileSys::VectorVfsFile>(MakeMetadata(true))) ==
            Loader::ResultStatus::Success);
    REQUIRE(metadata.UsesExtendedAcid());
    REQUIRE(metadata.Load(std::make_shared<FileSys::VectorVfsFile>(MakeMetadata(false))) ==
            Loader::ResultStatus::Success);
    REQUIRE_FALSE(metadata.UsesExtendedAcid());
    REQUIRE(metadata.Load(nullptr) == Loader::ResultStatus::ErrorBadNPDMHeader);
}

TEST_CASE("ExeFS main takes precedence over standalone NSO", "[loader][exefs]") {
    auto directory = std::make_shared<FileSys::VectorVfsDirectory>();
    auto main = std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>{'N', 'S', 'O', '0'},
                                                        "main", directory);
    directory->AddFile(main);
    directory->AddFile(std::make_shared<FileSys::VectorVfsFile>(MakeMetadata(false), "main.npdm"));
    REQUIRE(Loader::IdentifyFile(main) == Loader::FileType::DeconstructedRomDirectory);
    directory->DeleteFile("main.npdm");
    REQUIRE(Loader::AppLoader_DeconstructedRomDirectory::IdentifyType(main) == Loader::FileType::Error);
}

TEST_CASE("Extracted ExeFS exposes loose RomFS", "[loader][exefs]") {
    const bool sibling = GENERATE(false, true);
    auto root = std::make_shared<FileSys::VectorVfsDirectory>();
    auto exefs = std::make_shared<FileSys::VectorVfsDirectory>(
        std::vector<FileSys::VirtualFile>{}, std::vector<FileSys::VirtualDir>{}, "exefs", root);
    root->AddDirectory(exefs);
    auto main = std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>{'N', 'S', 'O', '0'},
                                                        "main", exefs);
    exefs->AddFile(main);
    exefs->AddFile(std::make_shared<FileSys::VectorVfsFile>(MakeMetadata(false), "main.npdm"));
    auto romfs = std::make_shared<FileSys::VectorVfsDirectory>(
        std::vector<FileSys::VirtualFile>{}, std::vector<FileSys::VirtualDir>{}, "romfs");
    romfs->AddFile(std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>{1, 2, 3}, "asset.bin"));
    (sibling ? root : exefs)->AddDirectory(romfs);
    Loader::AppLoader_DeconstructedRomDirectory loader(main);
    FileSys::VirtualFile image;
    REQUIRE(loader.ReadRomFS(image) == Loader::ResultStatus::Success);
    const auto extracted = FileSys::ExtractRomFS(image);
    REQUIRE(extracted != nullptr);
    const auto asset = extracted->GetFile("asset.bin");
    REQUIRE(asset != nullptr);
    REQUIRE(asset->ReadAllBytes() == std::vector<u8>{1, 2, 3});
}
