#pragma once

#include <QString>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

class HueSyncManager
{
public:
    HueSyncManager();
    ~HueSyncManager();

    // Returns the absolute path to the Hue Sync executable, or an empty string if not found.
    static QString discoverExecutable();

    // Launches Hue Sync minimized. Returns true on success.
    bool launch(const QString& exePath);

    // Terminates the launched process if it is still running.
    void terminate();

    bool isRunning() const;

private:
#ifdef Q_OS_WIN32
    HANDLE m_processHandle = nullptr;
    DWORD  m_processId     = 0;
#endif
};
