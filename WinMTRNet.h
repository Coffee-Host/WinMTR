#ifndef WINMTRNET_H_
#define WINMTRNET_H_

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

struct TraceConfig {
    int intervalMs;
    int pingSize;
    int maxHops;
    int timeoutMs;
    int graceMs;
    int firstTtl;
    int dueTtl;
    int maxUnknown;
    int cacheSeconds;
    int cycles;
    int tos;
    int bitPattern;
    bool useDns;
    bool lookupAsn;
    bool dontFragment;

    TraceConfig();
};

struct ResponderSnapshot {
    std::string address;
    std::string name;
    std::string country;
    std::string asn;
    std::string isp;
};

struct HopSnapshot {
    bool hasAddress;
    std::string address;
    std::string name;
    std::string country;
    std::string asn;
    std::string isp;
    int xmit;
    int returned;
    int dropped;
    int lossPercent;
    int best;
    int average;
    int worst;
    int last;
    int jitter;
    int standardDeviation;
    std::vector<ResponderSnapshot> responders;

    HopSnapshot();
};

class WinMTRNet {
public:
    WinMTRNet();
    ~WinMTRNet();

    void DoTrace(const sockaddr_storage& address, const TraceConfig& config);
    void ResetHops();
    void StopTrace();
    bool IsTracing() const;

    HopSnapshot GetHopSnapshot(int at) const;
    int GetMax() const;
    int GetFirstHopIndex() const;
    bool WaitForStop(DWORD timeoutMs) const;

    // Worker entry points. Callers outside WinMTRNet should not use these.
    bool SetAddress(int at, const sockaddr_storage& address, uint64_t sequence);
    void SetIdentity(int at, const sockaddr_storage& address,
        const std::string& name, const std::string& country,
        const std::string& asn, const std::string& isp);
    void RecordSent(int at);
    void RecordReply(int at, int roundTripTime);
    void RecordTimeout(int at);
    void QueueResolve(int at, const sockaddr_storage& address,
        const TraceConfig& config);
    void FinalizeTransit();
    bool ShouldProbeHop(int ttl) const;
    bool IsHopCached(int at, int seconds) const;
    bool ShouldEndBatch(int at, const TraceConfig& config,
        int& batchHostCount) const;

    // Compatibility accessors used by the existing dialogs and exporters.
    int GetAddr(int at) const;
    int GetName(int at, char* name) const;
    int GetBest(int at) const;
    int GetWorst(int at) const;
    int GetAvg(int at) const;
    int GetPercent(int at) const;
    int GetLast(int at) const;
    int GetReturned(int at) const;
    int GetXmit(int at) const;

private:
    struct Responder {
        bool hasAddress;
        sockaddr_storage address;
        char name[NI_MAXHOST];
        char country[8];
        char asn[24];
        char isp[192];
    };

    struct NetHost {
        bool hasAddress;
        sockaddr_storage address;
        uint64_t total;
        uint64_t totalSquares;
        uint64_t jitterTotal;
        int xmit;
        int returned;
        bool transit;
        bool up;
        ULONGLONG seenAt;
        int last;
        int best;
        int worst;
        int lastJitter;
        char name[NI_MAXHOST];
        char country[8];
        char asn[24];
        char isp[192];
        Responder responders[128];
        int responderCount;
    };

    struct ResolveRequest {
        std::string key;
        sockaddr_storage address;
        TraceConfig config;
    };

    struct ResolveTarget {
        int index;
        sockaddr_storage address;
        uint64_t generation;
    };

    struct ResolvedIdentity {
        std::string name;
        std::string country;
        std::string asn;
        std::string isp;
    };

    bool SameAddress(const sockaddr_storage& left, const sockaddr_storage& right) const;
    static unsigned __stdcall ResolverThreadEntry(void* parameter);
    void ResolverLoop();

    mutable std::mutex hostMutex;
    std::mutex resolverMutex;
    std::deque<ResolveRequest> resolverQueue;
    std::map<std::string, ResolvedIdentity> resolverCache;
    std::map<std::string, std::vector<ResolveTarget> > resolverTargets;
    std::set<std::string> resolvingKeys;
    NetHost host[64];
    sockaddr_storage remoteAddress;
    int configuredMaxHops;
    int configuredFirstTtl;
    int configuredDueTtl;
    std::atomic<int> destinationHop;
    std::atomic<int> highestProbeHop;
    std::atomic<bool> tracing;
    std::atomic<uint64_t> traceGeneration;
    std::atomic<uint64_t> lastDestinationSequence;
    HANDLE stopEvent;
    HANDLE resolverSemaphore;
    HANDLE resolverStopEvent;
    std::vector<HANDLE> resolverThreads;
};

#endif // WINMTRNET_H_
