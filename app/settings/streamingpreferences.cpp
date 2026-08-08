#include "streamingpreferences.h"
#include "utils.h"

#include <QSettings>
#include <QReadWriteLock>
#include <QtMath>

#include <QtDebug>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_BITRATE "bitrate"
#define SER_UNLOCK_BITRATE "unlockbitrate"
#define SER_AUTOADJUSTBITRATE "autoadjustbitrate"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_VIDEOCFG "videocfg"
#define SER_HDR "hdr"
#define SER_YUV444 "yuv444"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_FRAMEPACINGMODE "framepacingmode"
#define SER_CONNWARNINGS "connwarnings"
#define SER_CONFWARNINGS "confwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_AUTORECONNECTNOVIDEO "autoreconnectnovideo"
#define SER_MATCHHOSTLINKSPEED "matchhostlinkspeed"
#define SER_WAITFORGAME "waitforgame"
#define SER_SHOWPERFOVERLAY "showperfoverlay"
#define SER_OVERLAYMODE "overlaymode"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_KEEPAWAKE "keepawake"
#define SER_HUESYNC "huesync"
#define SER_HIDEHOSTIPS "hidehostips"
#define SER_TAILSCALE_AUTOSTART "tailscaleautostart"
#define SER_GLYPHSET "glyphset"
#define CURRENT_DEFAULT_VER 2

static StreamingPreferences* s_GlobalPrefs;

Q_GLOBAL_STATIC(QReadWriteLock, s_GlobalPrefsLock)

StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine)
    : m_QmlEngine(qmlEngine)
{
    reload();
}

StreamingPreferences* StreamingPreferences::get(QQmlEngine *qmlEngine)
{
    {
        QReadLocker readGuard(s_GlobalPrefsLock);

        // If we have a preference object and it's associated with a QML engine or
        // if the caller didn't specify a QML engine, return the existing object.
        if (s_GlobalPrefs && (s_GlobalPrefs->m_QmlEngine || !qmlEngine)) {
            // The lifetime logic here relies on the QML engine also being a singleton.
            Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(s_GlobalPrefsLock);

        // If we already have an preference object but the QML engine is now available,
        // associate the QML engine with the preferences.
        if (s_GlobalPrefs) {
            if (!s_GlobalPrefs->m_QmlEngine) {
                s_GlobalPrefs->m_QmlEngine = qmlEngine;
            }
            else {
                // We could reach this codepath if another thread raced with us
                // and created the object while we were outside the pref lock.
                Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            }
        }
        else {
            s_GlobalPrefs = new StreamingPreferences(qmlEngine);
        }

        return s_GlobalPrefs;
    }
}

StreamingPreferences* StreamingPreferences::clone(QObject* parent) const
{
    // Private ctor reloads from QSettings; we then overwrite with this object's
    // in-memory values so unsaved changes are captured too. The result is a plain
    // (non-singleton, non-QML, non-persisted) copy used for a single session.
    auto* p = new StreamingPreferences(nullptr);

    p->width = width;
    p->height = height;
    p->fps = fps;
    p->bitrateKbps = bitrateKbps;
    p->unlockBitrate = unlockBitrate;
    p->autoAdjustBitrate = autoAdjustBitrate;
    p->enableVsync = enableVsync;
    p->gameOptimizations = gameOptimizations;
    p->playAudioOnHost = playAudioOnHost;
    p->multiController = multiController;
    p->enableMdns = enableMdns;
    p->quitAppAfter = quitAppAfter;
    p->absoluteMouseMode = absoluteMouseMode;
    p->absoluteTouchMode = absoluteTouchMode;
    p->framePacingMode = framePacingMode;
    p->connectionWarnings = connectionWarnings;
    p->configurationWarnings = configurationWarnings;
    p->richPresence = richPresence;
    p->gamepadMouse = gamepadMouse;
    p->detectNetworkBlocking = detectNetworkBlocking;
    p->autoReconnectNoVideo = autoReconnectNoVideo;
    p->matchHostLinkSpeed = matchHostLinkSpeed;
    p->waitForGameOnScreen = waitForGameOnScreen;
    p->overlayMode = overlayMode;
    p->swapMouseButtons = swapMouseButtons;
    p->muteOnFocusLoss = muteOnFocusLoss;
    p->backgroundGamepad = backgroundGamepad;
    p->reverseScrollDirection = reverseScrollDirection;
    p->swapFaceButtons = swapFaceButtons;
    p->keepAwake = keepAwake;
    p->hueSyncIntegration = hueSyncIntegration;
    p->hideHostIps = hideHostIps;
    p->tailscaleAutoStart = tailscaleAutoStart;
    p->packetSize = packetSize;
    p->audioConfig = audioConfig;
    p->videoCodecConfig = videoCodecConfig;
    p->enableHdr = enableHdr;
    p->enableYUV444 = enableYUV444;
    p->videoDecoderSelection = videoDecoderSelection;
    p->windowMode = windowMode;
    p->recommendedFullScreenMode = recommendedFullScreenMode;
    p->uiDisplayMode = uiDisplayMode;
    p->captureSysKeysMode = captureSysKeysMode;
    p->glyphSet = glyphSet;

    p->setParent(parent);
    return p;
}

void StreamingPreferences::reload()
{
    QSettings settings;

    int defaultVer = settings.value(SER_DEFAULTVER, 0).toInt();

#ifdef Q_OS_DARWIN
    recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
#else
    // Wayland doesn't support modesetting, so use fullscreen desktop mode
    // unless we have a slow GPU (which can take advantage of wp_viewporter
    // to reduce GPU load with lower resolution video streams).
    if (WMUtils::isRunningWayland() && !WMUtils::isGpuSlow()) {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
    }
    else {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN;
    }
#endif

    width = settings.value(SER_WIDTH, 1280).toInt();
    height = settings.value(SER_HEIGHT, 720).toInt();
    fps = settings.value(SER_FPS, 60).toInt();
    enableYUV444 = settings.value(SER_YUV444, false).toBool();
    bitrateKbps = settings.value(SER_BITRATE, getDefaultBitrate(width, height, fps, enableYUV444)).toInt();
    unlockBitrate = settings.value(SER_UNLOCK_BITRATE, false).toBool();
    autoAdjustBitrate = settings.value(SER_AUTOADJUSTBITRATE, true).toBool();
    enableVsync = settings.value(SER_VSYNC, true).toBool();
    gameOptimizations = settings.value(SER_GAMEOPTS, true).toBool();
    playAudioOnHost = settings.value(SER_HOSTAUDIO, false).toBool();
    multiController = settings.value(SER_MULTICONT, true).toBool();
    enableMdns = settings.value(SER_MDNS, true).toBool();
    quitAppAfter = settings.value(SER_QUITAPPAFTER, false).toBool();
    absoluteMouseMode = settings.value(SER_ABSMOUSEMODE, false).toBool();
    absoluteTouchMode = settings.value(SER_ABSTOUCHMODE, true).toBool();
    // Frame pacing. New 4-state setting (Off/Automatic/Matched/Multiple). Migrate
    // from the old boolean: an enabled legacy toggle did automatic HW/SW pacing,
    // so it maps to FP_AUTO; a disabled one maps to FP_OFF.
    if (settings.contains(SER_FRAMEPACINGMODE)) {
        framePacingMode = static_cast<FramePacingMode>(settings.value(SER_FRAMEPACINGMODE,
                                                       static_cast<int>(FramePacingMode::FP_OFF)).toInt());
    }
    else {
        framePacingMode = settings.value(SER_FRAMEPACING, false).toBool()
                              ? FramePacingMode::FP_AUTO
                              : FramePacingMode::FP_OFF;
    }
    connectionWarnings = settings.value(SER_CONNWARNINGS, true).toBool();
    configurationWarnings = settings.value(SER_CONFWARNINGS, true).toBool();
    richPresence = settings.value(SER_RICHPRESENCE, true).toBool();
    gamepadMouse = settings.value(SER_GAMEPADMOUSE, true).toBool();
    detectNetworkBlocking = settings.value(SER_DETECTNETBLOCKING, true).toBool();
    autoReconnectNoVideo = settings.value(SER_AUTORECONNECTNOVIDEO, true).toBool();
    // On by default: once both sides are updated the pair just works, and the whole
    // handshake is skipped when the host is already at the right speed — which is the
    // common case, and costs nothing.
    matchHostLinkSpeed = settings.value(SER_MATCHHOSTLINKSPEED, true).toBool();
    // Off by default: without it StreamLight behaves as it always did, showing the stream as
    // soon as the session is up. Holding the launch screen until the host says the game is on
    // screen is the opt-in, because it is the answer to a problem not everyone has — and a
    // title that opens its own launcher never satisfies it at all.
    waitForGameOnScreen = settings.value(SER_WAITFORGAME, false).toBool();
    // Overlay verbosity. New 4-state setting (Off/Minimal/Default/Full). Migrate
    // from the old boolean: an enabled legacy overlay showed everything, so it
    // maps to OM_FULL; a disabled one maps to OM_OFF.
    if (settings.contains(SER_OVERLAYMODE)) {
        overlayMode = static_cast<OverlayMode>(settings.value(SER_OVERLAYMODE,
                                               static_cast<int>(OverlayMode::OM_OFF)).toInt());
    }
    else {
        overlayMode = settings.value(SER_SHOWPERFOVERLAY, false).toBool()
                          ? OverlayMode::OM_FULL
                          : OverlayMode::OM_OFF;
    }
    packetSize = settings.value(SER_PACKETSIZE, 0).toInt();
    swapMouseButtons = settings.value(SER_SWAPMOUSEBUTTONS, false).toBool();
    muteOnFocusLoss = settings.value(SER_MUTEONFOCUSLOSS, false).toBool();
    backgroundGamepad = settings.value(SER_BACKGROUNDGAMEPAD, false).toBool();
    reverseScrollDirection = settings.value(SER_REVERSESCROLL, false).toBool();
    swapFaceButtons = settings.value(SER_SWAPFACEBUTTONS, false).toBool();
    keepAwake = settings.value(SER_KEEPAWAKE, true).toBool();
    hueSyncIntegration = settings.value(SER_HUESYNC, false).toBool();
    hideHostIps = settings.value(SER_HIDEHOSTIPS, false).toBool();
    tailscaleAutoStart = settings.value(SER_TAILSCALE_AUTOSTART, false).toBool();
    enableHdr = settings.value(SER_HDR, false).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings.value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(CaptureSysKeysMode::CSK_OFF)).toInt());
    glyphSet = static_cast<GlyphSet>(settings.value(SER_GLYPHSET,
                                                    static_cast<int>(GlyphSet::GS_AUTO)).toInt());
    audioConfig = static_cast<AudioConfig>(settings.value(SER_AUDIOCFG,
                                                  static_cast<int>(AudioConfig::AC_STEREO)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(settings.value(SER_VIDEOCFG,
                                                  static_cast<int>(VideoCodecConfig::VCC_AUTO)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings.value(SER_VIDEODEC,
                                                  static_cast<int>(VideoDecoderSelection::VDS_AUTO)).toInt());
    windowMode = static_cast<WindowMode>(settings.value(SER_WINDOWMODE,
                                                        // Try to load from the old preference value too
                                                        static_cast<int>(settings.value(SER_FULLSCREEN, true).toBool() ?
                                                                             recommendedFullScreenMode : WindowMode::WM_WINDOWED)).toInt());
    // StreamLight 3.0 default: maximised — so the gamepad status bar is
    // visible at any screen resolution on first launch. Users can still
    // pick a different mode in Settings > Session > GUI mode.
    uiDisplayMode = static_cast<UIDisplayMode>(settings.value(SER_UIDISPLAYMODE,
                                               static_cast<int>(settings.value(SER_STARTWINDOWED, false).toBool() ? UIDisplayMode::UI_WINDOWED
                                                                                                                  : UIDisplayMode::UI_MAXIMIZED)).toInt());
    // Perform default settings updates as required based on last default version
    if (defaultVer < 1) {
#ifdef Q_OS_DARWIN
        // Update window mode setting on macOS from full-screen (old default) to borderless windowed (new default)
        if (windowMode == WindowMode::WM_FULLSCREEN) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
#endif
    }
    if (defaultVer < 2) {
        if (windowMode == WindowMode::WM_FULLSCREEN && WMUtils::isRunningWayland()) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
    }

    // Fixup VCC value to the new settings format with codec and HDR separate
    if (videoCodecConfig == VCC_FORCE_HEVC_HDR_DEPRECATED) {
        videoCodecConfig = VCC_AUTO;
        enableHdr = true;
    }
}

void StreamingPreferences::save()
{
    QSettings settings;

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_UNLOCK_BITRATE, unlockBitrate);
    settings.setValue(SER_AUTOADJUSTBITRATE, autoAdjustBitrate);
    settings.setValue(SER_VSYNC, enableVsync);
    settings.setValue(SER_GAMEOPTS, gameOptimizations);
    settings.setValue(SER_HOSTAUDIO, playAudioOnHost);
    settings.setValue(SER_MULTICONT, multiController);
    settings.setValue(SER_MDNS, enableMdns);
    settings.setValue(SER_QUITAPPAFTER, quitAppAfter);
    settings.setValue(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings.setValue(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings.setValue(SER_FRAMEPACINGMODE, static_cast<int>(framePacingMode));
    settings.setValue(SER_CONNWARNINGS, connectionWarnings);
    settings.setValue(SER_CONFWARNINGS, configurationWarnings);
    settings.setValue(SER_RICHPRESENCE, richPresence);
    settings.setValue(SER_GAMEPADMOUSE, gamepadMouse);
    settings.setValue(SER_PACKETSIZE, packetSize);
    settings.setValue(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings.setValue(SER_AUTORECONNECTNOVIDEO, autoReconnectNoVideo);
    settings.setValue(SER_MATCHHOSTLINKSPEED, matchHostLinkSpeed);
    settings.setValue(SER_WAITFORGAME, waitForGameOnScreen);
    settings.setValue(SER_OVERLAYMODE, static_cast<int>(overlayMode));
    settings.setValue(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings.setValue(SER_HDR, enableHdr);
    settings.setValue(SER_YUV444, enableYUV444);
    settings.setValue(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings.setValue(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    settings.setValue(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings.setValue(SER_UIDISPLAYMODE, static_cast<int>(uiDisplayMode));
    settings.setValue(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    settings.setValue(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings.setValue(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings.setValue(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings.setValue(SER_REVERSESCROLL, reverseScrollDirection);
    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings.setValue(SER_KEEPAWAKE, keepAwake);
    settings.setValue(SER_HUESYNC, hueSyncIntegration);
    settings.setValue(SER_HIDEHOSTIPS, hideHostIps);
    settings.setValue(SER_TAILSCALE_AUTOSTART, tailscaleAutoStart);
    settings.setValue(SER_GLYPHSET, static_cast<int>(glyphSet));
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps, bool yuv444)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (qSqrt(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        int factor;
    } resTable[] {
        { 640 * 360, 1 },
        { 854 * 480, 2 },
        { 1280 * 720, 5 },
        { 1920 * 1080, 10 },
        { 2560 * 1440, 20 },
        { 3840 * 2160, 40 },
        { -1, -1 },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    if (yuv444) {
        // This is rough estimation based on the fact that 4:4:4 doubles the amount of raw YUV data compared to 4:2:0
        resolutionFactor *= 2;
    }

    return qRound(resolutionFactor * frameRateFactor) * 1000;
}
