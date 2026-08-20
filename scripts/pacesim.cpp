// pacesim - a discrete model of the D3D11VA present path under SyncInterval >= 2.
//
// Purpose: choose between maximum-frame-latency caps WITHOUT another runtime round.
// A simulation only proves what its model says, so it has to reproduce what we have
// already measured on real hardware before any prediction of it is worth reading:
//
//   A  cap 3, depth 1   pre-5.1.1 healthy  block ~0,  render total 0.87, 60 pres/s
//   B  cap 3, depth 3   pre-5.1.1 broken   block 4-17 ms sawtooth,       60 pres/s
//   C  cap 1, 1080p     5.1.2 now          block ~5 then ~15.8 and stays, 60 pres/s
//   D  cap 1, 4K        5.1.2 now          46 pres/s of 57, block avg 20 max 33
//   E  cap 1 + gate     5.1.1              queue delay 10.86, render total 1.15
//
// ⚠️⚠️ AS IT STANDS THE MODEL FAILS THAT BAR: 3 of the 5 cases above do not come out.
// Its absolute numbers are therefore NOT USABLE and must never be quoted as measurements.
// What survives is the ORDERING - which cap beats which, and why - because that is
// arithmetic on the queue rather than a consequence of the constants being right, and it
// is the only thing the cap-2 decision rested on. Run it and read PART 1 first: the
// verdict per case is printed, so this is checked on every run rather than remembered.
//
// ⚠️ The first version of this file locked the panel to exactly twice the stream rate.
// That deleted the very thing under study: with the two clocks identical the phase
// never moves, nothing ever blocks, and four of the five cases came out flat zero.
// Nothing synchronises a host's software-timed 60 FPS with a panel's crystal, so the
// skew between them is a parameter here - and its SIGN decides whether the render loop
// is paced by the arriving frames or by the display.
//
// What it adds that no metric in the app can: end-to-end latency, arrival to photons.
// Both "frame queue delay" and "rendering time" are CPU-side and blind to frames
// sitting in the driver's queue, which is why this investigation kept mistaking one
// for the other.

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <random>

namespace {

const double kVblankNominal = 1000000.0 / 120.0;   // 8333.33 us at a nominal 120 Hz
const double kFrame         = 1000000.0 / 60.0;    // 16666.67 us at 60 FPS
const int    kSync          = 2;

struct Params {
    const char* name;
    int    maxLatency;
    double renderUs;
    double renderJitterUs;
    double arrivalJitterUs;
    int    primeDepth;      // presents already queued at start - how depth 3 happens
    bool   gate;            // 5.1.1: wait for a slot BEFORE latching a frame
    double skewPpm;         // panel clock vs stream clock; + = panel slower
};

struct Result {
    double presentsPerSec;
    double blockMin, blockAvg, blockMax;
    double queueDelayAvg;
    double renderTotalAvg;
    double latencyAvg, latencyMax;
    int    slotsMissed;
    std::vector<double> blockTrace;
};

Result run(const Params& p, double seconds, uint32_t seed)
{
    const double V = kVblankNominal * (1.0 + p.skewPpm * 1e-6);
    auto ceilToVblank = [V](double t) { return std::ceil(t / V - 1e-9) * V; };

    std::mt19937 rng(seed);
    auto jitter = [&](double amp) {
        if (amp <= 0) return 0.0;
        std::uniform_real_distribution<double> d(-amp, amp);
        return d(rng);
    };

    std::vector<double> arrival;
    int frames = (int)(seconds * 60.0) + 16;
    for (int i = 0; i < frames; i++) arrival.push_back(i * kFrame + jitter(p.arrivalJitterUs));
    std::sort(arrival.begin(), arrival.end());

    std::deque<double> pending;
    double lastDisplay = -kSync * V;
    double now = 0.0;

    for (int i = 0; i < p.primeDepth; i++) {
        double disp = std::max(ceilToVblank(now), lastDisplay + kSync * V);
        pending.push_back(disp);
        lastDisplay = disp;
    }

    double blockSum = 0, blockMin = 1e18, blockMax = 0;
    double queueSum = 0, renderSum = 0, latSum = 0, latMax = 0;
    int presented = 0, missed = 0;
    double prevDisp = -1;

    std::vector<double> trace;
    double traceSum = 0; int traceN = 0; double traceNext = 1000000.0;

    size_t next = 0;
    auto retire = [&](double t) { while (!pending.empty() && pending.front() <= t) pending.pop_front(); };

    while (now < seconds * 1000000.0 && next < arrival.size()) {
        if (p.gate) {
            retire(now);
            while ((int)pending.size() >= p.maxLatency) { now = pending.front(); retire(now); }
        }

        if (arrival[next] > now) now = arrival[next];
        size_t take = next;
        while (take + 1 < arrival.size() && arrival[take + 1] <= now) take++;
        double frameArrival = arrival[take];
        next = take + 1;

        double renderStart = now;
        queueSum += renderStart - frameArrival;
        now += p.renderUs + jitter(p.renderJitterUs);

        double blockStart = now;
        retire(now);
        while ((int)pending.size() >= p.maxLatency) { now = pending.front(); retire(now); }
        double block = now - blockStart;
        blockSum += block;
        blockMin = std::min(blockMin, block);
        blockMax = std::max(blockMax, block);
        renderSum += now - renderStart;

        double disp = std::max(ceilToVblank(now), lastDisplay + kSync * V);
        pending.push_back(disp);
        if (prevDisp >= 0 && disp - prevDisp > kSync * V + 1.0) missed++;
        prevDisp = disp;
        lastDisplay = disp;

        double lat = disp - frameArrival;
        latSum += lat; latMax = std::max(latMax, lat);
        presented++;

        traceSum += block; traceN++;
        if (now >= traceNext) { trace.push_back(traceSum / traceN); traceSum = 0; traceN = 0; traceNext += 1000000.0; }
    }

    Result r{};
    r.presentsPerSec = presented / seconds;
    r.blockMin = blockMin / 1000.0;
    r.blockAvg = blockSum / presented / 1000.0;
    r.blockMax = blockMax / 1000.0;
    r.queueDelayAvg = queueSum / presented / 1000.0;
    r.renderTotalAvg = renderSum / presented / 1000.0;
    r.latencyAvg = latSum / presented / 1000.0;
    r.latencyMax = latMax / 1000.0;
    r.slotsMissed = missed;
    r.blockTrace = trace;
    return r;
}

// The one number from each measured case that the model has to land on for that case
// to count as reproduced. Deliberately a computed check and not a flag someone sets by
// hand: a hand-set verdict goes stale in both directions, and the direction that would
// hurt is a model somebody improved while the file went on calling it broken.
struct Expect {
    const char* text;       // what the hardware said, verbatim, for the reader
    const char* metric;     // which figure below is the decisive one
    double lo, hi;          // the band it has to fall in
};

int g_cases = 0;
int g_reproduced = 0;

void report(const Params& p, const Result& r, const Expect& e)
{
    double actual = 0.0;
    if      (!strcmp(e.metric, "block"))   actual = r.blockAvg;
    else if (!strcmp(e.metric, "qdelay"))  actual = r.queueDelayAvg;
    else if (!strcmp(e.metric, "pres/s"))  actual = r.presentsPerSec;
    else if (!strcmp(e.metric, "blockend"))
        actual = r.blockTrace.empty() ? 0.0 : r.blockTrace.back() / 1000.0;

    bool ok = (actual >= e.lo && actual <= e.hi);
    g_cases++;
    if (ok) g_reproduced++;

    printf("%-24s cap=%d skew=%+.0fppm render=%.2fms\n",
           p.name, p.maxLatency, p.skewPpm, p.renderUs / 1000.0);
    printf("   pres/s %5.1f | block %5.2f/%5.2f/%5.2f | qdelay %5.2f | rtotal %5.2f | "
           "LATENCY %5.1f (max %5.1f) | missed %d\n",
           r.presentsPerSec, r.blockMin, r.blockAvg, r.blockMax,
           r.queueDelayAvg, r.renderTotalAvg, r.latencyAvg, r.latencyMax, r.slotsMissed);
    printf("   hardware said: %s\n", e.text);
    printf("   %s  %s = %.2f, needed %.2f..%.2f\n",
           ok ? "[reproduces]" : "[DOES NOT REPRODUCE]", e.metric, actual, e.lo, e.hi);
    printf("   block/s:");
    for (size_t i = 0; i < r.blockTrace.size() && i < 16; i++) printf(" %.1f", r.blockTrace[i] / 1000.0);
    printf("\n\n");
}

} // namespace

int main()
{
    const double S = 20.0;

    printf("=== PART 1 - validation against measured hardware ===\n\n");

    // Panel slightly SLOWER than twice the stream rate: the display is the bottleneck.
    const double kSlowPanel = +900.0;
    // Panel slightly FASTER: the arriving frames are the bottleneck.
    const double kFastPanel = -900.0;

    Params a{"A pre-5.1.1 healthy", 3,  850,  150,  300, 0, false, kFastPanel};
    Params b{"B pre-5.1.1 broken",  3,  850,  150,  300, 3, false, kSlowPanel};
    Params c{"C 5.1.2 now 1080p",   1,  850,  150,  300, 0, false, kSlowPanel};
    Params d{"D 5.1.2 now 4K",      1, 9000, 6000, 3000, 0, false, kSlowPanel};
    Params e{"E 5.1.1 gate",        1,  850,  150,  300, 0, true,  kSlowPanel};

    report(a, run(a, S, 1), {"block ~0, rtotal 0.87, qdelay 0.03, 60 pres/s",   "block",    0.0,  0.5});
    report(b, run(b, S, 2), {"block 4-17ms sawtooth, 60 pres/s",                "block",    4.0, 17.0});
    report(c, run(c, S, 3), {"block ~5 then ~15.8 and stays, qdelay 0.84",      "blockend", 14.0, 17.5});
    report(d, run(d, S, 4), {"46 pres/s of 57, block avg 20 max 33",            "pres/s",   43.0, 49.0});
    report(e, run(e, S, 5), {"qdelay 10.86, rtotal 1.15",                       "qdelay",    9.0, 13.0});

    printf("--- %d of %d measured cases reproduced ---\n\n", g_reproduced, g_cases);
    if (g_reproduced < g_cases) {
        printf("*** READ THIS BEFORE USING PART 2 ***\n"
               "The model does not reproduce every case measured on hardware, so its\n"
               "ABSOLUTE NUMBERS BELOW ARE NOT USABLE. What survives is the ORDERING -\n"
               "which cap is better than which, and why - because that part is arithmetic\n"
               "on the queue and does not depend on the constants being right. That is the\n"
               "only thing the cap-2 decision was taken on. See CLAUDE.md section 49.\n\n");
    }

    printf("=== PART 2 - the caps, 1080p, panel slower (worst sign) ===\n\n");
    for (int cap = 1; cap <= 4; cap++) {
        for (int prime = 0; prime <= 3; prime++) {
            if (prime >= cap && prime != 0) continue;
            Params p{"", cap, 850, 150, 300, prime, false, kSlowPanel};
            Result r = run(p, S, 100 + cap * 10 + prime);
            printf("  cap %d primed %d :  block %5.2f  qdelay %5.2f  rtotal %5.2f  "
                   "LATENCY %5.1f ms  missed %d\n",
                   cap, prime, r.blockAvg, r.queueDelayAvg, r.renderTotalAvg,
                   r.latencyAvg, r.slotsMissed);
        }
    }

    printf("\n=== PART 3 - the caps when rendering is slow (4K), panel slower ===\n\n");
    for (int cap = 1; cap <= 4; cap++) {
        Params p{"", cap, 9000, 6000, 3000, 0, false, kSlowPanel};
        Result r = run(p, S, 200 + cap);
        printf("  cap %d :  pres/s %5.1f  block %5.2f  LATENCY %5.1f ms  missed %d\n",
               cap, r.presentsPerSec, r.blockAvg, r.latencyAvg, r.slotsMissed);
    }

    printf("\n=== PART 4 - does the sign of the skew decide everything? ===\n\n");
    for (int cap = 1; cap <= 3; cap++) {
        for (double skew : {-900.0, -50.0, +50.0, +900.0}) {
            Params p{"", cap, 850, 150, 300, 0, false, skew};
            Result r = run(p, S, 300 + cap);
            printf("  cap %d skew %+6.0f ppm :  block %5.2f  LATENCY %5.1f ms  pres/s %5.1f\n",
                   cap, skew, r.blockAvg, r.latencyAvg, r.presentsPerSec);
        }
    }

    return 0;
}
