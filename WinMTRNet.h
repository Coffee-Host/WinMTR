#ifndef WINMTRNET_H_
#define WINMTRNET_H_

#include <atomic>
#include <deque>
#include <mutex>
#include <stdint.h>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

struct TraceConfig {
    int intervalMs;
    int pingSize;
    int maxHops;
    int timeoutMs;
    int graceMs;
    int cycles;
    int tos;
    int bitPattern;
    bool useDns;
    bool lookupAsn;
    bool dontFragment;

    TraceConfig();
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
    bool WaitForStop(DWORD timeoutMs) const;

    // Worker entry points. Callers outside WinMTRNet should not use these.
    bool SetAddress(int at, const sockaddr_storage& address);
    void SetIdentity(int at, const std::string& name, const std::string& country,
        const std::string& asn, const std::string& isp);
    void RecordSent(int at);
    void RecordReply(int at, int roundTripTime);
    void RecordTimeout(int at);
    void QueueResolve(int at, const sockaddr_storage& address,
        const TraceConfig& config);
    void FinalizeInFlight();
    bool ShouldProbeHop(int ttl) const;

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
    struct NetHost {
        bool hasAddress;
        sockaddr_storage address;
        uint64_t total;
        uint64_t totalSquares;
        uint64_t jitterTotal;
        int xmit;
        int returned;
        int inFlight;
        int last;
        int best;
        int worst;
        int lastJitter;
        char name[NI_MAXHOST];
        char country[8];
        char asn[24];
        char isp[192];
    };

    struct ResolveRequest {
        int index;
        sockaddr_storage address;
        TraceConfig config;
        uint64_t generation;
    };

    bool SameAddress(const sockaddr_storage& left, const sockaddr_storage& right) const;
    static unsigned __stdcall ResolverThreadEntry(void* parameter);
    void ResolverLoop();

    mutable std::mutex hostMutex;
    std::mutex resolverMutex;
    std::deque<ResolveRequest> resolverQueue;
    NetHost host[64];
    sockaddr_storage remoteAddress;
    int configuredMaxHops;
    std::atomic<int> destinationHop;
    std::atomic<bool> tracing;
    std::atomic<uint64_t> traceGeneration;
    HANDLE stopEvent;
    HANDLE resolverEvent;
    HANDLE resolverStopEvent;
    HANDLE resolverThread;
};

#endif // WINMTRNET_H_
