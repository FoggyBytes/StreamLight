#include "huesyncclient.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>
#include <QThread>

#include <SDL_log.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

// The search of the 2.2.0 integration (HueSyncManager, removed in 8.4.0), kept as it was: it
// found Hue Sync on every client it ran on, including custom and per-user installs.
QString HueSyncClient::executable()
{
#ifdef Q_OS_WIN32
    // HueSyncStarter.exe is Hue Sync's silent launcher; the main executable opens a window.
    const QStringList exeNames = { "HueSyncStarter.exe", "HueSync.exe", "Hue Sync.exe" };
    const QStringList hives = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };
    for (const QString& hive : hives) {
        QSettings reg(hive, QSettings::NativeFormat);
        for (const QString& group : reg.childGroups()) {
            reg.beginGroup(group);
            const bool isHue = reg.value("DisplayName").toString().contains("Hue Sync", Qt::CaseInsensitive);
            QString location = reg.value("InstallLocation").toString();
            reg.endGroup();
            if (!isHue || location.isEmpty()) continue;
            if (!location.endsWith('\\') && !location.endsWith('/')) location += '\\';
            for (const QString& exe : exeNames)
                if (QFile::exists(location + exe)) return location + exe;
        }
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList dirs = {
        env.value("ProgramFiles") + "\\Hue Sync\\",
        env.value("LOCALAPPDATA") + "\\Programs\\hue-sync\\",
        env.value("ProgramFiles") + "\\Philips Hue\\Hue Sync\\",
        env.value("ProgramFiles(x86)") + "\\Philips Hue\\Hue Sync\\",
    };
    for (const QString& dir : dirs) {
        if (dir.startsWith('\\')) continue;
        for (const QString& exe : exeNames)
            if (QFile::exists(dir + exe)) return dir + exe;
    }
#endif
    return {};
}

#ifdef Q_OS_WIN32
// Every process named HueSync.exe: terminated when kill is set, counted either way.
static int forEachHueSync(bool kill)
{
    int found = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"HueSync.exe") != 0) continue;
            found++;
            if (!kill) continue;
            HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (proc) {
                TerminateProcess(proc, 0);
                WaitForSingleObject(proc, 3000);
                CloseHandle(proc);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}
#endif

bool HueSyncClient::running()
{
#ifdef Q_OS_WIN32
    return forEachHueSync(false) > 0;
#else
    return false;
#endif
}

bool HueSyncClient::restart()
{
#ifdef Q_OS_WIN32
    const QString exe = executable();
    if (exe.isEmpty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Hue Sync: not installed on this device");
        return false;
    }
    if (forEachHueSync(true) > 0)
        QThread::msleep(500);   // the bridge sees the old stream end before the new one asks

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const std::wstring path = exe.toStdWString();
    if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Hue Sync: could not start %s (error %lu)",
                    exe.toUtf8().constData(), (unsigned long)GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Hue Sync: restarted to take the lights back");
    return true;
#else
    return false;
#endif
}
