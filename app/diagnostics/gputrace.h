// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "streaming/video/ffmpeg-renderers/pacer/vrr/tracequeue.h"
#include <QString>
#include <atomic>
#include <memory>
#include <thread>

// Independent live diagnostics. No replay schema or scheduling inputs.
class GpuTrace {
public:
    struct Row {
        const char* event = "unknown"; // static literal, never frame-owned data
        int64_t pts = -1;
        uint64_t outputUs = 0;
        uint64_t beginUs = 0;
        uint64_t endUs = 0;
        uint64_t object = 0;
        int64_t a = 0, b = 0, c = 0, d = 0, e = 0;
        char detail[192] = {}; // bounded copy; no driver-owned pointers
    };
    static std::unique_ptr<GpuTrace> create();
    struct ThreadSample {
        int64_t cpuUs = -1, voluntary = -1, involuntary = -1, tid = -1;
        static ThreadSample capture();
    };
    // Preserve a=result; add per-thread CPU and scheduling counters to b..e.
    // These observations do not call into a decoder or graphics driver.
    void recordThreadSpan(Row row, const ThreadSample& before);
    ~GpuTrace();
    void record(Row row);

private:
    explicit GpuTrace(const QString& path);
    void write(const QString& path);
    Vrr13::TraceQueue<Row, 16384> m_Queue;
    std::atomic<bool> m_Stop{false}, m_Accept{true};
    std::atomic<uint64_t> m_Dropped{0};
    std::thread m_Thread;
};
