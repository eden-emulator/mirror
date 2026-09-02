// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <span>
#include <cctype>
#include "common/container/unordered_map.h"

#include "common/hex_util.h"
#include "common/logging.h"
#include "common/swap.h"
#include "core/file_sys/ips_layer.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace FileSys {

enum class IPSFileType {
    IPS,
    IPS32,
    Error,
};

static IPSFileType IdentifyMagic(std::span<const u8> magic) {
    if (magic.size() >= 5) {
        if (std::memcmp(magic.data(), "PATCH", 5) == 0)
            return IPSFileType::IPS;
        if (std::memcmp(magic.data(), "IPS32", 5) == 0)
            return IPSFileType::IPS32;
    }
    return IPSFileType::Error;
}

static bool IsEOF(IPSFileType type, std::span<const u8> magic) {
    return (type == IPSFileType::IPS && magic.size() > 3 && std::memcmp(magic.data(), "EOF", 3) == 0)
        || (type == IPSFileType::IPS32 && magic.size() > 4 && std::memcmp(magic.data(), "EEOF", 4) == 0);
}

VirtualFile PatchIPS(const VirtualFile& in, const VirtualFile& ips) {
    if (in == nullptr || ips == nullptr)
        return nullptr;

    auto in_data = in->ReadAllBytes();
    auto const type = IdentifyMagic(in_data);
    if (type == IPSFileType::Error)
        return nullptr;

    std::vector<u8> temp(type == IPSFileType::IPS ? 3 : 4);
    u64 offset = 5; // After header
    while (ips->Read(temp.data(), temp.size(), offset) == temp.size()) {
        offset += temp.size();
        if (IsEOF(type, temp)) {
            break;
        }

        u32 real_offset = (type == IPSFileType::IPS32)
            ? ((temp[0] << 24) | (temp[1] << 16) | (temp[2] << 8) | temp[3])
            : ((temp[0] << 16) | (temp[1] << 8) | temp[2]);
        if (real_offset > in_data.size()) {
            return nullptr;
        }

        u16 data_size{};
        if (ips->ReadObject(&data_size, offset) != sizeof(u16))
            return nullptr;
        data_size = Common::swap16(data_size);
        offset += sizeof(u16);

        if (data_size == 0) { // RLE
            u16 rle_size{};
            if (ips->ReadObject(&rle_size, offset) != sizeof(u16))
                return nullptr;
            rle_size = Common::swap16(rle_size);
            offset += sizeof(u16);

            const auto data = ips->ReadByte(offset++);
            if (!data)
                return nullptr;

            if (real_offset + rle_size > in_data.size())
                rle_size = u16(in_data.size() - real_offset);
            std::memset(in_data.data() + real_offset, *data, rle_size);
        } else { // Standard Patch
            auto read = data_size;
            if (real_offset + read > in_data.size())
                read = u16(in_data.size() - real_offset);
            if (ips->Read(in_data.data() + real_offset, read, offset) != data_size)
                return nullptr;
            offset += data_size;
        }
    }
    if (IsEOF(type, temp)) {
        return std::make_shared<VectorVfsFile>(std::move(in_data), in->GetName(), in->GetContainingDirectory());
    }
    return nullptr;
}


struct IPSwitchRecord {
    std::array<uint8_t, 256 - sizeof(size_t)> data;
    size_t count;
};
struct IPSwitchCompiler::IPSwitchPatch {
    ::Common::unordered_map<u32, IPSwitchRecord> records;
    bool enabled;
};

IPSwitchCompiler::IPSwitchCompiler(VirtualFile patch_text_) : patch_text(std::move(patch_text_)) {
    Parse(patch_text->ReadAllBytes());
}

IPSwitchCompiler::~IPSwitchCompiler() = default;

std::array<u8, 32> IPSwitchCompiler::GetBuildID() const {
    return nso_build_id;
}

static IPSwitchRecord EscapeStringSequences(std::string_view sv) {
    IPSwitchRecord r{};
    for (auto it = sv.cbegin(); it != sv.cend(); ) {
        if (*it == '\\' && it + 1 < sv.cend()) {
            switch (it[1]) {
            case 'a': r.data[r.count] = '\a'; break;
            case 'b': r.data[r.count] = '\b'; break;
            case 'e': r.data[r.count] = '\e'; break;
            case 'f': r.data[r.count] = '\f'; break;
            case 'n': r.data[r.count] = '\n'; break;
            case 'r': r.data[r.count] = '\r'; break;
            case 't': r.data[r.count] = '\t'; break;
            case 'v': r.data[r.count] = '\v'; break;
            case '?': r.data[r.count] = '\?'; break;
            default: r.data[r.count] = it[1]; break;
            }
            ++r.count;
            it += 2;
        } else {
            ++r.count;
            ++it;
        }
    }
    return r;
}

void IPSwitchCompiler::Parse(std::span<u8 const> bytes) {
    LOG_INFO(Loader, "IPSwitchCompiler: '{}'", patch_text->GetName());
    bool is_little_endian = true;
    s64 offset_shift = 0;
    //bool print_values = false;
    auto const parse_line = [&](std::string_view const line) {
        // Keep in mind lines have trimmed spaces (at the end & start)!
        LOG_INFO(Loader, "<{}>", line);
        // IPSwitch is case insensitive
        // Yes this is how the logic goes for the main reference parsers!
        if (line.size() > 2 && line[0] == '@') {
            switch (line[1]) {
            // yes, @nsobid too -- NSO Build ID Specifier
            case 'n':
            case 'N':
                nso_build_id = Common::HexStringToArray<0x20>(fmt::format("{:0<64}", line.substr(8)));
                break;
            // @stop
            case 's':
            case 'S':
                return false;
            // @enabled
            case 'e':
            case 'E':
                patches.push_back({{}, true});
                break;
            // @disabled
            case 'd':
            case 'D':
                patches.push_back({{}, false});
                break;
            // @flag
            case 'f':
            case 'F': {
                if (line.starts_with("@flag offset_shift")) {
                    offset_shift = std::strtoll(line.data() + 19, nullptr, 0);  // Offset Shift Flag
                } else if (line.starts_with("@flag print_values")) {
                    //print_values = true; // Force printing of applied values
                }
                break;
            }
            case 'l':
            case 'L':
                is_little_endian = true;
                break;
            // IPS parsers dont support big endian no more, we do due to backcompat
            case 'b':
            case 'B':
                is_little_endian = false;
                break;
            default:
                LOG_WARNING(Loader, "Unknown flag {}", line);
                break;
            }
        } else {
            size_t offset = size_t(std::strtoul(line.data(), nullptr, 16));
            offset += size_t(offset_shift);
            if (auto const first_quote = line.find_first_of("\"\'"); first_quote != std::string::npos) {
                // string replacement
                char quote = line[first_quote];
                auto const start = line.cbegin() + first_quote + 1;
                auto end = start;
                for (; end < line.cend() && *end != quote; )
                    end += (*end == '\\') ? 2 : 1;
                if (start <= line.cend() && end <= line.cend()) {
                    LOG_INFO(Loader, "[S] value @ {:#08X} ", offset);
                    patches.back().records.insert_or_assign(u32(offset), EscapeStringSequences({start, end}));
                } else {
                    LOG_WARNING(Loader, "invalid string");
                }
            } else if (auto const first_space = line.find_last_of(" /\t\r\n"); first_space != std::string::npos) {
                IPSwitchRecord r{}; // hex replacement
                auto const start = line.cbegin() + first_space + 1;
                auto const end = line.cend();
                if (start <= line.cend() && end <= line.cend()) {
                    // Actually IPS wants ordering from {lsb, ..., msb} -- so LE and BE are inverted, fun!
                    auto const hs = Common::HexStringToVector({start, end}, is_little_endian);
                    std::memcpy(r.data.data(), hs.data(), hs.size());
                    r.count = hs.size();
                    LOG_INFO(Loader, "[H] value @ {:#08X}", offset);
                    patches.back().records.insert_or_assign(u32(offset), std::move(r));
                } else {
                    LOG_WARNING(Loader, "invalid line");
                }
            } else {
                LOG_WARNING(Loader, "unhandled line!");
            }
        }
        return true; //continue
    };

    for (auto it = bytes.begin(); it < bytes.end(); ) {
        auto const start = it;
        auto end = start;
        for (; end < bytes.end() && *end != '\n' && *end != '\r'; ++end)
            ;
        it = end + 1; //prepare for next line
        std::string_view const sline{
            reinterpret_cast<const char*>(bytes.data() + std::distance(bytes.begin(), start)),
            size_t(std::distance(start, end))
        };
        if (sline.size() > 0) {
            auto p = sline.cbegin();
            // skip space off line
            for (; p < sline.cend() && std::isspace(*p); ++p)
                ;
            // now make a nominal preprocessed line: remove comments
            char quote = '\0';
            auto const sline_start = p;
            auto last_char = p;
            for (; p < sline.cend(); ) {
                // we dont check for "//", IPS checks for '/' only...
                if (std::isspace(*p)) {
                    ++p;
                } else if ((!quote && p[0] == '/')
                || (!quote && p[0] == '#')) {
                    break;
                } else if (p[0] == '\"' || p[0] == '\'') {
                    quote = (p[0] == quote) ? '\0' : p[0];
                    ++p;
                    last_char = p;
                } else if (p + 1 < sline.cend() && p[0] == '\\') {
                    p += 2;
                    last_char = p;
                } else {
                    ++p;
                    last_char = p;
                }
            }
            // now we have the preprocessed string ;)
            std::string_view const pp_str(sline_start, last_char);
            if (pp_str.size() > 0 && !parse_line(pp_str))
                break;
        }
    }
}

VirtualFile IPSwitchCompiler::Apply(const VirtualFile& in) const {
    if (in == nullptr)
        return nullptr;

    auto in_data = in->ReadAllBytes();

    for (const auto& patch : patches) {
        if (patch.enabled) {
            for (const auto& record : patch.records) {
                if (record.first < in_data.size()) {
                    auto replace_size = record.second.count;
                    if (record.first + replace_size > in_data.size())
                        replace_size = in_data.size() - record.first;
                    std::memcpy(in_data.data() + record.first, record.second.data.data(), replace_size);
                } else {
                    LOG_WARNING(Loader, "record offs={:x},size={:x}", record.first, record.second.data.size());
                }
            }
        }
    }
    return std::make_shared<VectorVfsFile>(std::move(in_data), in->GetName(), in->GetContainingDirectory());
}

} // namespace FileSys
