#include "WinMTRGlobal.h"
#include "WinMTRNet.h"
#include "WinMTRNetworkInfo.h"

#include <algorithm>
#include <cmath>
#include <process.h>
#include <vector>
#include <icmpapi.h>
#include <iphlpapi.h>

namespace {

const int MAX_TRACE_HOPS = 64;

struct TraceThreadContext {
    int ttl;
    sockaddr_storage destination;
    TraceConfig config;
    WinMTRNet* network;
};

sockaddr_storage MakeIpv4Address(IPAddr address)
{
    sockaddr_storage storage = {};
    sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(&storage);
    ipv4->sin_family = AF_INET;
    ipv4->sin_addr.s_addr = address;
    return storage;
}

void ResolveHop(WinMTRNet* network, int index, const sockaddr_storage& address,
    const TraceConfig& config)
{
    std::string name;
    if (config.useDns)
        name = ReverseLookupAddress(address);
    if (name.empty())
        name = AddressToString(address);

    AsnNetworkInfo asn;
    if (config.lookupAsn)
        LookupAsnNetworkInfo(address, asn);
    network->SetIdentity(index, name, asn.countryCode, asn.asn, asn.isp);
}

unsigned __stdcall TraceThread(void* parameter)
{
    TraceThreadContext* context = static_cast<TraceThreadContext*>(parameter);
    WinMTRNet* network = context->network;
    const int index = context->ttl - 1;
    const bool ipv6 = context->destination.ss_family == AF_INET6;
    HANDLE icmp = ipv6 ? Icmp6CreateFile() : IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        delete context;
        return 0;
    }

    IP_OPTION_INFORMATION options = {};
    options.Ttl = static_cast<unsigned char>(context->ttl);
    options.Tos = static_cast<unsigned char>(context->config.tos);
    options.Flags = context->config.dontFragment ? IP_FLAG_DF : 0;

    std::vector<unsigned char> request(static_cast<size_t>(context->config.pingSize));
    const size_t replyHeader = ipv6 ? sizeof(ICMPV6_ECHO_REPLY) : sizeof(ICMP_ECHO_REPLY);
    std::vector<unsigned char> reply(replyHeader + request.size() + 32);

    int probe = 0;
    while (network->IsTracing() && network->ShouldProbeHop(context->ttl) &&
        (context->config.cycles == 0 || probe < context->config.cycles)) {
        const ULONGLONG started = GetTickCount64();
        const int pattern = context->config.bitPattern < 0
            ? ((context->ttl * 37 + probe * 17) & 0xff)
            : context->config.bitPattern;
        const unsigned char payloadByte = static_cast<unsigned char>(pattern);
        std::fill(request.begin(), request.end(), payloadByte);
        std::fill(reply.begin(), reply.end(), static_cast<unsigned char>(0));
        network->RecordSent(index);

        DWORD replyCount = 0;
        DWORD status = IP_REQ_TIMED_OUT;
        DWORD roundTripTime = 0;
        sockaddr_storage replyAddress = {};

        if (ipv6) {
            sockaddr_in6 source = {};
            source.sin6_family = AF_INET6;
            sockaddr_in6 destination =
                *reinterpret_cast<const sockaddr_in6*>(&context->destination);
            replyCount = Icmp6SendEcho2(icmp, NULL, NULL, NULL, &source, &destination,
                request.empty() ? NULL : &request[0],
                static_cast<WORD>(request.size()), &options,
                &reply[0], static_cast<DWORD>(reply.size()), context->config.timeoutMs);
            if (replyCount > 0) {
                const ICMPV6_ECHO_REPLY* response =
                    reinterpret_cast<const ICMPV6_ECHO_REPLY*>(&reply[0]);
                status = response->Status;
                roundTripTime = response->RoundTripTime;
                sockaddr_in6* replyIpv6 = reinterpret_cast<sockaddr_in6*>(&replyAddress);
                replyIpv6->sin6_family = AF_INET6;
                replyIpv6->sin6_port = response->Address.sin6_port;
                replyIpv6->sin6_flowinfo = response->Address.sin6_flowinfo;
                memcpy(&replyIpv6->sin6_addr, response->Address.sin6_addr,
                    sizeof(replyIpv6->sin6_addr));
                replyIpv6->sin6_scope_id = response->Address.sin6_scope_id;
            }
        } else {
            const sockaddr_in* destination =
                reinterpret_cast<const sockaddr_in*>(&context->destination);
            replyCount = IcmpSendEcho(icmp, destination->sin_addr.s_addr,
                request.empty() ? NULL : &request[0],
                static_cast<WORD>(request.size()), &options,
                &reply[0], static_cast<DWORD>(reply.size()), context->config.timeoutMs);
            if (replyCount > 0) {
                const ICMP_ECHO_REPLY* response =
                    reinterpret_cast<const ICMP_ECHO_REPLY*>(&reply[0]);
                status = response->Status;
                roundTripTime = response->RoundTripTime;
                replyAddress = MakeIpv4Address(response->Address);
            }
        }

        if (replyCount > 0 &&
            (status == IP_SUCCESS || status == IP_TTL_EXPIRED_TRANSIT)) {
            network->RecordReply(index, static_cast<int>(roundTripTime));
            if (network->SetAddress(index, replyAddress))
                ResolveHop(network, index, replyAddress, context->config);
        }

        ++probe;
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (network->IsTracing() && elapsed < static_cast<ULONGLONG>(context->config.intervalMs))
            network->WaitForStop(static_cast<DWORD>(context->config.intervalMs - elapsed));
    }

    IcmpCloseHandle(icmp);
    delete context;
    return 0;
}

} // namespace

TraceConfig::TraceConfig()
    : intervalMs(1000), pingSize(64), maxHops(30), timeoutMs(3000), cycles(0),
      tos(0), bitPattern(32), useDns(true), lookupAsn(true), dontFragment(true)
{
}

HopSnapshot::HopSnapshot()
    : hasAddress(false), xmit(0), returned(0), lossPercent(0), best(0), average(0),
      worst(0), last(0), jitter(0), standardDeviation(0)
{
}

WinMTRNet::WinMTRNet()
    : configuredMaxHops(30), destinationHop(MAX_TRACE_HOPS + 1), tracing(false),
      stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
{
    ZeroMemory(&remoteAddress, sizeof(remoteAddress));
    ResetHops();
}

WinMTRNet::~WinMTRNet()
{
    StopTrace();
    if (stopEvent)
        CloseHandle(stopEvent);
}

void WinMTRNet::ResetHops()
{
    std::lock_guard<std::mutex> lock(hostMutex);
    ZeroMemory(host, sizeof(host));
}

void WinMTRNet::DoTrace(const sockaddr_storage& address, const TraceConfig& config)
{
    {
        std::lock_guard<std::mutex> lock(hostMutex);
        ZeroMemory(host, sizeof(host));
        remoteAddress = address;
        configuredMaxHops = std::max(1, std::min(config.maxHops, MAX_TRACE_HOPS));
    }
    destinationHop.store(MAX_TRACE_HOPS + 1);
    ResetEvent(stopEvent);
    tracing.store(true);

    std::vector<HANDLE> threads;
    for (int ttl = 1; ttl <= configuredMaxHops; ++ttl) {
        TraceThreadContext* context = new TraceThreadContext;
        context->ttl = ttl;
        context->destination = address;
        context->config = config;
        context->network = this;
        const uintptr_t thread = _beginthreadex(NULL, 0, TraceThread, context, 0, NULL);
        if (thread == 0) {
            delete context;
            continue;
        }
        threads.push_back(reinterpret_cast<HANDLE>(thread));
    }

    if (!threads.empty())
        WaitForMultipleObjects(static_cast<DWORD>(threads.size()), &threads[0], TRUE, INFINITE);
    for (size_t i = 0; i < threads.size(); ++i)
        CloseHandle(threads[i]);
    tracing.store(false);
}

void WinMTRNet::StopTrace()
{
    tracing.store(false);
    if (stopEvent)
        SetEvent(stopEvent);
}

bool WinMTRNet::IsTracing() const
{
    return tracing.load();
}

bool WinMTRNet::WaitForStop(DWORD timeoutMs) const
{
    return stopEvent && WaitForSingleObject(stopEvent, timeoutMs) == WAIT_OBJECT_0;
}

bool WinMTRNet::SetAddress(int at, const sockaddr_storage& address)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return false;
    std::lock_guard<std::mutex> lock(hostMutex);
    if (host[at].hasAddress)
        return false;
    host[at].address = address;
    host[at].hasAddress = true;
    if (SameAddress(address, remoteAddress)) {
        int knownHop = destinationHop.load();
        const int discoveredHop = at + 1;
        while (discoveredHop < knownHop &&
            !destinationHop.compare_exchange_weak(knownHop, discoveredHop)) {
        }
    }
    return true;
}

bool WinMTRNet::ShouldProbeHop(int ttl) const
{
    return ttl > 0 && ttl <= destinationHop.load();
}

void WinMTRNet::SetIdentity(int at, const std::string& name, const std::string& country,
    const std::string& asn, const std::string& isp)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return;
    std::lock_guard<std::mutex> lock(hostMutex);
    strncpy_s(host[at].name, name.c_str(), _TRUNCATE);
    strncpy_s(host[at].country, country.c_str(), _TRUNCATE);
    strncpy_s(host[at].asn, asn.c_str(), _TRUNCATE);
    strncpy_s(host[at].isp, isp.c_str(), _TRUNCATE);
}

void WinMTRNet::RecordSent(int at)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return;
    std::lock_guard<std::mutex> lock(hostMutex);
    ++host[at].xmit;
}

void WinMTRNet::RecordReply(int at, int roundTripTime)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return;
    std::lock_guard<std::mutex> lock(hostMutex);
    NetHost& current = host[at];
    if (current.returned > 0) {
        current.lastJitter = abs(roundTripTime - current.last);
        current.jitterTotal += current.lastJitter;
    }
    current.last = roundTripTime;
    current.total += static_cast<uint64_t>(roundTripTime);
    current.totalSquares += static_cast<uint64_t>(roundTripTime) * roundTripTime;
    if (current.returned == 0 || roundTripTime < current.best)
        current.best = roundTripTime;
    if (current.returned == 0 || roundTripTime > current.worst)
        current.worst = roundTripTime;
    ++current.returned;
}

bool WinMTRNet::SameAddress(const sockaddr_storage& left,
    const sockaddr_storage& right) const
{
    if (left.ss_family != right.ss_family)
        return false;
    if (left.ss_family == AF_INET) {
        const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(&left);
        const sockaddr_in* b = reinterpret_cast<const sockaddr_in*>(&right);
        return a->sin_addr.s_addr == b->sin_addr.s_addr;
    }
    if (left.ss_family == AF_INET6) {
        const sockaddr_in6* a = reinterpret_cast<const sockaddr_in6*>(&left);
        const sockaddr_in6* b = reinterpret_cast<const sockaddr_in6*>(&right);
        return memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(IN6_ADDR)) == 0;
    }
    return false;
}

HopSnapshot WinMTRNet::GetHopSnapshot(int at) const
{
    HopSnapshot snapshot;
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return snapshot;
    std::lock_guard<std::mutex> lock(hostMutex);
    const NetHost& current = host[at];
    snapshot.hasAddress = current.hasAddress;
    snapshot.address = current.hasAddress ? AddressToString(current.address) : std::string();
    snapshot.name = current.name;
    snapshot.country = current.country;
    snapshot.asn = current.asn;
    snapshot.isp = current.isp;
    snapshot.xmit = current.xmit;
    snapshot.returned = current.returned;
    snapshot.lossPercent = current.xmit == 0 ? 0
        : 100 - (100 * current.returned / current.xmit);
    snapshot.best = current.best;
    snapshot.average = current.returned == 0 ? 0
        : static_cast<int>(current.total / current.returned);
    snapshot.worst = current.worst;
    snapshot.last = current.last;
    snapshot.jitter = current.returned <= 1 ? 0
        : static_cast<int>(current.jitterTotal / (current.returned - 1));
    if (current.returned > 0) {
        const double mean = static_cast<double>(current.total) / current.returned;
        const double variance = std::max(0.0,
            static_cast<double>(current.totalSquares) / current.returned - mean * mean);
        snapshot.standardDeviation = static_cast<int>(std::sqrt(variance) + 0.5);
    }
    return snapshot;
}

int WinMTRNet::GetMax() const
{
    std::lock_guard<std::mutex> lock(hostMutex);
    return std::min(configuredMaxHops, destinationHop.load());
}

int WinMTRNet::GetAddr(int at) const
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return 0;
    std::lock_guard<std::mutex> lock(hostMutex);
    if (!host[at].hasAddress || host[at].address.ss_family != AF_INET)
        return 0;
    const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&host[at].address);
    return ntohl(ipv4->sin_addr.s_addr);
}

int WinMTRNet::GetName(int at, char* name) const
{
    const HopSnapshot snapshot = GetHopSnapshot(at);
    const std::string value = snapshot.name.empty() ? snapshot.address : snapshot.name;
    strcpy_s(name, NI_MAXHOST, value.c_str());
    return 0;
}

int WinMTRNet::GetBest(int at) const { return GetHopSnapshot(at).best; }
int WinMTRNet::GetWorst(int at) const { return GetHopSnapshot(at).worst; }
int WinMTRNet::GetAvg(int at) const { return GetHopSnapshot(at).average; }
int WinMTRNet::GetPercent(int at) const { return GetHopSnapshot(at).lossPercent; }
int WinMTRNet::GetLast(int at) const { return GetHopSnapshot(at).last; }
int WinMTRNet::GetReturned(int at) const { return GetHopSnapshot(at).returned; }
int WinMTRNet::GetXmit(int at) const { return GetHopSnapshot(at).xmit; }
