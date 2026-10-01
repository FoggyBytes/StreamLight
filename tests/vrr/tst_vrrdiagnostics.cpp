#include <QtTest>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "../../app/diagnostics/diagnosticcapture.h"
#include "../../app/diagnostics/diagnosticzip.h"

class VrrDiagnosticsTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void desktopDirectory();
    void scopedCaptureAndExport();
    void preservesExternalLauncher();
    void failuresKeepEnvironment();
    void preservesConnectionsAndInterruptedCapture();
    void zipBoundaries();
    void latestPointerCannotEscapeCaptureRoot();
};

static QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

static void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}

void VrrDiagnosticsTest::init()
{
    qunsetenv("MOONLIGHT_VRR_TRACE");
    qunsetenv("MOONLIGHT_VRR_DEEP_TRACE");
    qunsetenv("MOONLIGHT_VRR_ALIGN");
    QVERIFY(!DiagnosticCapture::isActive());
}

void VrrDiagnosticsTest::desktopDirectory()
{
    // Resolve the platform's actual Desktop (including localized/redirected
    // Windows desktops), without writing anything into the user's profile.
    const auto desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QCOMPARE(DiagnosticCapture::rootDirectory(), desktop.isEmpty() ? QString() :
        QDir(desktop).filePath("vrr-diagnostics"));
}

void VrrDiagnosticsTest::scopedCaptureAndExport()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = temporary.filePath(QString::fromUtf8("captures-测试"));
    qputenv("MOONLIGHT_VRR_DEEP_TRACE", "0");
    qputenv("MOONLIGHT_VRR_ALIGN", "1");
    QString error;
    auto capture = DiagnosticCapture::begin(root, {{"requested_fps", 116}}, error);
    QVERIFY2(capture != nullptr, qPrintable(error));
    QVERIFY(DiagnosticCapture::isActive());
    const auto folder = capture->directory();
    const auto trace = QDir(folder).filePath("Moonlight.vrrtrace");
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_TRACE"), trace);
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_DEEP_TRACE"), QString("1"));
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_ALIGN"), QString("1"));
    QVERIFY(!DiagnosticCapture::begin(root, {}, error));
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_TRACE"), trace);
    QVERIFY(DiagnosticCapture::exportLatest(root, error).isEmpty());
    QVERIFY(error.contains("still active"));
    DiagnosticCapture::appendLog("already redacted test log\n");
    writeFile(trace, "compressed trace fixture\n");
    capture->finish();
    capture->finish(); // Idempotent; destructor is safe too.
    QVERIFY(!DiagnosticCapture::isActive());
    QVERIFY(qEnvironmentVariableIsEmpty("MOONLIGHT_VRR_TRACE"));
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_DEEP_TRACE"), QString("0"));
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_ALIGN"), QString("1"));
    const auto info = QJsonDocument::fromJson(readFile(QDir(folder).filePath("capture-info.json"))).object();
    QVERIFY(!info.contains("comparison_arm"));
    QVERIFY(!info.contains("timing_policy"));
    QCOMPARE(info["requested_fps"].toInt(), 116);
    QCOMPARE(info["trace_files"].toInt(), 1);
    QVERIFY(info["clean_session_close"].toBool());
    QVERIFY(!info["executable_sha256"].toString().isEmpty());
    QCOMPARE(readFile(QDir(folder).filePath("Moonlight.log")), QByteArray("already redacted test log\n"));

    // Only capture files may leave the application, never settings or keys.
    writeFile(QDir(folder).filePath("Moonlight.ini"), "private settings");
    writeFile(QDir(folder).filePath("key.pem"), "private key");
    const auto zip = DiagnosticCapture::exportLatest(root, error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const auto bytes = readFile(zip);
    QVERIFY(bytes.startsWith("PK\003\004"));
    QVERIFY(bytes.contains("Moonlight.vrrtrace"));
    QVERIFY(bytes.contains("Moonlight.log"));
    QVERIFY(bytes.contains("capture-info.json"));
    QVERIFY(!bytes.contains("Moonlight.ini"));
    QVERIFY(!bytes.contains("key.pem"));
    const auto second = DiagnosticCapture::exportLatest(root, error);
    QVERIFY(!second.isEmpty() && second != zip);
    // Optional export for an independent ZIP reader, without relying on Python
    // being installed on the Windows gaming client or CI runner.
    const auto exportPath = qEnvironmentVariable("MOONLIGHT_DIAGNOSTICS_TEST_EXPORT");
    if (!exportPath.isEmpty()) QVERIFY(QFile::copy(zip, exportPath));
}

void VrrDiagnosticsTest::preservesExternalLauncher()
{
    QTemporaryDir temporary;
    qputenv("MOONLIGHT_VRR_TRACE", "external.vrrtrace");
    qputenv("MOONLIGHT_VRR_DEEP_TRACE", "0");
    QString error;
    QVERIFY(!DiagnosticCapture::begin(temporary.path(), {}, error));
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_TRACE"), QString("external.vrrtrace"));
    QCOMPARE(qEnvironmentVariable("MOONLIGHT_VRR_DEEP_TRACE"), QString("0"));
    QVERIFY(!DiagnosticCapture::isActive());
    QVERIFY(QDir(temporary.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

void VrrDiagnosticsTest::failuresKeepEnvironment()
{
    QTemporaryDir temporary;
    QString error;
    QVERIFY(!DiagnosticCapture::begin("", {}, error));
    QVERIFY(!DiagnosticCapture::begin("relative", {}, error));
    QVERIFY(!DiagnosticCapture::begin("//server/share/capture", {}, error));
    writeFile(temporary.filePath("blocked"), "not a directory");
    QVERIFY(!DiagnosticCapture::begin(temporary.filePath("blocked"), {}, error));
    QVERIFY(qEnvironmentVariableIsEmpty("MOONLIGHT_VRR_TRACE"));
    QVERIFY(!DiagnosticCapture::isActive());
    QVERIFY(DiagnosticCapture::exportLatest(temporary.path(), error).isEmpty());
    QVERIFY(DiagnosticCapture::exportLatest("", error).isEmpty());
    // A failed latest-run pointer must not enable an unexportable capture or
    // allow a stale pointer to silently select a previous run.
    QVERIFY(QDir().mkdir(temporary.filePath("latest-capture.json")));
    QVERIFY(!DiagnosticCapture::begin(temporary.path(), {}, error));
    QVERIFY(error.contains("register"));
    QVERIFY(!DiagnosticCapture::isActive());
    QVERIFY(qEnvironmentVariableIsEmpty("MOONLIGHT_VRR_TRACE"));
    QVERIFY(qEnvironmentVariableIsEmpty("MOONLIGHT_VRR_DEEP_TRACE"));
}

void VrrDiagnosticsTest::preservesConnectionsAndInterruptedCapture()
{
    QTemporaryDir temporary;
    QString error;
    auto first = DiagnosticCapture::begin(temporary.path(), {}, error);
    QVERIFY2(first != nullptr, qPrintable(error));
    const auto firstFolder = first->directory();
    const auto manifest = QDir(firstFolder).filePath("capture-info.json");
    QVERIFY(!QJsonDocument::fromJson(readFile(manifest)).object()["clean_session_close"].toBool());
    writeFile(QDir(firstFolder).filePath("Moonlight.vrrtrace"), "last connection");
    writeFile(QDir(firstFolder).filePath("Moonlight-connection-1.vrrtrace"), "first connection");
    first.reset();
    const auto zip = DiagnosticCapture::exportLatest(temporary.path(), error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QVERIFY(readFile(zip).contains("Moonlight-connection-1.vrrtrace"));
    const auto original = readFile(QDir(firstFolder).filePath("Moonlight.vrrtrace"));
    auto second = DiagnosticCapture::begin(temporary.path(), {}, error);
    QVERIFY2(second != nullptr, qPrintable(error));
    QVERIFY(second->directory() != firstFolder);
    QCOMPARE(readFile(QDir(firstFolder).filePath("Moonlight.vrrtrace")), original);
    const auto secondFolder = second->directory();
    second.reset();
    // Simulate an exited/crashed process: files are unlocked, but completion
    // was not certified. Export must preserve that fact for the reviewer.
    writeFile(QDir(secondFolder).filePath("capture-info.json"), "{\"clean_session_close\":false}");
    const auto incompleteZip = DiagnosticCapture::exportLatest(temporary.path(), error);
    QVERIFY2(!incompleteZip.isEmpty(), qPrintable(error));
    QVERIFY(readFile(incompleteZip).contains("\"clean_session_close\":false"));
}

void VrrDiagnosticsTest::zipBoundaries()
{
    QTemporaryDir temporary;
    const QDir root(temporary.path());
    QString error;
    const auto zip = temporary.filePath("export.zip");
    QVERIFY(!writeDiagnosticZip(root, zip, error));
    writeFile(temporary.filePath("Moonlight.log"), {});
    writeFile(temporary.filePath("Moonlight-connection-2.vrrtrace"), QByteArray(2 * 1024 * 1024 + 17, 'x'));
    QVERIFY2(writeDiagnosticZip(root, zip, error), qPrintable(error));
    const auto original = readFile(zip);
    QVERIFY(!writeDiagnosticZip(root, zip, error));
    QCOMPARE(readFile(zip), original);
    QVERIFY(!writeDiagnosticZip(root, temporary.filePath("missing/export.zip"), error));
    // A trace symlink must never make an unrelated file part of a bundle.
#ifndef Q_OS_WIN
    writeFile(temporary.filePath("secret"), "do not export");
    QVERIFY(QFile::link(temporary.filePath("secret"), temporary.filePath("Moonlight-symlink.vrrtrace")));
    const auto safe = temporary.filePath("safe.zip");
    QVERIFY(writeDiagnosticZip(root, safe, error));
    QVERIFY(!readFile(safe).contains("do not export"));
#endif
}

void VrrDiagnosticsTest::latestPointerCannotEscapeCaptureRoot()
{
    QTemporaryDir temporary;
    QString error;
    auto first = DiagnosticCapture::begin(temporary.path(), {{"capture_sequence", 1}}, error);
    QVERIFY2(first != nullptr, qPrintable(error));
    const auto firstFolder = first->directory();
    first.reset();
    auto second = DiagnosticCapture::begin(temporary.path(), {{"capture_sequence", 2}}, error);
    QVERIFY2(second != nullptr, qPrintable(error));
    const auto secondFolder = second->directory();
    second.reset();
    auto zip = DiagnosticCapture::exportLatest(temporary.path(), error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QVERIFY(readFile(zip).contains("\"capture_sequence\": 2"));
    // Explicitly select the older direct child, proving the pointer is used
    // instead of sorting labels, exported ZIPs or directory modification times.
    writeFile(temporary.filePath("latest-capture.json"), QJsonDocument(QJsonObject{
        {"directory", QFileInfo(firstFolder).fileName()}}).toJson());
    zip = DiagnosticCapture::exportLatest(temporary.path(), error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QVERIFY(readFile(zip).contains("\"capture_sequence\": 1"));
    writeFile(temporary.filePath("latest-capture.json"), "{\"directory\":\"../outside\"}");
    zip = DiagnosticCapture::exportLatest(temporary.path(), error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo(zip).absolutePath() == temporary.path());
    QVERIFY(QFileInfo(secondFolder).exists());
}

QTEST_GUILESS_MAIN(VrrDiagnosticsTest)
#include "tst_vrrdiagnostics.moc"
