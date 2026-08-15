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
    E_SUCCESS = 0,
    E_PERM = 1,
    E_NOENT = 2,
    E_SRCH = 3,
    E_INTR = 4,
    E_IO = 5,
    E_NXIO = 6,
    E_2BIG = 7,
    E_NOEXEC = 8,
    E_BADF = 9,
    E_CHILD = 10,
    E_AGAIN = 11,
    E_NOMEM = 12,
    E_ACCES = 13,
    E_FAULT = 14,
    E_NOTBLK = 15,
    E_BUSY = 16,
    E_EXIST = 17,
    E_XDEV = 18,
    E_NODEV = 19,
    E_NOTDIR = 20,
    E_ISDIR = 21,
    E_INVAL = 22,
    E_NFILE = 23,
    E_MFILE = 24,
    E_NOTTY = 25,
    E_TXTBSY = 26,
    E_FBIG = 27,
    E_NOSPC = 28,
    E_SPIPE = 29,
    E_ROFS = 30,
    E_MLINK = 31,
    E_PIPE = 32,
    E_DOM = 33,
    E_RANGE = 34,
    E_DEADLK = 35,
    E_NAMETOOLONG = 36,
    E_NOLCK = 37,
    E_NOSYS = 38,
    E_NOTEMPTY = 39,
    E_LOOP = 40,
    E_NOMSG = 42,
    E_IDRM = 43,
    E_CHRNG = 44,
    E_L2NSYNC = 45,
    E_L3HLT = 46,
    E_L3RST = 47,
    E_LNRNG = 48,
    E_UNATCH = 49,
    E_NOCSI = 50,
    E_L2HLT = 51,
    E_BADE = 52,
    E_BADR = 53,
    E_XFULL = 54,
    E_NOANO = 55,
    E_BADRQC = 56,
    E_BADSSL = 57,
    E_BFONT = 59,
    E_NOSTR = 60,
    E_NODATA = 61,
    E_TIME = 62,
    E_NOSR = 63,
    E_NONET = 64,
    E_NOPKG = 65,
    E_REMOTE = 66,
    E_NOLINK = 67,
    E_ADV = 68,
    E_SRMNT = 69,
    E_COMM = 70,
    E_PROTO = 71,
    E_MULTIHOP = 72,
    E_DOTDOT = 73,
    E_BADMSG = 74,
    E_OVERFLOW = 75,
    E_NOTUNUQ = 76,
    E_BADFD = 77,
    E_REMCHG = 78,
    E_LIBACC = 79,
    E_LIBBAD = 80,
    E_LIBSCN = 81,
    E_LIBMAX = 82,
    E_LIBEXEC = 83,
    E_ILSEQ = 84,
    E_RESTART = 85,
    E_STRPIPE = 86,
    E_USERS = 87,
    E_NOTSOCK = 88,
    E_DESTADDRREQ = 89,
    E_MSGSIZE = 90,
    E_PROTOTYPE = 91,
    E_NOPROTOOPT = 92,
    E_PROTONOSUPPORT = 93,
    E_SOCKTNOSUPPORT = 94,
    E_OPNOTSUPP = 95,
    E_PFNOSUPPORT = 96,
    E_AFNOSUPPORT = 97,
    E_ADDRINUSE = 98,
    E_ADDRNOTAVAIL = 99,
    E_NETDOWN = 100,
    E_NETUNREACH = 101,
    E_NETRESET = 102,
    E_CONNABORTED = 103,
    E_CONNRESET = 104,
    E_NOBUFS = 105,
    E_ISCONN = 106,
    E_NOTCONN = 107,
    E_SHUTDOWN = 108,
    E_TOOMANYREFS = 109,
    E_TIMEDOUT = 110,
    E_CONNREFUSED = 111,
    E_HOSTDOWN = 112,
    E_HOSTUNREACH = 113,
    E_ALREADY = 114,
    E_INPROGRESS = 115,
    E_STALE = 116,
    E_UCLEAN = 117,
    E_NOTNAM = 118,
    E_NAVAIL = 119,
    E_ISNAM = 120,
    E_REMOTEIO = 121,
    E_DQUOT = 122,
    E_NOMEDIUM = 123,
    E_MEDIUMTYPE = 124,
    E_CANCELED = 125,
    E_NOKEY = 126,
    E_KEYEXPIRED = 127,
    E_KEYREVOKED = 128,
    E_KEYREJECTED = 129,
    E_OWNERDEAD = 130,
    E_NOTRECOVERABLE = 131,
    E_RFKILL = 132,
    E_HWPOISON = 133,
    E_PROCLIM = 156,
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
    ICMP = 1,
    TCP = 6,
    UDP = 17,
    CONFIG = 0xfffe,
    SOCKET = 0xffff, // i.e. SOL_SOCKET
};

enum class MsgOpt : u32 {
    OOB = 0x00001,
    PEEK = 0x00002,
    DONTROUTE = 0x00004,
    EOR_ = 0x00008,
    TRUNC = 0x00010,
    CTRUNC = 0x00020,
    WAITALL = 0x00040,
    DONTWAIT = 0x00080,
    EOF_ = 0x00100,
    NOSIGNAL = 0x20000,
};

enum class OptName : u32 {
    DEBUG = 0x0001,
    ACCEPTCONN = 0x0002,
    REUSEADDR = 0x0004,
    KEEPALIVE = 0x0008,
    DONTROUTE = 0x0010,
    BROADCAST = 0x0020,
    USELOOPBACK = 0x0040,
    LINGER = 0x0080,
    OOBINLINE = 0x0100,
    REUSEPORT = 0x0200,
    TIMESTAMP = 0x0400,
    NOSIGPIPE = 0x0800, // at least according to libnx
    ACCEPTFILER = 0x1000,
    SNDBUF = 0x1001,
    RCVBUF = 0x1002,
    SNDTIMEO = 0x1005,
    RCVTIMEO = 0x1006,
    ERROR_ = 0x1007,   // avoid name collision with Windows macro
    ACCEPTFILTER = 0x1000,
    BINTIME = 0x2000,
    NO_OFFLOAD = 0x4000,
    NO_DDP = 0x8000,
};
enum class TcpOptName : u32 {
    NODELAY = 0x0001,
    MAXSEG = 0x0002,
    NOPUSH = 0x0004,
    NOOPT = 0x0008,
    MS5SIG = 0x0010,
    INFO = 0x0020
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

enum class FcntlFlags : u32 {
    NONBLOCK = 0x004,
    NONBLOCK_NX = 0x800,
    // Provided for convenience
    NONBLOCK_ANY = u32(NONBLOCK) | u32(NONBLOCK_NX),
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
    IN_ = 0x0001,
    PRI_ = 0x0002,
    OUT_ = 0x0004,
    ERR_ = 0x0008,
    HUP_ = 0x0010,
    NVAL = 0x0020,
    RDNORM = 0x0040,
    RDBAND = 0x0080,
    WRBAND = 0x0100,
    IGNEOF = 0x2000,
};
DECLARE_ENUM_FLAG_OPERATORS(PollEvents);

struct PollFD {
    s32 fd;
    Network::PollEvents events;
    Network::PollEvents revents;
};
static_assert(sizeof(PollFD) == 8);

struct Linger {
    s32 onoff;
    s32 linger;
};
static_assert(sizeof(Linger) == 8);

struct Timeval {
    u64 tv_sec;
    u64 tv_usec;
};
static_assert(sizeof(Timeval) == 16);

/// @brief Cross-platform addrinfo structure (not guest)
struct AddrInfo {
    Domain family;
    Type socket_type;
    Protocol protocol;
    SockAddrIn addr;
    std::optional<std::string> canon_name;
};

} // namespace Network
