#pragma once

#include <QObject>
#include <QRect>
#include <QVariantMap>

#include "SDL_compat.h"

class SystemProperties : public QObject
{
    Q_OBJECT

    friend class SystemPropertyQueryThread;

public:
    SystemProperties();
    ~SystemProperties();

    // Static properties queried synchronously during the constructor
    Q_PROPERTY(bool isRunningWayland MEMBER isRunningWayland CONSTANT)
    Q_PROPERTY(bool isRunningXWayland MEMBER isRunningXWayland CONSTANT)
    Q_PROPERTY(bool isWow64 MEMBER isWow64 CONSTANT)
    Q_PROPERTY(QString friendlyNativeArchName MEMBER friendlyNativeArchName CONSTANT)
    Q_PROPERTY(bool hasDesktopEnvironment MEMBER hasDesktopEnvironment CONSTANT)
    Q_PROPERTY(bool hasBrowser MEMBER hasBrowser CONSTANT)
    Q_PROPERTY(bool hasDiscordIntegration MEMBER hasDiscordIntegration CONSTANT)
    Q_PROPERTY(bool usesMaterial3Theme MEMBER usesMaterial3Theme CONSTANT)
    Q_PROPERTY(QString versionString MEMBER versionString CONSTANT)

    // True on the first launch after upgrading from a build that shared Moonlight's
    // settings store (5.3.0 and earlier), where nothing carries over. Drives the
    // one-time notice on the host list — see storereset.h. CONSTANT because the
    // answer is decided once at startup, before any of this exists.
    Q_PROPERTY(bool settingsWereReset MEMBER settingsWereReset CONSTANT)

    // Properties queried asynchronously (startAsyncLoad() must be called!)
    Q_PROPERTY(bool hasHardwareAcceleration MEMBER hasHardwareAcceleration NOTIFY hasHardwareAccelerationChanged)
    Q_PROPERTY(bool rendererAlwaysFullScreen MEMBER rendererAlwaysFullScreen NOTIFY rendererAlwaysFullScreenChanged)
    Q_PROPERTY(QString unmappedGamepads MEMBER unmappedGamepads NOTIFY unmappedGamepadsChanged)
    Q_PROPERTY(QSize maximumResolution MEMBER maximumResolution NOTIFY maximumResolutionChanged)
    Q_PROPERTY(bool supportsHdr MEMBER supportsHdr NOTIFY supportsHdrChanged)

    // Either startAsyncLoad()+waitForAsyncLoad() or refreshDisplays() must be invoked first
    Q_INVOKABLE QRect getNativeResolution(int displayIndex);
    Q_INVOKABLE QRect getSafeAreaResolution(int displayIndex);
    Q_INVOKABLE int getRefreshRate(int displayIndex);

    Q_INVOKABLE void startAsyncLoad();
    Q_INVOKABLE void waitForAsyncLoad();
    Q_INVOKABLE void refreshDisplays();

    // Restarts StreamLight by spawning a detached copy of the running executable
    // (with the same CLI arguments) and quitting the current process. Used by
    // settings that require a fresh boot to take effect (e.g. Tailscale auto-start).
    Q_INVOKABLE void restartApplication();

    // Powers off THIS (client) PC. Used by the Power dialog's "Client" / "Both"
    // targets. Windows-only; a no-op on other platforms.
    // installUpdates: install pending Windows updates before powering off
    // ("Update and shut down", via InitiateShutdown + SHUTDOWN_INSTALL_UPDATES).
    Q_INVOKABLE void shutdownClient(bool installUpdates = false);

    // True when this (client) PC has a Windows update installed and waiting for a
    // reboot. Read-only registry probe; Windows-only (false elsewhere). Used to hint
    // the user in the Power dialog. Like the host side, a false result does not
    // guarantee there is nothing to install — see WindowsUpdateState on the host.
    Q_INVOKABLE bool updatesPending();

    // This device's wired connection, for Settings — which has no host in context.
    // Returns {usable, mbps, adapter, reason, status}. Host-specific screens must use
    // ComputerModel::probeLocalLink() instead: a given host can still be reached over
    // Wi-Fi or Tailscale while the default route is a perfectly good cable.
    Q_INVOKABLE QVariantMap localLinkInfo();

    // Destroys and rebuilds the native window behind our top-level QWindow, preserving
    // geometry and visibility. Used on return from a stream under the Xbox full screen
    // experience, where Qt keeps presenting frames at full rate (frameSwapped climbs,
    // isExposed() is true, the window is foreground and not cloaked) yet nothing reaches
    // the display — the window's composition binding did not survive the stream window
    // taking exclusive full screen. Showing, raising and re-entering full screen all leave
    // the same HWND in place and change nothing; only a new HWND gets a new swap chain and
    // a fresh binding. Windows-only in effect, harmless elsewhere.
    Q_INVOKABLE void recreateNativeWindow();

    // The user has read the "settings were reset" notice. Stamps the store marker so
    // it never appears again. Not tied to settingsWereReset staying true for the rest
    // of this session: the property is CONSTANT and the dialog is shown once anyway.
    Q_INVOKABLE void acknowledgeSettingsReset();

signals:
    void unmappedGamepadsChanged();
    void hasHardwareAccelerationChanged();
    void rendererAlwaysFullScreenChanged();
    void maximumResolutionChanged();
    void supportsHdrChanged();

private slots:
    void updateDecoderProperties(bool hasHardwareAcceleration, bool rendererAlwaysFullScreen, QSize maximumResolution, bool supportsHdr);

private:
    QThread* systemPropertyQueryThread = nullptr;
    SDL_Window* testWindow = nullptr;

    // Properties set by the constructor
    bool isRunningWayland;
    bool isRunningXWayland;
    bool isWow64;
    QString friendlyNativeArchName;
    bool hasDesktopEnvironment;
    bool hasBrowser;
    bool hasDiscordIntegration;
    QString versionString;
    bool usesMaterial3Theme;
    bool settingsWereReset;

    // Properties only set if startAsyncLoad() is called
    bool hasHardwareAcceleration;
    bool rendererAlwaysFullScreen;
    QSize maximumResolution;
    bool supportsHdr;
    QString unmappedGamepads;

    // Properties set by refreshDisplays()
    QList<QRect> monitorNativeResolutions;
    QList<QRect> monitorSafeAreaResolutions;
    QList<int> monitorRefreshRates;
};

