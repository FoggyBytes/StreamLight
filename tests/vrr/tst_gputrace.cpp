#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <set>
#include "diagnostics/gputrace.h"
#include "diagnostics/diagnosticzip.h"

class GpuTraceTest : public QObject {
    Q_OBJECT
private slots:
    void optInAndPaths();
    void concurrentDrainAndExport();
    void threadCounters();
};

void GpuTraceTest::threadCounters()
{
    const auto before = GpuTrace::ThreadSample::capture();
    QTest::qSleep(15);
    const auto after = GpuTrace::ThreadSample::capture();
#ifdef Q_OS_LINUX
    QVERIFY(before.tid > 0);
    QCOMPARE(before.tid, after.tid);
    QVERIFY(after.cpuUs >= before.cpuUs);
    QVERIFY(after.voluntary > before.voluntary);
    QVERIFY(after.involuntary >= before.involuntary);
#else
    QCOMPARE(before.tid, int64_t(-1));
    QCOMPARE(after.tid, int64_t(-1));
#endif
}

void GpuTraceTest::optInAndPaths()
{
    qunsetenv("MOONLIGHT_VRR_TRACE");
    qputenv("MOONLIGHT_VRR_DEEP_TRACE", "1");
    QVERIFY(!GpuTrace::create());
    qputenv("MOONLIGHT_VRR_TRACE", "//server/share/capture.vrrtrace");
    QVERIFY(!GpuTrace::create());
    qputenv("MOONLIGHT_VRR_TRACE", "\\\\server\\share\\capture.vrrtrace");
    QVERIFY(!GpuTrace::create());
    QTemporaryDir dir;
    qputenv("MOONLIGHT_VRR_TRACE", dir.filePath("Moonlight.vrrtrace").toUtf8());
    qputenv("MOONLIGHT_VRR_DEEP_TRACE", "0");
    QVERIFY(!GpuTrace::create());
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0);
}

void GpuTraceTest::concurrentDrainAndExport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("MOONLIGHT_VRR_TRACE", dir.filePath("Moonlight.vrrtrace").toUtf8());
    qputenv("MOONLIGHT_VRR_DEEP_TRACE", "1");
    auto trace = GpuTrace::create();
    QVERIFY(trace);
    auto producer = [&trace](int offset) {
        for (int i = 0; i < 4000; ++i) {
            const auto id = offset + i;
            trace->record({"fixture", id, uint64_t(id + 1), 100, 200, 0, 7});
        }
    };
    std::thread first(producer, 0), second(producer, 4000);
    first.join(); second.join();
    const auto cpu = GpuTrace::ThreadSample::capture();
    trace->recordThreadSpan({"thread_fixture", 42, 43, 100, 200, 0, -11}, cpu);
    trace.reset(); // must drain before returning, even if the writer was asleep
    const auto files = QDir(dir.path()).entryList({"*.csv"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QFile file(dir.filePath(files[0]));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto data = file.readAll();
    QVERIFY(data.contains("closed=1; truncated=0; dropped_rows="));
    QVERIFY(data.contains("gpu_trace_version=4; decoder_output_query=disabled; worker_status_query=disabled"));
    QVERIFY(data.contains("thread_fixture,42,43,100,200,0,-11,"));
    std::set<int> ids;
    for (const auto& line : data.split('\n')) {
        if (!line.startsWith("fixture,")) continue;
        const auto fields = line.split(',');
        QCOMPARE(fields.size(), 12);
        const auto id = fields[1].toInt();
        QVERIFY(ids.insert(id).second);
        QCOMPARE(fields[2].toInt(), id + 1);
        QCOMPARE(fields[3], QByteArray("100"));
        QCOMPARE(fields[4], QByteArray("200"));
    }
    const auto dropped = data.mid(data.lastIndexOf("dropped_rows=") + 13).trimmed().toULongLong();
    QVERIFY(!ids.empty());
    QCOMPARE(qulonglong(ids.size()) + dropped, qulonglong(8000));

    // Reconnect/probe instances cannot truncate each other's sidecars.
    auto next = GpuTrace::create();
    next->record({"next"});
    next.reset();
    QCOMPARE(QDir(dir.path()).entryList({"*.csv"}, QDir::Files).size(), 2);
    QFile unrelated(dir.filePath("private.csv"));
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    unrelated.write("not a capture"); unrelated.close();
    const auto zip = dir.filePath("export.zip");
    QString error;
    QVERIFY2(writeDiagnosticZip(QDir(dir.path()), zip, error), qPrintable(error));
    QFile archive(zip);
    QVERIFY(archive.open(QIODevice::ReadOnly));
    const auto bytes = archive.readAll();
    QVERIFY(bytes.contains(files[0].toUtf8()));
    QVERIFY(!bytes.contains("private.csv"));
    qunsetenv("MOONLIGHT_VRR_TRACE");
    qunsetenv("MOONLIGHT_VRR_DEEP_TRACE");
}

QTEST_GUILESS_MAIN(GpuTraceTest)
#include "tst_gputrace.moc"
