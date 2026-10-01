// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "core/file_sys/fsa/fs_i_file.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/file_sys/vfs/vfs_vector.h"

TEST_CASE("Guest file flush publishes buffered host writes before closing",
          "[core][filesystem][flush]") {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("eden-flush-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(directory));
    SCOPE_EXIT {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    };
    FileSys::RealVfsFilesystem filesystem;
    const auto path = directory / "test.bin";
    auto file = filesystem.CreateFile(path.string(), FileSys::OpenMode::ReadWrite);
    REQUIRE(file != nullptr);
    REQUIRE(file->Resize(3));
    FileSys::Fsa::IFile guest(file);
    const std::array<char, 3> expected{'a', 'b', 'c'};
    SECTION("Explicit Flush") {
        REQUIRE(guest.Write(0, expected.data(), expected.size(), FileSys::WriteOption::None) ==
                ResultSuccess);
        REQUIRE(guest.Flush() == ResultSuccess);
    }
    SECTION("Write with Flush option") {
        REQUIRE(guest.Write(0, expected.data(), expected.size(), FileSys::WriteOption::Flush) ==
                ResultSuccess);
    }
    SECTION("Zero-length Write with Flush option") {
        REQUIRE(guest.Write(0, expected.data(), expected.size(), FileSys::WriteOption::None) ==
                ResultSuccess);
        REQUIRE(guest.Write(0, nullptr, 0, FileSys::WriteOption::Flush) == ResultSuccess);
    }
    // A separate native handle must see the data while the guest handle stays open.
    std::ifstream reader(path, std::ios::binary);
    std::array<char, 3> observed{};
    reader.read(observed.data(), observed.size());
    REQUIRE(reader.gcount() == static_cast<std::streamsize>(expected.size()));
    REQUIRE(observed == expected);
}

TEST_CASE("Guest file flush propagates backing-file failure through wrappers",
          "[core][filesystem][flush]") {
    struct FailingFlushFile : FileSys::VectorVfsFile {
        bool Flush() override {
            return false;
        }
    };
    auto backing = std::make_shared<FailingFlushFile>();
    REQUIRE(backing->Resize(1));
    auto offset = std::make_shared<FileSys::OffsetVfsFile>(backing, 1);
    FileSys::Fsa::IFile guest(offset);
    const u8 value = 42;
    REQUIRE(guest.Flush() == ResultUnknown);
    REQUIRE(guest.Write(0, &value, 1, FileSys::WriteOption::Flush) == ResultUnknown);
    REQUIRE(guest.Write(0, nullptr, 0, FileSys::WriteOption::Flush) == ResultUnknown);
    REQUIRE(guest.Write(0, &value, 1, FileSys::WriteOption::None) == ResultSuccess);
}
