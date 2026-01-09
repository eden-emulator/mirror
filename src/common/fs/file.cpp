// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#ifdef ANDROID
#include "common/fs/fs_android.h"
#endif
#include "common/logging/log.h"
#include "common/literals.h"

#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#ifdef _MSC_VER
#define fileno _fileno
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

namespace Common::FS {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32

/**
* Converts the file access mode and file type enums to a file access mode wide string.
*
* @param mode File access mode
* @param type File type
*
* @returns A pointer to a wide string representing the file access mode.
*/
[[nodiscard]] constexpr const wchar_t* AccessModeToWStr(FileAccessMode mode, FileType type) {
    switch (type) {
    case FileType::BinaryFile:
        switch (mode) {
        case FileAccessMode::Read:
            return L"rb";
        case FileAccessMode::Write:
            return L"wb";
        case FileAccessMode::Append:
            return L"ab";
        case FileAccessMode::ReadWrite:
            return L"r+b";
        case FileAccessMode::ReadAppend:
            return L"a+b";
        }
        break;
    case FileType::TextFile:
        switch (mode) {
        case FileAccessMode::Read:
            return L"r";
        case FileAccessMode::Write:
            return L"w";
        case FileAccessMode::Append:
            return L"a";
        case FileAccessMode::ReadWrite:
            return L"r+";
        case FileAccessMode::ReadAppend:
            return L"a+";
        }
        break;
    }

    return L"";
}

/**
* Converts the file-share access flag enum to a Windows defined file-share access flag.
*
* @param flag File-share access flag
*
* @returns Windows defined file-share access flag.
*/
[[nodiscard]] constexpr int ToWindowsFileShareFlag(FileShareFlag flag) {
    switch (flag) {
    case FileShareFlag::ShareNone:
    default:
        return _SH_DENYRW;
    case FileShareFlag::ShareReadOnly:
        return _SH_DENYWR;
    case FileShareFlag::ShareWriteOnly:
        return _SH_DENYRD;
    case FileShareFlag::ShareReadWrite:
        return _SH_DENYNO;
    }
}

#else

/**
* Converts the file access mode and file type enums to a file access mode string.
*
* @param mode File access mode
* @param type File type
*
* @returns A pointer to a string representing the file access mode.
*/
[[nodiscard]] constexpr const char* AccessModeToStr(FileAccessMode mode, FileType type) {
    switch (type) {
    case FileType::BinaryFile:
        switch (mode) {
        case FileAccessMode::Read:
            return "rb";
        case FileAccessMode::Write:
            return "wb";
        case FileAccessMode::Append:
            return "ab";
        case FileAccessMode::ReadWrite:
            return "r+b";
        case FileAccessMode::ReadAppend:
            return "a+b";
        }
        break;
    case FileType::TextFile:
        switch (mode) {
        case FileAccessMode::Read:
            return "r";
        case FileAccessMode::Write:
            return "w";
        case FileAccessMode::Append:
            return "a";
        case FileAccessMode::ReadWrite:
            return "r+";
        case FileAccessMode::ReadAppend:
            return "a+";
        }
        break;
    }

    return "";
}

#endif

/**
* Converts the seek origin enum to a seek origin integer.
*
* @param origin Seek origin
*
* @returns Seek origin integer.
*/
[[nodiscard]] constexpr int ToSeekOrigin(SeekOrigin origin) {
    switch (origin) {
    case SeekOrigin::SetOrigin:
    default:
        return SEEK_SET;
    case SeekOrigin::CurrentPosition:
        return SEEK_CUR;
    case SeekOrigin::End:
        return SEEK_END;
    }
}

} // Anonymous namespace

std::string ReadStringFromFile(const std::filesystem::path& path, FileType type) {
    if (!IsFile(path)) {
        return "";
    }

    IOFile io_file{path, FileAccessMode::Read, type};

    return io_file.ReadString(io_file.GetSize());
}

size_t WriteStringToFile(const std::filesystem::path& path, FileType type,
                        std::string_view string) {
    if (Exists(path) && !IsFile(path)) {
        return 0;
    }

    IOFile io_file{path, FileAccessMode::Write, type};

    return io_file.WriteString(string);
}

size_t AppendStringToFile(const std::filesystem::path& path, FileType type,
                        std::string_view string) {
    if (Exists(path) && !IsFile(path)) {
        return 0;
    }

    IOFile io_file{path, FileAccessMode::Append, type};

    return io_file.WriteString(string);
}

IOFile::IOFile() = default;

IOFile::IOFile(const std::string& path, FileAccessMode mode, FileType type, FileShareFlag flag) {
    Open(path, mode, type, flag);
}

IOFile::IOFile(std::string_view path, FileAccessMode mode, FileType type, FileShareFlag flag) {
    Open(path, mode, type, flag);
}

IOFile::IOFile(const fs::path& path, FileAccessMode mode, FileType type, FileShareFlag flag) {
    Open(path, mode, type, flag);
}

IOFile::~IOFile() {
    Close();
}

IOFile::IOFile(IOFile&& other) noexcept {
    std::swap(file_path, other.file_path);
    std::swap(file_access_mode, other.file_access_mode);
    std::swap(file_type, other.file_type);
    std::swap(file, other.file);
}

IOFile& IOFile::operator=(IOFile&& other) noexcept {
    std::swap(file_path, other.file_path);
    std::swap(file_access_mode, other.file_access_mode);
    std::swap(file_type, other.file_type);
    std::swap(file, other.file);
    return *this;
}

fs::path IOFile::GetPath() const {
    return file_path;
}

FileAccessMode IOFile::GetAccessMode() const {
    return file_access_mode;
}

FileType IOFile::GetType() const {
    return file_type;
}

#if defined(__unix__)
static int PlatformMapReadOnly(IOFile& io, const char* path) {
    io.mmap_fd = open(path, O_RDONLY);
    if (io.mmap_fd > 0) {
        struct stat st;
        fstat(io.mmap_fd, &st);
        io.mmap_size = st.st_size;

        int map_flags = MAP_PRIVATE;
#ifdef MAP_PREFAULT_READ
        // Prefaults reads so the final resulting pagetable from this big stupid mmap()
        // isn't comically lazily loaded, we just coalesce everything in-place for our
        // lovely mmap flags; if we didn't prefault the reads the page table will be
        // constructed in-place (i.e on a read-by-read basis) causing lovely soft-faults
        // which would nuke any performance gains.
        //
        // This of course incurs a cost in the initial mmap(2) call, but that is fine.
        map_flags |= MAP_PREFAULT_READ;
#endif
#ifdef MAP_NOSYNC
        // This causes physical media to not be synched to our file/memory
        // This means that if the read-only file is written to, we won't see changes
        // or we may see changes which are just funnily scattered, in any case
        // this presumes the files won't be changed during execution
        //
        // Do not ever use this on write files (if we ever support that); this will create
        // a fun amount of fragmentation on the disk.
        map_flags |= MAP_NOSYNC;
#endif
#ifdef MAP_ALIGNED_SUPER
        // File must be big enough that it's worth to super align. We can't just super-align every
        // file otherwise we will run out of alignments for actually important files :)
        // System doesn't guarantee a super alignment, but if it's available it will delete
        // about 3 layers(?) of the TLB tree for each read/write.
        // Again the cost of faults may make this negligible gains, but hey, we gotta work
        // what we gotta work with.
        using namespace Common::Literals;
        u64 big_file_threshold = 512_MiB;
        map_flags |= u64(st.st_size) >= big_file_threshold ? MAP_ALIGNED_SUPER : 0;
#endif
        io.mmap_base = (u8*)mmap(nullptr, io.mmap_size, PROT_READ, map_flags, io.mmap_fd, 0);
        if (io.mmap_base == MAP_FAILED) {
            close(io.mmap_fd);
            io.mmap_fd = -1;
        } else {
            posix_madvise(io.mmap_base, io.mmap_size, POSIX_MADV_WILLNEED);
        }
    }
    return io.mmap_fd;
}
static void PlatformUnmap(IOFile& io) {
    if (io.mmap_fd != -1) {
        munmap(io.mmap_base, io.mmap_size);
        close(io.mmap_fd);
        io.mmap_fd = -1;
    }
}
#elif defined(__APPLE__)
// NO IMPLEMENTATION YET
#else
static int PlatformMapReadOnly(IOFile& io, const wchar_t* path) {
    io.file_handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (HANDLE(io.file_handle) != INVALID_HANDLE_VALUE) {
        io.mapping_handle = CreateFileMappingW(HANDLE(io.file_handle), nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (io.mapping_handle) {
            io.mmap_base = (u8*)MapViewOfFile(HANDLE(io.mapping_handle), FILE_MAP_READ, 0, 0, 0);
            if (io.mmap_base) {
                _LARGE_INTEGER pvalue;
                GetFileSizeEx(io.file_handle, &pvalue);
                io.mmap_size = uint32_t(pvalue.QuadPart);
            } else {
                CloseHandle(io.mapping_handle);
                CloseHandle(io.file_handle);
                return -1;
            }
        } else {
            CloseHandle(io.file_handle);
            return -1;
        }
    }
    return 0;
}
static void PlatformUnmap(IOFile& io) {
    if(io.mapping_handle) {
        if(io.mmap_base)
            UnmapViewOfFile(HANDLE(io.mmap_base));
        CloseHandle(HANDLE(io.mapping_handle));
    }
    if(io.file_handle != INVALID_HANDLE_VALUE)
        CloseHandle(HANDLE(io.file_handle));
}
#endif

void IOFile::Open(const fs::path& path, FileAccessMode mode, FileType type, FileShareFlag flag) {
    Close();
    file_path = path;
    file_access_mode = mode;
    file_type = type;
    errno = 0;
#ifdef _WIN32
    // TODO: this probably can use better logic but oh well I'm not a windowser
    file_handle = nullptr;
    if (type == FileType::BinaryFile && mode == FileAccessMode::Read) {
        if (PlatformMapReadOnly(*this, path.c_str()) == -1) {
            LOG_ERROR(Common_Filesystem, "Error mmap'ing file"); //: {}", path.c_str());
        }
    }
    if (file_handle == nullptr) {
        if (flag != FileShareFlag::ShareNone) {
            file = _wfsopen(path.c_str(), AccessModeToWStr(mode, type), ToWindowsFileShareFlag(flag));
        } else {
            _wfopen_s(&file, path.c_str(), AccessModeToWStr(mode, type));
        }
    }
#elif ANDROID
    if (Android::IsContentUri(path)) {
        ASSERT_MSG(mode == FileAccessMode::Read, "Content URI file access is for read-only!");
        if (PlatformMapReadOnly(*this, path.c_str()) == -1) {
            LOG_ERROR(Common_Filesystem, "Error mmap'ing file: {}", path.c_str());
            int const fd = Android::OpenContentUri(path, Android::OpenMode::Read);
            if (fd != -1) {
                file = fdopen(fd, "r");
                if (errno != 0 && file == nullptr)
                    LOG_ERROR(Common_Filesystem, "Error opening file: {}, error: {}", path.c_str(), strerror(errno));
            } else {
                LOG_ERROR(Common_Filesystem, "Error opening file: {}", path.c_str());
            }
        }
    } else {
        file = std::fopen(path.c_str(), AccessModeToStr(mode, type));
    }
#elif defined(__HAIKU__) || defined(__managarm__) || defined(__OPENORBIS__) || defined(__APPLE__)
    file = std::fopen(path.c_str(), AccessModeToStr(mode, type));
#elif defined(__unix__)
    if (type == FileType::BinaryFile && mode == FileAccessMode::Read) {
        if (PlatformMapReadOnly(*this, path.c_str()) == -1) {
            LOG_ERROR(Common_Filesystem, "Error mmap'ing file: {}", path.c_str());
        }
    }
    if (mmap_fd == -1) {
        file = std::fopen(path.c_str(), AccessModeToStr(mode, type)); // mmap(2) failed or simply we can't use it
    }
#else
    // Some other fancy OS (ahem fucking Darwin/Mac OSX)
    file = std::fopen(path.c_str(), AccessModeToStr(mode, type));
#endif
    if (!IsOpen()) {
        const auto ec = std::error_code{errno, std::generic_category()};
        LOG_ERROR(Common_Filesystem, "Failed to open the file at path={}, ec_message={}",
            PathToUTF8String(file_path), ec.message());
    }
}

void IOFile::Close() {
#if defined(__APPLE__)
    // NO IMPLEMENTATION YET
#else
    PlatformUnmap(*this);
#endif
    if (file) {
        errno = 0;
        const auto close_result = std::fclose(file) == 0;
        if (!close_result) {
            const auto ec = std::error_code{errno, std::generic_category()};
            LOG_ERROR(Common_Filesystem, "Failed to close the file at path={}, ec_message={}",
                PathToUTF8String(file_path), ec.message());
        }
        file = nullptr;
    }
}

bool IOFile::IsOpen() const {
    return file != nullptr || IsMappedFile();
}

std::string IOFile::ReadString(size_t length) const {
    std::vector<char> string_buffer(length);

    const auto chars_read = ReadSpan<char>(string_buffer);
    const auto string_size = chars_read != length ? chars_read : length;

    return std::string{string_buffer.data(), string_size};
}

size_t IOFile::WriteString(std::span<const char> string) const {
    return WriteSpan(string);
}

bool IOFile::Flush() const {
    ASSERT(!IsMappedFile());
    if (file) {
        errno = 0;
        auto const flush_result = std::fflush(file) == 0;
        if (!flush_result) {
            const auto ec = std::error_code{errno, std::generic_category()};
            LOG_ERROR(Common_Filesystem, "Failed to flush the file at path={}, ec_message={}",
                PathToUTF8String(file_path), ec.message());
        }
        return flush_result;
    }
    return false;
}

bool IOFile::Commit() const {
    ASSERT(!IsMappedFile());
    if (file) {
        errno = 0;
#ifdef _WIN32
        const auto commit_result = std::fflush(file) == 0 && _commit(fileno(file)) == 0;
#else
        const auto commit_result = std::fflush(file) == 0 && fsync(fileno(file)) == 0;
#endif
        if (!commit_result) {
            const auto ec = std::error_code{errno, std::generic_category()};
            LOG_ERROR(Common_Filesystem, "Failed to commit the file at path={}, ec_message={}",
                PathToUTF8String(file_path), ec.message());
        }
        return commit_result;
    }
    return false;
}

bool IOFile::SetSize(u64 size) const {
    ASSERT(!IsMappedFile());
    if (file) {
        errno = 0;
#ifdef _WIN32
        const auto set_size_result = _chsize_s(fileno(file), s64(size)) == 0;
#else
        const auto set_size_result = ftruncate(fileno(file), s64(size)) == 0;
#endif
        if (!set_size_result) {
            const auto ec = std::error_code{errno, std::generic_category()};
            LOG_ERROR(Common_Filesystem, "Failed to resize the file at path={}, size={}, ec_message={}",
                PathToUTF8String(file_path), size, ec.message());
        }
        return set_size_result;
    }
    return false;
}

u64 IOFile::GetSize() const {
    if (IsMappedFile())
        return mmap_size;
    if (file) {
        // Flush any unwritten buffered data into the file prior to retrieving the file mmap_size.
        std::fflush(file);
#if ANDROID
        u64 file_size = 0;
        if (Android::IsContentUri(file_path)) {
            file_size = Android::GetSize(file_path);
        } else {
            std::error_code ec;

            file_size = fs::file_size(file_path, ec);

            if (ec) {
                LOG_ERROR(Common_Filesystem, "Failed to retrieve the file mmap_size of path={}, ec_message={}",
                    PathToUTF8String(file_path), ec.message());
                return 0;
            }
        }
#else
        std::error_code ec;
        auto const file_size = fs::file_size(file_path, ec);
        if (ec) {
            LOG_ERROR(Common_Filesystem, "Failed to retrieve the file mmap_size of path={}, ec_message={}",
                PathToUTF8String(file_path), ec.message());
            return 0;
        }
#endif
        return file_size;
    }
    return 0;
}

bool IOFile::Seek(s64 offset, SeekOrigin origin) const {
    if (IsMappedFile()) {
        // fuck you to whoever made this method const
        switch (origin) {
        case SeekOrigin::SetOrigin:
            mmap_offset = s64(offset);
            break;
        case SeekOrigin::CurrentPosition:
            mmap_offset += s64(offset);
            break;
        case SeekOrigin::End:
            mmap_offset = s64(mmap_size) + s64(offset);
            break;
        }
        return true;
    }
    if (file) {
        errno = 0;
        const auto seek_result = fseeko(file, offset, ToSeekOrigin(origin)) == 0;
        if (!seek_result) {
            const auto ec = std::error_code{errno, std::generic_category()};
            LOG_ERROR(Common_Filesystem, "Failed to seek the file at path={}, offset={}, origin={}, ec_message={}",
                PathToUTF8String(file_path), offset, origin, ec.message());
        }
        return seek_result;
    }
    return false;
}

s64 IOFile::Tell() const {
    if (IsMappedFile()) {
        errno = 0;
        return s64(mmap_offset);
    }
    if (file) {
        errno = 0;
        return ftello(file);
    }
    return 0;
}

} // namespace Common::FS
