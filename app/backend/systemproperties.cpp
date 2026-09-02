#include "systemproperties.h"
#include "linkspeed.h"
#include "singleinstance.h"
#include "storereset.h"
#include "utils.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QWindow>
#include <QLibraryInfo>
#include <QProcess>
#include <QStringList>

#include "settings/videooptions.h"
#include "streaming/session.h"
#include "streaming/streamutils.h"

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <QSettings>
// Advapi32: OpenProcessToken / LookupPrivilegeValue / AdjustTokenPrivileges /
// InitiateShutdownW (SeShutdownPrivilege handling in shutdownClient()).
#pragma comment(lib, "Advapi32.lib")
// InitiateShutdown flags are not always exposed by the SDK headers in scope here.
#ifndef SHUTDOWN_FORCE_SELF
#define SHUTDOWN_FORCE_SELF      0x00000002
#endif
#ifndef SHUTDOWN_POWEROFF
#define SHUTDOWN_POWEROFF        0x00000008
#endif
#ifndef SHUTDOWN_INSTALL_UPDATES
#define SHUTDOWN_INSTALL_UPDATES 0x00000040
#endif
#endif

#ifdef Q_OS_WIN32
namespace
{
    /*
     * Is this machine set up for the Xbox full screen experience? Two independent markers,
     * either of which is enough: the handheld device form, and a gaming home app pointing at
     * the Xbox app. Both are what the community tools write to turn the experience on, so a
     * desktop that has been converted counts too.
     *
     * ⚠️ Either, not both: this gate must never say "no" where the grey screen can happen.
     * An unnecessary window rebuild is a hitch; a skipped necessary one is an app that looks
     * broken.
     *
     * ⚠️ And it says DEVICE, not SESSION, which is what the data allows and not a shortcut.
     * Measured 01/09/2026 across four cases: an Ally in the Xbox experience and an ordinary
     * desktop are indistinguishable from inside the process — explorer.exe is the shell in
     * both, and GetShellWindow, the taskbar, Progman and the Winlogon shell value all read
     * the same. The one signal that moved with the shell was Progman's visibility, and only
     * when the device had BOOTED into the experience; entering it from the desktop looked
     * exactly like a desktop — and that is the case where the grey screen was observed. A
     * session detector would therefore have skipped the fix precisely where it is needed.
     *
     * Measured values. Ally: DeviceForm 46 (0x2E), GamingHomeApp set. Desktop PC: neither.
     */
    bool detectGamingPostureDevice()
    {
        const QSettings oem(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OEM"),
                            QSettings::NativeFormat);
        const QString deviceForm = oem.value(QStringLiteral("DeviceForm")).toString();

        const QSettings gaming(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration"),
                               QSettings::NativeFormat);
        const QString gamingHomeApp = gaming.value(QStringLiteral("GamingHomeApp")).toString();

        // 0x2E is the handheld device form. Stored as a number, read back as text.
        const bool handheld = (deviceForm.toInt() == 0x2E);
        const bool gamingHome = !gamingHomeApp.isEmpty();
        const bool result = handheld || gamingHome;

        qInfo() << "[gaming posture] DeviceForm:" << deviceForm
                << " GamingHomeApp:" << (gamingHome ? gamingHomeApp : QStringLiteral("<none>"))
                << " => window rebuild after a full-screen stream:"
                << (result ? "ON" : "off");

        return result;
    }
}
#endif

class SystemPropertyQueryThread : public QThread
{
public:
    SystemPropertyQueryThread(SystemProperties* properties)
        : QThread(properties), m_Properties(properties)
    {
        setObjectName("System Properties Async Query Thread");
    }

private:
    void run() override
    {
        bool hasHardwareAcceleration;
        bool rendererAlwaysFullScreen;
        bool supportsHdr;
        QSize maximumResolution;

        Session::getDecoderInfo(m_Properties->testWindow, hasHardwareAcceleration, rendererAlwaysFullScreen, supportsHdr, maximumResolution);

        // Propagate the decoder properties to the SystemProperties singleton and emit any change signals on the main thread
        QMetaObject::invokeMethod(m_Properties, "updateDecoderProperties",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, hasHardwareAcceleration),
                                  Q_ARG(bool, rendererAlwaysFullScreen),
                                  Q_ARG(QSize, maximumResolution),
                                  Q_ARG(bool, supportsHdr));
    }

private:
    SystemProperties* m_Properties;
};

SystemProperties::SystemProperties()
{
    versionString = QString(VERSION_STR);

#ifdef Q_OS_WIN32
    // Read once: it describes the machine, and the machine does not change under us.
    isGamingPostureDevice = detectGamingPostureDevice();
#else
    isGamingPostureDevice = false;
#endif

    hasDesktopEnvironment = WMUtils::isRunningDesktopEnvironment();
    isRunningWayland = WMUtils::isRunningWayland();
    isRunningXWayland = isRunningWayland && QGuiApplication::platformName() == "xcb";
    usesMaterial3Theme = QLibraryInfo::version() >= QVersionNumber(6, 5, 0);
    settingsWereReset = StoreReset::settingsWereReset();
    QString nativeArch = QSysInfo::currentCpuArchitecture();

#ifdef Q_OS_WIN32
    {
        USHORT processArch, machineArch;

        // Use IsWow64Process2() because it doesn't lie on ARM64
        if (IsWow64Process2(GetCurrentProcess(), &processArch, &machineArch)) {
            switch (machineArch) {
            case IMAGE_FILE_MACHINE_I386:
                nativeArch = "i386";
                break;
            case IMAGE_FILE_MACHINE_AMD64:
                nativeArch = "x86_64";
                break;
            case IMAGE_FILE_MACHINE_ARM64:
                nativeArch = "arm64";
                break;
            }
        }

        isWow64 = nativeArch != QSysInfo::buildCpuArchitecture();
    }
#else
    isWow64 = false;
#endif

    if (nativeArch == "i386") {
        friendlyNativeArchName = "x86";
    }
    else if (nativeArch == "x86_64") {
        friendlyNativeArchName = "x64";
    }
    else {
        friendlyNativeArchName = nativeArch.toUpper();
    }

    // Assume we can probably launch a browser if we're in a GUI environment
    hasBrowser = hasDesktopEnvironment;

#ifdef HAVE_DISCORD
    hasDiscordIntegration = true;
#else
    hasDiscordIntegration = false;
#endif

    // These will be queried asynchronously to avoid blocking the UI
    hasHardwareAcceleration = true;
    rendererAlwaysFullScreen = false;
    supportsHdr = true;
    maximumResolution = QSize(0, 0);
}

SystemProperties::~SystemProperties()
{
    waitForAsyncLoad();
}

void SystemProperties::updateDecoderProperties(bool hasHardwareAcceleration, bool rendererAlwaysFullScreen, QSize maximumResolution, bool supportsHdr)
{
    SDL_assert(testWindow);

    if (hasHardwareAcceleration != this->hasHardwareAcceleration) {
        this->hasHardwareAcceleration = hasHardwareAcceleration;
        emit hasHardwareAccelerationChanged();
    }

    if (rendererAlwaysFullScreen != this->rendererAlwaysFullScreen) {
        this->rendererAlwaysFullScreen = rendererAlwaysFullScreen;
        emit rendererAlwaysFullScreenChanged();
    }

    if (maximumResolution != this->maximumResolution) {
        this->maximumResolution = maximumResolution;
        emit maximumResolutionChanged();
    }

    if (supportsHdr != this->supportsHdr) {
        this->supportsHdr = supportsHdr;
        emit supportsHdrChanged();
    }

    SDL_DestroyWindow(testWindow);
    testWindow = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

QRect SystemProperties::getNativeResolution(int displayIndex)
{
    // Returns default constructed QRect if out of bounds
    Q_ASSERT(!monitorNativeResolutions.isEmpty());
    return monitorNativeResolutions.value(displayIndex);
}

QRect SystemProperties::getSafeAreaResolution(int displayIndex)
{
    // Returns default constructed QRect if out of bounds
    Q_ASSERT(!monitorSafeAreaResolutions.isEmpty());
    return monitorSafeAreaResolutions.value(displayIndex);
}

int SystemProperties::getRefreshRate(int displayIndex)
{
    // Returns 0 if out of bounds
    Q_ASSERT(!monitorRefreshRates.isEmpty());
    return monitorRefreshRates.value(displayIndex);
}

void SystemProperties::startAsyncLoad()
{
    if (systemPropertyQueryThread) {
        // Already started/completed
        return;
    }

    // This isn't actually asynchronous (due to the need to synchronize with
    // SdlGamepadKeyNavigation), but we don't query it in the constructor
    // because it's expensive.
    unmappedGamepads = SdlInputHandler::getUnmappedGamepads();
    if (!unmappedGamepads.isEmpty()) {
        emit unmappedGamepadsChanged();
    }

    // We initialize the video subsystem and test window on the main thread
    // because some platforms (macOS) do not support window creation on
    // non-main threads.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return;
    }

    // Update display related attributes (max FPS, native resolution, etc).
    refreshDisplays();

    testWindow = SDL_CreateWindow("", 0, 0, 1280, 720,
                                  SDL_WINDOW_HIDDEN | StreamUtils::getPlatformWindowFlags());
    if (!testWindow) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create test window with platform flags: %s",
                    SDL_GetError());

        testWindow = SDL_CreateWindow("", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
        if (!testWindow) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create window for hardware decode test: %s",
                         SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }
    }

    systemPropertyQueryThread = new SystemPropertyQueryThread(this);
    systemPropertyQueryThread->start();
}

void SystemProperties::waitForAsyncLoad()
{
    if (systemPropertyQueryThread) {
        systemPropertyQueryThread->wait();
    }
}

void SystemProperties::refreshDisplays()
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return;
    }

    monitorNativeResolutions.clear();

    SDL_DisplayMode bestMode;
    for (int displayIndex = 0; displayIndex < SDL_GetNumVideoDisplays(); displayIndex++) {
        SDL_DisplayMode desktopMode;
        SDL_Rect safeArea;

        if (StreamUtils::getNativeDesktopMode(displayIndex, &desktopMode, &safeArea)) {
            if (desktopMode.w <= 8192 && desktopMode.h <= 8192) {
                monitorNativeResolutions.insert(displayIndex, QRect(0, 0, desktopMode.w, desktopMode.h));
                monitorSafeAreaResolutions.insert(displayIndex, QRect(0, 0, safeArea.w, safeArea.h));
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping resolution over 8K: %dx%d",
                            desktopMode.w, desktopMode.h);
            }

            // Start at desktop mode and work our way up
            bestMode = desktopMode;
            for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
                SDL_DisplayMode mode;
                if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0) {
                    if (mode.w == desktopMode.w && mode.h == desktopMode.h) {
                        if (mode.refresh_rate > bestMode.refresh_rate) {
                            bestMode = mode;
                        }
                    }
                }
            }

            // Try to normalize values around our our standard refresh rates.
            // Some displays/OSes report values that are slightly off.
            if (bestMode.refresh_rate >= 58 && bestMode.refresh_rate <= 62) {
                monitorRefreshRates.append(60);
            }
            else if (bestMode.refresh_rate >= 28 && bestMode.refresh_rate <= 32) {
                monitorRefreshRates.append(30);
            }
            else {
                monitorRefreshRates.append(bestMode.refresh_rate);
            }
        }
    }

    // Hand the pickers what the displays reported (5.5.0). Here rather than at each call
    // site so there is exactly one moment where "what this machine can do" is decided, and
    // it is the moment the numbers are read.
    QList<QSize> nativeSizes;
    for (const QRect& r : monitorNativeResolutions) {
        nativeSizes.append(r.size());
    }
    VideoOptions::setNativeDisplays(nativeSizes, monitorRefreshRates);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

QVariantMap SystemProperties::videoOptions()
{
    /*
     * Normally the snapshot is already there: startAsyncLoad() takes it during startup.
     * It is not on the paths where that never runs — a launch straight into a stream, where
     * runConfigChecks is false — and a picker built then would silently offer the presets
     * alone, which is precisely the bug this feature exists to end.
     *
     * ⚠️ Only when it is empty. refreshDisplays() clears monitorNativeResolutions but
     * APPENDS to monitorRefreshRates (upstream's asymmetry, untouched here), so calling it
     * a second time over a populated list would duplicate every rate.
     */
    if (VideoOptions::displayCount() == 0) {
        refreshDisplays();
    }

    QVariantMap map;

    QVariantList frameRates;
    for (int fps : VideoOptions::frameRates()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("value"), fps);
        entry.insert(QStringLiteral("label"), VideoOptions::frameRateLabel(fps));
        // isNative, not native: the latter is a reserved word in some ECMAScript editions
        // and this map is read from QML.
        entry.insert(QStringLiteral("isNative"), VideoOptions::isNativeFrameRate(fps));
        frameRates.append(entry);
    }

    QVariantList resolutions;
    for (const QSize& size : VideoOptions::resolutions()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("width"), size.width());
        entry.insert(QStringLiteral("height"), size.height());
        entry.insert(QStringLiteral("label"), VideoOptions::resolutionLabel(size));
        entry.insert(QStringLiteral("isNative"), VideoOptions::isNativeResolution(size));
        resolutions.append(entry);
    }

    map.insert(QStringLiteral("fps"), frameRates);
    map.insert(QStringLiteral("res"), resolutions);
    map.insert(QStringLiteral("fpsHint"), VideoOptions::nativeRefreshHint());
    map.insert(QStringLiteral("resHint"), VideoOptions::nativeResolutionHint());
    map.insert(QStringLiteral("displays"), VideoOptions::displayCount());
    return map;
}

void SystemProperties::restartApplication()
{
    // Build the new argv: same executable path, same CLI arguments minus argv[0].
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty()) {
        args.removeFirst();
    }

    const QString exePath = QCoreApplication::applicationFilePath();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SystemProperties: restarting (%s)", exePath.toUtf8().constData());

    // ⚠️ Stop listening on the single-instance pipe BEFORE spawning, or the restart
    // silently turns into a plain quit — which is exactly what it did until 5.4.0.
    //
    // The replacement runs SingleInstance::attach() within a few hundred ms, long
    // before this process has finished tearing down Qt, SDL and FFmpeg. It connects
    // to the pipe we are still holding, concludes an instance is already running,
    // sends RAISE and returns 0. We then quit — and there is nothing left. Nobody
    // sees an error, because from each process's own point of view it did the right
    // thing. Closing the pipe first makes the child's probe fail, which is what makes
    // it become the primary instance.
    //
    // Ordered so a failed spawn is recoverable: if startDetached fails we are still
    // running, so re-listen and leave everything as it was.
    SingleInstance* instance = SingleInstance::primary();
    if (instance != nullptr) {
        instance->release();
    }

    // QProcess::startDetached spawns a fully independent child process — it does
    // not wait on the parent exiting, so calling quit() right after is safe.
    if (!QProcess::startDetached(exePath, args)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SystemProperties: restart failed to spawn detached process");
        if (instance != nullptr) {
            instance->attach();
        }
        return;
    }

    QCoreApplication::quit();
}

void SystemProperties::shutdownClient(bool installUpdates)
{
#ifdef Q_OS_WIN32
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SystemProperties: powering off client PC%s",
                installUpdates ? " (installing pending updates first)" : "");

    // Enable SeShutdownPrivilege on our process token, then request a full power-off.
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValue(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
            AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
        }
        CloseHandle(token);
    }

    // "Update and shut down": InitiateShutdown is the only documented way to install
    // pending updates before power-off (ExitWindowsEx does not). SHUTDOWN_FORCE_SELF
    // guarantees our own session logs off; a planned reason avoids the unplanned-state
    // file delay. Falls through to the plain power-off if it is refused.
    if (installUpdates) {
        DWORD rc = InitiateShutdownW(nullptr, nullptr, 0,
                                     SHUTDOWN_INSTALL_UPDATES | SHUTDOWN_POWEROFF | SHUTDOWN_FORCE_SELF,
                                     SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
        if (rc == ERROR_SUCCESS) {
            return;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SystemProperties: InitiateShutdown(install updates) failed (rc %lu); plain power-off",
                    rc);
    }

    if (ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF, SHTDN_REASON_MAJOR_OTHER)) {
        return;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "SystemProperties: ExitWindowsEx failed (err %lu); falling back to shutdown.exe",
                GetLastError());

    // Fallback: the shutdown CLI does its own privilege handling. Use the full
    // System32 path (not bare "shutdown") so a hijacked PATH can't redirect it.
    const QString shutdownExe = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))
                                + QStringLiteral("\\System32\\shutdown.exe");
    QProcess::startDetached(shutdownExe, { QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0") });
#else
    Q_UNUSED(installUpdates);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "SystemProperties: shutdownClient is only supported on Windows");
#endif
}

void SystemProperties::acknowledgeSettingsReset()
{
    StoreReset::acknowledge();
}

void SystemProperties::recreateNativeWindow()
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* win : windows) {
        if (win == nullptr || win->handle() == nullptr || !win->isVisible()) {
            continue;
        }

        // Captured before the teardown: destroy() drops the platform window, and with it
        // everything Windows knew about this one.
        const QWindow::Visibility visibility = win->visibility();
        const QRect geometry = win->geometry();

        // Releases the HWND and the graphics resources hanging off it, the stale swap chain
        // included. Qt Quick handles losing the platform window here and rebuilds the scene
        // graph against the new one.
        win->destroy();

        win->setGeometry(geometry);
        // Recreates the platform window and re-applies the state in one step, so a
        // full-screen window comes back full screen rather than windowed.
        win->setVisibility(visibility);
        win->requestActivate();

        // One top-level window is ours; anything else Qt owns (popups, tooltips) must not
        // be dragged through this.
        break;
    }
}

QVariantMap SystemProperties::localLinkInfo()
{
    LinkSpeed::Info info = LinkSpeed::probeDefaultRoute();

    static const char* const kStatus[] = { "wired", "wireless", "virtual", "nolinkspeed", "unavailable" };

    QVariantMap out;
    out[QStringLiteral("status")]  = QString::fromLatin1(kStatus[static_cast<int>(info.status)]);
    out[QStringLiteral("mbps")]    = static_cast<qulonglong>(info.mbps);
    out[QStringLiteral("adapter")] = info.adapterName;
    out[QStringLiteral("reason")]  = info.reason;
    out[QStringLiteral("usable")]  = info.usable();
    return out;
}

bool SystemProperties::updatesPending()
{
#ifdef Q_OS_WIN32
    // The two registry keys Windows sets when an installed update is waiting for a
    // reboot. Mirrors WindowsUpdateState on the host. KEY_WOW64_64KEY: always read the
    // 64-bit view regardless of our own bitness.
    auto keyExists = [](const wchar_t* subKey) -> bool {
        HKEY h = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &h) == ERROR_SUCCESS) {
            RegCloseKey(h);
            return true;
        }
        return false;
    };
    return keyExists(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending")
        || keyExists(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");
#else
    return false;
#endif
}
