#pragma once

// Test-only fixtures for the greenfield VRR worker.  They deliberately model
// just the IVrrFramePresenter boundary: no window, decoder, network, or
// graphics device is created by these tests.

#include "../../app/streaming/video/ffmpeg-renderers/ivrrframepresenter.h"

#include <Limelight.h>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/mem.h>
}

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

class FakeVrrFramePresenter final : public IVrrFramePresenter {
public:
    bool canLatchAdaptivePresent() const override
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_CanLatch;
    }

    uint64_t waitForDecode(AVFrame* frame) override
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        const int number = frame == nullptr ? -1 : static_cast<int>(
            reinterpret_cast<intptr_t>(frame->opaque));
        if (number < 0 || number != m_BlockedDecodeFrame) {
            return 0;
        }
        const uint64_t startUs = LiGetMicroseconds();
        ++m_DecodeWaitCount;
        m_Condition.notify_all();
        while (m_BlockedDecodeFrame == number) {
            m_Condition.wait(lock);
        }
        return LiGetMicroseconds() - startUs;
    }

    uint64_t waitForDecode(AVFrame* frame, uint64_t boundary) override
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_WaitedDecodeBoundaries.push_back(boundary);
        }
        return waitForDecode(frame);
    }

    VrrFallbackReason checkSupport() const override
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Support;
    }

    uint64_t captureDecodeBoundary() override
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ++m_DecodeBoundaryCaptureCount;
        return m_DecodeBoundary;
    }

    VrrPrepareResult prepareFrame(AVFrame* frame, uint64_t decodeBoundary,
                                  const VrrPresentRequest& request) override
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_PrepareRequests.push_back(request);
        }
        return prepareFrame(frame, decodeBoundary);
    }

    VrrPrepareResult prepareFrame(AVFrame* frame,
                                  uint64_t decodeBoundary) override
    {
        VrrPrepareResult result;
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_PreparedFrame = frame;
        m_PreparedDecodeBoundaries.push_back(decodeBoundary);
        m_PreparedFrameNumber = frame == nullptr ? -1 : static_cast<int>(
            reinterpret_cast<intptr_t>(frame->opaque));
        m_PreparedFrames.push_back(m_PreparedFrameNumber);
        ++m_PrepareCount;
        m_Condition.notify_all();

        while ((m_BlockPreparation && !m_ReleasePreparation) ||
               m_PrepareCount > m_PreparationLimit) {
            m_Condition.wait(lock);
        }

        result.prepared = m_PreparationSucceeds && frame != nullptr;
        result.sourceFrameReusable = m_SourceFrameReusable && result.prepared;
        result.cancellationMaySubmit =
            m_CancellationMaySubmit && frame != nullptr;
        if (!result.prepared && !result.cancellationMaySubmit) {
            m_PreparedFrame = nullptr;
        }
        return result;
    }

    VrrPresentFeedback presentAdaptive(
        const VrrPresentRequest& request) override
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_PresentRequests.push_back(request);
        }
        VrrPresentFeedback feedback;
        int frameNumber = -1;
        uint64_t preSubmissionDelayUs = 0;
        uint64_t postSubmissionDelayUs = 0;
        bool presentCancelled = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_PreparedFrame == nullptr) {
                feedback.cancelled = true;
                return feedback;
            }

            frameNumber = m_PreparedFrameNumber;
            m_PreparedFrame = nullptr;
            preSubmissionDelayUs = m_PreSubmissionDelayUs;
            postSubmissionDelayUs = m_PresentDelayUs;
            presentCancelled = m_PresentCancelled;
        }

        // Model native work before and after the actual submission boundary.
        if (preSubmissionDelayUs != 0) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(preSubmissionDelayUs));
        }
        const uint64_t submissionTimeUs = LiGetMicroseconds();

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_PresentCallTimesUs.push_back(submissionTimeUs);
        }

        if (postSubmissionDelayUs != 0) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(postSubmissionDelayUs));
        }

        feedback.presented = true;
        feedback.nativeBackendValid = true;
        feedback.nativeBackend = VrrNativePresentationBackend::Vulkan;
        feedback.nativePresentResultValid = true;
        feedback.nativePresentResult = 0;
        feedback.cancelled = presentCancelled;
        feedback.submissionTimeValid = true;
        feedback.submissionTimeUs = submissionTimeUs;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_PresentedFrames.push_back(frameNumber);
            m_PresentReturnTimesUs.push_back(LiGetMicroseconds());
            ++m_PresentCount;
            m_Condition.notify_all();
        }
        return feedback;
    }

    VrrPresentFeedback cancelFrame() override
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        VrrPresentFeedback feedback;
        feedback.cancelled = true;
        const bool nativeSubmitAttempted =
            m_CancellationMaySubmit && m_PreparedFrame != nullptr;
        if (nativeSubmitAttempted) {
            feedback.nativeBackendValid = true;
            feedback.nativeBackend = VrrNativePresentationBackend::Vulkan;
            feedback.nativePresentResultValid = true;
            feedback.nativePresentResult = m_CancelSubmits ? 0 : -1;
        }
        if (m_CancelSubmits && m_PreparedFrame != nullptr) {
            const int frameNumber = m_PreparedFrameNumber;
            const uint64_t callTimeUs = LiGetMicroseconds();
            m_PresentedFrames.push_back(frameNumber);
            m_PresentCallTimesUs.push_back(callTimeUs);
            m_PresentReturnTimesUs.push_back(callTimeUs);
            ++m_PresentCount;
            feedback.presented = true;
            feedback.submissionTimeValid = true;
            feedback.submissionTimeUs = callTimeUs;
        }
        m_PreparedFrame = nullptr;
        ++m_CancelCount;
        m_Condition.notify_all();
        return feedback;
    }

    void setSuspended(bool suspended) override
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        suspended ? ++m_SuspendedCount : ++m_ResumedCount;
        m_Condition.notify_all();
    }

    void setSupport(VrrFallbackReason support)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Support = support;
    }

    void setCanLatch(bool canLatch)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CanLatch = canLatch;
    }

    void blockDecodeFrame(int number)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_BlockedDecodeFrame = number;
    }

    void releaseDecode()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_BlockedDecodeFrame = -1;
        m_Condition.notify_all();
    }

    void setPreparationLimit(size_t count)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PreparationLimit = count;
        m_Condition.notify_all();
    }

    void setCancelSubmits(bool enabled)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CancelSubmits = enabled;
    }

    void setPreparationSucceeds(bool succeeds)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PreparationSucceeds = succeeds;
    }

    void setCancellationMaySubmit(bool maySubmit)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CancellationMaySubmit = maySubmit;
    }

    void setSourceFrameReusable(bool reusable)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_SourceFrameReusable = reusable;
    }

    void setDecodeBoundary(uint64_t decodeBoundary)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_DecodeBoundary = decodeBoundary;
    }

    void setPresentDelayUs(uint64_t delayUs)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PresentDelayUs = delayUs;
    }

    void setPreSubmissionDelayUs(uint64_t delayUs)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PreSubmissionDelayUs = delayUs;
    }

    void setPresentCancelled(bool cancelled)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PresentCancelled = cancelled;
    }

    void blockPreparation()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_BlockPreparation = true;
        m_ReleasePreparation = false;
    }

    void releasePreparation()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_BlockPreparation = false;
        m_ReleasePreparation = true;
        m_Condition.notify_all();
    }

    bool waitForPrepareCount(size_t count,
                             std::chrono::milliseconds timeout =
                                 std::chrono::milliseconds(2000))
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        return m_Condition.wait_for(lock, timeout, [&] {
            return m_PrepareCount >= count;
        });
    }

    bool waitForDecodeWaitCount(size_t count)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        return m_Condition.wait_for(lock, std::chrono::milliseconds(2000), [&] {
            return m_DecodeWaitCount >= count;
        });
    }

    bool waitForPresentCount(size_t count,
                             std::chrono::milliseconds timeout =
                                 std::chrono::milliseconds(2000))
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        return m_Condition.wait_for(lock, timeout, [&] {
            return m_PresentCount >= count;
        });
    }

    bool waitForCancelCount(size_t count,
                            std::chrono::milliseconds timeout =
                                std::chrono::milliseconds(2000))
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        return m_Condition.wait_for(lock, timeout, [&] {
            return m_CancelCount >= count;
        });
    }

    size_t presentCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PresentCount;
    }

    size_t cancelCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_CancelCount;
    }

    size_t resumedCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_ResumedCount;
    }

    size_t suspendedCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_SuspendedCount;
    }

    std::vector<int> presentedFrames() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PresentedFrames;
    }

    std::vector<int> preparedFrames() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PreparedFrames;
    }

    std::vector<uint64_t> presentCallTimesUs() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PresentCallTimesUs;
    }

    std::vector<uint64_t> presentReturnTimesUs() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PresentReturnTimesUs;
    }

    std::vector<VrrPresentRequest> presentRequests() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PresentRequests;
    }

    std::vector<VrrPresentRequest> prepareRequests() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PrepareRequests;
    }

    size_t decodeBoundaryCaptureCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_DecodeBoundaryCaptureCount;
    }

    std::vector<uint64_t> preparedDecodeBoundaries() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PreparedDecodeBoundaries;
    }

    std::vector<uint64_t> waitedDecodeBoundaries() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_WaitedDecodeBoundaries;
    }

private:
    mutable std::mutex m_Mutex;
    std::condition_variable m_Condition;
    VrrFallbackReason m_Support = VrrFallbackReason::NoFallback;
    bool m_CanLatch = false;
    bool m_PreparationSucceeds = true;
    bool m_CancellationMaySubmit = false;
    bool m_SourceFrameReusable = false;
    bool m_CancelSubmits = false;
    bool m_BlockPreparation = false;
    bool m_ReleasePreparation = false;
    bool m_PresentCancelled = false;
    uint64_t m_PreSubmissionDelayUs = 0;
    uint64_t m_PresentDelayUs = 0;
    uint64_t m_DecodeBoundary = 0;
    int m_BlockedDecodeFrame = -1;
    size_t m_DecodeWaitCount = 0;
    size_t m_PreparationLimit = std::numeric_limits<size_t>::max();
    size_t m_PrepareCount = 0;
    size_t m_PresentCount = 0;
    size_t m_CancelCount = 0;
    size_t m_SuspendedCount = 0;
    size_t m_ResumedCount = 0;
    size_t m_DecodeBoundaryCaptureCount = 0;
    AVFrame* m_PreparedFrame = nullptr;
    int m_PreparedFrameNumber = -1;
    std::vector<int> m_PreparedFrames;
    std::vector<int> m_PresentedFrames;
    std::vector<VrrPresentRequest> m_PresentRequests;
    std::vector<VrrPresentRequest> m_PrepareRequests;
    std::vector<uint64_t> m_PreparedDecodeBoundaries;
    std::vector<uint64_t> m_WaitedDecodeBoundaries;
    std::vector<uint64_t> m_PresentCallTimesUs;
    std::vector<uint64_t> m_PresentReturnTimesUs;
};

struct TrackedFrameLifetime {
    std::atomic_uint releases { 0 };
};

inline void releaseTrackedFrameBuffer(void* opaque, uint8_t* data)
{
    static_cast<TrackedFrameLifetime*>(opaque)->releases.fetch_add(1);
    av_free(data);
}

inline PacedFrame makeTrackedPacedFrame(int frameNumber,
                                        uint32_t rtpTimestamp,
                                        uint64_t decodeCompleteUs,
                                        TrackedFrameLifetime& lifetime)
{
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        return {};
    }

    uint8_t* data = static_cast<uint8_t*>(av_malloc(1));
    if (data == nullptr) {
        av_frame_free(&frame);
        return {};
    }

    frame->buf[0] = av_buffer_create(data, 1, releaseTrackedFrameBuffer,
                                     &lifetime, 0);
    if (frame->buf[0] == nullptr) {
        av_free(data);
        av_frame_free(&frame);
        return {};
    }

    frame->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(frameNumber));

    return PacedFrame(frame, frameNumber, rtpTimestamp, true, decodeCompleteUs);
}
