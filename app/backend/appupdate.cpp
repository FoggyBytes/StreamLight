// Derived from ArtMoon's appupdate by Onaiaku — https://github.com/onaiaku/ArtMoon
// (commits 9ebfc60, 1418d3f, 6ed4df2), GPLv3. What was changed and why is in appupdate.h.

#include "appupdate.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>

#include <SDL.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#define SECURITY_WIN32
#include <security.h>
#include <taskschd.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace {

// StreamLightUpdater.exe and its task (updater/common.cpp, StreamLight.iss).
const wchar_t* const UpdaterTask = L"StreamLight Update";
const char* const UpdaterExe = "StreamLightUpdater.exe";
// The run of the updater that reopened the app, once its notice has been shown.
const char* const NoticedRunKey = "appupdate/noticedrun";

// How long the updater may take to answer, then to take over the screen.
constexpr qint64 UpdaterStartMs = 20000;
constexpr qint64 UpdaterCoverMs = 20000;

#ifdef Q_OS_WIN32
QString knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    QString result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p)) && p) {
        result = QString::fromWCharArray(p);
    }
    CoTaskMemFree(p);
    return result;
}

class Bstr
{
public:
    explicit Bstr(const wchar_t* s) : m_B(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(m_B); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return m_B; }
private:
    BSTR m_B;
};

/*
 * The task elevates only for an administrator (split token, or UAC off): for a standard user
 * it would run with ordinary rights and the updater would stop at once. Then the app goes
 * straight to the prompt, which asks for an administrator's password.
 */
bool tokenCanElevate()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size);
    CloseHandle(token);
    if (!ok) return false;
    if (type == TokenElevationTypeLimited || type == TokenElevationTypeFull) return true;
    // Default: UAC is off (an administrator then has a full token) or this is a standard user.
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    BOOL member = FALSE;
    return CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize)
            && CheckTokenMembership(nullptr, sid, &member) && member;
}

QString currentUserName()
{
    wchar_t name[512];
    ULONG len = ARRAYSIZE(name);
    return GetUserNameExW(NameSamCompatible, name, &len) ? QString::fromWCharArray(name, int(len)) : QString();
}

// "DOMAIN\user", "user" or "MACHINE\user": the account part decides, and the domain when both have one.
bool sameAccount(const QString& a, const QString& b)
{
    const QString ua = a.section(QLatin1Char('\\'), -1), ub = b.section(QLatin1Char('\\'), -1);
    if (ua.compare(ub, Qt::CaseInsensitive) != 0) return false;
    const QString da = a.contains(QLatin1Char('\\')) ? a.section(QLatin1Char('\\'), 0, 0) : QString();
    const QString db = b.contains(QLatin1Char('\\')) ? b.section(QLatin1Char('\\'), 0, 0) : QString();
    return da.isEmpty() || db.isEmpty() || da.compare(db, Qt::CaseInsensitive) == 0;
}

/*
 * Starts "StreamLight Update" when it is there, enabled and registered for this account.
 * Nothing is passed: the task's action takes no arguments (updater/task.cpp).
 */
bool runUpdaterTask()
{
    if (!tokenCanElevate()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: this account cannot elevate, the prompt it is");
        return false;
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    do {
        ComPtr<ITaskService> svc;
        if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc)))) break;
        VARIANT empty;
        VariantInit(&empty);
        if (FAILED(svc->Connect(empty, empty, empty, empty))) break;
        ComPtr<ITaskFolder> folder;
        if (FAILED(svc->GetFolder(Bstr(L"\\"), &folder))) break;
        ComPtr<IRegisteredTask> task;
        HRESULT hr = folder->GetTask(Bstr(UpdaterTask), &task);
        if (FAILED(hr)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: no usable update task (0x%08lx)", hr);
            break;
        }
        VARIANT_BOOL enabled = VARIANT_FALSE;
        task->get_Enabled(&enabled);
        ComPtr<ITaskDefinition> def;
        ComPtr<IPrincipal> principal;
        BSTR user = nullptr;
        QString owner;
        if (SUCCEEDED(task->get_Definition(&def)) && SUCCEEDED(def->get_Principal(&principal))
                && SUCCEEDED(principal->get_UserId(&user)) && user) {
            owner = QString::fromWCharArray(user);
        }
        SysFreeString(user);
        if (enabled != VARIANT_TRUE || !sameAccount(owner, currentUserName())) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: update task is %s, for %s",
                        enabled == VARIANT_TRUE ? "enabled" : "disabled", qPrintable(owner));
            break;
        }
        ComPtr<IRunningTask> running;
        hr = task->Run(empty, &running);
        if (FAILED(hr)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: could not start the update task (0x%08lx)", hr);
            break;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: update task started");
        ok = true;
    } while (false);
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}
#endif

const char* const Owner = "FoggyBytes";

// "Don't remind me" in the startup prompt: the tag it was ticked for.
const char* const SkippedVersionKey = "appupdate/skippedversion";

// The lookup is a few kilobytes of JSON asked for from the Settings screen or at startup, with
// the event loop running either way — Qt's own transfer timeout is safe there.
constexpr int LookupTimeoutMs = 15000;

// The download's watchdog (see m_StallWatch in the header). A tick later than LateTickMs means
// the main thread was held — by a stream, in practice — and says nothing about the network.
constexpr int StallTimeoutMs = 30000;
constexpr int StallTickMs = 5000;
constexpr int LateTickMs = 2 * StallTickMs;

// OutputBaseFilename=StreamLight_{#AppVersion}_Installer in StreamLight.iss. Anchored at both
// ends: a ".exe.sig" or a "_Installer_debug.exe" must not qualify.
const QRegularExpression& installerPattern()
{
    static const QRegularExpression re(
        QStringLiteral("^StreamLight_(\\d+(?:\\.\\d+){1,3})_Installer\\.exe$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// SHA-256 of a file on disk, lower-case hex; empty if it cannot be read.
QByteArray fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result().toHex();
}

}

AppUpdate::AppUpdate(QObject* parent)
    : QObject(parent),
      m_Nam(new QNetworkAccessManager(this))
{
    m_Nam->setStrictTransportSecurityEnabled(true);
    // Asset downloads redirect from github.com to GitHub's storage host; https to https only.
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    m_StallWatch.setInterval(StallTickMs);
    connect(&m_StallWatch, &QTimer::timeout, this, &AppUpdate::checkStall);

    m_StatusPoll.setInterval(100);
    connect(&m_StatusPoll, &QTimer::timeout, this, &AppUpdate::pollStatus);
    readAfterUpdate();
}

AppUpdate::~AppUpdate()
{
    m_StallWatch.stop();
    if (m_Download != nullptr) {
        // Disconnected first: abort() emits finished synchronously, and the handler would
        // otherwise run from inside this destructor and signal a QML engine being torn down.
        disconnect(m_Download, nullptr, this, nullptr);
        m_Download->abort();
    }
    delete m_File;      // uncommitted QSaveFile: discards its temp file
}

QList<int> AppUpdate::parseVersion(const QString& version)
{
    QString s = version;
    // U+FEFF is what a UTF-8 BOM in version.txt becomes by the time it reaches VERSION_STR.
    s.remove(QChar(0xFEFF));
    s = s.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        s.remove(0, 1);
    }

    const QStringList parts = s.split(QLatin1Char('.'));
    if (parts.size() < 2 || parts.size() > 4) {
        return {};
    }

    QList<int> numbers;
    for (const QString& part : parts) {
        bool ok = false;
        const int n = part.toInt(&ok);
        // toInt accepts a sign; a version component never has one.
        if (!ok || n < 0 || part.startsWith(QLatin1Char('+'))) {
            return {};
        }
        numbers.append(n);
    }
    return numbers;
}

int AppUpdate::compareVersions(const QString& a, const QString& b)
{
    const QList<int> va = parseVersion(a);
    const QList<int> vb = parseVersion(b);
    if (va.isEmpty() || vb.isEmpty()) {
        return 0;
    }

    const int n = qMax(va.size(), vb.size());
    for (int i = 0; i < n; i++) {
        const int x = i < va.size() ? va[i] : 0;
        const int y = i < vb.size() ? vb[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

QString AppUpdate::installedVersion()
{
    return QString(VERSION_STR);
}

QString AppUpdate::downloadDir()
{
#ifdef Q_OS_WIN32
    // 8.6.0: where StreamLightUpdater.exe looks for it (updater/common.cpp, userDir) — read
    // from the known folder on both sides, so neither depends on TEMP or on Qt's own paths.
    return knownFolder(FOLDERID_LocalAppData) + QStringLiteral("\\FoggyBytes\\StreamLight\\update");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("StreamLight-update"));
#endif
}

void AppUpdate::clearDownloads()
{
    // Best effort. The installer of the update that just ran can still be open for the few
    // seconds Setup takes to exit after relaunching us; whatever is locked now goes next time.
    QDir(downloadDir()).removeRecursively();
#ifdef Q_OS_WIN32
    // Where 5.8.0–8.5.x downloaded, before the updater needed a fixed place.
    QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
         .filePath(QStringLiteral("StreamLight-update"))).removeRecursively();
#endif
}

bool AppUpdate::updateAvailable() const
{
    return !m_LatestStreamLight.isEmpty()
            && !parseVersion(m_LatestStreamLight).isEmpty()
            && !parseVersion(installedVersion()).isEmpty()
            && compareVersions(installedVersion(), m_LatestStreamLight) < 0;
}

bool AppUpdate::shouldPrompt() const
{
    // An installer to update with, not just a newer tag: "Yes" leads to Update now, and a
    // release without one would put the user straight onto an error.
    if (!updateAvailable() || !m_HasAsset) {
        return false;
    }

    const QString skipped = QSettings().value(QLatin1String(SkippedVersionKey)).toString();
    if (skipped.isEmpty() || parseVersion(skipped).isEmpty()) {
        return true;
    }
    // Skipping one release also silences an older one, never a newer one.
    return compareVersions(m_LatestStreamLight, skipped) > 0;
}

void AppUpdate::skipLatestVersion()
{
    if (m_LatestStreamLight.isEmpty()) {
        return;
    }
    QSettings().setValue(QLatin1String(SkippedVersionKey), m_LatestStreamLight);
}

void AppUpdate::setState(State state, const QString& failureReason)
{
    if (m_State == state && m_FailureReason == failureReason) {
        return;
    }
    m_State = state;
    m_FailureReason = failureReason;
    emit stateChanged();
}

void AppUpdate::fail(const QString& reason)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: %s", qPrintable(reason));
    setState(Failed, reason);
}

QNetworkReply* AppUpdate::getJson(const QString& repo)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                                 .arg(QLatin1String(Owner), repo)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub's API refuses requests without a User-Agent. Qt adds a generic one when none is
    // set; naming ourselves is what GitHub asks for.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("StreamLight/%1").arg(parseVersion(installedVersion()).isEmpty()
                                                           ? QStringLiteral("unknown")
                                                           : installedVersion().remove(QChar(0xFEFF)).trimmed()));
    request.setTransferTimeout(LookupTimeoutMs);
    return m_Nam->get(request);
}

void AppUpdate::checkLatest()
{
    // A new Settings screen opening mid-update must not pull the release, or the verified
    // installer waiting for Install now, out from under it.
    if (holdsDownload()) {
        return;
    }

    // Leftovers of earlier updates, ~28 MB each. Nothing is in use: holdsDownload() is false.
    clearDownloads();

    setState(Checking);

    QNetworkReply* sl = getJson(QStringLiteral("StreamLight"));
    connect(sl, &QNetworkReply::finished, this, [this, sl]() { handleStreamLight(sl); });

    QNetworkReply* st = getJson(QStringLiteral("StreamTweak"));
    connect(st, &QNetworkReply::finished, this, [this, st]() { handleStreamTweak(st); });
}

void AppUpdate::handleStreamTweak(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        return;     // the label keeps saying "checking…", as it always has on a failed lookup
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QString tag = doc.object().value(QStringLiteral("tag_name")).toString();
    if (!tag.isEmpty() && tag != m_LatestStreamTweak) {
        m_LatestStreamTweak = tag;
        emit latestChanged();
    }
}

void AppUpdate::handleStreamLight(QNetworkReply* reply)
{
    reply->deleteLater();

    // An update started while this lookup was in flight (updateNow() accepts Checking) is
    // working from the asset it was started with; this answer must not swap it out.
    if (holdsDownload()) {
        return;
    }
    const bool ownsState = (m_State == Checking);

    if (reply->error() != QNetworkReply::NoError) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: release lookup failed: %s",
                    qPrintable(reply->errorString()));
        if (ownsState) {
            setState(Idle);
        }
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    const QJsonObject obj = doc.object();
    const QString tag = obj.value(QStringLiteral("tag_name")).toString();
    if (doc.isNull() || tag.isEmpty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: release lookup returned no tag: %s",
                    qPrintable(err.errorString()));
        if (ownsState) {
            setState(Idle);
        }
        return;
    }

    // The installer for THIS release: its name must match the pattern and carry the tag's own
    // version, so an old installer left attached to a newer release is not mistaken for it.
    m_HasAsset = false;
    m_Asset = Asset();
    const QList<int> tagVersion = parseVersion(tag);
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QRegularExpressionMatch match = installerPattern().match(name);
        if (!match.hasMatch() || parseVersion(match.captured(1)) != tagVersion) {
            continue;
        }

        m_Asset.url = asset.value(QStringLiteral("browser_download_url")).toString();
        m_Asset.name = name;
        const QString digest = asset.value(QStringLiteral("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:"), Qt::CaseInsensitive)) {
            m_Asset.sha256 = digest.mid(7).trimmed().toLower().toLatin1();
        }
        m_HasAsset = !m_Asset.url.isEmpty();
        break;
    }

    // ⚠️ Changed is emitted after the asset is known, not before: the startup prompt reacts to
    // this signal and asks shouldPrompt(), which needs m_HasAsset. The tag alone may not change
    // (a second lookup finding the same release), so the asset answer is signalled either way.
    m_LatestStreamLight = tag;
    emit latestChanged();
    if (ownsState) {
        setState(Idle);
    }
}

void AppUpdate::setInstallBlocked(bool blocked)
{
    if (m_InstallBlocked == blocked) {
        return;
    }
    m_InstallBlocked = blocked;
    emit installBlockedChanged();
}

void AppUpdate::updateNow()
{
    switch (m_State) {
    case Ready:
        // Refused here as well as greyed in the button's label: this is the one call that
        // quits the app, so it does not trust the QML to have checked.
        if (m_InstallBlocked) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "AppUpdate: install held — something on Home is still in progress");
            return;
        }
        launchInstaller();
        return;
    case Downloading:
    case Launching:
        return;
    case Idle:
    case Checking:
    case Failed:
        // Checking is NOT a reason to refuse. The button is on screen because an earlier lookup
        // found a newer release, and a press while Settings re-checks used to be dropped
        // without a word; the asset from that lookup is the one to use, and
        // handleStreamLight() leaves it alone once the download has started.
        startDownload();
        return;
    }
}

void AppUpdate::startDownload()
{
#ifndef Q_OS_WIN32
    fail(tr("Updating from the app is Windows only"));
    return;
#else
    if (!updateAvailable()) {
        fail(tr("No newer version to install"));
        return;
    }
    if (!m_HasAsset) {
        fail(tr("This release has no installer"));
        return;
    }
    // Refused before the download, not after: without a published digest there is nothing to
    // check the file against, and it would run elevated.
    if (m_Asset.sha256.size() != 64) {
        fail(tr("This release has no checksum"));
        return;
    }

    clearDownloads();
    if (!QDir().mkpath(downloadDir())) {
        fail(tr("Could not create the download folder"));
        return;
    }

    delete m_File;
    m_File = new QSaveFile(QDir(downloadDir()).filePath(m_Asset.name));
    if (!m_File->open(QIODevice::WriteOnly)) {
        fail(tr("Could not save the installer"));
        delete m_File;
        m_File = nullptr;
        return;
    }

    m_Hash.reset();
    m_WriteFailed = false;
    m_Stalled = false;
    m_InstallerPath.clear();
    m_Progress = -1;
    emit progressChanged();
    setState(Downloading);

    QNetworkRequest request{QUrl(m_Asset.url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StreamLight"));
    m_Download = m_Nam->get(request);

    connect(m_Download, &QNetworkReply::readyRead, this, &AppUpdate::handleDownloadData);
    connect(m_Download, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_SinceData.restart();
        const int p = total > 0 ? int(received * 100 / total) : -1;
        if (p != m_Progress) {
            m_Progress = p;
            emit progressChanged();
        }
    });
    connect(m_Download, &QNetworkReply::finished, this, &AppUpdate::handleDownloadFinished);

    m_SinceData.start();
    m_SinceTick.start();
    m_StallWatch.start();
#endif
}

void AppUpdate::checkStall()
{
    if (m_Download == nullptr) {
        m_StallWatch.stop();
        return;
    }

    const bool late = m_SinceTick.elapsed() > LateTickMs;
    m_SinceTick.restart();
    if (late) {
        // The main thread was held (a stream, in practice), so whatever arrived meanwhile is
        // still queued behind this tick. Not evidence of a stall: start the clock over.
        m_SinceData.restart();
        return;
    }

    if (m_SinceData.elapsed() > StallTimeoutMs) {
        m_Stalled = true;
        m_Download->abort();    // finishes with OperationCanceledError, handled below
    }
}

void AppUpdate::handleDownloadData()
{
    if (m_Download == nullptr || m_File == nullptr) {
        return;
    }
    m_SinceData.restart();
    const QByteArray chunk = m_Download->readAll();
    m_Hash.addData(chunk);
    if (m_File->write(chunk) != chunk.size()) {
        m_WriteFailed = true;
        m_File->cancelWriting();
        m_Download->abort();    // finishes with an error, handled below
    }
}

void AppUpdate::handleDownloadFinished()
{
    QNetworkReply* reply = m_Download;
    m_Download = nullptr;
    m_StallWatch.stop();
    if (reply == nullptr) {
        return;
    }
    reply->deleteLater();

    // Whatever is still buffered: readyRead is not guaranteed to have drained it.
    if (reply->error() == QNetworkReply::NoError && m_File != nullptr) {
        const QByteArray tail = reply->readAll();
        m_Hash.addData(tail);
        if (m_File->write(tail) != tail.size()) {
            m_WriteFailed = true;
            m_File->cancelWriting();
        }
    }

    const auto discard = [this]() {
        delete m_File;      // not committed: the temp file goes with it
        m_File = nullptr;
    };

    if (m_WriteFailed) {
        discard();
        fail(tr("Could not save the installer"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: download failed: %s",
                    qPrintable(reply->errorString()));
        discard();
        fail(m_Stalled ? tr("Download stalled") : tr("Download failed"));
        return;
    }

    if (m_Hash.result().toHex() != m_Asset.sha256) {
        discard();
        fail(tr("Checksum mismatch"));
        return;
    }

    const QString path = m_File->fileName();
    const bool committed = m_File->commit();
    delete m_File;
    m_File = nullptr;
    if (!committed) {
        fail(tr("Could not save the installer"));
        return;
    }

    // Nothing runs from here. The download ending is not a moment the user chose, and every
    // defect this feature has had came from treating it as one — see "Two presses" in the
    // header. The button now offers Install now.
    m_InstallerPath = path;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: installer verified, ready to install");
    setState(Ready);
}

void AppUpdate::launchInstaller()
{
    // Checked again, not trusted from the download: Install now can come a long time after,
    // and the file has spent that time in a folder the user's own processes can write to.
    if (m_InstallerPath.isEmpty() || !QFile::exists(m_InstallerPath)) {
        fail(tr("The downloaded installer is gone"));
        return;
    }
    if (fileSha256(m_InstallerPath) != m_Asset.sha256) {
        fail(tr("Checksum mismatch"));
        return;
    }

    // 8.6.0: StreamLightUpdater.exe runs it — see "The installer is run by StreamLightUpdater"
    // in the header. The app stays up, showing UpdateScreen, until the updater has checked its
    // own copy of the file and covered the screen; then it quits (pollStatus).
    setState(Launching);
    startUpdater();
}

QString AppUpdate::installFrom() const
{
    return installedVersion().remove(QChar(0xFEFF)).trimmed();
}

QString AppUpdate::installTo() const
{
    QString v = m_LatestStreamLight.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) v.remove(0, 1);
    return v;
}

void AppUpdate::setPhase(InstallPhase phase, const QString& message)
{
    if (m_Phase == phase && m_PhaseMessage == message) return;
    m_Phase = phase;
    m_PhaseMessage = message;
    m_PhaseSince = QDateTime::currentMSecsSinceEpoch();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: update screen phase %d %s", int(phase),
                qPrintable(message));
    emit installPhaseChanged();
}

QString AppUpdate::statusFile()
{
#ifdef Q_OS_WIN32
    return knownFolder(FOLDERID_ProgramData) + QStringLiteral("\\FoggyBytes\\StreamLight\\update-status\\status.json");
#else
    return QString();
#endif
}

QString AppUpdate::messageFor(const QString& reason)
{
    if (reason == QLatin1String("location")) return tr("StreamLight's install folder was not found");
    if (reason == QLatin1String("network")) return tr("Could not reach GitHub");
    if (reason == QLatin1String("release")) return tr("This release has no checksum");
    if (reason == QLatin1String("uptodate")) return tr("This version is already installed");
    if (reason == QLatin1String("nofile")) return tr("The downloaded installer is gone");
    if (reason == QLatin1String("checksum")) return tr("Checksum mismatch");
    if (reason == QLatin1String("folders")) return tr("The update folder is not safe to use");
    if (reason.startsWith(QLatin1String("setup:"))) return tr("Setup stopped with code %1").arg(reason.mid(6));
    return tr("The update did not install");
}

void AppUpdate::startUpdater()
{
#ifdef Q_OS_WIN32
    // Whatever an earlier run left: the updater takes only files newer than its own start.
    QFile::remove(QDir(downloadDir()).filePath(QStringLiteral("look.json")));
    QFile::remove(QDir(downloadDir()).filePath(QStringLiteral("cancel")));

    m_TriggeredAt = QDateTime::currentMSecsSinceEpoch();
    if (runUpdaterTask()) {
        setPhase(PhaseCheck);
        m_StatusPoll.start();
        return;
    }
    // No task for this account (an install from before 8.6.0, another account, a standard
    // user): the same updater through the Windows prompt — said first, since a pad cannot
    // answer it.
    setPhase(PhaseAsk);
#else
    fail(tr("Updating from the app is Windows only"));
#endif
}

void AppUpdate::confirmPermission()
{
#ifdef Q_OS_WIN32
    if (m_Phase != PhaseAsk) return;
    const QString exe = QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath())
                                                 .filePath(QLatin1String(UpdaterExe)));
    const std::wstring file = exe.toStdWString();
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.nShow = SW_SHOWNORMAL;
    m_TriggeredAt = QDateTime::currentMSecsSinceEpoch();
    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: updater not started through the prompt (%lu)", err);
        if (err == ERROR_CANCELLED) setPhase(PhaseCancelled);
        else setPhase(PhaseFailed, tr("Could not start the updater"));
        return;
    }
    setPhase(PhaseCheck);
    m_StatusPoll.start();
#endif
}

void AppUpdate::cancelInstall()
{
    if (m_Phase == PhaseAsk) {
        setPhase(PhaseCancelled);
        return;
    }
    if (m_Phase != PhaseCheck) return;
    // The updater looks for this file while it checks and before it takes over the screen;
    // past that point there is nothing left to cancel, and the button is gone.
    QFile cancel(QDir(downloadDir()).filePath(QStringLiteral("cancel")));
    if (cancel.open(QIODevice::WriteOnly | QIODevice::Truncate)) cancel.write("1");
    m_StatusPoll.stop();
    setPhase(PhaseCancelled);
}

void AppUpdate::closeInstallScreen()
{
    if (m_Phase != PhaseFailed && m_Phase != PhaseCancelled) return;
    const bool failed = m_Phase == PhaseFailed;
    const QString message = m_PhaseMessage;
    m_StatusPoll.stop();
    setPhase(PhaseNone);
    // Settings' card says what happened: Retry after a failure, Install now after going back
    // (the verified download is still there, and is checked again before it runs).
    if (failed) fail(message);
    else setState(QFile::exists(m_InstallerPath) ? Ready : Idle);
}

void AppUpdate::pollStatus()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QFile f(statusFile());
    QJsonObject status;
    if (f.open(QIODevice::ReadOnly) && f.size() < 64 * 1024) {
        status = QJsonDocument::fromJson(f.readAll()).object();
    }
    // Only this run's: started after the request (with a margin for the clock's granularity).
    const bool ours = !status.isEmpty()
            && qint64(status.value(QStringLiteral("started")).toDouble()) >= m_TriggeredAt - 5000;
    const QString phase = ours ? status.value(QStringLiteral("phase")).toString() : QString();

    if (phase == QLatin1String("failed")) {
        m_StatusPoll.stop();
        setPhase(PhaseFailed, messageFor(status.value(QStringLiteral("reason")).toString()));
        return;
    }
    if (phase == QLatin1String("cancelled")) {
        m_StatusPoll.stop();
        setPhase(PhaseCancelled);
        return;
    }
    if (phase == QLatin1String("ready") && m_Phase == PhaseCheck) {
        setPhase(PhaseClosing);         // UpdateScreen freezes and calls writeLook
        return;
    }
    if (phase == QLatin1String("covering") || phase == QLatin1String("installing")) {
        // The updater's window is over this one: closing now shows nothing of it.
        m_StatusPoll.stop();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: the updater has the screen, quitting");
        // The installer cannot replace StreamLight.exe while this process holds it.
        QCoreApplication::quit();
        return;
    }
    if (m_Phase == PhaseCheck && phase.isEmpty() && now - m_TriggeredAt > UpdaterStartMs) {
        m_StatusPoll.stop();
        setPhase(PhaseFailed, tr("The updater did not start"));
    } else if (m_Phase == PhaseClosing && now - m_PhaseSince > UpdaterCoverMs) {
        m_StatusPoll.stop();
        setPhase(PhaseFailed, tr("The updater did not take over"));
    }
}

void AppUpdate::writeLook(QObject* window, const QVariantMap& look)
{
#ifdef Q_OS_WIN32
    if (m_Phase != PhaseClosing) return;
    auto* w = qobject_cast<QWindow*>(window);
    if (!w) {
        // Not to be relied on (the QML passes its own), but a missing window must not cost the
        // hand-over: the app has one main window.
        w = QGuiApplication::focusWindow();
        if (!w && !QGuiApplication::topLevelWindows().isEmpty()) w = QGuiApplication::topLevelWindows().first();
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: look without a window from QML, using %s",
                    w ? "the application's" : "none");
    }
    if (!w) return;
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    RECT client = {};
    POINT origin = { 0, 0 };
    if (!GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: look not written, no client area (%lu)", GetLastError());
        return;
    }
    const double dpr = w->devicePixelRatio();

    // Everything positional is in the window's logical pixels: scaled here, once.
    auto px = [dpr](const QVariant& v) { return v.toDouble() * dpr; };
    auto rect = [&](const QVariant& v) {
        const QVariantList l = v.toList();
        QJsonArray a;
        for (const QVariant& x : l) a.append(px(x));
        return a;
    };
    auto colour = [](const QVariant& v) { return v.value<QColor>().name(QColor::HexArgb); };
    auto text = [&](const QVariant& v, const char* xKey) {
        const QVariantMap m = v.toMap();
        return QJsonObject{ { QStringLiteral("px"), px(m.value(QStringLiteral("px"))) },
                            { QLatin1String(xKey), px(m.value(QLatin1String(xKey))) },
                            { QStringLiteral("baseline"), px(m.value(QStringLiteral("baseline"))) } };
    };

    const QVariantMap colours = look.value(QStringLiteral("colors")).toMap();
    QJsonObject c;
    for (const char* key : { "top", "mid", "foot", "ink", "text", "text2", "text3", "line" }) {
        c.insert(QLatin1String(key), colour(colours.value(QLatin1String(key))));
    }
    const QVariantMap word = look.value(QStringLiteral("word")).toMap();
    QJsonObject wordJson{ { QStringLiteral("px"), px(word.value(QStringLiteral("px"))) },
                          { QStringLiteral("baseline"), px(word.value(QStringLiteral("baseline"))) },
                          { QStringLiteral("x"), rect(word.value(QStringLiteral("x"))) } };

    const QJsonObject out{
        { QStringLiteral("v"), 1 },
        { QStringLiteral("rect"), QJsonArray{ int(origin.x), int(origin.y), int(client.right - client.left),
                                              int(client.bottom - client.top) } },
        { QStringLiteral("dpr"), dpr },
        { QStringLiteral("u"), look.value(QStringLiteral("u")).toDouble() },
        { QStringLiteral("t"), look.value(QStringLiteral("t")).toDouble() },
        { QStringLiteral("waves"), look.value(QStringLiteral("waves")).toBool() },
        { QStringLiteral("colors"), c },
        { QStringLiteral("icon"), rect(look.value(QStringLiteral("icon"))) },
        { QStringLiteral("bar"), rect(look.value(QStringLiteral("bar"))) },
        { QStringLiteral("word"), wordJson },
        { QStringLiteral("status"), text(look.value(QStringLiteral("status")), "x") },
        { QStringLiteral("percent"), text(look.value(QStringLiteral("percent")), "right") },
        { QStringLiteral("version"), text(look.value(QStringLiteral("version")), "cx") },
    };
    QSaveFile f(QDir(downloadDir()).filePath(QStringLiteral("look.json")));
    const bool written = f.open(QIODevice::WriteOnly)
            && f.write(QJsonDocument(out).toJson(QJsonDocument::Compact)) > 0 && f.commit();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AppUpdate: look %s (%ldx%ld at %ld,%ld, dpr %.2f)",
                written ? "written" : "NOT written", long(client.right - client.left),
                long(client.bottom - client.top), long(origin.x), long(origin.y), dpr);
#else
    Q_UNUSED(window)
    Q_UNUSED(look)
#endif
}

void AppUpdate::readAfterUpdate()
{
    QFile f(statusFile());
    if (statusFile().isEmpty() || !f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) return;
    const QJsonObject s = QJsonDocument::fromJson(f.readAll()).object();
    const qint64 started = qint64(s.value(QStringLiteral("started")).toDouble());
    const qint64 at = qint64(s.value(QStringLiteral("at")).toDouble());
    const QString phase = s.value(QStringLiteral("phase")).toString();
    if (started <= 0 || QDateTime::currentMSecsSinceEpoch() - at > 10 * 60 * 1000) return;
    if (QSettings().value(QLatin1String(NoticedRunKey)).toLongLong() == started) return;

    if (phase == QLatin1String("done") && s.value(QStringLiteral("to")).toString() == installFrom()) {
        m_AfterUpdateNotice = tr("Updated to StreamLight %1").arg(installFrom());
    } else if (phase == QLatin1String("failed") && s.value(QStringLiteral("late")).toBool()) {
        // Setup itself failed, after this app had closed: the version is still the old one.
        m_AfterUpdateNotice = tr("The update did not install — %1").arg(messageFor(s.value(QStringLiteral("reason")).toString()));
    } else {
        return;
    }
    m_AfterUpdateStarted = started;
}

void AppUpdate::acknowledgeAfterUpdate()
{
    if (m_AfterUpdateNotice.isEmpty()) return;
    QSettings().setValue(QLatin1String(NoticedRunKey), m_AfterUpdateStarted);
    m_AfterUpdateNotice.clear();
    emit afterUpdateNoticeChanged();
}
