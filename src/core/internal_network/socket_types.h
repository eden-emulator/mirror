// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

#include "common/common_types.h"
#include "common/common_funcs.h"

// Most of these structures are direct mappings of guest's
// expectations for these values, in other words, they're the
// values that HOS is expected to use AND handle.

namespace Network {

enum class Errno : u32 {
    SUCCESS = 0,
    BADF = 9,
    AGAIN = 11,
    INVAL = 22,
    MFILE = 24,
    PIPE = 32,
    MSGSIZE = 90,
    ADDRINUSE = 98,
    NETDOWN = 100,
    NETUNREACH = 101,
    CONNABORTED = 103,
    CONNRESET = 104,
    ISCONN = 106,
    NOTCONN = 107,
    TIMEDOUT = 110,
    CONNREFUSED = 111,
    HOSTUNREACH = 113,
    INPROGRESS = 115,
    /* made up error? */
    OTHER = 196,
};

enum class GetAddrInfoError : s32 {
    SUCCESS = 0,
    ADDRFAMILY = 1,
    AGAIN = 2,
    BADFLAGS = 3,
    FAIL = 4,
    FAMILY = 5,
    MEMORY = 6,
    NODATA = 7,
    NONAME = 8,
    SERVICE = 9,
    SOCKTYPE = 10,
    SYSTEM = 11,
    BADHINTS = 12,
    PROTOCOL = 13,
    OVERFLOW_ = 14, // avoid name collision with Windows macro
    OTHER = 15,
};

enum class Domain : u32 {
    Unspecified = 0,
    UNIX = 1,
    INET = 2,
    IMPLINK = 3,
    PUP = 4,
    CHAOS = 5,
    NETBIOS = 6,
    ISO = 7,
    ECMA = 8,
    DATAKIT = 9,
    CCITT = 10,
    SNA = 11,
    DECnet = 12,
    DLI = 13,
    LAT = 14,
    HYLINK = 15,
    APPLETALK = 16,
    ROUTE = 17,
    LINK = 18,
    COIP = 20,
    CNT = 21,
    IPX = 23,
    SIP = 24,
    ISDN = 26,
    INET6 = 28,
    NATM = 29,
    ATM = 30,
    NETGRAPH = 32,
    SLOW = 33,
    SCLUSTER = 34,
    ARP = 35,
    BLUETOOTH = 36,
    IEEE80211 = 37,
    NETLINK = 38,
    INET_SDP = 40,
    INET6_SDP = 42,
};

enum class Type : u32 {
    Unspecified = 0,
    STREAM = 1,
    DGRAM = 2,
    RAW = 3,
    RDM = 4,
    SEQPACKET = 5,
};

enum class Protocol : u32 {
    IP = 0,
    ICMP = 1,
    TCP = 6,
    UDP = 17,
    //
    IPV6 = 41,
    RAW = 255,
    //
    HOPOPTS = 0,
    IGMP = 2,
    GGP = 3,
    IPV4 = 4,
    ST = 7,
    EGP = 8,
    PIGP = 9,
    RCCMON = 10,
    NVPII = 11,
    PUP = 12,
    ARGUS = 13,
    EMCON = 14,
    XNET = 15,
    CHAOS = 16,
    MUX = 18,
    MEAS = 19,
    HMP = 20,
    PRM = 21,
    IDP = 22,
    TRUNK1 = 23,
    TRUNK2 = 24,
    LEAF1 = 25,
    LEAF2 = 26,
    RDP = 27,
    IRTP = 28,
    TP = 29,
    BLT = 30,
    NSP = 31,
    INP = 32,
    DCCP = 33,
    //3PC = 34,
    IDPR = 35,
    XTP = 36,
    DDP = 37,
    CMTP = 38,
    TPXX = 39,
    IL = 40,
    SDRP = 42,
    ROUTING = 43,
    FRAGMENT = 44,
    IDRP = 45,
    RSVP = 46,
    GRE = 47,
    MHRP = 48,
    BHA = 49,
    ESP = 50,
    AH = 51,
    INLSP = 52,
    SWIPE = 53,
    NHRP = 54,
    MOBILE = 55,
    TLSP = 56,
    SKIP = 57,
    ICMPV6 = 58,
    NONE = 59,
    DSTOPTS = 60,
    AHIP = 61,
    CFTP = 62,
    HELLO = 63,
    SATEXPAK = 64,
    KRYPTOLAN = 65,
    RVD = 66,
    IPPC = 67,
    ADFS = 68,
    SATMON = 69,
    VISA = 70,
    IPCV = 71,
    CPNX = 72,
    CPHB = 73,
    WSN = 74,
    PVP = 75,
    BRSATMON = 76,
    ND = 77,
    WBMON = 78,
    WBEXPAK = 79,
    EON = 80,
    VMTP = 81,
    SVMTP = 82,
    VINES = 83,
    TTP = 84,
    IGP = 85,
    DGP = 86,
    TCF = 87,
    IGRP = 88,
    OSPFIGP = 89,
    SRPC = 90,
    LARP = 91,
    MTP = 92,
    AX25 = 93,
    IPEIP = 94,
    MICP = 95,
    SCCSP = 96,
    ETHERIP = 97,
    ENCAP = 98,
    APES = 99,
    GMTP = 100,
    IPCOMP = 108,
    SCTP = 132,
    MH = 135,
    UDPLITE = 136,
    HIP = 139,
    SHIM6 = 140,
    PIM = 103,
    CARP = 112,
    PGM = 113,
    MPLS = 137,
    PFSYNC = 240,
};

enum class SocketLevel : u32 {
    IP = 0,
    TCP = 6,
    SOCKET = 0xffff, // i.e. SOL_SOCKET
};

enum class OptName : u32 {
    REUSEADDR = 0x4,
    KEEPALIVE = 0x8,
    BROADCAST = 0x20,
    LINGER = 0x80,
    SNDBUF = 0x1001,
    RCVBUF = 0x1002,
    SNDTIMEO = 0x1005,
    RCVTIMEO = 0x1006,
    ERROR_ = 0x1007,   // avoid name collision with Windows macro
    NOSIGPIPE = 0x800, // at least according to libnx
    ACCEPTFILTER = 0x1000,
    BINTIME = 0x2000,
    NO_OFFLOAD = 0x4000,
    NO_DDP = 0x8000,
};

enum class ShutdownHow : s32 {
    RD = 0,
    WR = 1,
    RDWR = 2,
};

enum class FcntlCmd : s32 {
    GETFL = 3,
    SETFL = 4,
};

/// Array of IPv4 address
using IPv4Address = std::array<u8, 4>;

struct SockAddrIn {
    u8 len;
    u8 family;
    u16 portno;
    IPv4Address ip;
    std::array<u8, 248> zeroes;
};
static_assert(sizeof(SockAddrIn) == 0x100);

enum class PollEvents : u16 {
    // Using Pascal case because IN is a macro on Windows.
    In = 1 << 0,
    Pri = 1 << 1,
    Out = 1 << 2,
    Err = 1 << 3,
    Hup = 1 << 4,
    Nval = 1 << 5,
    RdNorm = 1 << 6,
    RdBand = 1 << 7,
    WrBand = 1 << 8,
};
DECLARE_ENUM_FLAG_OPERATORS(PollEvents);

struct PollFD {
    s32 fd;
    Network::PollEvents events;
    Network::PollEvents revents;
};

struct Linger {
    u32 onoff;
    u32 linger;
};

constexpr u32 FLAG_MSG_PEEK = 0x2;
constexpr u32 FLAG_MSG_DONTWAIT = 0x80;
constexpr u32 FLAG_O_NONBLOCK = 0x800;

/// @brief Cross-platform addrinfo structure (not guest)
struct AddrInfo {
    Domain family;
    Type socket_type;
    Protocol protocol;
    SockAddrIn addr;
    std::optional<std::string> canon_name;
};

} // namespace Network
