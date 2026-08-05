#include "WinMTRGlobal.h"
#include "WinMTRNet.h"
#include "WinMTRNetworkInfo.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <process.h>
#include <vector>
#include <icmpapi.h>
#include <iphlpapi.h>
#include <winternl.h>

namespace {

const int MAX_TRACE_HOPS = 64;
const int MAX_TRACE_RESPONDERS = 128;
const int RESOLVER_WORKERS = 2;
const size_t MAX_RESOLVER_CACHE_ENTRIES = 1024;

struct AsyncTraceState {
    AsyncTraceState(WinMTRNet* owner, const sockaddr_storage& target,
        const TraceConfig& traceConfig)
        : network(owner), destination(target), config(traceConfig),
          icmp(INVALID_HANDLE_VALUE), pending(0), pendingZeroEvent(NULL)
    {
    }

    WinMTRNet* network;
    sockaddr_storage destination;
    TraceConfig config;
    HANDLE icmp;
    std::atomic<int> pending;
    HANDLE pendingZeroEvent;
};

struct AsyncProbeContext {
    AsyncTraceState* trace;
    int index;
    uint64_t sequence;
    bool ipv6;
    std::vector<unsigned char> request;
    std::vector<unsigned char> reply;
};

sockaddr_storage MakeIpv4Address(IPAddr address)
{
    sockaddr_storage storage = {};
    sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(&storage);
    ipv4->sin_family = AF_INET;
    ipv4->sin_addr.s_addr = address;
    return storage;
}

bool IsTraceReplyStatus(DWORD status)
{
    return status == IP_SUCCESS || status == IP_TTL_EXPIRED_TRANSIT ||
        status == IP_TTL_EXPIRED_REASSEM;
}

void CompleteProbe(AsyncProbeContext* context)
{
    DWORD replyCount = 0;
    DWORD status = IP_REQ_TIMED_OUT;
    DWORD roundTripTime = 0;
    sockaddr_storage replyAddress = {};

    if (context->ipv6) {
        replyCount = Icmp6ParseReplies(&context->reply[0],
            static_cast<DWORD>(context->reply.size()));
        if (replyCount > 0) {
            const ICMPV6_ECHO_REPLY* response =
                reinterpret_cast<const ICMPV6_ECHO_REPLY*>(&context->reply[0]);
            status = response->Status;
            roundTripTime = response->RoundTripTime;
            sockaddr_in6* ipv6 = reinterpret_cast<sockaddr_in6*>(&replyAddress);
            ipv6->sin6_family = AF_INET6;
            ipv6->sin6_port = response->Address.sin6_port;
            ipv6->sin6_flowinfo = response->Address.sin6_flowinfo;
            memcpy(&ipv6->sin6_addr, response->Address.sin6_addr,
                sizeof(ipv6->sin6_addr));
            ipv6->sin6_scope_id = response->Address.sin6_scope_id;
        }
    } else {
        replyCount = IcmpParseReplies(&context->reply[0],
            static_cast<DWORD>(context->reply.size()));
        if (replyCount > 0) {
            const ICMP_ECHO_REPLY* response =
                reinterpret_cast<const ICMP_ECHO_REPLY*>(&context->reply[0]);
            status = response->Status;
            roundTripTime = response->RoundTripTime;
            replyAddress = MakeIpv4Address(response->Address);
        }
    }

    if (replyCount > 0 && IsTraceReplyStatus(status)) {
        context->trace->network->RecordReply(context->index,
            static_cast<int>(roundTripTime));
        if (context->trace->network->SetAddress(context->index, replyAddress,
            context->sequence)) {
            context->trace->network->QueueResolve(context->index, replyAddress,
                context->trace->config);
        }
    } else {
        context->trace->network->RecordTimeout(context->index);
    }

    if (context->trace->pending.fetch_sub(1) == 1)
        SetEvent(context->trace->pendingZeroEvent);
    delete context;
}

VOID WINAPI ProbeCompletionRoutine(PVOID context, PIO_STATUS_BLOCK, ULONG)
{
    CompleteProbe(static_cast<AsyncProbeContext*>(context));
}

bool SubmitProbe(AsyncTraceState& trace, int index, int probeOrdinal)
{
    AsyncProbeContext* context = new (std::nothrow) AsyncProbeContext;
    if (!context)
        return false;

    context->trace = &trace;
    context->index = index;
    context->sequence = static_cast<uint64_t>(probeOrdinal);
    context->ipv6 = trace.destination.ss_family == AF_INET6;
    context->request.resize(static_cast<size_t>(trace.config.pingSize));
    const int pattern = trace.config.bitPattern < 0
        ? (((index + 1) * 37 + probeOrdinal * 17) & 0xff)
        : trace.config.bitPattern;
    std::fill(context->request.begin(), context->request.end(),
        static_cast<unsigned char>(pattern));

    const size_t replyHeader = context->ipv6
        ? sizeof(ICMPV6_ECHO_REPLY) : sizeof(ICMP_ECHO_REPLY);
    context->reply.resize(replyHeader + context->request.size() + 32);
    std::fill(context->reply.begin(), context->reply.end(),
        static_cast<unsigned char>(0));

    if (trace.pending.fetch_add(1) == 0)
        ResetEvent(trace.pendingZeroEvent);
    trace.network->RecordSent(index);

    IP_OPTION_INFORMATION options = {};
    options.Ttl = static_cast<unsigned char>(index + 1);
    options.Tos = static_cast<unsigned char>(trace.config.tos);
    options.Flags = trace.config.dontFragment ? IP_FLAG_DF : 0;

    DWORD result = 0;
    if (context->ipv6) {
        sockaddr_in6 source = {};
        source.sin6_family = AF_INET6;
        sockaddr_in6 destination =
            *reinterpret_cast<const sockaddr_in6*>(&trace.destination);
        result = Icmp6SendEcho2(trace.icmp, NULL,
            reinterpret_cast<FARPROC>(ProbeCompletionRoutine), context,
            &source, &destination,
            context->request.empty() ? NULL : &context->request[0],
            static_cast<WORD>(context->request.size()), &options,
            &context->reply[0], static_cast<DWORD>(context->reply.size()),
            static_cast<DWORD>(trace.config.timeoutMs));
    } else {
        const sockaddr_in* destination =
            reinterpret_cast<const sockaddr_in*>(&trace.destination);
        result = IcmpSendEcho2(trace.icmp, NULL,
            reinterpret_cast<FARPROC>(ProbeCompletionRoutine), context,
            destination->sin_addr.s_addr,
            context->request.empty() ? NULL : &context->request[0],
            static_cast<WORD>(context->request.size()), &options,
            &context->reply[0], static_cast<DWORD>(context->reply.size()),
            static_cast<DWORD>(trace.config.timeoutMs));
    }

    if (result != 0) {
        CompleteProbe(context);
    } else if (GetLastError() != ERROR_IO_PENDING) {
        CompleteProbe(context);
    }
    return true;
}

void ResolveHopValues(const sockaddr_storage& address, const TraceConfig& config,
    std::string& name, AsnNetworkInfo& asn)
{
    if (config.useDns)
        name = ReverseLookupAddress(address);
    if (name.empty())
        name = AddressToString(address);
    if (config.lookupAsn)
        LookupAsnNetworkInfo(address, asn);
}

} // namespace

TraceConfig::TraceConfig()
    : intervalMs(1000), pingSize(64), maxHops(30), timeoutMs(3000),
      graceMs(5000), firstTtl(1), dueTtl(0), maxUnknown(5),
      cacheSeconds(0), cycles(0), tos(0), bitPattern(32), useDns(true),
      lookupAsn(true), dontFragment(true)
{
}

HopSnapshot::HopSnapshot()
    : hasAddress(false), xmit(0), returned(0), dropped(0), lossPercent(0),
      best(0), average(0), worst(0), last(0), jitter(0), standardDeviation(0)
{
}

WinMTRNet::WinMTRNet()
    : configuredMaxHops(30), configuredFirstTtl(1), configuredDueTtl(0),
      destinationHop(MAX_TRACE_HOPS + 1), highestProbeHop(1),
      tracing(false), traceGeneration(0), lastDestinationSequence(0),
      stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL)),
      resolverSemaphore(CreateSemaphore(NULL, 0, 4096, NULL)),
      resolverStopEvent(CreateEvent(NULL, TRUE, FALSE, NULL)),
      resolverThreads()
{
    ZeroMemory(&remoteAddress, sizeof(remoteAddress));
    ResetHops();
    if (resolverSemaphore && resolverStopEvent) {
        for (int i = 0; i < RESOLVER_WORKERS; ++i) {
            const uintptr_t thread = _beginthreadex(NULL, 0,
                ResolverThreadEntry, this, 0, NULL);
            if (thread)
                resolverThreads.push_back(reinterpret_cast<HANDLE>(thread));
        }
    }
}

WinMTRNet::~WinMTRNet()
{
    StopTrace();
    if (resolverStopEvent)
        SetEvent(resolverStopEvent);
    for (size_t i = 0; i < resolverThreads.size(); ++i)
        WaitForSingleObject(resolverThreads[i], INFINITE);
    for (size_t i = 0; i < resolverThreads.size(); ++i)
        CloseHandle(resolverThreads[i]);
    if (resolverStopEvent)
        CloseHandle(resolverStopEvent);
    if (resolverSemaphore)
        CloseHandle(resolverSemaphore);
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
    TraceConfig activeConfig = config;
    activeConfig.intervalMs = std::max(1, activeConfig.intervalMs);
    activeConfig.timeoutMs = std::max(1, activeConfig.timeoutMs);
    activeConfig.graceMs = std::max(0, activeConfig.graceMs);
    activeConfig.maxHops = std::max(1,
        std::min(activeConfig.maxHops, MAX_TRACE_HOPS));
    activeConfig.firstTtl = std::max(1,
        std::min(activeConfig.firstTtl, activeConfig.maxHops));
    activeConfig.dueTtl = std::max(0,
        std::min(activeConfig.dueTtl, activeConfig.maxHops));
    activeConfig.maxUnknown = std::max(1,
        std::min(activeConfig.maxUnknown, MAX_TRACE_HOPS));
    activeConfig.cacheSeconds = std::max(0, activeConfig.cacheSeconds);

    {
        std::lock_guard<std::mutex> lock(hostMutex);
        ZeroMemory(host, sizeof(host));
        remoteAddress = address;
        configuredMaxHops = activeConfig.maxHops;
        configuredFirstTtl = activeConfig.firstTtl;
        configuredDueTtl = activeConfig.dueTtl;
    }
    traceGeneration.fetch_add(1);
    destinationHop.store(MAX_TRACE_HOPS + 1);
    highestProbeHop.store(activeConfig.firstTtl);
    lastDestinationSequence.store(0);
    ResetEvent(stopEvent);
    tracing.store(true);

    AsyncTraceState trace(this, address, activeConfig);
    trace.icmp = address.ss_family == AF_INET6
        ? Icmp6CreateFile() : IcmpCreateFile();
    trace.pendingZeroEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (trace.icmp == INVALID_HANDLE_VALUE || !trace.pendingZeroEvent) {
        if (trace.icmp != INVALID_HANDLE_VALUE)
            IcmpCloseHandle(trace.icmp);
        if (trace.pendingZeroEvent)
            CloseHandle(trace.pendingZeroEvent);
        tracing.store(false);
        return;
    }

    int batchAt = activeConfig.firstTtl - 1;
    int batchHostCount = std::min(10,
        activeConfig.maxHops - activeConfig.firstTtl + 1);
    int completedCycles = 0;
    int probeOrdinal = 0;
    bool normalCompletion = false;
    ULONGLONG nextSend = GetTickCount64();

    while (tracing.load()) {
        const ULONGLONG now = GetTickCount64();
        if (now < nextSend) {
            const ULONGLONG remaining = nextSend - now;
            WaitForSingleObjectEx(stopEvent,
                static_cast<DWORD>(std::min<ULONGLONG>(remaining, MAXDWORD)), TRUE);
            continue;
        }

        if (!IsHopCached(batchAt, activeConfig.cacheSeconds))
            SubmitProbe(trace, batchAt, probeOrdinal++);

        int nextBatchHostCount = batchHostCount;
        if (ShouldEndBatch(batchAt, activeConfig, nextBatchHostCount)) {
            batchHostCount = std::max(1, nextBatchHostCount);
            highestProbeHop.store(activeConfig.firstTtl - 1 +
                batchHostCount);
            ++completedCycles;
            if (activeConfig.cycles > 0 &&
                completedCycles >= activeConfig.cycles) {
                normalCompletion = true;
                break;
            }
            batchAt = activeConfig.firstTtl - 1;
        } else {
            ++batchAt;
        }

        const DWORD delta = static_cast<DWORD>(std::max(1,
            activeConfig.intervalMs / batchHostCount));
        if (now > nextSend + static_cast<ULONGLONG>(activeConfig.intervalMs))
            nextSend = now + delta;
        else
            nextSend += delta;
    }

    if (normalCompletion && activeConfig.graceMs > 0) {
        const ULONGLONG graceEnds = GetTickCount64() +
            static_cast<ULONGLONG>(activeConfig.graceMs);
        while (tracing.load()) {
            const ULONGLONG now = GetTickCount64();
            if (now >= graceEnds)
                break;
            const ULONGLONG remaining = graceEnds - now;
            WaitForSingleObjectEx(stopEvent,
                static_cast<DWORD>(std::min<ULONGLONG>(remaining, MAXDWORD)), TRUE);
        }
    }

    while (trace.pending.load() > 0) {
        const DWORD result = WaitForSingleObjectEx(
            trace.pendingZeroEvent, INFINITE, TRUE);
        if (result == WAIT_FAILED)
            SleepEx(1, TRUE);
    }

    FinalizeTransit();
    IcmpCloseHandle(trace.icmp);
    CloseHandle(trace.pendingZeroEvent);
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

bool WinMTRNet::SetAddress(int at, const sockaddr_storage& address,
    uint64_t sequence)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return false;
    std::lock_guard<std::mutex> lock(hostMutex);
    NetHost& current = host[at];
    int responder = 0;
    for (; responder < current.responderCount; ++responder) {
        if (SameAddress(current.responders[responder].address, address))
            break;
    }
    const bool isNewResponder = responder == current.responderCount;
    const bool canStoreResponder = responder < MAX_TRACE_RESPONDERS;
    if (isNewResponder && canStoreResponder) {
        Responder& path = current.responders[responder];
        path.hasAddress = true;
        path.address = address;
        ++current.responderCount;
    }

    const bool isDestination = SameAddress(address, remoteAddress);
    const bool useAsLatest = !isDestination || configuredDueTtl == 0 ||
        at + 1 >= configuredDueTtl;
    const bool changedLatest = useAsLatest &&
        (!current.hasAddress || !SameAddress(current.address, address));
    if (changedLatest) {
        current.address = address;
        current.hasAddress = true;
        if (responder < current.responderCount) {
            const Responder& path = current.responders[responder];
            strncpy_s(current.name, path.name, _TRUNCATE);
            strncpy_s(current.country, path.country, _TRUNCATE);
            strncpy_s(current.asn, path.asn, _TRUNCATE);
            strncpy_s(current.isp, path.isp, _TRUNCATE);
        } else {
            current.name[0] = '\0';
            current.country[0] = '\0';
            current.asn[0] = '\0';
            current.isp[0] = '\0';
        }
    }

    if (isDestination && useAsLatest) {
        uint64_t previous = lastDestinationSequence.load();
        while (sequence >= previous &&
            !lastDestinationSequence.compare_exchange_weak(previous, sequence)) {
        }
        if (sequence >= previous)
            destinationHop.store(at + 1);
    }
    return isNewResponder && (canStoreResponder || changedLatest);
}

bool WinMTRNet::ShouldProbeHop(int ttl) const
{
    return ttl > 0 && ttl <= destinationHop.load();
}

bool WinMTRNet::IsHopCached(int at, int seconds) const
{
    if (seconds <= 0 || at < 0 || at >= MAX_TRACE_HOPS)
        return false;
    std::lock_guard<std::mutex> lock(hostMutex);
    const NetHost& current = host[at];
    if (!current.up || current.seenAt == 0)
        return false;
    const ULONGLONG age = GetTickCount64() - current.seenAt;
    return age <= static_cast<ULONGLONG>(seconds) * 1000;
}

bool WinMTRNet::ShouldEndBatch(int at, const TraceConfig& config,
    int& batchHostCount) const
{
    const int first = config.firstTtl - 1;
    if (at < first)
        return false;
    std::lock_guard<std::mutex> lock(hostMutex);
    int consecutiveUnknown = 0;
    for (int index = first; index < at; ++index) {
        if (!host[index].hasAddress) {
            ++consecutiveUnknown;
        } else {
            consecutiveUnknown = 0;
            if (SameAddress(host[index].address, remoteAddress) &&
                config.dueTtl <= index + 1) {
                batchHostCount = index - first + 1;
                return true;
            }
        }
    }
    const bool reachedDestination = host[at].hasAddress &&
        SameAddress(host[at].address, remoteAddress) &&
        config.dueTtl <= at + 1;
    const bool currentUnknown = !host[at].hasAddress;
    if (reachedDestination ||
        (currentUnknown && consecutiveUnknown > config.maxUnknown &&
            config.dueTtl <= at + 1) ||
        at >= config.maxHops - 1) {
        batchHostCount = at - first + 1;
        return true;
    }
    return false;
}

void WinMTRNet::SetIdentity(int at, const sockaddr_storage& address,
    const std::string& name, const std::string& country,
    const std::string& asn, const std::string& isp)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return;
    std::lock_guard<std::mutex> lock(hostMutex);
    NetHost& current = host[at];
    for (int i = 0; i < current.responderCount; ++i) {
        Responder& path = current.responders[i];
        if (!SameAddress(path.address, address))
            continue;
        strncpy_s(path.name, name.c_str(), _TRUNCATE);
        strncpy_s(path.country, country.c_str(), _TRUNCATE);
        strncpy_s(path.asn, asn.c_str(), _TRUNCATE);
        strncpy_s(path.isp, isp.c_str(), _TRUNCATE);
        break;
    }
    if (current.hasAddress && SameAddress(current.address, address)) {
        strncpy_s(current.name, name.c_str(), _TRUNCATE);
        strncpy_s(current.country, country.c_str(), _TRUNCATE);
        strncpy_s(current.asn, asn.c_str(), _TRUNCATE);
        strncpy_s(current.isp, isp.c_str(), _TRUNCATE);
    }
}

void WinMTRNet::RecordSent(int at)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return;
    std::lock_guard<std::mutex> lock(hostMutex);
    NetHost& current = host[at];
    if (current.xmit > 0)
        current.up = false;
    ++current.xmit;
    current.transit = true;
    int knownHighest = highestProbeHop.load();
    while (at + 1 > knownHighest &&
        !highestProbeHop.compare_exchange_weak(knownHighest, at + 1)) {
    }
}

void WinMTRNet::RecordReply(int at, int roundTripTime)
{
    if (at < 0 || at >= MAX_TRACE_HOPS)
        return;
    std::lock_guard<std::mutex> lock(hostMutex);
    NetHost& current = host[at];
    current.transit = false;
    current.up = true;
    current.seenAt = GetTickCount64();
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

void WinMTRNet::RecordTimeout(int at)
{
    (void) at;
}

void WinMTRNet::QueueResolve(int at, const sockaddr_storage& address,
    const TraceConfig& config)
{
    if (resolverThreads.empty() || !resolverSemaphore) {
        SetIdentity(at, address, AddressToString(address), std::string(),
            std::string(), std::string());
        return;
    }
    const std::string key = AddressToString(address) +
        (config.useDns ? "|dns" : "|numeric") +
        (config.lookupAsn ? "|asn" : "|noasn");
    ResolvedIdentity cached;
    bool cacheHit = false;
    bool queued = false;
    ResolveRequest request = {};
    request.key = key;
    request.address = address;
    request.config = config;
    {
        std::lock_guard<std::mutex> lock(resolverMutex);
        const std::map<std::string, ResolvedIdentity>::const_iterator found =
            resolverCache.find(key);
        if (found != resolverCache.end()) {
            cached = found->second;
            cacheHit = true;
        } else {
            ResolveTarget target = {};
            target.index = at;
            target.address = address;
            target.generation = traceGeneration.load();
            resolverTargets[key].push_back(target);
            if (resolvingKeys.insert(key).second) {
                resolverQueue.push_back(request);
                queued = true;
            }
        }
    }
    if (cacheHit) {
        SetIdentity(at, address, cached.name, cached.country,
            cached.asn, cached.isp);
    } else if (queued) {
        ReleaseSemaphore(resolverSemaphore, 1, NULL);
    }
}

void WinMTRNet::FinalizeTransit()
{
    std::lock_guard<std::mutex> lock(hostMutex);
    for (int i = 0; i < MAX_TRACE_HOPS; ++i)
        host[i].transit = false;
}

unsigned __stdcall WinMTRNet::ResolverThreadEntry(void* parameter)
{
    static_cast<WinMTRNet*>(parameter)->ResolverLoop();
    return 0;
}

void WinMTRNet::ResolverLoop()
{
    HANDLE events[2] = { resolverStopEvent, resolverSemaphore };
    for (;;) {
        const DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0)
            return;
        if (result != WAIT_OBJECT_0 + 1)
            continue;

        ResolveRequest request = {};
        {
            std::lock_guard<std::mutex> lock(resolverMutex);
            if (resolverQueue.empty())
                continue;
            request = resolverQueue.front();
            resolverQueue.pop_front();
        }

        ResolvedIdentity identity;
        AsnNetworkInfo asn;
        ResolveHopValues(request.address, request.config, identity.name, asn);
        identity.country = asn.countryCode;
        identity.asn = asn.asn;
        identity.isp = asn.isp;

        std::vector<ResolveTarget> targets;
        {
            std::lock_guard<std::mutex> lock(resolverMutex);
            if (resolverCache.size() >= MAX_RESOLVER_CACHE_ENTRIES)
                resolverCache.erase(resolverCache.begin());
            resolverCache[request.key] = identity;
            targets.swap(resolverTargets[request.key]);
            resolverTargets.erase(request.key);
            resolvingKeys.erase(request.key);
        }
        const uint64_t generation = traceGeneration.load();
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].generation == generation) {
                SetIdentity(targets[i].index, targets[i].address,
                    identity.name, identity.country, identity.asn,
                    identity.isp);
            }
        }
    }
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
    snapshot.address = current.hasAddress
        ? AddressToString(current.address) : std::string();
    snapshot.name = current.name;
    snapshot.country = current.country;
    snapshot.asn = current.asn;
    snapshot.isp = current.isp;
    snapshot.responders.reserve(current.responderCount);
    for (int i = 0; i < current.responderCount; ++i) {
        const Responder& path = current.responders[i];
        ResponderSnapshot responder;
        responder.address = AddressToString(path.address);
        responder.name = path.name;
        responder.country = path.country;
        responder.asn = path.asn;
        responder.isp = path.isp;
        snapshot.responders.push_back(responder);
    }
    snapshot.xmit = current.xmit;
    snapshot.returned = current.returned;
    const int lossBase = current.xmit - (current.transit ? 1 : 0);
    snapshot.dropped = lossBase <= 0
        ? 0 : std::max(0, lossBase - current.returned);
    snapshot.lossPercent = lossBase <= 0
        ? 0 : 100 - (100 * current.returned / lossBase);
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
    return std::min(configuredMaxHops, highestProbeHop.load());
}

int WinMTRNet::GetFirstHopIndex() const
{
    std::lock_guard<std::mutex> lock(hostMutex);
    return std::max(0, configuredFirstTtl - 1);
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
    const std::string value = snapshot.name.empty()
        ? snapshot.address : snapshot.name;
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
