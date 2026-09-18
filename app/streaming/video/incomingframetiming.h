// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Decoder-owned source cadence measurement. Only raw host RTP timestamps enter
// this metric; receive, decode, and presentation timing cannot affect it.
class IncomingFrameTiming
{
public:
    static constexpr size_t WindowSize = 30;

    struct Sample {
        uint64_t sequence = 0;
        double varianceTicksSquared = 0;
        bool valid = false;
    };

    // A soft consistency score, not a probability of perceptible stutter.
    // score = 100 / (1 + (standardDeviationMs / 6)^4).
    // Sub-ms noise contributes very little; sustained large variation matters.
    static double smoothnessPercent(double varianceTicksSquared)
    {
        constexpr double softKneeTicks = 6.0 * 90.0;
        const double normalizedVariance = varianceTicksSquared /
            (softKneeTicks * softKneeTicks);
        return 100.0 / (1.0 + normalizedVariance * normalizedVariance);
    }

    Sample observe(uint32_t frameNumber, uint32_t rtpTimestamp)
    {
        Sample sample;
        sample.sequence = ++m_Sequence;
        const uint32_t interval = rtpTimestamp - m_PreviousTimestamp;
        const bool adjacent = m_HaveFrame && frameNumber - m_PreviousFrame == 1;
        m_PreviousFrame = frameNumber;
        m_PreviousTimestamp = rtpTimestamp;
        m_HaveFrame = true;

        // Unsigned subtraction handles RTP/frame-number wrap. Missing frames,
        // absent/repeated timestamps, and backwards timestamps break the chain.
        if (!adjacent || interval == 0 || interval >= 0x80000000U) {
            m_IntervalCount = 0;
            m_NextInterval = 0;
            return sample;
        }

        m_Intervals[m_NextInterval] = interval;
        m_NextInterval = (m_NextInterval + 1) % WindowSize;
        if (m_IntervalCount < WindowSize) {
            ++m_IntervalCount;
        }
        if (m_IntervalCount < WindowSize) {
            return sample;
        }

        // Population variance of the observed frame times around their own
        // recent mean, not the negotiated FPS. Recompute in two passes so a
        // large stall leaving the window cannot cause cancellation/drift.
        uint64_t total = 0;
        for (const auto ticks : m_Intervals) {
            total += ticks;
        }
        const double mean = double(total) / WindowSize;
        for (const auto ticks : m_Intervals) {
            const double deviation = double(ticks) - mean;
            sample.varianceTicksSquared += deviation * deviation;
        }
        sample.varianceTicksSquared /= WindowSize;
        sample.valid = true;
        return sample;
    }

private:
    std::array<uint32_t, WindowSize> m_Intervals {};
    size_t m_IntervalCount = 0;
    size_t m_NextInterval = 0;
    uint64_t m_Sequence = 0;
    uint32_t m_PreviousFrame = 0;
    uint32_t m_PreviousTimestamp = 0;
    bool m_HaveFrame = false;
};
