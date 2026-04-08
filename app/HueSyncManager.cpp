#include "HueSyncManager.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>

#include <SDL_log.h>

#ifdef Q_OS_WIN32
#include <tlhelp32.h>
#endif

HueSyncManager::HueSyncManager() = default;

HueSyncManager::~HueSyncManager()
{
    terminate();
}

// Searches the Windows Uninstall registry hives for an entry whose DisplayName
// contains "Hue Sync" and returns the path to the best available executable
// (HueSyncStarter.exe preferred over HueSync.exe) derived from InstallLocation.
// Returns an empty string if nothing is found.
static QString findInRegistry()
{
#ifdef Q_OS_WIN32
    const QStringList hives = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };

    // HueSyncStarter.exe is the dedicated silent-launch helper. Prefer it over
    // the main executable, which always opens a visible window on startup.
    const QStringList exeNames = { "HueSyncStarter.exe", "HueSync.exe", "Hue Sync.exe" };

    for (const QString& hive : hives) {
        QSettings reg(hive, QSettings::NativeFormat);
        const QStringList groups = reg.childGroups();

        for (const QString& group : groups) {
            reg.beginGroup(group);
            const QString displayName = reg.value("DisplayName").toString();
            if (displayName.contains("Hue Sync", Qt::CaseInsensitive)) {
                QString installLocation = reg.value("InstallLocation").toString();
                reg.endGroup();

                if (installLocation.isEmpty()) {
                    continue;
                }

                if (!installLocation.endsWith('\\') && !installLocation.endsWith('/')) {
                    installLocation += '\\';
                }

                for (const QString& exe : exeNames) {
                    const QString candidate = installLocation + exe;
                    if (QFile::exists(candidate)) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                    "HueSyncManager: found via registry at %s",
                                    candidate.toUtf8().constData());
                        return candidate;
                    }
                }
            } else {
                reg.endGroup();
            }
        }
    }
#endif
    return {};
}

QString HueSyncManager::discoverExecutable()
{
#ifdef Q_OS_WIN32
    // Primary: look up the install location from the Windows Uninstall registry.
    // This handles custom install paths and per-user (Electron) installations.
    const QString fromRegistry = findInRegistry();
    if (!fromRegistry.isEmpty()) {
        return fromRegistry;
    }

    // Fallback: well-known paths. HueSyncStarter.exe is tried first in each directory.
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString localAppData    = env.value("LOCALAPPDATA");
    const QString programFiles    = env.value("ProgramFiles");
    const QString programFilesX86 = env.value("ProgramFiles(x86)");

    const QStringList candidates = {
        programFiles    + "\\Hue Sync\\HueSyncStarter.exe",
        programFiles    + "\\Hue Sync\\HueSync.exe",
        programFiles    + "\\Hue Sync\\Hue Sync.exe",
        localAppData    + "\\Programs\\hue-sync\\HueSyncStarter.exe",
        localAppData    + "\\Programs\\hue-sync\\HueSync.exe",
        localAppData    + "\\Programs\\hue-sync\\Hue Sync.exe",
        programFiles    + "\\Philips Hue\\Hue Sync\\HueSyncStarter.exe",
        programFiles    + "\\Philips Hue\\Hue Sync\\HueSync.exe",
        programFiles    + "\\Philips Hue\\Hue Sync\\Hue Sync.exe",
        programFilesX86 + "\\Philips Hue\\Hue Sync\\HueSyncStarter.exe",
        programFilesX86 + "\\Philips Hue\\Hue Sync\\HueSync.exe",
        programFilesX86 + "\\Philips Hue\\Hue Sync\\Hue Sync.exe",
    };

    for (const QString& path : candidates) {
        if (!path.startsWith('\\') && QFile::exists(path)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "HueSyncManager: found via fallback path at %s",
                        path.toUtf8().constData());
            return path;
        }
    }
#endif
    return {};
}

bool HueSyncManager::launch(const QString& exePath)
{
#ifdef Q_OS_WIN32
    STARTUPINFOW si = {};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi = {};

    std::wstring cmdLine = exePath.toStdWString();

    BOOL ok = CreateProcessW(
        cmdLine.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (ok) {
        m_processHandle = pi.hProcess;
        m_processId     = pi.dwProcessId;
        CloseHandle(pi.hThread);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "HueSyncManager: launched PID %lu", (unsigned long)m_processId);
        return true;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "HueSyncManager: CreateProcessW failed (error %lu)", (unsigned long)GetLastError());
#endif
    return false;
}

#ifdef Q_OS_WIN32
// Terminates all running processes with the given executable name.
static void killProcessByName(const wchar_t* exeName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                if (proc) {
                    TerminateProcess(proc, 0);
                    WaitForSingleObject(proc, 3000);
                    CloseHandle(proc);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "HueSyncManager: terminated %ls (PID %lu)",
                                exeName, (unsigned long)entry.th32ProcessID);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
}
#endif

void HueSyncManager::terminate()
{
#ifdef Q_OS_WIN32
    // Release the handle to HueSyncStarter.exe (may have already exited after
    // spawning HueSync.exe — that is its normal behaviour).
    if (m_processHandle) {
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
        m_processId     = 0;
    }

    // Kill the actual Hue Sync application by process name regardless of how
    // it was spawned (directly or via HueSyncStarter.exe).
    killProcessByName(L"HueSync.exe");
#endif
}

bool HueSyncManager::isRunning() const
{
#ifdef Q_OS_WIN32
    if (!m_processHandle) {
        return false;
    }
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(m_processHandle, &exitCode);
    return exitCode == STILL_ACTIVE;
#else
    return false;
#endif
}
