// StreamLightUpdater.exe (8.6.0). What it is for, and what it checks: updater.pro and SECURITY.md.
//
//   (no arguments)        the update, as the task or "runas" starts it (apply.cpp, stageCopy)
//   --run                 its copy in update\run, which does the work (stageRun)
//   --register <user>     StreamLight.iss, elevated: create the task for that account
//                         (the uninstaller deletes it with schtasks, [UninstallRun])
#include "apply.h"
#include "common.h"
#include "task.h"

#include <QFileInfo>
#include <QStringList>

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Physical pixels everywhere: the screen has to land exactly where StreamLight's was.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    QStringList args;
    for (int i = 0; i < argc; ++i) args.append(QString::fromWCharArray(argv[i]));
    LocalFree(argv);

    if (args.size() == 1) return up::stageCopy();
    if (args.size() == 2 && args.at(1) == QLatin1String("--run")) return up::stageRun();

    if (args.size() != 3 || args.at(1) != QLatin1String("--register")) return 1;
    if (!up::isElevated()) return 2;

    QString why;
    if (up::foldersAreSafe(&why)) up::enableLog();
    else up::log(QStringLiteral("folders: %1").arg(why));

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QString error;
    const QString appDir = up::nativePath(QFileInfo(up::thisExe()).absolutePath());
    const bool ok = up::registerTask(appDir, args.at(2), &error);
    up::log(QStringLiteral("register %1: %2").arg(args.at(2), ok ? QStringLiteral("ok") : error));
    if (SUCCEEDED(com)) CoUninitialize();
    return ok ? 0 : 1;
}
