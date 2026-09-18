#include "../../app/streaming/video/incomingframetiming.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {
int failures = 0;
using Sample = IncomingFrameTiming::Sample;

void check(bool condition, const char* description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

void near(double actual, double expected, const char* description)
{
    check(std::abs(actual - expected) < 0.000001, description);
}

struct Stream {
    IncomingFrameTiming timing;
    uint32_t frame = 1;
    uint32_t timestamp = 0;
    Stream() { timing.observe(frame, timestamp); }
    Sample next(uint32_t ticks) { timestamp += ticks; return timing.observe(++frame, timestamp); }
};

Sample measure(const std::vector<uint32_t>& intervals)
{
    Stream stream;
    Sample sample;
    for (const auto ticks : intervals) { sample = stream.next(ticks); }
    return sample;
}

double score(Sample sample)
{
    check(sample.valid, "score needs a complete source interval window");
    return IncomingFrameTiming::smoothnessPercent(sample.varianceTicksSquared);
}

void steadyCadence(uint32_t ticks)
{
    Stream stream;
    for (size_t i = 0; i < 300; ++i) {
        const auto sample = stream.next(ticks);
        check(sample.valid == (i >= 29), "require 30 complete frame times before scoring");
        if (sample.valid) {
            near(sample.varianceTicksSquared, 0, "steady cadence has zero variance at every rate");
            near(score(sample), 100, "steady cadence scores 100 percent");
        }
    }
}
}

int main()
{
    steadyCadence(750);  // 120 FPS
    steadyCadence(1500); // 60 FPS
    steadyCadence(1800); // 50 FPS
    steadyCadence(3000); // 30 FPS cutscene
    steadyCadence(3003); // 29.97 FPS

    double previous = 100;
    for (const uint32_t amplitude : {1U, 45U, 90U, 180U, 270U, 540U}) {
        std::vector<uint32_t> intervals;
        for (int i = 0; i < 30; ++i) { intervals.push_back(i % 2 ? 1500 + amplitude : 1500 - amplitude); }
        const auto sample = measure(intervals);
        near(sample.varianceTicksSquared, double(amplitude) * amplitude,
             "alternating deviations have the expected population variance");
        const double current = score(sample);
        check(current < previous && current > 0, "larger variance lowers the score continuously");
        previous = current;
        if (amplitude == 90) { near(current, 99.92289899768698, "1 ms standard deviation remains essentially smooth"); }
        if (amplitude == 540) { near(current, 50, "6 ms standard deviation is the soft knee"); }
    }
    std::vector<uint32_t> small(30, 1500);
    for (int i = 0; i < 30; ++i) { small[i] += i % 2 ? 40 : -40; }
    check(score(measure(small)) > 99.99, "sub-ms adjacent variation must not read as 90-95 percent smooth");

    Stream transition;
    for (int i = 0; i < 30; ++i) { transition.next(1500); }
    for (int i = 0; i < 30; ++i) {
        const auto sample = transition.next(1800);
        check(score(sample) > 99.4, "60 to 50 FPS shift must not look like severe jitter");
        if (i == 29) { near(score(sample), 100, "new steady cadence settles after 30 intervals"); }
    }

    previous = 100;
    for (const int stalls : {1, 2, 4, 8}) {
        std::vector<uint32_t> intervals(30, 1500);
        for (int i = 0; i < stalls; ++i) { intervals[i * 3] = 3000; }
        const double current = score(measure(intervals));
        check(current < previous, "more repeated stalls in the window lower smoothness");
        previous = current;
    }
    std::vector<uint32_t> moderate(30, 1500);
    std::vector<uint32_t> severe(30, 1500);
    moderate[15] = 9000;
    severe[15] = 18000;
    check(score(measure(severe)) < score(measure(moderate)), "longer host stalls remain more severe");

    Stream expiry;
    for (int i = 0; i < 30; ++i) { expiry.next(1500); }
    check(score(expiry.next(9000)) < 50, "a 100 ms stall must affect the score");
    for (int i = 0; i < 29; ++i) { check(score(expiry.next(1500)) < 50, "stall stays in the 30-interval window"); }
    near(score(expiry.next(1500)), 100, "stall expires exactly after 30 subsequent frame times");
    expiry.next(0x7FFFFFFFU); // Exercise cancellation after a large but forward timestamp gap.
    for (int i = 0; i < 30; ++i) { expiry.next(1500); }
    near(score(expiry.next(1500)), 100, "large expired stalls leave no floating point drift");

    Stream loss;
    for (int i = 0; i < 30; ++i) { loss.next(1500); }
    ++loss.frame;
    check(!loss.next(3000).valid, "missing frames invalidate coverage without blaming host jitter");
    for (int i = 0; i < 29; ++i) { check(!loss.next(1500).valid, "loss needs a fresh window"); }
    near(score(loss.next(1500)), 100, "coverage recovers with 30 new intervals");
    const auto duplicate = loss.timing.observe(loss.frame, loss.timestamp);
    check(!duplicate.valid, "duplicate frames invalidate old evidence");
    check(!loss.timing.observe(loss.frame - 1, loss.timestamp - 1500).valid,
          "out-of-order frames invalidate coverage");

    IncomingFrameTiming wrap;
    uint32_t frame = UINT32_MAX - 29;
    uint32_t timestamp = UINT32_MAX - 22499;
    wrap.observe(frame, timestamp);
    Sample wrapped;
    for (int i = 0; i < 30; ++i) { timestamp += 750; wrapped = wrap.observe(++frame, timestamp); }
    near(score(wrapped), 100, "RTP and frame number wrap including timestamp zero remain valid");
    const auto backwards = wrap.observe(++frame, timestamp - 1);
    check(!backwards.valid && backwards.sequence > wrapped.sequence,
          "new invalid timing replaces an older valid stats snapshot");

    IncomingFrameTiming absent;
    for (uint32_t i = 0; i < 60; ++i) {
        check(!absent.observe(i, 0).valid, "missing timestamps must display N/A");
    }
    return failures == 0 ? 0 : 1;
}
