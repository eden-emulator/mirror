// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "common/error.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/internal_network/network.h"
#include "core/internal_network/network_interface.h"
#include "core/internal_network/sockets.h"
#include "network/network.h"

namespace Network {

namespace {

enum class CallType {
    Send,
    Other,
};

#ifdef _WIN32

using socklen_t = int;

SOCKET interrupt_socket = static_cast<SOCKET>(-1);

void InterruptSocketOperations() {
    closesocket(interrupt_socket);
}

void AcknowledgeInterrupt() {
    interrupt_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

void Initialize() {
    WSADATA wsa_data;
    (void)WSAStartup(MAKEWORD(2, 2), &wsa_data);

    AcknowledgeInterrupt();
}

void Finalize() {
    InterruptSocketOperations();
    WSACleanup();
}

SOCKET GetInterruptSocket() {
    return interrupt_socket;
}

sockaddr TranslateFromSockAddrIn(Network::SockAddrIn input) {
    sockaddr_in result;

#ifdef __unix__
    result.sin_len = sizeof(result);
#endif

    switch (static_cast<Domain>(input.family)) {
    case Domain::INET:
        result.sin_family = AF_INET;
        break;
    default:
        UNIMPLEMENTED_MSG("Unhandled sockaddr family={}", input.family);
        result.sin_family = AF_INET;
        break;
    }

    result.sin_port = htons(input.portno);

    auto& ip = result.sin_addr.S_un.S_un_b;
    ip.s_b1 = input.ip[0];
    ip.s_b2 = input.ip[1];
    ip.s_b3 = input.ip[2];
    ip.s_b4 = input.ip[3];

    sockaddr addr;
    std::memcpy(&addr, &result, sizeof(addr));
    return addr;
}

LINGER MakeLinger(bool enable, u32 linger_value) {
    ASSERT(linger_value <= (std::numeric_limits<u_short>::max)());

    LINGER value;
    value.l_onoff = enable ? 1 : 0;
    value.l_linger = static_cast<u_short>(linger_value);
    return value;
}

bool EnableNonBlock(SOCKET fd, bool enable) {
    u_long value = enable ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &value) != SOCKET_ERROR;
}

Errno TranslateNativeError(int e, CallType call_type = CallType::Other) {
    switch (e) {
    case 0: return Errno::SUCCESS;
    case WSAECONNABORTED:
        if (call_type == CallType::Send) {
            // Winsock yields WSAECONNABORTED from `send` in situations where Unix
            // systems, and actual Switches, yield EPIPE.
            return Errno::PIPE;
        } else {
            return Errno::CONNABORTED;
        }
    case WSAEBADF: return Errno::BADF;
    case WSAEINVAL: return Errno::INVAL;
    case WSAEMFILE: return Errno::MFILE;
    case WSAENOTCONN: return Errno::NOTCONN;
    case WSAEWOULDBLOCK: return Errno::AGAIN;
    case WSAECONNREFUSED: return Errno::CONNREFUSED;
    case WSAECONNRESET: return Errno::CONNRESET;
    case WSAEHOSTUNREACH: return Errno::HOSTUNREACH;
    case WSAENETDOWN: return Errno::NETDOWN;
    case WSAENETUNREACH: return Errno::NETUNREACH;
    case WSAEMSGSIZE: return Errno::MSGSIZE;
    case WSAETIMEDOUT: return Errno::TIMEDOUT;
    case WSAEINPROGRESS: return Errno::INPROGRESS;
    case WSAEISCONN: return Errno::ISCONN;
    case WSAEADDRINUSE: return Errno::ADDRINUSE;
    case WSAEADDRNOTAVAIL: return Errno::ADDRNOTAVAIL;
    case WSAEPROTOTYPE: return Errno::PROTOTYPE;
    case WSAENOPROTOOPT: return Errno::NOPROTOOPT;
    case WSAEPROTONOSUPPORT: return Errno::PROTONOSUPPORT;
    case WSAESOCKTNOSUPPORT: return Errno::SOCKTNOSUPPORT;
#ifdef WSAENOTSUP
    // Not defined by fucking MSVC because MSVC is stupid as shitfuckery
    case WSAENOTSUP: return Errno::NOTSUP;
#endif
    default:
        UNIMPLEMENTED_MSG("Unimplemented errno={}", e);
        return Errno::OTHER;
    }
}

#else // ^^^ Windows vvv POSIX

using SOCKET = int;
using WSAPOLLFD = pollfd;
using ULONG = u64;

constexpr SOCKET SOCKET_ERROR = -1;

constexpr int SD_RECEIVE = SHUT_RD;
constexpr int SD_SEND = SHUT_WR;
constexpr int SD_BOTH = SHUT_RDWR;

int interrupt_pipe_fd[2] = {-1, -1};

void Initialize() {
    if (pipe(interrupt_pipe_fd) != 0) {
        LOG_ERROR(Network, "Failed to create interrupt pipe!");
    }
    int flags = fcntl(interrupt_pipe_fd[0], F_GETFL);
    ASSERT_MSG(fcntl(interrupt_pipe_fd[0], F_SETFL, flags | O_NONBLOCK) == 0,
               "Failed to set nonblocking state for interrupt pipe");
}

void Finalize() {
    if (interrupt_pipe_fd[0] >= 0) {
        close(interrupt_pipe_fd[0]);
    }
    if (interrupt_pipe_fd[1] >= 0) {
        close(interrupt_pipe_fd[1]);
    }
}

void InterruptSocketOperations() {
    u8 value = 0;
    ASSERT(write(interrupt_pipe_fd[1], &value, sizeof(value)) == 1);
}

void AcknowledgeInterrupt() {
    u8 value = 0;
    ssize_t ret = read(interrupt_pipe_fd[0], &value, sizeof(value));
    if (ret != 1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR(Network, "Failed to acknowledge interrupt on shutdown");
    }
}

SOCKET GetInterruptSocket() {
    return interrupt_pipe_fd[0];
}

sockaddr TranslateFromSockAddrIn(Network::SockAddrIn input) {
    sockaddr_in result;

    switch (static_cast<Domain>(input.family)) {
    case Domain::INET:
        result.sin_family = AF_INET;
        break;
    default:
        UNIMPLEMENTED_MSG("Unhandled sockaddr family={}", input.family);
        result.sin_family = AF_INET;
        break;
    }

    result.sin_port = htons(input.portno);

    result.sin_addr.s_addr = input.ip[0] | input.ip[1] << 8 | input.ip[2] << 16 | input.ip[3] << 24;

    sockaddr addr;
    std::memcpy(&addr, &result, sizeof(addr));
    return addr;
}

int WSAPoll(WSAPOLLFD* fds, ULONG nfds, int timeout) {
    return poll(fds, nfds_t(nfds), timeout);
}

int closesocket(SOCKET fd) {
    return close(fd);
}

linger MakeLinger(bool enable, u32 linger_value) {
    linger value{};
    value.l_onoff = enable ? 1 : 0;
    value.l_linger = linger_value;
    return value;
}

bool EnableNonBlock(int fd, bool enable) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        return false;
    }
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags) == 0;
}

Errno TranslateNativeError(int e, CallType call_type = CallType::Other) {
    switch (e) {
    case 0: return Errno::SUCCESS;
#define NETWORK_ERROR_LIST \
    NETWORK_ERROR_ELEM(BADF) \
    NETWORK_ERROR_ELEM(INVAL) \
    NETWORK_ERROR_ELEM(MFILE) \
    NETWORK_ERROR_ELEM(PIPE) \
    NETWORK_ERROR_ELEM(CONNABORTED) \
    NETWORK_ERROR_ELEM(NOTCONN) \
    NETWORK_ERROR_ELEM(AGAIN) \
    NETWORK_ERROR_ELEM(CONNREFUSED) \
    NETWORK_ERROR_ELEM(CONNRESET) \
    NETWORK_ERROR_ELEM(HOSTUNREACH) \
    NETWORK_ERROR_ELEM(NETDOWN) \
    NETWORK_ERROR_ELEM(NETUNREACH) \
    NETWORK_ERROR_ELEM(MSGSIZE) \
    NETWORK_ERROR_ELEM(TIMEDOUT) \
    NETWORK_ERROR_ELEM(INPROGRESS) \
    NETWORK_ERROR_ELEM(ISCONN) \
    NETWORK_ERROR_ELEM(PROTOTYPE) \
    NETWORK_ERROR_ELEM(NOPROTOOPT) \
    NETWORK_ERROR_ELEM(PROTONOSUPPORT) \
    NETWORK_ERROR_ELEM(SOCKTNOSUPPORT) \
    NETWORK_ERROR_ELEM(NOTSUP) \
    NETWORK_ERROR_ELEM(ADDRINUSE) \
    NETWORK_ERROR_ELEM(ADDRNOTAVAIL) \
    NETWORK_ERROR_ELEM(NOTSOCK) \
    NETWORK_ERROR_ELEM(ALREADY) \
    NETWORK_ERROR_ELEM(STALE)
#define NETWORK_ERROR_ELEM(name) case E##name: return Errno::name;
    NETWORK_ERROR_LIST
#undef NETWORK_ERROR_ELEM
#undef NETWORK_ERROR_LIST
    default:
        UNIMPLEMENTED_MSG("Unimplemented errno={} ({})", e, strerror(e));
        return Errno::OTHER;
    }
}

#endif

Errno GetAndLogLastError(CallType call_type = CallType::Other) {
#ifdef _WIN32
    int e = WSAGetLastError();
#else
    int e = errno;
#endif
    const Errno err = TranslateNativeError(e, call_type);
    if (err == Errno::AGAIN || err == Errno::TIMEDOUT || err == Errno::INPROGRESS) {
        // These happen during normal operation, so only log them at debug level.
        LOG_DEBUG(Network, "Socket operation error: {}", Common::NativeErrorToString(e));
        return err;
    }
    LOG_ERROR(Network, "Socket operation error: {}", Common::NativeErrorToString(e));
    return err;
}

GetAddrInfoError TranslateGetAddrInfoErrorFromNative(int gai_err) {
    switch (gai_err) {
    case 0: return GetAddrInfoError::SUCCESS;
    case EAI_AGAIN: return GetAddrInfoError::AGAIN;
    case EAI_BADFLAGS: return GetAddrInfoError::BADFLAGS;
    case EAI_FAIL: return GetAddrInfoError::FAIL;
    case EAI_FAMILY: return GetAddrInfoError::FAMILY;
    case EAI_MEMORY: return GetAddrInfoError::MEMORY;
    case EAI_NONAME: return GetAddrInfoError::NONAME;
    case EAI_SERVICE: return GetAddrInfoError::SERVICE;
    case EAI_SOCKTYPE: return GetAddrInfoError::SOCKTYPE;
    // These codes may not be defined on all systems:
#ifdef EAI_ADDRFAMILY
    case EAI_ADDRFAMILY: return GetAddrInfoError::ADDRFAMILY;
#endif
#ifdef EAI_SYSTEM
    case EAI_SYSTEM: return GetAddrInfoError::SYSTEM;
#endif
#ifdef EAI_BADHINTS
    case EAI_BADHINTS: return GetAddrInfoError::BADHINTS;
#endif
#ifdef EAI_PROTOCOL
    case EAI_PROTOCOL: return GetAddrInfoError::PROTOCOL;
#endif
#ifdef EAI_OVERFLOW
    case EAI_OVERFLOW: return GetAddrInfoError::OVERFLOW_;
#endif
    default:
#ifdef EAI_NODATA
        // This can't be a case statement because it would create a duplicate
        // case on Windows where EAI_NODATA is an alias for EAI_NONAME.
        if (gai_err == EAI_NODATA)
            return GetAddrInfoError::NODATA;
#endif
        return GetAddrInfoError::OTHER;
    }
}

#ifdef __FreeBSD__
#define NETWORK_DOMAIN_TRANSLATE_LIST \
    NETWORK_DOMAIN_TRANSLATE_ELEM(UNIX) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(IMPLINK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(PUP) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(CHAOS) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(NETBIOS) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ISO) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ECMA) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DATAKIT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(CCITT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(SNA) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DECnet) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DLI) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(LAT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(HYLINK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(APPLETALK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ROUTE) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(LINK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(COIP) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(CNT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(IPX) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(SIP) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ISDN) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET6) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(NATM) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ATM) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(NETGRAPH) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(SLOW) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(SCLUSTER) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ARP) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(BLUETOOTH) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(IEEE80211) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(NETLINK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET_SDP) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET6_SDP)
#elif defined(__linux__)
#define NETWORK_DOMAIN_TRANSLATE_LIST \
    NETWORK_DOMAIN_TRANSLATE_ELEM(UNIX) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(SNA) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DECnet) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(APPLETALK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ROUTE) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(IPX) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ISDN) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET6) \
    /*NETWORK_DOMAIN_TRANSLATE_ELEM(ATM)*/ \
    NETWORK_DOMAIN_TRANSLATE_ELEM(BLUETOOTH) \
    /*NETWORK_DOMAIN_TRANSLATE_ELEM(NETLINK)*/
#elif defined(_WIN32)
#define NETWORK_DOMAIN_TRANSLATE_LIST \
    NETWORK_DOMAIN_TRANSLATE_ELEM(UNIX) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(IMPLINK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(PUP) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(CHAOS) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ISO) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ECMA) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DATAKIT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(CCITT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(SNA) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DECnet) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(DLI) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(LAT) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(HYLINK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(APPLETALK) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(NETBIOS) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(ATM) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET6)
#else
#define NETWORK_DOMAIN_TRANSLATE_LIST \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET) \
    NETWORK_DOMAIN_TRANSLATE_ELEM(INET6)
#endif

Domain TranslateDomainFromNative(int domain) {
    switch (domain) {
    case AF_UNSPEC: return Domain::Unspecified;
#define NETWORK_DOMAIN_TRANSLATE_ELEM(x) case AF_##x: return Domain::x;
    NETWORK_DOMAIN_TRANSLATE_LIST
#undef NETWORK_DOMAIN_TRANSLATE_ELEM
    default:
        UNIMPLEMENTED_MSG("Unhandled domain={}", domain);
        return Domain::INET;
    }
}

int TranslateDomainToNative(Domain domain) {
    switch (domain) {
    case Domain::Unspecified: return AF_UNSPEC;
#define NETWORK_DOMAIN_TRANSLATE_ELEM(x) case Domain::x: return AF_##x;
    NETWORK_DOMAIN_TRANSLATE_LIST
#undef NETWORK_DOMAIN_TRANSLATE_ELEM
    default:
        UNIMPLEMENTED_MSG("Unimplemented domain={}", domain);
        return 0;
    }
}
#undef NETWORK_DOMAIN_TRANSLATE_LIST

// Must account for SOCK_CLOEXEC and SOCK_NONBLOCK
// so mask the lower bits as those aren't usually used for flags
Type TranslateTypeFromNative(int type) {
    switch (type & 0xff) {
    case 0: return Type::Unspecified;
    case SOCK_STREAM: return Type::STREAM;
    case SOCK_DGRAM: return Type::DGRAM;
    case SOCK_RAW: return Type::RAW;
    case SOCK_RDM: return Type::RDM;
    case SOCK_SEQPACKET: return Type::SEQPACKET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return Type::STREAM;
    }
}

int TranslateTypeToNative(Type type) {
    switch (Type(int(type) & 0xff)) {
    case Type::Unspecified: return 0;
    case Type::STREAM: return SOCK_STREAM;
    case Type::DGRAM: return SOCK_DGRAM;
    case Type::RAW: return SOCK_RAW;
    case Type::RDM: return SOCK_RDM;
    case Type::SEQPACKET: return SOCK_SEQPACKET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return 0;
    }
}

// Some of those protocols may not be supported on some platforms
// It doesn't really matter, except that some homebrew may not work correctly
// Official software uses TCP & UDP mainly, SCTP is used by some homebrew as well
#ifdef __FreeBSD__
#define NETWORK_PROTOCOL_TRANSLATE_LIST \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TCP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RAW) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(GGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPV4) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ST) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(EGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PIGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RCCMON) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NVPII) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PUP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ARGUS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(EMCON) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(XNET) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(CHAOS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MUX) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MEAS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(HMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PRM) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TRUNK1) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TRUNK2) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(LEAF1) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(LEAF2) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IRTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(BLT) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NSP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(INP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DCCP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(3PC)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IDPR) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(XTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(CMTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TPXX) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IL) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SDRP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ROUTING) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(FRAGMENT) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IDRP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RSVP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(GRE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MHRP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(BHA) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ESP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(AH) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(INLSP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SWIPE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NHRP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MOBILE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TLSP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SKIP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NONE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DSTOPTS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(AHIP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(CFTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(HELLO) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SATEXPAK) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(KRYPTOLAN) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RVD) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPPC) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ADFS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SATMON) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(VISA) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPCV) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(CPNX) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(CPHB) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(WSN) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PVP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(BRSATMON) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ND) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(WBMON) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(WBEXPAK) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(EON) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(VMTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SVMTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(VINES) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TCF) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGRP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(OSPFIGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SRPC) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(LARP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(AX25) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPEIP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MICP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SCCSP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ETHERIP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ENCAP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(APES) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(GMTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPCOMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SCTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MH) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDPLITE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(HIP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SHIM6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PIM) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(CARP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PGM) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MPLS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PFSYNC)
#elif defined(__OPENORBIS__)
#define NETWORK_PROTOCOL_TRANSLATE_LIST \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(HOPOPTS)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGMP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(IPIP)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TCP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(EGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PUP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DCCP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ROUTING) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(FRAGMENT) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RSVP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(GRE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ESP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(AH) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NONE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DSTOPTS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MTP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(BEETPH)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ENCAP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PIM) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(COMP)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SCTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MH) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDPLITE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MPLS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RAW)
#elif defined(__linux__)
// Other platforms get fucked
#define NETWORK_PROTOCOL_TRANSLATE_LIST \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(HOPOPTS)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGMP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(IPIP)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TCP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(EGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PUP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DCCP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ROUTING) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(FRAGMENT) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RSVP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(GRE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ESP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(AH) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NONE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DSTOPTS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(MTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ENCAP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PIM) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(COMP)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SCTP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDPLITE)
#elif defined(_WIN32)
#define NETWORK_PROTOCOL_TRANSLATE_LIST \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(HOPOPTS)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGMP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(GGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPV4) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ST) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TCP) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(CBT)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(EGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IGP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PUP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(RDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(IPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ROUTING) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(FRAGMENT) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ESP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(AH) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ICMPV6) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(NONE) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(DSTOPTS) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(ND) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(ICLFXBM)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PIM) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(PGM) \
    /*NETWORK_PROTOCOL_TRANSLATE_ELEM(L2TP)*/ \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SCTP)
#else
#define NETWORK_PROTOCOL_TRANSLATE_LIST \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(TCP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(UDP) \
    NETWORK_PROTOCOL_TRANSLATE_ELEM(SCTP)
#endif
[[nodiscard]] Protocol TranslateProtocolFromNative(u32 protocol) {
    switch (protocol) {
#define NETWORK_PROTOCOL_TRANSLATE_ELEM(x) case IPPROTO_##x: return Protocol::x;
    NETWORK_PROTOCOL_TRANSLATE_LIST
#undef NETWORK_PROTOCOL_TRANSLATE_ELEM
    default:
        UNIMPLEMENTED_MSG("Unimplemented protocol={}", protocol);
        return Protocol::IP;
    }
}
[[nodiscard]] u32 TranslateProtocolToNative(Protocol protocol) {
    switch (protocol) {
#define NETWORK_PROTOCOL_TRANSLATE_ELEM(x) case Protocol::x: return IPPROTO_##x;
    NETWORK_PROTOCOL_TRANSLATE_LIST
#undef NETWORK_PROTOCOL_TRANSLATE_ELEM
    default:
        UNIMPLEMENTED_MSG("Unimplemented protocol={}", protocol);
        return 0;
    }
}
#undef NETWORK_PROTOCOL_TRANSLATE_LIST

Network::SockAddrIn TranslateToSockAddrIn(sockaddr_in input, size_t input_len) {
    Network::SockAddrIn result{};
    result.len = 16;
    result.family = u8(TranslateDomainFromNative(input.sin_family));
    result.portno = ntohs(input.sin_port);
    result.ip = TranslateIPv4(input.sin_addr);
    result.zeroes = {};
    return result;
}

static s16 TranslatePollEvents(Network::PollEvents events) noexcept {
    s16 result = 0;
    const auto translate = [&result, &events](Network::PollEvents guest, s16 host) {
        if (True(events & guest)) {
            events &= ~guest;
            result |= host;
        }
    };
    translate(Network::PollEvents::IN_, POLLIN);
    translate(Network::PollEvents::PRI_, POLLPRI);
    translate(Network::PollEvents::OUT_, POLLOUT);
    translate(Network::PollEvents::ERR_, POLLERR);
    translate(Network::PollEvents::HUP_, POLLHUP);
    translate(Network::PollEvents::NVAL, POLLNVAL);
    translate(Network::PollEvents::RDNORM, POLLRDNORM);
    translate(Network::PollEvents::RDBAND, POLLRDBAND);
    translate(Network::PollEvents::WRBAND, POLLWRBAND);
#ifdef POLLIGNEOF
    translate(Network::PollEvents::IGNEOF, POLLIGNEOF);
#endif
#ifdef _WIN32
    s16 allowed_events = POLLRDBAND | POLLRDNORM | POLLWRNORM;
    // Unlike poll on other OSes, WSAPoll will complain if any other flags are set on input.
    if (result & ~allowed_events) {
        LOG_WARNING(Network, "Removing WSAPoll input events 0x{:x} because Windows doesn't support them", result & ~allowed_events);
    }
    result &= allowed_events;
#endif
    UNIMPLEMENTED_IF_MSG((u16)events != 0, "Unhandled guest events=0x{:x}", (u16)events);
    return result;
}

static Network::PollEvents TranslatePollRevents(s16 revents) {
    Network::PollEvents result{};
    const auto translate = [&result, &revents](s16 host, Network::PollEvents guest) {
        if ((revents & host) != 0) {
            revents &= s16(~host);
            result |= guest;
        }
    };
    translate(POLLIN, Network::PollEvents::IN_);
    translate(POLLPRI, Network::PollEvents::PRI_);
    translate(POLLOUT, Network::PollEvents::OUT_);
    translate(POLLERR, Network::PollEvents::ERR_);
    translate(POLLHUP, Network::PollEvents::HUP_);
    translate(POLLNVAL, Network::PollEvents::NVAL);
    translate(POLLRDNORM, Network::PollEvents::RDNORM);
    translate(POLLRDBAND, Network::PollEvents::RDBAND);
    translate(POLLWRBAND, Network::PollEvents::WRBAND);
#ifdef POLLIGNEOF
    translate(POLLIGNEOF, Network::PollEvents::IGNEOF);
#endif
    UNIMPLEMENTED_IF_MSG(revents != 0, "Unhandled host revents=0x{:x}", revents);
    return result;
}

} // Anonymous namespace

NetworkInstance::NetworkInstance() {
    Initialize();
}

NetworkInstance::~NetworkInstance() {
    Finalize();
}

void CancelPendingSocketOperations() {
    InterruptSocketOperations();
}

void RestartSocketOperations() {
    AcknowledgeInterrupt();
}

std::optional<IPv4Address> GetHostIPv4Address() {
    const auto network_interface = Network::GetSelectedNetworkInterface();
    if (!network_interface.has_value()) {
        // Only print the error once to avoid log spam
        static bool print_error = true;
        if (print_error) {
            LOG_ERROR(Network, "GetSelectedNetworkInterface returned no interface");
            print_error = false;
        }

        return {};
    }

    return TranslateIPv4(network_interface->ip_address);
}

std::string IPv4AddressToString(IPv4Address ip_addr) {
    std::array<char, INET_ADDRSTRLEN> buf = {};
    ASSERT(inet_ntop(AF_INET, &ip_addr, buf.data(), sizeof(buf)) == buf.data());
    return std::string(buf.data());
}

u32 IPv4AddressToInteger(IPv4Address ip_addr) {
    return static_cast<u32>(ip_addr[0]) << 24 | static_cast<u32>(ip_addr[1]) << 16 |
           static_cast<u32>(ip_addr[2]) << 8 | static_cast<u32>(ip_addr[3]);
}

std::variant<std::vector<AddrInfo>, GetAddrInfoError> GetAddressInfo(const std::string& host, const std::optional<std::string>& service) {
    LOG_DEBUG(Network, "host={},service={}", host, service.value_or("no"));
    addrinfo hints{};
    hints.ai_family = AF_INET; // Switch only supports IPv4.
    addrinfo* addrinfo = nullptr;
    s32 gai_err = getaddrinfo(host.c_str(), service.has_value() ? service->c_str() : nullptr, &hints, &addrinfo);
    if (gai_err != 0) {
        return TranslateGetAddrInfoErrorFromNative(gai_err);
    }
    std::vector<AddrInfo> ret{};

    auto const add_entries = [&](auto const&& filter_fn) {
        for (auto* current = addrinfo; current; current = current->ai_next) {
            if (filter_fn(current)) {
                LOG_DEBUG(Network, "- entry prot={},socktype={},family={},len={}", current->ai_protocol, current->ai_socktype, current->ai_family, current->ai_addrlen);
                // We should only get AF_INET results due to the hints value.
                if (current->ai_family == AF_INET && current->ai_addrlen == sizeof(sockaddr_in)) {
                    auto& out = ret.emplace_back();
                    out.family = TranslateDomainFromNative(current->ai_family);
                    out.socket_type = TranslateTypeFromNative(current->ai_socktype);
                    out.protocol = TranslateProtocolFromNative(current->ai_protocol);
                    out.addr = TranslateToSockAddrIn(*reinterpret_cast<sockaddr_in*>(current->ai_addr), current->ai_addrlen);
                    if (current->ai_canonname != nullptr) {
                        out.canon_name = current->ai_canonname;
                    }
                } else {
                    LOG_ERROR(Network, "invalid entry family={},len={}", current->ai_family, current->ai_addrlen);
                }
            }
        }
    };

    // Let me explain slowly, so HB app store depends ENTIRELY on the switch returning
    // getaddrinfo stuff in a particular order, right?
    // cURL is used by hb app store, which uses a tiny library called "get"; the
    // specific version that 99% of users run has a bug with DNS cache queries...
    // unfortunely we can only do what we do best: emulate the issue away.
    //
    // Please see this fuckery here -> https://github.com/curl/curl/issues/9274
    // Place fucking UDP last
    add_entries([](auto* current) {
        return current->ai_protocol != IPPROTO_UDP;
    });
    // Off to the shitter you go!
    add_entries([](auto* current) {
        return current->ai_protocol == IPPROTO_UDP;
    });
    freeaddrinfo(addrinfo);
    return ret;
}

std::pair<s32, Errno> Poll(std::span<HostPollFD> pollfds, s32 timeout) {
    LOG_DEBUG(Network, "pollfds={},timeout={}", pollfds.size(), timeout);

    std::vector<WSAPOLLFD> host_pollfds(pollfds.size());
    std::transform(pollfds.begin(), pollfds.end(), host_pollfds.begin(), [](auto const e) {
        WSAPOLLFD result;
        result.fd = e.socket->GetFD();
        result.events = TranslatePollEvents(e.events);
        result.revents = 0;
        return result;
    });

    host_pollfds.push_back(WSAPOLLFD{
        .fd = GetInterruptSocket(),
        .events = POLLIN,
        .revents = 0,
    });

    const int result = WSAPoll(host_pollfds.data(), ULONG(host_pollfds.size()), timeout);
    if (result == 0) {
        ASSERT(std::all_of(host_pollfds.begin(), host_pollfds.end(), [](auto const fd) {
            return fd.revents == 0;
        }));
        return {0, Errno::SUCCESS};
    }

    for (size_t i = 0; i < pollfds.size(); ++i)
        pollfds[i].revents = TranslatePollRevents(host_pollfds[i].revents);

    if (result <= 0) {
        ASSERT(result == SOCKET_ERROR);
        return {-1, GetAndLogLastError()};
    }
    return {result, Errno::SUCCESS};
}

Socket::~Socket() {
    if (fd == INVALID_SOCKET) {
        return;
    }
    (void)closesocket(fd);
    fd = INVALID_SOCKET;
}

Socket::Socket(Socket&& rhs) noexcept {
    fd = std::exchange(rhs.fd, INVALID_SOCKET);
}

static s32 TranslateOptNameToNative(Network::SocketLevel level, Network::OptName optname) {
    switch (level) {
    case Network::SocketLevel::SOCKET:
        switch (optname) {
        // managarm doesn't like these
#ifdef SO_DEBUG
        case Network::OptName::DEBUG: return SO_DEBUG;
#endif
#ifdef SO_ACCEPTCONN
        case Network::OptName::ACCEPTCONN: return SO_ACCEPTCONN;
#endif
        case Network::OptName::LINGER: return SO_LINGER;
        case Network::OptName::REUSEADDR: return SO_REUSEADDR;
        case Network::OptName::KEEPALIVE: return SO_KEEPALIVE;
        case Network::OptName::BROADCAST: return SO_BROADCAST;
        case Network::OptName::SNDBUF: return SO_SNDBUF;
        case Network::OptName::RCVBUF: return SO_RCVBUF;
        case Network::OptName::SNDTIMEO: return SO_SNDTIMEO;
        case Network::OptName::RCVTIMEO: return SO_RCVTIMEO;
        case Network::OptName::NOSIGPIPE: return SO_NOSIGPIPE;
#ifdef SO_REUSEPORT
        case Network::OptName::REUSEPORT: return SO_REUSEPORT;
#endif
#ifdef SO_ACCEPTFILTER
        case Network::OptName::ACCEPTFILTER: return SO_ACCEPTFILTER;
#endif
#ifdef SO_TIMESTAMP
        case Network::OptName::TIMESTAMP: return SO_TIMESTAMP;
#endif
        case Network::OptName::ERROR_: return SO_ERROR;
        default:
            break;
        }
        break;
    case Network::SocketLevel::TCP:
        switch (Network::TcpOptName(optname)) {
        case Network::TcpOptName::NODELAY: return TCP_NODELAY;
        case Network::TcpOptName::MAXSEG: return TCP_MAXSEG;
        case Network::TcpOptName::NOPUSH: return TCP_NOPUSH;
        case Network::TcpOptName::NOOPT: return TCP_NOOPT;
        case Network::TcpOptName::MS5SIG: return TCP_MD5SIG;
        case Network::TcpOptName::INFO: return TCP_INFO;
        default:
            break;
        }
        break;
    default:
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented level={},optname={}", level, optname);
    return 0;
}

static s32 TranslateSocketLevelToNative(Network::SocketLevel level) {
    switch (level) {
    case Network::SocketLevel::SOCKET: return SOL_SOCKET;
    // FreeBSD doesn't define below but Linux does :-(
#ifdef SOL_IP
    case Network::SocketLevel::IP: return SOL_IP;
#else
    case Network::SocketLevel::IP: return IPPROTO_IP;
#endif
#ifdef SOL_ICMP
    case Network::SocketLevel::ICMP: return SOL_ICMP;
#else
    case Network::SocketLevel::ICMP: return IPPROTO_ICMP;
#endif
#ifdef SOL_TCP
    case Network::SocketLevel::TCP: return SOL_TCP;
#else
    case Network::SocketLevel::TCP: return IPPROTO_TCP;
#endif
#ifdef SOL_UDP
    case Network::SocketLevel::UDP: return SOL_UDP;
#else
    case Network::SocketLevel::UDP: return IPPROTO_UDP;
#endif
#ifdef SOL_CONFIG
    case Network::SocketLevel::CONFIG: return SOL_CONFIG;
#endif
    default:
        UNIMPLEMENTED_MSG("Unimplemented level={}", level);
        return SOL_SOCKET;
    }
}

Errno Socket::GetSockOpt(Network::SocketLevel level, Network::OptName optname, std::span<u8> value) {
    socklen_t len = socklen_t(value.size());
    auto const native_level = TranslateSocketLevelToNative(level);
    auto const native_optname = TranslateOptNameToNative(level, optname);
    const int result = getsockopt(fd, native_level, native_optname, reinterpret_cast<char*>(value.data()), &len);
    if (result != SOCKET_ERROR) {
        ASSERT(len == socklen_t(value.size()));
        return Errno::SUCCESS;
    }
    return GetAndLogLastError();
}

Errno Socket::SetNonBlock(bool enable) {
    if (EnableNonBlock(fd, enable)) {
        is_non_blocking = enable;
        return Errno::SUCCESS;
    }
    return GetAndLogLastError();
}

Errno Socket::SetSockOpt(Network::SocketLevel level, Network::OptName optname, std::span<const u8> optval) {
    LOG_DEBUG(Network, "level={},optname={},optval={}", level, optname, optval.size());
    auto const native_level = TranslateSocketLevelToNative(level);
    auto const native_optname = TranslateOptNameToNative(level, optname);
    // TODO: is it >= or ==? for sizes
    if (optname == Network::OptName::LINGER) {
        if (optval.size() >= sizeof(Network::Linger)) {
            Network::Linger linger{};
            std::memcpy(&linger, optval.data(), sizeof(linger));
            auto const linger_optval = MakeLinger(bool(linger.onoff), linger.linger);
            return setsockopt(fd, native_level, native_optname, reinterpret_cast<const char*>(&linger_optval), sizeof(linger_optval)) != SOCKET_ERROR
                ? Errno::SUCCESS
                : GetAndLogLastError();
        }
        return Errno::INVAL;
    }
    return setsockopt(fd, native_level, native_optname, reinterpret_cast<const char*>(optval.data()), socklen_t(optval.size())) != SOCKET_ERROR
        ? Errno::SUCCESS
        : GetAndLogLastError();
}

Errno Socket::Initialize(Domain domain, Type type, Protocol protocol) {
    fd = socket(TranslateDomainToNative(domain), TranslateTypeToNative(type), TranslateProtocolToNative(protocol));
    if (fd != INVALID_SOCKET)
        return Errno::SUCCESS;
    return GetAndLogLastError();
}

std::pair<Socket::AcceptResult, Errno> Socket::Accept() {
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    const bool wait_for_accept = !is_non_blocking;
    if (wait_for_accept) {
        std::vector<WSAPOLLFD> host_pollfds{
            WSAPOLLFD{fd, POLLIN, 0},
            WSAPOLLFD{GetInterruptSocket(), POLLIN, 0},
        };

        while (true) {
            const int pollres =
                WSAPoll(host_pollfds.data(), static_cast<ULONG>(host_pollfds.size()), -1);
            if (host_pollfds[1].revents != 0) {
                // Interrupt signaled before a client could be accepted, break
                return {AcceptResult{}, Errno::AGAIN};
            }
            if (pollres > 0) {
                break;
            }
        }
    }

    const SOCKET new_socket = accept(fd, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    if (new_socket == INVALID_SOCKET) {
        return {AcceptResult{}, GetAndLogLastError()};
    }

    AcceptResult result{
        .socket = std::make_unique<Socket>(new_socket),
        .sockaddr_in = TranslateToSockAddrIn(addr, addrlen),
    };

    return {std::move(result), Errno::SUCCESS};
}

Errno Socket::Connect(Network::SockAddrIn addr_in) {
    const sockaddr host_addr_in = TranslateFromSockAddrIn(addr_in);
    if (connect(fd, &host_addr_in, sizeof(host_addr_in)) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

std::pair<Network::SockAddrIn, Errno> Socket::GetPeerName() {
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR) {
        return {Network::SockAddrIn{}, GetAndLogLastError()};
    }

    return {TranslateToSockAddrIn(addr, addrlen), Errno::SUCCESS};
}

std::pair<Network::SockAddrIn, Errno> Socket::GetSockName() {
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR) {
        return {Network::SockAddrIn{}, GetAndLogLastError()};
    }

    return {TranslateToSockAddrIn(addr, addrlen), Errno::SUCCESS};
}

Errno Socket::Bind(Network::SockAddrIn addr) {
    const sockaddr addr_in = TranslateFromSockAddrIn(addr);
    if (bind(fd, &addr_in, sizeof(addr_in)) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

Errno Socket::Listen(s32 backlog) {
    if (listen(fd, backlog) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

Errno Socket::Shutdown(ShutdownHow how) {
    int host_how = 0;
    switch (how) {
    case ShutdownHow::RD:
        host_how = SD_RECEIVE;
        break;
    case ShutdownHow::WR:
        host_how = SD_SEND;
        break;
    case ShutdownHow::RDWR:
        host_how = SD_BOTH;
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented flag how={}", how);
        return Errno::SUCCESS;
    }
    if (shutdown(fd, host_how) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

static s32 TranslateMsgOptToNative(s32 flags) {
    s32 r = 0;
#ifdef MSG_OOB
    if (0 != (flags & s32(MsgOpt::OOB))) r |= MSG_OOB;
#endif
#ifdef MSG_PEEK
    if (0 != (flags & s32(MsgOpt::PEEK))) r |= MSG_PEEK;
#endif
#ifdef MSG_DONTROUTE
    if (0 != (flags & s32(MsgOpt::DONTROUTE))) r |= MSG_DONTROUTE;
#endif
#ifdef MSG_EOR
    if (0 != (flags & s32(MsgOpt::EOR_))) r |= MSG_EOR;
#endif
#ifdef MSG_TRUNC
    if (0 != (flags & s32(MsgOpt::TRUNC))) r |= MSG_TRUNC;
#endif
#ifdef MSG_CTRUNC
    if (0 != (flags & s32(MsgOpt::CTRUNC))) r |= MSG_CTRUNC;
#endif
#ifdef MSG_WAITALL
    if (0 != (flags & s32(MsgOpt::WAITALL))) r |= MSG_WAITALL;
#endif
#ifdef MSG_DONTWAIT
    if (0 != (flags & s32(MsgOpt::DONTWAIT))) r |= MSG_DONTWAIT;
#endif
#ifdef MSG_EOF
    if (0 != (flags & s32(MsgOpt::EOF_))) r |= MSG_EOF;
#endif
    return r;
}

std::pair<s32, Errno> Socket::Recv(int flags, std::span<u8> message) {
    LOG_DEBUG(Network, "flags={},message={}", flags, message.size());
    ASSERT(message.size() < size_t((std::numeric_limits<int>::max)()));

    auto const native_flags = TranslateMsgOptToNative(flags);
    auto const result = recv(fd, reinterpret_cast<char*>(message.data()), int(message.size()), native_flags);
    if (result != SOCKET_ERROR)
        return {s32(result), Errno::SUCCESS};
    return {-1, GetAndLogLastError()};
}

std::pair<s32, Errno> Socket::RecvFrom(int flags, std::span<u8> message, Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "flags={},message={},addr={}", flags, message.size(), fmt::ptr(addr));
    ASSERT(flags == 0 && message.size() < size_t((std::numeric_limits<int>::max)()));

    sockaddr_in addr_in{};
    socklen_t addrlen = sizeof(addr_in);
    socklen_t* const p_addrlen = addr ? &addrlen : nullptr;
    sockaddr* const p_addr_in = addr ? reinterpret_cast<sockaddr*>(&addr_in) : nullptr;

    auto const native_flags = TranslateMsgOptToNative(flags);
    auto const result = recvfrom(fd, reinterpret_cast<char*>(message.data()), int(message.size()), native_flags, p_addr_in, p_addrlen);
    if (result != SOCKET_ERROR) {
        if (addr) {
            *addr = TranslateToSockAddrIn(addr_in, addrlen);
        }
        return {s32(result), Errno::SUCCESS};
    }
    return {-1, GetAndLogLastError()};
}

std::pair<s32, Errno> Socket::Send(std::span<const u8> message, int flags) {
    LOG_DEBUG(Network, "flags={},message={}", flags, message.size());
    ASSERT(message.size() < size_t((std::numeric_limits<int>::max)()));

    int native_flags = TranslateMsgOptToNative(flags);
#ifdef __unix__
    // NOSIGNAL is set for all sockets
    native_flags |= MSG_NOSIGNAL; // do not send us SIGPIPE
#endif
    const auto result = send(fd, reinterpret_cast<const char*>(message.data()), int(message.size()), native_flags);
    if (result != SOCKET_ERROR)
        return {s32(result), Errno::SUCCESS};
    return {-1, GetAndLogLastError(CallType::Send)};
}

std::pair<s32, Errno> Socket::SendTo(u32 flags, std::span<const u8> message, const Network::SockAddrIn* addr) {
    LOG_DEBUG(Network, "flags={},message={},addr={}", flags, message.size(), fmt::ptr(addr));
    ASSERT(message.size() < size_t((std::numeric_limits<int>::max)()));

    const sockaddr* to = nullptr;
    const int to_len = addr ? sizeof(sockaddr) : 0;
    sockaddr host_addr_in;

    if (addr) {
        host_addr_in = TranslateFromSockAddrIn(*addr);
        to = &host_addr_in;
    }

    int native_flags = TranslateMsgOptToNative(flags);
#ifdef __unix__
    // NOSIGNAL is set for all sockets
    native_flags |= MSG_NOSIGNAL; // do not send us SIGPIPE
#endif

    const auto result = sendto(fd, reinterpret_cast<const char*>(message.data()), int(message.size()), native_flags, to, to_len);
    if (result != SOCKET_ERROR)
        return {s32(result), Errno::SUCCESS};
    return {-1, GetAndLogLastError(CallType::Send)};
}

Errno Socket::Close() {
    [[maybe_unused]] const int result = closesocket(fd);
    if (result != 0) {
        LOG_WARNING(Network, "closesocket failed, socket may already be closed");
    }
    fd = INVALID_SOCKET;

    return Errno::SUCCESS;
}

std::pair<Errno, Errno> Socket::GetPendingError() {
    std::vector<u8> tmp(sizeof(s32));
    auto const getsockopt_err = GetSockOpt(Network::SocketLevel::SOCKET, Network::OptName::ERROR_, tmp);
    s32 pending_err{};
    std::memcpy(&pending_err, tmp.data(), sizeof(pending_err));
    return {TranslateNativeError(pending_err), getsockopt_err};
}

bool Socket::IsOpened() const {
    return fd != INVALID_SOCKET;
}

void Socket::HandleProxyPacket(const ProxyPacket& packet) {
    LOG_WARNING(Network, "ProxyPacket received, but not in Proxy mode!");
}

} // namespace Network
