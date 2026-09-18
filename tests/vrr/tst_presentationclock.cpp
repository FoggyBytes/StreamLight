#include "../../app/streaming/video/ffmpeg-renderers/presentationclock.h"

#include <cstdio>
#include <limits>

int main()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    };
    const auto sample = PresentationClockSample::translate(90000, 100000, 20000, 20004);
    expect(sample.timeUs == 19002 && sample.uncertaintyUs == 3,
           "interrupt time is translated into the midpoint of the worker clock bracket");
    const auto laterEpoch = PresentationClockSample::translate(1090000, 1100000, 120000, 120004);
    expect(laterEpoch.timeUs == 119002, "a fresh correlation tolerates a changed clock epoch");
    expect(!PresentationClockSample::translate(100001, 100000, 20000, 20004).timeUs,
           "a future display timestamp is not accepted");
    expect(!PresentationClockSample::translate(0, 100000, 20000, 20004).timeUs,
           "a missing display timestamp is not accepted");
    expect(!PresentationClockSample::translate(1, 1000002, 200000, 200004).timeUs,
           "events older than the feedback matching window are rejected");
    expect(!PresentationClockSample::translate(90000, 100000, 20004, 20000).timeUs,
           "backwards worker clock brackets are rejected");
    expect(!PresentationClockSample::translate(90000, 100000, 20000, 20501).timeUs,
           "scheduler stalls cannot create a precise timestamp");
    expect(!PresentationClockSample::translate(90000, 100000, 10, 14).timeUs,
           "samples before the worker clock epoch cannot underflow");
    const auto max = std::numeric_limits<uint64_t>::max();
    expect(PresentationClockSample::qpcTo100ns(12345678, 10000000) == 12345678,
           "10 MHz QPC already uses 100 ns units");
    expect(PresentationClockSample::qpcTo100ns(30000000, 24000000) == 12500000,
           "non-10 MHz QPC must be scaled, not treated as 100 ns ticks");
    expect(PresentationClockSample::qpcTo100ns(max, 10000000) == max,
           "scaling large QPC values must not multiply the full counter");
    expect(!PresentationClockSample::qpcTo100ns(1, 0) &&
           !PresentationClockSample::qpcTo100ns(max, 1),
           "invalid frequency or unrepresentable time must fail closed");
    // Reproduces the observed 20 ms QPC/interrupt-clock epoch difference:
    // a displayed event 8 ms ago appeared 12 ms in the future with the old clock.
    const auto qpcNow = PresentationClockSample::qpcTo100ns(30000000, 24000000);
    const auto verified = PresentationClockSample::translate(qpcNow - 80000, qpcNow, 50000, 50004);
    expect(verified.timeUs == 42002,
           "display time must correlate to the presentation QPC clock");
    expect(!PresentationClockSample::translate(qpcNow - 80000, qpcNow - 200000, 50000, 50004).timeUs,
           "using the interrupt-clock epoch reproduces the rejected display event");
    const auto large = PresentationClockSample::translate(max - 10000, max, max - 4, max);
    expect(large.timeUs == max - 1002, "large counter values do not overflow");
    return failures ? 1 : 0;
}
