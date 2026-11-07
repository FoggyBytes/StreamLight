#include "apply.h"
#include "common.h"
#include "release.h"
#include "screen.h"
#include "userlaunch.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QStringList>

#include <windows.h>

namespace up {

namespace {

// Held for the whole run: one update at a time. Global, so another session cannot start one.
const wchar_t* const k_RunMutex = L"Global\\StreamLightUpdater";

struct Mutex {
    HANDLE h = nullptr;
    bool mine = false;
    explicit Mutex(const wchar_t* name)
    {
        h = CreateMutexW(nullptr, TRUE, name);
        mine = h && GetLastError() != ERROR_ALREADY_EXISTS;
    }
    ~Mutex() { if (h) { if (mine) ReleaseMutex(h); CloseHandle(h); } }
};

int fail(const QString& reason, int code = 1)
{
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("failed") }, { QStringLiteral("reason"), reason } });
    return code;
}

QString folderOf(const QString& file)
{
    return nativePath(QFileInfo(file).absolutePath());
}

bool sameFolder(const QString& a, const QString& b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

ULONGLONG nowFileTime()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// A file the user wrote after this run began (look.json, cancel).
bool freshUserFile(const QString& name, qint64 sinceMs)
{
    const QFileInfo fi(userDir() + QLatin1Char('\\') + name);
    return fi.exists() && fi.isFile() && fi.lastModified().toMSecsSinceEpoch() >= sinceMs;
}

bool cancelled(qint64 sinceMs)
{
    if (!freshUserFile(QStringLiteral("cancel"), sinceMs)) return false;
    log(QStringLiteral("cancelled by StreamLight"));
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("cancelled") } });
    return true;
}

// SHA-256 read through the handle that keeps the file locked against writes and deletion.
QByteArray hashHandle(HANDLE file)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    LARGE_INTEGER zero = {};
    SetFilePointerEx(file, zero, nullptr, FILE_BEGIN);
    QByteArray buf(1 << 20, Qt::Uninitialized);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buf.data(), DWORD(buf.size()), &read, nullptr)) return QByteArray();
        if (read == 0) break;
        hash.addData(QByteArrayView(buf.constData(), qsizetype(read)));
    }
    return hash.result().toHex();
}

// This process's environment with TEMP and TMP pointed at update\tmp: the Setup loader unpacks
// the real setup into %TEMP%, and the user's own temp folder is theirs to write.
std::wstring setupEnvironment()
{
    QStringList vars;
    LPWCH block = GetEnvironmentStringsW();
    for (LPWCH p = block; p && *p; p += wcslen(p) + 1) {
        const QString v = QString::fromWCharArray(p);
        if (v.startsWith(QLatin1String("TEMP="), Qt::CaseInsensitive)
                || v.startsWith(QLatin1String("TMP="), Qt::CaseInsensitive)) {
            continue;
        }
        vars.append(v);
    }
    FreeEnvironmentStringsW(block);
    vars.append(QStringLiteral("TEMP=") + nativePath(tmpDir()));
    vars.append(QStringLiteral("TMP=") + nativePath(tmpDir()));
    vars.sort(Qt::CaseInsensitive);
    std::wstring env;
    for (const QString& v : vars) {
        env += wide(v);
        env.push_back(L'\0');
    }
    env.push_back(L'\0');
    return env;
}

DWORD runSetup(const QString& setup)
{
    const QString cmd = QStringLiteral("\"%1\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /APPUPDATE /LOG=\"%2\"")
            .arg(nativePath(setup), nativePath(statusDir() + QStringLiteral("\\setup.log")));
    std::wstring line = wide(cmd);
    std::wstring env = setupEnvironment();
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(wide(nativePath(setup)).c_str(), line.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, env.data(), wide(nativePath(workDir())).c_str(), &si, &pi)) {
        log(QStringLiteral("setup: could not start (%1)").arg(GetLastError()));
        return DWORD(-1);
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 30 * 60 * 1000);
    DWORD code = DWORD(-1);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    log(QStringLiteral("setup: exit code %1").arg(code));
    return code;
}

void waitForExit(const QList<DWORD>& pids, DWORD ms)
{
    QList<HANDLE> handles;
    for (DWORD pid : pids) {
        if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) handles.append(h);
    }
    if (!handles.isEmpty()) {
        WaitForMultipleObjects(DWORD(handles.size()), handles.constData(), TRUE, ms);
    }
    for (HANDLE h : handles) CloseHandle(h);
}

struct FindWindowData { QList<DWORD> pids; bool found = false; };

BOOL CALLBACK findVisible(HWND hwnd, LPARAM lp)
{
    auto* data = reinterpret_cast<FindWindowData*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    RECT r;
    if (data->pids.contains(pid) && IsWindowVisible(hwnd) && GetWindowRect(hwnd, &r)
            && r.right - r.left > 64 && r.bottom - r.top > 64) {
        data->found = true;
        return FALSE;
    }
    return TRUE;
}

// The new StreamLight has a window on screen, so lifting this one shows it and not the desktop.
bool waitForNewWindow(const QString& appDir, ULONGLONG startedAfter, DWORD ms)
{
    const ULONGLONG until = GetTickCount64() + ms;
    while (GetTickCount64() < until) {
        FindWindowData data;
        data.pids = streamLightProcesses(appDir, startedAfter);
        if (!data.pids.isEmpty()) {
            EnumWindows(findVisible, reinterpret_cast<LPARAM>(&data));
            if (data.found) return true;
        }
        Sleep(100);
    }
    return false;
}

} // namespace

int stageCopy()
{
    if (!isElevated()) return 2;       // no status: only an elevated process may write it
    QString why;
    if (!foldersAreSafe(&why)) {
        OutputDebugStringW(wide(QStringLiteral("StreamLightUpdater: ") + why).c_str());
        return 3;
    }
    enableLog();
    log(QStringLiteral("started from %1").arg(thisExe()));

    {
        // A run already going: leave it alone, and its status with it.
        Mutex running(k_RunMutex);
        if (!running.mine) { log(QStringLiteral("an update is already running")); return 4; }
    }

    const QString appDir = installDir();
    if (appDir.isEmpty() || !sameFolder(folderOf(thisExe()), appDir)) {
        log(QStringLiteral("not started from the install folder (%1)").arg(appDir));
        return fail(QStringLiteral("location"));
    }
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("checking") } });

    const QString run = runDir();
    for (const char* name : { "StreamLightUpdater.exe", "Qt6Core.dll" }) {
        const QString from = appDir + QLatin1Char('\\') + QLatin1String(name);
        const QString to = run + QLatin1Char('\\') + QLatin1String(name);
        if (!CopyFileW(wide(nativePath(from)).c_str(), wide(nativePath(to)).c_str(), FALSE)) {
            log(QStringLiteral("could not copy %1 (%2)").arg(from).arg(GetLastError()));
            return fail(QStringLiteral("folders"));
        }
    }

    const QString copy = run + QStringLiteral("\\StreamLightUpdater.exe");
    std::wstring line = wide(QStringLiteral("\"%1\" --run").arg(nativePath(copy)));
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(wide(nativePath(copy)).c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        wide(nativePath(run)).c_str(), &si, &pi)) {
        log(QStringLiteral("could not start the copy (%1)").arg(GetLastError()));
        return fail(QStringLiteral("folders"));
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

int stageRun()
{
    if (!isElevated()) return 2;
    QString why;
    if (!foldersAreSafe(&why)) {
        OutputDebugStringW(wide(QStringLiteral("StreamLightUpdater: ") + why).c_str());
        return 3;
    }
    enableLog();
    if (!sameFolder(folderOf(thisExe()), runDir())) {
        log(QStringLiteral("--run outside update\\run: refused"));
        return 5;
    }
    Mutex running(k_RunMutex);
    if (!running.mine) { log(QStringLiteral("an update is already running")); return 4; }
    log(QStringLiteral("run"));
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("checking") } });

    // ---- 1. what is installed, and what GitHub says is the latest ----------------------------
    const QString appDir = installDir();
    const QString installed = appDir.isEmpty() ? QString() : fileVersion(appDir + QStringLiteral("\\StreamLight.exe"));
    if (installed.isEmpty()) return fail(QStringLiteral("location"));

    Release release;
    QString reason;
    if (!fetchLatestRelease(release, &reason)) return fail(reason);
    if (compareVersions(release.version, installed) <= 0) {
        log(QStringLiteral("%1 is not newer than %2").arg(release.version, installed));
        return fail(QStringLiteral("uptodate"));
    }
    writeStatus({ { QStringLiteral("from"), installed }, { QStringLiteral("to"), release.version } });
    if (cancelled(runStarted() - 60000)) return 0;

    // ---- 2. a copy of StreamLight's download, checked where the user cannot reach it ---------
    const QString source = userDir() + QLatin1Char('\\') + release.assetName;
    const QString setup = workDir() + QLatin1Char('\\') + release.assetName;
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) return fail(QStringLiteral("nofile"));
    for (const QFileInfo& old : QDir(workDir()).entryInfoList({ QStringLiteral("StreamLight_*_Installer.exe") }, QDir::Files)) {
        QFile::remove(old.absoluteFilePath());
    }
    if (!CopyFileW(wide(nativePath(source)).c_str(), wide(nativePath(setup)).c_str(), FALSE)) {
        log(QStringLiteral("could not copy the installer (%1)").arg(GetLastError()));
        return fail(QStringLiteral("nofile"));
    }
    // ⚠️ Kept open until Setup has finished: read and execute only, no writing and no deleting
    // by anyone — and a folder holding an open file cannot be renamed either. What was hashed
    // is what runs.
    HANDLE lock = CreateFileW(wide(nativePath(setup)).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return fail(QStringLiteral("nofile"));
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{ lock };
    if (hashHandle(lock) != release.sha256) {
        log(QStringLiteral("checksum mismatch"));
        CloseHandle(lock);
        closer.h = INVALID_HANDLE_VALUE;
        QFile::remove(setup);
        return fail(QStringLiteral("checksum"));
    }
    log(QStringLiteral("installer checked against the release digest"));

    // ---- 3. take over the screen from StreamLight --------------------------------------------
    const qint64 readyAt = nowMs();
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("ready") } });
    // StreamLight writes it ~150 ms after "ready" (two polls and a frame). 8 s is room for a
    // slow machine; longer only keeps the user looking at a still screen.
    const ULONGLONG lookUntil = GetTickCount64() + 8000;
    bool haveLook = false;
    while (GetTickCount64() < lookUntil) {
        if (cancelled(readyAt - 2000)) return 0;
        if (freshUserFile(QStringLiteral("look.json"), readyAt - 1000)) { haveLook = true; break; }
        if (streamLightProcesses(appDir).isEmpty()) break;      // gone already: nothing to match
        Sleep(50);
    }
    Look look = defaultLook();
    log(haveLook ? QStringLiteral("look.json after %1 ms").arg(nowMs() - readyAt)
                 : QStringLiteral("no look.json from StreamLight: drawn with defaults"));
    if (haveLook) {
        QFile f(userDir() + QStringLiteral("\\look.json"));
        if (f.open(QIODevice::ReadOnly) && f.size() <= 16 * 1024) {
            Look parsed;
            if (parseLook(f.readAll(), parsed)) look = parsed;
            else log(QStringLiteral("look.json refused: drawn with defaults"));
        }
    }
    Screen screen;
    screen.start(look, installed, release.version);
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("covering") } });

    waitForExit(streamLightProcesses(appDir), 20000);
    if (!streamLightProcesses(appDir).isEmpty()) log(QStringLiteral("StreamLight still running: Setup will close it"));

    // ---- 4. Setup ----------------------------------------------------------------------------
    writeStatus({ { QStringLiteral("phase"), QStringLiteral("installing") } });
    const DWORD code = runSetup(setup);
    CloseHandle(lock);
    closer.h = INVALID_HANDLE_VALUE;
    QFile::remove(setup);
    const bool installedOk = code == 0;
    if (installedOk) {
        screen.setPhase(ScreenPhase::Starting);
        writeStatus({ { QStringLiteral("phase"), QStringLiteral("starting") } });
    } else {
        screen.setPhase(ScreenPhase::Failed);
        writeStatus({ { QStringLiteral("phase"), QStringLiteral("failed") },
                      { QStringLiteral("reason"), QStringLiteral("setup:%1").arg(qint64(qint32(code))) },
                      { QStringLiteral("late"), true } });
        Sleep(2500);
    }

    // ---- 5. StreamLight again, as the user ---------------------------------------------------
    const ULONGLONG before = nowFileTime();
    const bool launched = launchAsUser(appDir + QStringLiteral("\\StreamLight.exe"));
    if (launched) waitForNewWindow(appDir, before, 25000);
    screen.close();
    if (installedOk) {
        writeStatus({ { QStringLiteral("phase"), QStringLiteral("done") },
                      { QStringLiteral("relaunched"), launched } });
    }
    log(QStringLiteral("end"));
    return installedOk ? 0 : 1;
}

} // namespace up
