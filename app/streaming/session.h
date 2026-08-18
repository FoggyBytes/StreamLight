#pragma once

#include <QAtomicInt>
#include <QSemaphore>
#include <QQuickWindow>

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"
#include "../HostMetricsPoller.h"
#include "../backend/linkmatcher.h"
#include "../backend/launchgate.h"
#include "launchcurtain.h"
#include "../SessionTelemetrySampler.h"
#include "../HueSyncManager.h"

class SupportedVideoFormatList : public QList<int>
{
public:
    operator int() const
    {
        int value = 0;

        for (const int v : *this) {
            value |= v;
        }

        return value;
    }

    void
    removeByMask(int mask)
    {
        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                this->removeAt(i);
            }
            else {
                i++;
            }
        }
    }

    void
    deprioritizeByMask(int mask)
    {
        QList<int> deprioritizedList;

        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                deprioritizedList.append(this->takeAt(i));
            }
            else {
                i++;
            }
        }

        this->append(std::move(deprioritizedList));
    }

    int maskByServerCodecModes(int serverCodecModes)
    {
        int mask = 0;

        const QMap<int, int> mapping = {
            {SCM_H264, VIDEO_FORMAT_H264},
            {SCM_H264_HIGH8_444, VIDEO_FORMAT_H264_HIGH8_444},
            {SCM_HEVC, VIDEO_FORMAT_H265},
            {SCM_HEVC_MAIN10, VIDEO_FORMAT_H265_MAIN10},
            {SCM_HEVC_REXT8_444, VIDEO_FORMAT_H265_REXT8_444},
            {SCM_HEVC_REXT10_444, VIDEO_FORMAT_H265_REXT10_444},
            {SCM_AV1_MAIN8, VIDEO_FORMAT_AV1_MAIN8},
            {SCM_AV1_MAIN10, VIDEO_FORMAT_AV1_MAIN10},
            {SCM_AV1_HIGH8_444, VIDEO_FORMAT_AV1_HIGH8_444},
            {SCM_AV1_HIGH10_444, VIDEO_FORMAT_AV1_HIGH10_444},
        };

        for (QMap<int, int>::const_iterator it = mapping.cbegin(); it != mapping.cend(); ++it) {
            if (serverCodecModes & it.key()) {
                mask |= it.value();
                serverCodecModes &= ~it.key();
            }
        }

        // Make sure nobody forgets to update this for new SCM values
        SDL_assert(serverCodecModes == 0);

        int val = *this;
        return val & mask;
    }
};

class Session : public QObject
{
    Q_OBJECT

    /**
     * Everything the launch curtain shows. QML binds to it for the whole launch: the stream
     * window stays hidden until the game is on screen, so there is only ever one curtain.
     */
    Q_PROPERTY(LaunchCurtain* curtain READ curtain CONSTANT)

    /**
     * Is this launch actually holding the stream back until the game is on screen?
     *
     * The EFFECTIVE value — the cascade lands in this session's own preferences, so a per-game
     * or per-profile override is already in it; reading the singleton would read the bottom of
     * the cascade. The launch screen needs it to know whether "press B to see the host now"
     * means anything: with the wait off the window is revealed on the first frame regardless,
     * and the prompt was advertising an escape from something nobody was being held by.
     */
    Q_PROPERTY(bool waitsForGame READ waitsForGame CONSTANT)

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;

public:
    LaunchCurtain* curtain() { return &m_Curtain; }
    bool waitsForGame() const { return !m_UnlockMode && m_Preferences->waitForGameOnScreen; }

    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);
    virtual ~Session();

    Q_INVOKABLE bool initialize(QQuickWindow* qtWindow);
    Q_INVOKABLE void start();
    Q_INVOKABLE void interrupt();

    // Give up on a launch that has not produced a picture yet — X behind the launch screen.
    // Same teardown as interrupt(), plus a mark so the segue knows the end that follows was
    // asked for and shows no error over it. Safe at any point: LiInterruptConnection() aborts
    // a handshake in progress, and a session that never started simply never runs.
    Q_INVOKABLE void cancelLaunch();
    Q_INVOKABLE bool launchWasCancelled() const { return m_LaunchCancelled; }

    // Request a clean shutdown of an established session. Unlike interrupt(),
    // this does NOT call LiInterruptConnection() — it only pushes SDL_QUIT and
    // lets the SDL event loop perform an orderly LiStopConnection() with a
    // protocol-level BYE to the host. Use this for stop requests that arrive
    // after the stream is fully running (e.g. StreamTweak "Return to client").
    void requestGracefulStop();
    Q_PROPERTY(QStringList launchWarnings MEMBER m_LaunchWarnings NOTIFY launchWarningsChanged);

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    // Host and app of this session — used by the live Stream Settings overlay to
    // resolve which profile layer (per-game / host profile / global) to save to.
    NvComputer* getComputer() const { return m_Computer; }
    const NvApp& getApp() const { return m_App; }

    // Show or hide the performance overlay — bound to the overlay hotkey
    // (Ctrl+Alt+Shift+S / Select+L1+R1+X).
    //
    // It used to cycle Off -> Minimal -> Default -> Full, because verbosity was the only
    // thing there was to choose. Since the content is picked line by line in Settings,
    // three levels would be three answers to a question the user has already answered, so
    // the key does the one thing left: shows it or hides it.
    void togglePerfOverlay()
    {
        const bool show = !m_Preferences->showPerfOverlay;
        // Apply to the live session preferences (which may be a per-game clone).
        m_Preferences->showPerfOverlay = show;
        // Persist on the GLOBAL singleton — not the session clone — so Settings >
        // Overlay stays in sync and we never write per-game overrides to the
        // global store.
        auto* global = StreamingPreferences::get();
        global->showPerfOverlay = show;
        global->save();
        m_OverlayManager.setOverlayState(Overlay::OverlayDebug, show);
    }

    /** Thread-safe snapshot of the latest host metrics from StreamTweak. */
    HostMetrics getHostMetrics() const
    {
        if (m_HostMetricsPoller)
            return m_HostMetricsPoller->latestMetrics();
        return HostMetrics{};
    }

    /** Accessors used by SessionTelemetrySampler for thread-safe decoder stat reads. */
    SDL_mutex*    decoderLock()  const { return m_DecoderLock;  }
    IVideoDecoder* videoDecoder() const { return m_VideoDecoder; }

    void flushWindowEvents();

    void setShouldExit(bool quitHostApp = false);

    // True if the last connection terminated because no video traffic ever
    // arrived from the host (ML_ERROR_NO_VIDEO_TRAFFIC). This is usually a
    // transient host warm-up race (virtual display / HDR / AV1 encoder not yet
    // producing frames on a cold game-session start), so the UI auto-retries once.
    Q_INVOKABLE bool wasNoVideoTraffic() const { return m_NoVideoTraffic; }

    // Build a fresh Session for the same host+app, used to auto-resume after a
    // transient no-video failure. The new session resumes since the game session
    // already exists on the host.
    Q_INVOKABLE Session* createRetrySession();

    // --- Live "Stream Settings" reconfigure (4.4.0) ---
    // Request applying new stream parameters mid-session. Since the bitrate /
    // resolution / fps / HDR are negotiated only in the start SDP, this is done
    // host-agnostically by ending the current session and resuming with new
    // preferences (a brief reconnect "blip"). The params are stored and the
    // current connection is interrupted; StreamSegue then resumes via
    // createReconfiguredSession(). Frame pacing alone is client-side and must NOT
    // come through here (it's applied live without a reconnect).
    Q_INVOKABLE void requestReconfigure(int width, int height, int fps,
                                        int bitrateKbps, bool enableHdr, int framePacingMode);
    Q_INVOKABLE bool hasPendingReconfigure() const { return m_HasPendingReconfigure; }
    Q_INVOKABLE Session* createReconfiguredSession();

    /**
     * Asks the host to match its wired link speed to this device, before we connect.
     * Lives on Session because every launch path — app grid, next app, CLI, quit-and-
     * relaunch, and the 4.4.0 resume flows — builds a Session and hands it to StreamSegue,
     * so hooking it here means no path can silently skip it.
     *
     * Always emits linkMatchFinished exactly once, including on every failure: the launch
     * must never be blocked by a tuning feature. Call it before initialize()/start().
     */
    Q_INVOKABLE void beginLinkMatch();

    /**
     * Brings the stream window on screen. It is created hidden and stays that way for the
     * whole launch, so that the curtain covering the wait is the QML one and only that one —
     * there is no handover between two renderings of the same screen, and therefore nothing
     * to keep aligned across resolutions, DPI settings and scaling factors.
     *
     * <p>Safe to call from any thread and before the window exists: it only records the
     * request and hands it to the SDL loop, which does the work at the top of an iteration
     * rather than re-entrantly from inside a message dispatch.</p>
     */
    /// @param onDemand the user asked for it (B / Esc), so show the window whether or not any
    ///        picture has arrived. The gate's own reveal waits for the first frame instead —
    ///        see revealWindowNow() — but "show me now" has to mean now, including when the
    ///        answer is a black screen, because that is exactly when it gets pressed.
    Q_INVOKABLE void revealStreamWindow(bool onDemand = false);

    /// Called by whichever decoder path is live when the first frame of a session arrives.
    /// ⚠️ There are two, and both must call it: a pull renderer (the FFmpeg decoder, i.e. the
    /// normal path on Windows) never goes through Session::drSubmitDecodeUnit at all.
    static void notifyFirstFrame();

    // --- Remote PIN unlock ---
    //
    // This session exists only to type a PIN into the host's logon screen, and its window
    // must never appear: the user is looking at the QML pad, and behind it is a lock screen
    // they explicitly said they never want to see. Setting this before initialize() also
    // skips the link match (renegotiating the adapter before the host has even logged in
    // would black out the link for a session lasting as long as a PIN), the launch gate
    // (which exists to reveal the window, the one thing we must not do) and telemetry
    // (nothing here is worth recording).
    Q_INVOKABLE void setUnlockMode(bool on) { m_UnlockMode = on; }
    Q_INVOKABLE bool isUnlockMode() const   { return m_UnlockMode; }

    /**
     * A left click, to dismiss the lock-screen shade. Deliberately not a keystroke: if the
     * PIN field happens to be showing already, a key would land in it as a stray digit and
     * burn an attempt, whereas a click is inert either way.
     */
    Q_INVOKABLE void unlockClick();

    /**
     * The PIN is buffered here and not sent digit by digit, so a mistake can be taken back
     * with unlockBackspace() instead of becoming a failed attempt. It lives in a plain byte
     * buffer that is wiped after use — never in a QString or a QML property, which we could
     * neither pin down nor overwrite. QML is told the count and nothing else.
     */
    Q_INVOKABLE void unlockDigit(int digit);
    Q_INVOKABLE void unlockBackspace();
    Q_INVOKABLE void unlockClearPin();
    Q_INVOKABLE int  unlockPinLength() const { return m_UnlockPinLen; }

    /** Sends the buffered digits, then wipes the buffer. Windows Hello submits on its own
     *  once the PIN reaches its configured length, so there is no Enter to press. */
    Q_INVOKABLE void unlockSubmitPin();

signals:
    /**
     * The stream window is now on screen, so whoever is still showing the curtain should
     * stop. Emitted on the SDL/main thread, after the window is up — never before, or the
     * desktop would show through in between.
     */
    void streamWindowRevealed();

    /** Progress line for the launch screen while the host's link is being switched. */
    void linkMatchStage(QString text);

    /** @param warning non-empty when the attempt failed; show it and carry on regardless. */
    void linkMatchFinished(bool changed, QString warning);

    /**
     * The host's view of the launch, from the moment the stream starts until the game is on
     * screen. This is the part of the wait the client cannot see for itself: everything on
     * this side is already done, and what the stream carries meanwhile is the host's desktop
     * reconfiguring itself.
     */
    void launchPhaseChanged(int phase, QString foreground, qint64 elapsedMs);

    /** The curtain must come down. Always emitted once per session that raised it. */
    void launchGateFinished(int finalPhase);

    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void displayLaunchError(QString text);

    void quitStarting();

    void sessionFinished(int portTestResult);

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();

    void launchWarningsChanged();

private:
    void exec();

    bool startConnectionAsync();

    bool validateLaunch(SDL_Window* testWindow);

    void refreshHostCapabilities();

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool initializeAudioRenderer();

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen();

    /** Does the actual reveal. SDL loop only — never call it from a Qt callback. */
    void revealWindowNow();

    void notifyMouseEmulationMode(bool enabled);

    void updateOptimalWindowDisplayMode();

    enum class DecoderAvailability {
        None,
        Software,
        Hardware
    };

    static
    DecoderAvailability getDecoderAvailability(SDL_Window* window,
                                               StreamingPreferences::VideoDecoderSelection vds,
                                               int videoFormat, int width, int height, int frameRate);

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool testOnly,
                       IVideoDecoder*& chosenDecoder,
                       int framePacingMode = 0);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

    static
    void clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger);

    static
    void clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

    static
    void clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

    static
    void clSetAdaptiveTriggers(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, uint8_t *left, uint8_t *right);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    SupportedVideoFormatList m_SupportedVideoFormats; // Sorted in order of descending priority
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    SDL_Window* m_Window;

    // The window is created hidden and revealed when the launch is over. Both flags belong
    // to the SDL loop and are only touched there; m_RevealRequested is the one crossing over
    // from the Qt side, so it is the only one that has to be atomic.
    QAtomicInt m_RevealRequested { 0 };
    // Set by the decode thread on the first unit in, read by the SDL loop.
    QAtomicInt m_FirstFrameSeen { 0 };
    // The user pressed B/Esc, so the wait for a first frame no longer applies.
    bool m_RevealOnDemand = false;
    bool m_WindowRevealed = false;
    bool m_CaptureOnReveal = false;

    IVideoDecoder* m_VideoDecoder;
    SDL_mutex* m_DecoderLock;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    QQuickWindow* m_QtWindow;
    bool m_UnexpectedTermination;
    bool m_NoVideoTraffic = false;
    // Pending live-reconfigure request (4.4.0). Applied via a resume reconnect.
    bool m_HasPendingReconfigure = false;
    int m_RcWidth = 0, m_RcHeight = 0, m_RcFps = 0, m_RcBitrateKbps = 0, m_RcFramePacing = 0;
    bool m_RcEnableHdr = false;
    SdlInputHandler* m_InputHandler;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;
    QStringList m_LaunchWarnings;
    bool m_ShouldExit;

    bool m_AsyncConnectionSuccess;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig;
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;
    HostMetricsPoller*       m_HostMetricsPoller      = nullptr;
    LinkMatcher*             m_LinkMatcher            = nullptr;
    LaunchGate*              m_LaunchGate             = nullptr;
    LaunchCurtain            m_Curtain;
    SessionTelemetrySampler* m_TelemetrySampler       = nullptr;
    HueSyncManager*          m_HueSyncManager         = nullptr;

    // Remote PIN unlock. The buffer is fixed and small: a Windows Hello PIN is a handful of
    // digits, and a fixed array is something we can actually overwrite afterwards.
    bool m_UnlockMode = false;

    // The user pressed X behind the launch screen. Read by the segue when the session ends, so
    // a cancellation is not dressed up as a failure.
    bool m_LaunchCancelled = false;
    static constexpr int MaxUnlockPinDigits = 16;
    char m_UnlockPin[MaxUnlockPinDigits] = {};
    int  m_UnlockPinLen = 0;
    void wipeUnlockPin();

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
