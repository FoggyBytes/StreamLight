#include "TailscaleManager.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>

#include <SDL_log.h>

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

// Tailscale-ipn.exe is the background IPN/tray process. Launching it is what
// brings the Tailscale VPN online. The CLI (tailscale.exe) only issues commands
// to an already-running daemon, so it is not what we want to spawn.
static const wchar_t* kTailscaleProcessName = L"tailscale-ipn.exe";

// Searches the Windows Uninstall registry hives for an entry whose DisplayName
// contains "Tailscale" and returns the path to tailscale-ipn.exe derived from
// the registered InstallLocation. Returns an empty string if nothing is found.
//
// Only the official Windows installer from https://tailscale.com/download is
// supported. The Microsoft Store package registers differently and is not
// covered here.
static QString findInRegistry()
{
#ifdef Q_OS_WIN32
    const QStringList hives = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };

    for (const QString& hive : hives) {
        QSettings reg(hive, QSettings::NativeFormat);
        const QStringList groups = reg.childGroups();

        for (const QString& group : groups) {
            reg.beginGroup(group);
            const QString displayName = reg.value("DisplayName").toString();
            if (displayName.contains("Tailscale", Qt::CaseInsensitive)) {
                QString installLocation = reg.value("InstallLocation").toString();
                reg.endGroup();

                if (installLocation.isEmpty()) {
                    continue;
                }

                if (!installLocation.endsWith('\\') && !installLocation.endsWith('/')) {
                    installLocation += '\\';
                }

                const QString candidate = installLocation + "tailscale-ipn.exe";
                if (QFile::exists(candidate)) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "TailscaleManager: found via registry at %s",
                                candidate.toUtf8().constData());
                    return candidate;
                }
            } else {
                reg.endGroup();
            }
        }
    }
#endif
    return {};
}

QString TailscaleManager::discoverExecutable()
{
#ifdef Q_OS_WIN32
    // Primary: registry lookup (handles custom install paths).
    const QString fromRegistry = findInRegistry();
    if (!fromRegistry.isEmpty()) {
        return fromRegistry;
    }

    // Fallback: the well-known default install path used by the official
    // tailscale.com installer.
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString programFiles    = env.value("ProgramFiles");

    const QString candidate = programFiles + "\\Tailscale\\tailscale-ipn.exe";
    if (!programFiles.isEmpty() && QFile::exists(candidate)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "TailscaleManager: found via fallback path at %s",
                    candidate.toUtf8().constData());
        return candidate;
    }
#endif
    return {};
}

bool TailscaleManager::isAlreadyRunning()
{
#ifdef Q_OS_WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kTailscaleProcessName) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
#else
    return false;
#endif
}

bool TailscaleManager::launch(const QString& exePath)
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
        // Fire-and-forget: we do not track the spawned process. Close both
        // handles immediately so the child becomes fully independent.
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "TailscaleManager: launched PID %lu", (unsigned long)pi.dwProcessId);
        return true;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "TailscaleManager: CreateProcessW failed (error %lu)",
                (unsigned long)GetLastError());
#else
    Q_UNUSED(exePath);
#endif
    return false;
}
