#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <windows.h>

/*
 * What every part of the updater shares: where things live, who may write there, the status
 * StreamLight reads, the log, and the few facts about the installed copy.
 *
 * The folders (SECURITY.md, "Updating without the Windows permission prompt"):
 *
 *   %ProgramData%\FoggyBytes\StreamLight\update          SYSTEM + administrators only
 *       run\     the copy of this program that does the work
 *       tmp\     TEMP and TMP of Setup
 *       StreamLight_<v>_Installer.exe   the checked copy of the installer
 *   %ProgramData%\FoggyBytes\StreamLight\update-status   users may read, not write
 *       status.json, updater.log, setup.log
 *   %LOCALAPPDATA%\FoggyBytes\StreamLight\update         the user's: StreamLight downloads here
 *       StreamLight_<v>_Installer.exe, look.json, cancel
 */
namespace up {

// The task and the window class. StreamLight.iss names both too.
extern const wchar_t* const k_TaskName;           // "StreamLight Update"
extern const wchar_t* const k_ScreenClass;        // "StreamLightUpdaterScreen"
// Setup → screen: wParam = progress, lParam = maximum (CurInstallProgressChanged).
constexpr UINT k_MsgSetupProgress = WM_APP + 1;

QString baseDir();      // %ProgramData%\FoggyBytes\StreamLight
QString workDir();      // …\update
QString runDir();       // …\update\run
QString tmpDir();       // …\update\tmp
QString statusDir();    // …\update-status
QString userDir();      // %LOCALAPPDATA%\FoggyBytes\StreamLight\update — of THIS process's user

QString thisExe();
bool isElevated();

/**
 * The folder exists, is not a junction or other reparse point, belongs to SYSTEM or the
 * administrators, and — when `closed` — grants no write, delete or permission change to anyone
 * else. `create` makes it (with SYSTEM and administrators only) when it is missing.
 */
bool folderIsSafe(const QString& path, bool closed, bool create, QString* why);
/** The same, creating a missing folder readable by users (the status folder). */
bool folderIsSafe(const QString& path, bool closed, bool create, QString* why, bool usersRead);

/** All of the above for the chain the work and the status live in. */
bool foldersAreSafe(QString* why);

/** One line, timestamped, into update-status\updater.log — once the folders are known safe. */
void log(const QString& line);
void enableLog();

/**
 * status.json: the fields given, plus `v`, `started` (this run's start, ms since the epoch) and
 * `at`. Written whole and swapped in, so StreamLight never reads half a file.
 */
void writeStatus(const QJsonObject& fields);
qint64 runStarted();
qint64 nowMs();

/** "8.6.0" style: negative if a < b. A leading "v" and a BOM are tolerated. */
QList<int> parseVersion(const QString& v);
int compareVersions(const QString& a, const QString& b);

/** {app}, from the uninstall entry Setup keeps under HKLM — which only administrators write. */
QString installDir();
/** The product version of a file, "8.5.1" (three parts), or empty. */
QString fileVersion(const QString& path);

/** Running StreamLight.exe processes started from `appDir`. */
QList<DWORD> streamLightProcesses(const QString& appDir, ULONGLONG startedAfter = 0);

QString nativePath(const QString& p);
std::wstring wide(const QString& s);

} // namespace up
