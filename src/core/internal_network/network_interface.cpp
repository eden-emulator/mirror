// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <numeric>
#include <ranges>
#include <bit>

#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/internal_network/emu_net_state.h"
#include "core/internal_network/network_interface.h"

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <cerrno>
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace Network {

#ifdef _WIN32

std::vector<Network::NetworkInterface> GetAvailableNetworkInterfaces() {

    ULONG buf_size = 0;
    if (GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
            nullptr, nullptr, &buf_size) != ERROR_BUFFER_OVERFLOW) {
        LOG_ERROR(Network, "GetAdaptersAddresses(overrun probe) failed");
        return {};
    }

    std::vector<u8> buffer(buf_size, 0);
    auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    if (GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS,
            nullptr, addrs, &buf_size) != NO_ERROR) {
        LOG_ERROR(Network, "GetAdaptersAddresses(data) failed");
        return {};
    }

    std::vector<Network::NetworkInterface> result;

    for (auto* a = addrs; a; a = a->Next) {

        if (a->OperStatus != IfOperStatusUp || !a->FirstUnicastAddress ||
            !a->FirstUnicastAddress->Address.lpSockaddr)
            continue;

        const in_addr ip =
            reinterpret_cast<sockaddr_in*>(a->FirstUnicastAddress->Address.lpSockaddr)->sin_addr;

        ULONG mask_raw = 0;
        if (ConvertLengthToIpv4Mask(a->FirstUnicastAddress->OnLinkPrefixLength, &mask_raw) !=
            NO_ERROR)
            continue;

        in_addr mask{.S_un{.S_addr{mask_raw}}};

        in_addr gw{.S_un{.S_addr{0}}};
        if (a->FirstGatewayAddress && a->FirstGatewayAddress->Address.lpSockaddr)
            gw = reinterpret_cast<sockaddr_in*>(a->FirstGatewayAddress->Address.lpSockaddr)
                     ->sin_addr;

        result.emplace_back(Network::NetworkInterface{
            .name = Common::UTF16ToUTF8(std::wstring{a->FriendlyName}),
            .ip_address = ip,
            .subnet_mask = mask,
            .gateway = gw,
            .kind = (a->IfType == IF_TYPE_IEEE80211 ? HostAdapterKind::Wifi : HostAdapterKind::Ethernet)
        });
    }

    return result;
}

#else

std::vector<Network::NetworkInterface> GetAvailableNetworkInterfaces() {
    struct ifaddrs* ifaddr = nullptr;

    if (getifaddrs(&ifaddr) != 0) {
        LOG_ERROR(Network, "getifaddrs: {}", std::strerror(errno));
        return {};
    }

    // TODO: This is still horrible, it was worse before (somehow)
    struct RoutingEntry {
        std::string iface_name;
        u32 dest;
        u32 gateway;
        u32 flags;
    };
    std::vector<RoutingEntry> routes{};
#ifdef ANDROID
    // Even through Linux based, we can't reliably obtain routing information from there :(
#else
    if (std::ifstream file("/proc/net/route"); file.is_open()) {
        file.ignore((std::numeric_limits<std::streamsize>::max)(), '\n'); //ignore header
        for (std::string line; std::getline(file, line);) {
            std::istringstream iss{line};
            RoutingEntry info{};
            iss >> info.iface_name >> std::hex
                >> info.dest >> info.gateway >> info.flags;
            routes.emplace_back(info);
        }
    } else {
        LOG_WARNING(Network, "\"/proc/net/route\" not found - using gateway 0");
    }
#endif
    std::vector<Network::NetworkInterface> ifaces;
    for (auto ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_netmask == nullptr /* Have a netmask and address */
        || ifa->ifa_addr->sa_family != AF_INET /* Must be of kind AF_INET */
        || (ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) /* Not loopback */
            continue;
        // Just use 0 as the gateway address if not found OR routes are empty :)
        auto const it = std::ranges::find_if(routes, [&ifa](auto const& e) {
            return e.iface_name == ifa->ifa_name
                && e.dest == 0 // not the default route
                && (e.flags & 0x02) != 0; // flag RTF_GATEWAY (defined in <linux/route.h>)
        });
        in_addr gw; // Solaris defines s_addr as a macro, can't use special C++ shenanigans here
        gw.s_addr = it != routes.end() ? it->gateway : 0;
        ifaces.emplace_back(Network::NetworkInterface{
            .name = ifa->ifa_name,
            .ip_address = std::bit_cast<struct sockaddr_in>(*ifa->ifa_addr).sin_addr,
            .subnet_mask = std::bit_cast<struct sockaddr_in>(*ifa->ifa_netmask).sin_addr,
            .gateway = gw
        });
    }
    freeifaddrs(ifaddr);
    return ifaces;
}

#endif // _WIN32

std::optional<Network::NetworkInterface> GetSelectedNetworkInterface() {
    auto const& sel_if = Settings::values.network_interface.GetValue();
    if (auto const ifaces = Network::GetAvailableNetworkInterfaces(); ifaces.size() > 0) {
        if (sel_if.empty())
            return ifaces[0];
        if (auto const res = std::ranges::find_if(ifaces, [&sel_if](const auto& iface) {
            return iface.name == sel_if;
        }); res != ifaces.end())
            return *res;
        // Only print the error once to avoid log spam
        static bool print_error = true;
        if (print_error) {
            LOG_WARNING(Network, "Couldn't find interface \"{}\"", sel_if);
            print_error = false;
        }
        return std::nullopt;
    }
    LOG_WARNING(Network, "No interfaces");
    return std::nullopt;
}

void SelectFirstNetworkInterface() {
    if (auto const ifaces = Network::GetAvailableNetworkInterfaces(); ifaces.size() > 0)
        Settings::values.network_interface.SetValue(ifaces[0].name);
}

} // namespace Network
