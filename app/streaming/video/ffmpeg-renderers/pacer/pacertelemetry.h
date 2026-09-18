// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. CHANGED: std::max/std::min are
// parenthesised — (std::max)(...) — because windows.h defines max/min as macros in the TUs
// that include this one, which Nonary's build never hits. Nothing else was touched.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <QMutex>
#include "vrr/readinesswindow.h"

// Pacer work can happen on the decoder, render, V-sync, and VRR worker
// threads. Keep its cumulative measurements separate from VIDEO_STATS, which
// is owned by the decoder thread and is periodically windowed/reset there.
// A snapshot is copied while holding this mutex, so its counters and latest
// state always describe one coherent publication.
struct PacerTelemetrySnapshot {
    uint64_t sequence = 0;

    uint64_t renderedFrames = 0;
    uint64_t pacerDroppedFrames = 0;
    uint64_t totalClientProcessingTimeUs = 0;
    uint64_t totalQueuePacingTimeUs = 0;
    uint64_t totalRenderingTimeUs = 0;

    bool vrrActive = false;
    uint64_t vrrPacingDroppedFrames = 0;
    uint64_t vrrEligibleFrames = 0;
    uint64_t vrrPrepareLateFrames = 0;
    Vrr13::ReadinessWindow::Snapshot vrrReadiness;
    uint64_t vrrOnTimeTargetPerMillion = 0;
    bool vrrBufferAtLimit = false;
    uint64_t vrrQueueResidenceUs = 0;
    uint64_t vrrDecodeWaitUs = 0;
    uint64_t vrrBufferUs = 0;
    uint64_t vrrMotionPairs = 0;
    uint64_t vrrMotionHitches = 0;
    uint64_t vrrCadenceIntervals = 0;
    uint64_t vrrCadenceHitches = 0;
    uint64_t vrrEstimatedCadenceIntervals = 0;
    uint64_t vrrEstimatedCadenceHitches = 0;
    uint64_t vrrTargetWaitEntryLateFrames = 0;
    uint64_t vrrPresentFailedFrames = 0;
    uint64_t vrrPresentCancelledFrames = 0;
    uint64_t vrrSpacingCorrections = 0;

    uint64_t vrrPrepareLatenessP50Us = 0;
    uint64_t vrrPrepareLatenessP95Us = 0;
    uint64_t vrrPrepareLatenessP99Us = 0;
    int64_t vrrSubmitErrorP50Us = 0;
    int64_t vrrSubmitErrorP95Us = 0;
    int64_t vrrSubmitErrorP99Us = 0;
    int64_t vrrSubmitErrorMaxUs = 0;

    // These are a decision-time sample, not an aggregate or a proxy for the
    // target used by a different frame.
    uint64_t vrrStateSequence = 0;
    uint64_t vrrStateSampleTimeUs = 0;
    int64_t vrrReadinessBudgetUs = 0;
    uint64_t vrrTimingBudgetUs = 0;
    uint64_t vrrRenderLeadUs = 0;
    uint64_t vrrRenderWakeLeadUs = 0;
    uint64_t vrrTargetWakeLeadUs = 0;
    uint64_t vrrGuardUs = 0;
    uint64_t vrrSourcePeriodUs = 0;
};

struct VrrTelemetrySample {
    uint64_t queueResidenceUs = 0;
    uint64_t decodeWaitUs = 0;
    uint64_t bufferUs = 0;
    uint64_t submissionUs = 0;
    // Reset only at a real presentation epoch boundary. Local drops and host
    // stalls affect motion and must not erase the following interval.
    bool motionDiscontinuity = false;
    uint64_t decisionTimeUs = 0;
    uint64_t clientProcessingTimeUs = 0;
    uint64_t renderingTimeUs = 0;
    uint64_t preparationLatenessUs = 0;
    // Cumulative verified display-interval counters from the controller.
    uint64_t cadenceIntervals = 0;
    uint64_t cadenceHitches = 0;
    uint64_t estimatedCadenceIntervals = 0;
    uint64_t estimatedCadenceHitches = 0;
    int64_t submitErrorUs = 0;

    bool prepareLate = false;
    bool targetWaitEntryLate = false;
    bool spacingCorrected = false;
    bool presented = false;
    bool cancelled = false;

    int64_t readinessBudgetUs = 0;
    uint64_t timingBudgetUs = 0;
    uint64_t renderLeadUs = 0;
    uint64_t renderWakeLeadUs = 0;
    uint64_t targetWakeLeadUs = 0;
    uint64_t guardUs = 0;
    uint64_t sourcePeriodUs = 0;
};

class PacerTelemetry {
public:
    PacerTelemetrySnapshot snapshot() const
    {
        QMutexLocker lock(&m_Lock);
        PacerTelemetrySnapshot snapshot = m_Snapshot;
        populatePrepareLatenessPercentilesLocked(snapshot);
        populateSubmitErrorPercentilesLocked(snapshot);
        return snapshot;
    }

    void beginVrrSession()
    {
        QMutexLocker lock(&m_Lock);
        m_Snapshot.vrrActive = true;
        touchLocked();
    }

    void recordLegacyDrop()
    {
        QMutexLocker lock(&m_Lock);
        ++m_Snapshot.pacerDroppedFrames;
        touchLocked();
    }

    void recordLegacyFrame(uint64_t clientProcessingTimeUs,
                           uint64_t renderingTimeUs)
    {
        QMutexLocker lock(&m_Lock);
        recordPresentedTimingLocked(clientProcessingTimeUs,
                                    renderingTimeUs);
        touchLocked();
    }

    void recordVrrDrop()
    {
        QMutexLocker lock(&m_Lock);
        ++m_Snapshot.pacerDroppedFrames;
        ++m_Snapshot.vrrPacingDroppedFrames;
        touchLocked();
    }

    void recordVrrReadiness(uint64_t atUs, uint64_t latenessUs, bool dropped,
                           uint64_t targetPerMillion, bool capacityLimited,
                           bool decisionValid, bool thresholdedMissPolicy,
                           bool meaningfulMissesOnly = false, bool intervalPolicy = false,
                           const Vrr13::IntervalBuffer::Stats* interval = nullptr)
    {
        QMutexLocker lock(&m_Lock);
        m_ReadinessWindow.record(atUs, latenessUs, dropped);
        const auto intervalStats = interval ? *interval : m_Snapshot.vrrReadiness.interval;
        m_Snapshot.vrrReadiness = m_ReadinessWindow.snapshot(
            0, thresholdedMissPolicy, meaningfulMissesOnly);
        m_Snapshot.vrrReadiness.intervalPolicy = intervalPolicy;
        m_Snapshot.vrrReadiness.interval = intervalStats;
        m_Snapshot.vrrOnTimeTargetPerMillion = targetPerMillion;
        if (decisionValid) m_Snapshot.vrrBufferAtLimit = capacityLimited;
        touchLocked();
    }

    void recordVrrOutcome(bool presented, bool cancelled)
    {
        QMutexLocker lock(&m_Lock);
        recordVrrOutcomeLocked(presented, cancelled);
        touchLocked();
    }

    void recordVrrFrame(const VrrTelemetrySample& sample)
    {
        QMutexLocker lock(&m_Lock);

        if (sample.motionDiscontinuity) {
            m_LastMotionSubmissionUs = m_LastMotionIntervalUs = 0;
        }
        if (sample.presented && !sample.cancelled && sample.submissionUs) {
            if (m_LastMotionSubmissionUs && sample.submissionUs > m_LastMotionSubmissionUs) {
                const uint64_t interval = sample.submissionUs - m_LastMotionSubmissionUs;
                if (m_LastMotionIntervalUs) {
                    ++m_Snapshot.vrrMotionPairs;
                    const uint64_t jerk = (std::max)(interval, m_LastMotionIntervalUs) -
                        (std::min)(interval, m_LastMotionIntervalUs);
                    m_Snapshot.vrrMotionHitches += jerk > 2000;
                }
                m_LastMotionIntervalUs = interval;
            }
            else {
                m_LastMotionIntervalUs = 0;
            }
            m_LastMotionSubmissionUs = sample.submissionUs;
        }
        ++m_Snapshot.vrrEligibleFrames;
        m_Snapshot.vrrCadenceIntervals = sample.cadenceIntervals;
        m_Snapshot.vrrCadenceHitches = sample.cadenceHitches;
        m_Snapshot.vrrEstimatedCadenceIntervals = sample.estimatedCadenceIntervals;
        m_Snapshot.vrrEstimatedCadenceHitches = sample.estimatedCadenceHitches;
        if (sample.prepareLate) {
            ++m_Snapshot.vrrPrepareLateFrames;
            addPrepareLatenessLocked(sample.preparationLatenessUs);
        }
        if (sample.targetWaitEntryLate) {
            ++m_Snapshot.vrrTargetWaitEntryLateFrames;
        }
        // Submission error is meaningful only for output that was actually
        // presented. Cancelled submissions are reported separately.
        if (sample.presented && !sample.cancelled) {
            addSubmitErrorLocked(sample.submitErrorUs);
        }
        if (sample.spacingCorrected) {
            ++m_Snapshot.vrrSpacingCorrections;
        }
        recordVrrOutcomeLocked(sample.presented, sample.cancelled);
        if (sample.presented && !sample.cancelled) {
            m_Snapshot.vrrQueueResidenceUs += sample.queueResidenceUs;
            m_Snapshot.vrrDecodeWaitUs += sample.decodeWaitUs;
            m_Snapshot.vrrBufferUs += sample.bufferUs;
            recordPresentedTimingLocked(sample.clientProcessingTimeUs,
                                        sample.renderingTimeUs, sample.decodeWaitUs);
        }

        touchLocked();
        m_Snapshot.vrrStateSequence = m_Snapshot.sequence;
        m_Snapshot.vrrStateSampleTimeUs = sample.decisionTimeUs;
        m_Snapshot.vrrReadinessBudgetUs = sample.readinessBudgetUs;
        m_Snapshot.vrrTimingBudgetUs = sample.timingBudgetUs;
        m_Snapshot.vrrRenderLeadUs = sample.renderLeadUs;
        m_Snapshot.vrrRenderWakeLeadUs = sample.renderWakeLeadUs;
        m_Snapshot.vrrTargetWakeLeadUs = sample.targetWakeLeadUs;
        m_Snapshot.vrrGuardUs = sample.guardUs;
        m_Snapshot.vrrSourcePeriodUs = sample.sourcePeriodUs;
    }

private:
    Vrr13::ReadinessWindow m_ReadinessWindow;
    static constexpr size_t kPrepareLatenessSampleCount = 128;
    static constexpr size_t kSubmitErrorSampleCount = 128;

    static size_t percentileIndex(size_t count, size_t percentile)
    {
        // Use the nearest-rank definition. count is nonzero at each call.
        return ((count * percentile + 99) / 100) - 1;
    }

    void touchLocked()
    {
        ++m_Snapshot.sequence;
    }

    void recordVrrOutcomeLocked(bool presented, bool cancelled)
    {
        if (cancelled) {
            ++m_Snapshot.vrrPresentCancelledFrames;
        }
        else if (!presented) {
            ++m_Snapshot.vrrPresentFailedFrames;
        }
    }

    void recordPresentedTimingLocked(uint64_t clientProcessingTimeUs,
                                     uint64_t renderingTimeUs,
                                     uint64_t decodeWaitUs = 0)
    {
        // GPU decode synchronization remains in internal client timing, but
        // is neither rendering nor the queue delay shown in the overlay.
        const uint64_t boundedRenderingTimeUs = (std::min)(
            renderingTimeUs, clientProcessingTimeUs);
        m_Snapshot.totalClientProcessingTimeUs += clientProcessingTimeUs;
        m_Snapshot.totalRenderingTimeUs += boundedRenderingTimeUs;
        m_Snapshot.totalQueuePacingTimeUs +=
            clientProcessingTimeUs - boundedRenderingTimeUs -
            (std::min)(decodeWaitUs, clientProcessingTimeUs - boundedRenderingTimeUs);
        ++m_Snapshot.renderedFrames;
    }

    void addPrepareLatenessLocked(uint64_t latenessUs)
    {
        m_PrepareLatenessSamples[m_NextPrepareLatenessSample] = latenessUs;
        m_NextPrepareLatenessSample =
            (m_NextPrepareLatenessSample + 1) % kPrepareLatenessSampleCount;
        m_PrepareLatenessSampleSize = (std::min)(
            m_PrepareLatenessSampleSize + 1, kPrepareLatenessSampleCount);
    }

    void populatePrepareLatenessPercentilesLocked(
        PacerTelemetrySnapshot& snapshot) const
    {
        if (m_PrepareLatenessSampleSize == 0) {
            return;
        }

        std::array<uint64_t, kPrepareLatenessSampleCount> sortedSamples =
            m_PrepareLatenessSamples;
        std::sort(sortedSamples.begin(),
                  sortedSamples.begin() + m_PrepareLatenessSampleSize);
        snapshot.vrrPrepareLatenessP50Us = sortedSamples[
            percentileIndex(m_PrepareLatenessSampleSize, 50)];
        snapshot.vrrPrepareLatenessP95Us = sortedSamples[
            percentileIndex(m_PrepareLatenessSampleSize, 95)];
        snapshot.vrrPrepareLatenessP99Us = sortedSamples[
            percentileIndex(m_PrepareLatenessSampleSize, 99)];
    }

    void addSubmitErrorLocked(int64_t errorUs)
    {
        m_SubmitErrorSamples[m_NextSubmitErrorSample] = errorUs;
        m_NextSubmitErrorSample =
            (m_NextSubmitErrorSample + 1) % kSubmitErrorSampleCount;
        m_SubmitErrorSampleSize = (std::min)(
            m_SubmitErrorSampleSize + 1, kSubmitErrorSampleCount);
    }

    void populateSubmitErrorPercentilesLocked(
        PacerTelemetrySnapshot& snapshot) const
    {
        if (m_SubmitErrorSampleSize == 0) {
            return;
        }

        std::array<int64_t, kSubmitErrorSampleCount> sortedSamples =
            m_SubmitErrorSamples;
        std::sort(sortedSamples.begin(),
                  sortedSamples.begin() + m_SubmitErrorSampleSize);
        snapshot.vrrSubmitErrorP50Us = sortedSamples[
            percentileIndex(m_SubmitErrorSampleSize, 50)];
        snapshot.vrrSubmitErrorP95Us = sortedSamples[
            percentileIndex(m_SubmitErrorSampleSize, 95)];
        snapshot.vrrSubmitErrorP99Us = sortedSamples[
            percentileIndex(m_SubmitErrorSampleSize, 99)];
        snapshot.vrrSubmitErrorMaxUs = sortedSamples[
            m_SubmitErrorSampleSize - 1];
    }

    mutable QMutex m_Lock;
    PacerTelemetrySnapshot m_Snapshot;
    uint64_t m_LastMotionSubmissionUs = 0;
    uint64_t m_LastMotionIntervalUs = 0;
    std::array<uint64_t, kPrepareLatenessSampleCount> m_PrepareLatenessSamples {};
    size_t m_NextPrepareLatenessSample = 0;
    size_t m_PrepareLatenessSampleSize = 0;
    std::array<int64_t, kSubmitErrorSampleCount> m_SubmitErrorSamples {};
    size_t m_NextSubmitErrorSample = 0;
    size_t m_SubmitErrorSampleSize = 0;
};
