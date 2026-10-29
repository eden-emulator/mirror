// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/file_sys/vfs/vfs_real.h"

#ifdef _WIN32
TEST_CASE("Real VFS evicts handles before reaching the Windows stdio limit", "[vfs-file-limit]") {
    struct StdioLimit {
        int previous = _getmaxstdio();
        ~StdioLimit() { _setmaxstdio(previous); }
    } limit;
    REQUIRE(_setmaxstdio(128) == 128);
    struct Directory {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("eden-vfs-limit-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Directory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } directory;
    REQUIRE(std::filesystem::create_directory(directory.path));
    constexpr size_t FileCount = 192;
    for (size_t i = 0; i < FileCount; ++i) {
        std::ofstream out(directory.path / std::to_string(i), std::ios::binary);
        REQUIRE(out.is_open());
        out.put(static_cast<char>(i));
    }
    FileSys::RealVfsFilesystem filesystem;
    std::vector<FileSys::VirtualFile> files;
    for (size_t i = 0; i < FileCount; ++i) {
        auto file = filesystem.OpenFile((directory.path / std::to_string(i)).string(),
                                        FileSys::OpenMode::Read);
        REQUIRE(file != nullptr);
        REQUIRE(file->ReadBytes(1) == std::vector<u8>{static_cast<u8>(i)});
        files.push_back(std::move(file));
    }
    // Retained VFS objects must reopen evicted handles without losing file identity.
    for (size_t i = 0; i < FileCount; ++i) {
        REQUIRE(files[i]->ReadBytes(1) == std::vector<u8>{static_cast<u8>(i)});
    }
}
#endif
