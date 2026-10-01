// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "gputrace.h"
#include "SDL_compat.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <chrono>
#ifdef Q_OS_LINUX
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#endif

GpuTrace::ThreadSample GpuTrace::ThreadSample::capture()
{
    ThreadSample result;
#ifdef Q_OS_LINUX
    timespec cpu{};
    rusage usage{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu) == 0 &&
        getrusage(RUSAGE_THREAD, &usage) == 0) {
        result.cpuUs = int64_t(cpu.tv_sec) * 1000000 + cpu.tv_nsec / 1000;
        result.voluntary = usage.ru_nvcsw;
        result.involuntary = usage.ru_nivcsw;
        result.tid = syscall(SYS_gettid);
    }
#endif
    return result;
}

void GpuTrace::recordThreadSpan(Row row, const ThreadSample& before)
{
    const auto after = ThreadSample::capture();
    row.b = row.c = row.d = row.e = -1;
    if (before.tid >= 0 && before.tid == after.tid) {
        row.b = after.cpuUs - before.cpuUs;
        row.c = after.voluntary - before.voluntary;
        row.d = after.involuntary - before.involuntary;
        row.e = after.tid;
    }
    record(row);
}

std::unique_ptr<GpuTrace> GpuTrace::create()
{
    const auto base = qEnvironmentVariable("MOONLIGHT_VRR_TRACE");
    if (!qEnvironmentVariable("MOONLIGHT_VRR_DEEP_TRACE").startsWith(QLatin1Char('1')) ||
        base.isEmpty() || base.startsWith(QStringLiteral("//")) ||
        base.startsWith(QStringLiteral("\\\\"))) return {};
    static std::atomic<uint64_t> sequence{0};
    const auto path = base + QStringLiteral(".gpu-%1-%2-%3.csv")
        .arg(QCoreApplication::applicationPid()).arg(QDateTime::currentMSecsSinceEpoch())
        .arg(sequence.fetch_add(1));
    return std::unique_ptr<GpuTrace>(new GpuTrace(path));
}

GpuTrace::GpuTrace(const QString& path) : m_Thread([this, path] { write(path); }) {}

GpuTrace::~GpuTrace()
{
    // Decoder and renderer producers have stopped before renderer destruction.
    m_Stop.store(true);
    m_Thread.join();
}

void GpuTrace::record(Row row)
{
    if (!m_Accept.load(std::memory_order_relaxed) || !m_Queue.push(std::move(row)))
        m_Dropped.fetch_add(1, std::memory_order_relaxed);
}

void GpuTrace::write(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        m_Accept.store(false);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU trace open failed: %s", qPrintable(path));
        return;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPU diagnostics v4: %s", qPrintable(path));
    QByteArray batch("# gpu_trace_version=4; decoder_output_query=disabled; worker_status_query=disabled; thread_counters=enabled_where_supported; CPU times=LiGetMicroseconds; shader samples=delayed GPU durations, NOT current-frame timestamps\n"
                     "event,rtp_pts,decoder_output_us,begin_us,end_us,object_id,a,b,c,d,e,detail\n");
    bool healthy = true;
    bool truncated = false;
    for (;;) {
        const bool stopping = m_Stop.load();
        Row row;
        while (m_Queue.pop(row)) {
            batch += row.event;
            const auto append = [&batch](auto value) { batch += ','; batch += QByteArray::number(value); };
            append(qlonglong(row.pts)); append(qulonglong(row.outputUs));
            append(qulonglong(row.beginUs)); append(qulonglong(row.endUs)); append(qulonglong(row.object));
            append(qlonglong(row.a)); append(qlonglong(row.b)); append(qlonglong(row.c));
            append(qlonglong(row.d)); append(qlonglong(row.e));
            batch += ",\"";
            // CSV escaping stays on the writer thread, including shader names.
            for (char ch : row.detail) {
                if (!ch) break;
                if (ch == '"') batch += '"';
                batch += ch;
            }
            batch += "\"\n";
            if (batch.size() >= 65536) break;
        }
        if (!batch.isEmpty()) {
            healthy = file.write(batch) == batch.size();
            batch.clear();
        } else if (stopping) break;
        if (!healthy || file.pos() >= 256 * 1024 * 1024) {
            truncated = true;
            m_Accept.store(false);
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU trace stopped: write failure or 256 MiB cap: %s", qPrintable(path));
            break;
        }
        if (!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto footer = QByteArray("# closed=1; truncated=") + (truncated ? "1" : "0") +
        "; dropped_rows=" + QByteArray::number(qulonglong(m_Dropped.load())) + '\n';
    healthy = healthy && file.write(footer) == footer.size() && file.flush();
    if (!healthy) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU trace write failed: %s", qPrintable(path));
}
