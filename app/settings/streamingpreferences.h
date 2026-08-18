#pragma once

#include <QColor>
#include <QObject>
#include <QRect>
#include <QQmlEngine>

class StreamingPreferences : public QObject
{
    Q_OBJECT

public:
    static StreamingPreferences* get(QQmlEngine *qmlEngine = nullptr);

    Q_INVOKABLE static int
    getDefaultBitrate(int width, int height, int fps, bool yuv444);

    Q_INVOKABLE void save();

    /*
     * The overlay's appearance, resolved from the enums above.
     *
     * These exist so the in-stream overlay and the preview in Settings read the SAME
     * numbers. The preview's whole job is to be believed, and a preview that keeps its
     * own copy of the palette is one edit away from lying about it.
     */
    Q_INVOKABLE QColor overlayTextColorValue() const;
    Q_INVOKABLE int    overlayFontPixelSize() const;
    /** Box background, transparency already applied. */
    Q_INVOKABLE QColor overlayBoxColorValue() const;

    void reload();

    // Creates a standalone copy of these preferences (not the QML singleton, not
    // persisted). Used to build per-game effective settings at launch without
    // mutating the global object. See AppSettingsManager.
    StreamingPreferences* clone(QObject* parent = nullptr) const;

    enum AudioConfig
    {
        AC_STEREO,
        AC_51_SURROUND,
        AC_71_SURROUND
    };
    Q_ENUM(AudioConfig)

    enum VideoCodecConfig
    {
        VCC_AUTO,
        VCC_FORCE_H264,
        VCC_FORCE_HEVC,
        VCC_FORCE_HEVC_HDR_DEPRECATED, // Kept for backwards compatibility
        VCC_FORCE_AV1
    };
    Q_ENUM(VideoCodecConfig)

    enum VideoDecoderSelection
    {
        VDS_AUTO,
        VDS_FORCE_HARDWARE,
        VDS_FORCE_SOFTWARE
    };
    Q_ENUM(VideoDecoderSelection)

    enum WindowMode
    {
        WM_FULLSCREEN,
        WM_FULLSCREEN_DESKTOP,
        WM_WINDOWED
    };
    Q_ENUM(WindowMode)

    enum UIDisplayMode
    {
        UI_WINDOWED,
        UI_MAXIMIZED,
        UI_FULLSCREEN
    };
    Q_ENUM(UIDisplayMode)

    enum CaptureSysKeysMode
    {
        CSK_OFF,
        CSK_FULLSCREEN,
        CSK_ALWAYS,
    };
    Q_ENUM(CaptureSysKeysMode);

    // ── Performance overlay ──────────────────────────────────────────────────
    // Replaces the Off/Minimal/Default/Full profiles of 4.x. The three profiles
    // were three fixed answers to "which of these figures do you want", and the
    // question only ever had one right answer per person — so the content is now
    // a per-item choice (overlayItems) and everything else here is presentation.
    // Asked for by @Soladus on issue #9: the block was too wide for a 1080p
    // handheld and covered the game.

    // Two corners, not three. Centre was dropped so that the side is always a real
    // choice between two halves of the screen — which is what lets the live Stream
    // Settings overlay take the OTHER corner automatically instead of overlapping
    // this one or needing a setting of its own.
    enum OverlayPosition
    {
        OVP_TOP_LEFT,
        OVP_TOP_RIGHT
    };
    Q_ENUM(OverlayPosition)

    // The five colours overlays of this kind have always used. Saturated on
    // purpose: they are read against moving video, not against a page.
    enum OverlayTextColor
    {
        OTC_WHITE,
        OTC_GREEN,
        OTC_YELLOW,
        OTC_CYAN,
        OTC_ORANGE
    };
    Q_ENUM(OverlayTextColor)

    enum OverlayFontSize
    {
        OFS_SMALL,      // 16pt
        OFS_MEDIUM,     // 20pt — what every build before this one used
        OFS_LARGE       // 25pt
    };
    Q_ENUM(OverlayFontSize)

    // Which lines the overlay draws, as a bitmask in `overlayItems`. A mask and
    // not a bool each because it is one setting — "what is on the overlay" — and
    // because adding the thirteenth line should not mean a thirteenth config key.
    enum OverlayItem
    {
        OI_VIDEO        = 1 << 0,   // resolution, frame rate, codec
        OI_BITRATE      = 1 << 1,
        OI_BITRATE_PEAK = 1 << 2,   // windowed peak alongside the average
        OI_FRAMERATES   = 1 << 3,   // incoming / decoding / rendering breakdown
        OI_HOST_LATENCY = 1 << 4,   // host capture+encode time
        OI_NET_DROPS    = 1 << 5,
        OI_JITTER_DROPS = 1 << 6,
        OI_LATENCY      = 1 << 7,   // round-trip time and its variance
        OI_DECODE_TIME  = 1 << 8,
        OI_QUEUE_DELAY  = 1 << 9,
        OI_RENDER_TIME  = 1 << 10,
        OI_PACING       = 1 << 11,
        OI_HOST_METRICS = 1 << 12,  // the StreamTweak block
        OI_ALL          = (1 << 13) - 1
    };
    Q_ENUM(OverlayItem)

    // Frame pacing mode. FP_OFF = no pacing; FP_AUTO = hardware when the display
    // refresh is an integer multiple of the stream FPS, software otherwise;
    // FP_MATCHED = always software (for a display running at the stream's FPS);
    // FP_MULTIPLE = hardware only, no software fallback (display at a multiple of
    // the stream's FPS). See FFmpegVideoDecoder and D3D11VARenderer.
    enum FramePacingMode
    {
        FP_OFF,
        FP_AUTO,
        FP_MATCHED,
        FP_MULTIPLE
    };
    Q_ENUM(FramePacingMode)

    // Whether and how to switch the display's refresh rate when the stream window
    // goes exclusive fullscreen. Only has an effect there — in borderless and
    // windowed the desktop mode is used as-is and nothing is switched.
    //
    // RR_HIGHEST is the inherited Moonlight behaviour and the default: pick the
    // highest refresh rate the stream's FPS divides into, so a 60 FPS stream on a
    // 120 Hz panel moves the panel to 120 Hz. That is deliberate — it's what the
    // 2:2 hardware cadence needs to exist (see D3D11VARenderer) — but it also puts
    // host and client on two unsynchronised clocks two refreshes apart, which is
    // where the drifting present latency of issue #9 comes from.
    //
    // RR_MATCH_FPS asks for a refresh rate equal to the stream's FPS instead, so
    // there is no cadence to keep. RR_OFF leaves the panel on whatever the user
    // set it to, which is what @Soladus was doing by hand.
    //
    // RR_AUTO pairs this with the frame pacing choice, which is narrower than it
    // sounds: FP_MATCHED is documented as software pacing "for a display running
    // at the stream's FPS", so it gets one, and everything else keeps the highest
    // multiple. It deliberately does not move the default configuration.
    enum RefreshRateMode
    {
        RR_OFF,
        RR_AUTO,
        RR_HIGHEST,
        RR_MATCH_FPS
    };
    Q_ENUM(RefreshRateMode)

    // Controller glyph set shown across the gamepad-first UI. GS_AUTO uses the
    // family detected from the connected pad (the historical behaviour); the
    // others force a specific vendor's button icons regardless of detection.
    enum GlyphSet
    {
        GS_AUTO,
        GS_XBOX,
        GS_PLAYSTATION,
        GS_NINTENDO
    };
    Q_ENUM(GlyphSet)

    // How the Home page reads the clock back. A setting rather than the system locale
    // because this app is English-only by design: following the locale would hand the
    // format to whichever English variant Windows is set to, and en-US would put the
    // month first for a user who never asked for that.
    enum ClockFormat
    {
        CF_24H,
        CF_12H
    };
    Q_ENUM(ClockFormat)

    enum DateFormat
    {
        DF_DMY,     // 11/08/2026
        DF_MDY,     // 08/11/2026
        DF_YMD      // 2026/08/11
    };
    Q_ENUM(DateFormat)

    Q_PROPERTY(int width MEMBER width NOTIFY displayModeChanged)
    Q_PROPERTY(int height MEMBER height NOTIFY displayModeChanged)
    Q_PROPERTY(int fps MEMBER fps NOTIFY displayModeChanged)
    Q_PROPERTY(int bitrateKbps MEMBER bitrateKbps NOTIFY bitrateChanged)
    Q_PROPERTY(bool unlockBitrate MEMBER unlockBitrate NOTIFY unlockBitrateChanged)
    Q_PROPERTY(bool autoAdjustBitrate MEMBER autoAdjustBitrate NOTIFY autoAdjustBitrateChanged)
    Q_PROPERTY(bool enableVsync MEMBER enableVsync NOTIFY enableVsyncChanged)
    Q_PROPERTY(bool gameOptimizations MEMBER gameOptimizations NOTIFY gameOptimizationsChanged)
    Q_PROPERTY(bool playAudioOnHost MEMBER playAudioOnHost NOTIFY playAudioOnHostChanged)
    Q_PROPERTY(bool multiController MEMBER multiController NOTIFY multiControllerChanged)
    Q_PROPERTY(bool enableMdns MEMBER enableMdns NOTIFY enableMdnsChanged)
    Q_PROPERTY(bool quitAppAfter MEMBER quitAppAfter NOTIFY quitAppAfterChanged)
    Q_PROPERTY(bool absoluteMouseMode MEMBER absoluteMouseMode NOTIFY absoluteMouseModeChanged)
    Q_PROPERTY(bool absoluteTouchMode MEMBER absoluteTouchMode NOTIFY absoluteTouchModeChanged)
    Q_PROPERTY(FramePacingMode framePacingMode MEMBER framePacingMode NOTIFY framePacingModeChanged)
    Q_PROPERTY(RefreshRateMode refreshRateMode MEMBER refreshRateMode NOTIFY refreshRateModeChanged)
    Q_PROPERTY(bool connectionWarnings MEMBER connectionWarnings NOTIFY connectionWarningsChanged)
    Q_PROPERTY(bool configurationWarnings MEMBER configurationWarnings NOTIFY configurationWarningsChanged)
    Q_PROPERTY(bool richPresence MEMBER richPresence NOTIFY richPresenceChanged)
    Q_PROPERTY(bool gamepadMouse MEMBER gamepadMouse NOTIFY gamepadMouseChanged)
    Q_PROPERTY(bool detectNetworkBlocking MEMBER detectNetworkBlocking NOTIFY detectNetworkBlockingChanged)
    Q_PROPERTY(bool autoReconnectNoVideo MEMBER autoReconnectNoVideo NOTIFY autoReconnectNoVideoChanged)
    Q_PROPERTY(bool matchHostLinkSpeed MEMBER matchHostLinkSpeed NOTIFY matchHostLinkSpeedChanged)
    Q_PROPERTY(bool waitForGameOnScreen MEMBER waitForGameOnScreen NOTIFY waitForGameOnScreenChanged)
    Q_PROPERTY(bool showPerfOverlay MEMBER showPerfOverlay NOTIFY overlayChanged)
    Q_PROPERTY(OverlayPosition overlayPosition MEMBER overlayPosition NOTIFY overlayChanged)
    Q_PROPERTY(OverlayTextColor overlayTextColor MEMBER overlayTextColor NOTIFY overlayChanged)
    Q_PROPERTY(OverlayFontSize overlayFontSize MEMBER overlayFontSize NOTIFY overlayChanged)
    Q_PROPERTY(int overlayTransparency MEMBER overlayTransparency NOTIFY overlayChanged)
    Q_PROPERTY(int overlayItems MEMBER overlayItems NOTIFY overlayChanged)
    Q_PROPERTY(AudioConfig audioConfig MEMBER audioConfig NOTIFY audioConfigChanged)
    Q_PROPERTY(VideoCodecConfig videoCodecConfig MEMBER videoCodecConfig NOTIFY videoCodecConfigChanged)
    Q_PROPERTY(bool enableHdr MEMBER enableHdr NOTIFY enableHdrChanged)
    Q_PROPERTY(bool enableYUV444 MEMBER enableYUV444 NOTIFY enableYUV444Changed)
    Q_PROPERTY(VideoDecoderSelection videoDecoderSelection MEMBER videoDecoderSelection NOTIFY videoDecoderSelectionChanged)
    Q_PROPERTY(WindowMode windowMode MEMBER windowMode NOTIFY windowModeChanged)
    Q_PROPERTY(WindowMode recommendedFullScreenMode MEMBER recommendedFullScreenMode CONSTANT)
    Q_PROPERTY(UIDisplayMode uiDisplayMode MEMBER uiDisplayMode NOTIFY uiDisplayModeChanged)
    Q_PROPERTY(bool swapMouseButtons MEMBER swapMouseButtons NOTIFY mouseButtonsChanged)
    Q_PROPERTY(bool muteOnFocusLoss MEMBER muteOnFocusLoss NOTIFY muteOnFocusLossChanged)
    Q_PROPERTY(bool backgroundGamepad MEMBER backgroundGamepad NOTIFY backgroundGamepadChanged)
    Q_PROPERTY(bool reverseScrollDirection MEMBER reverseScrollDirection NOTIFY reverseScrollDirectionChanged)
    Q_PROPERTY(bool swapFaceButtons MEMBER swapFaceButtons NOTIFY swapFaceButtonsChanged)
    Q_PROPERTY(bool keepAwake MEMBER keepAwake NOTIFY keepAwakeChanged)
    Q_PROPERTY(bool hueSyncIntegration MEMBER hueSyncIntegration NOTIFY hueSyncIntegrationChanged)
    Q_PROPERTY(bool hideHostIps MEMBER hideHostIps NOTIFY hideHostIpsChanged)
    Q_PROPERTY(bool tailscaleAutoStart MEMBER tailscaleAutoStart NOTIFY tailscaleAutoStartChanged)
    Q_PROPERTY(CaptureSysKeysMode captureSysKeysMode MEMBER captureSysKeysMode NOTIFY captureSysKeysModeChanged)
    Q_PROPERTY(GlyphSet glyphSet MEMBER glyphSet NOTIFY glyphSetChanged)
    Q_PROPERTY(ClockFormat clockFormat MEMBER clockFormat NOTIFY clockFormatChanged)
    Q_PROPERTY(DateFormat dateFormat MEMBER dateFormat NOTIFY dateFormatChanged)
    // Directly accessible members for preferences
    int width;
    int height;
    int fps;
    int bitrateKbps;
    bool unlockBitrate;
    bool autoAdjustBitrate;
    bool enableVsync;
    bool gameOptimizations;
    bool playAudioOnHost;
    bool multiController;
    bool enableMdns;
    bool quitAppAfter;
    bool absoluteMouseMode;
    bool absoluteTouchMode;
    FramePacingMode framePacingMode;
    RefreshRateMode refreshRateMode;
    bool connectionWarnings;
    bool configurationWarnings;
    bool richPresence;
    bool gamepadMouse;
    bool detectNetworkBlocking;
    bool autoReconnectNoVideo;
    // Ask the host to match its wired link speed to this device before connecting.
    bool matchHostLinkSpeed;
    // Hold the launch screen until the host reports the game is on screen, instead of showing
    // the stream as soon as there is a picture. Off by default — the opt-in is the wait, not
    // the other way round. Overridable per host profile and per game.
    bool waitForGameOnScreen;
    bool showPerfOverlay;
    OverlayPosition overlayPosition;
    OverlayTextColor overlayTextColor;
    OverlayFontSize overlayFontSize;
    // Percent of the box that lets the game through: 0 = solid, 60 = barely there.
    int overlayTransparency;
    int overlayItems;
    bool swapMouseButtons;
    bool muteOnFocusLoss;
    bool backgroundGamepad;
    bool reverseScrollDirection;
    bool swapFaceButtons;
    bool keepAwake;
    bool hueSyncIntegration;
    bool hideHostIps;
    bool tailscaleAutoStart;
    int packetSize;
    AudioConfig audioConfig;
    VideoCodecConfig videoCodecConfig;
    bool enableHdr;
    bool enableYUV444;
    VideoDecoderSelection videoDecoderSelection;
    WindowMode windowMode;
    WindowMode recommendedFullScreenMode;
    UIDisplayMode uiDisplayMode;
    CaptureSysKeysMode captureSysKeysMode;
    GlyphSet glyphSet;
    ClockFormat clockFormat;
    DateFormat dateFormat;

signals:
    void displayModeChanged();
    void bitrateChanged();
    void unlockBitrateChanged();
    void autoAdjustBitrateChanged();
    void enableVsyncChanged();
    void gameOptimizationsChanged();
    void playAudioOnHostChanged();
    void multiControllerChanged();
    void unsupportedFpsChanged();
    void enableMdnsChanged();
    void quitAppAfterChanged();
    void absoluteMouseModeChanged();
    void absoluteTouchModeChanged();
    void audioConfigChanged();
    void videoCodecConfigChanged();
    void enableHdrChanged();
    void enableYUV444Changed();
    void videoDecoderSelectionChanged();
    void uiDisplayModeChanged();
    void windowModeChanged();
    void framePacingModeChanged();
    void refreshRateModeChanged();
    void connectionWarningsChanged();
    void configurationWarningsChanged();
    void richPresenceChanged();
    void gamepadMouseChanged();
    void detectNetworkBlockingChanged();
    void autoReconnectNoVideoChanged();
    void matchHostLinkSpeedChanged();
    void waitForGameOnScreenChanged();
    // One signal for the whole overlay group: the settings page redraws its preview
    // from all six at once, so six signals would only mean six ways to forget one.
    void overlayChanged();
    void mouseButtonsChanged();
    void muteOnFocusLossChanged();
    void backgroundGamepadChanged();
    void reverseScrollDirectionChanged();
    void swapFaceButtonsChanged();
    void captureSysKeysModeChanged();
    void keepAwakeChanged();
    void hueSyncIntegrationChanged();
    void hideHostIpsChanged();
    void tailscaleAutoStartChanged();
    void glyphSetChanged();
    void clockFormatChanged();
    void dateFormatChanged();
private:
    explicit StreamingPreferences(QQmlEngine *qmlEngine);

    QQmlEngine* m_QmlEngine;
};

