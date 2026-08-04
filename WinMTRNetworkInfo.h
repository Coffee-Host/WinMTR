#ifndef WINMTRNETWORKINFO_H_
#define WINMTRNETWORKINFO_H_

#include <string>
#include <vector>
#include <winsock2.h>

struct IpNetworkDetails {
    bool available;
    std::string address;
    std::string hostname;
    std::string city;
    std::string region;
    std::string country;
    std::string countryCode;
    std::string asn;
    std::string isp;

    IpNetworkDetails() : available(false) {}
};

struct PublicNetworkInfo {
    IpNetworkDetails ipv4;
    IpNetworkDetails ipv6;
    IpNetworkDetails dnsResolver;
    bool dnsDiagnosticAvailable;
    std::string dnsEcs;
    std::vector<std::string> dnsServers;
    std::string error;

    PublicNetworkInfo() : dnsDiagnosticAvailable(false) {}
};

struct AsnNetworkInfo {
    std::string countryCode;
    std::string asn;
    std::string isp;
};

PublicNetworkInfo QueryPublicNetworkInfo();
bool LookupAsnNetworkInfo(const sockaddr_storage& address, AsnNetworkInfo& info);
std::string AddressToString(const sockaddr_storage& address);
std::string ReverseLookupAddress(const sockaddr_storage& address);

#endif // WINMTRNETWORKINFO_H_
