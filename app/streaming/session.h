#pragma once

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

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;

public:
    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);
    virtual ~Session();

    Q_INVOKABLE bool initialize(QQuickWindow* qtWindow);
    Q_INVOKABLE void start();
    Q_INVOKABLE void interrupt();

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

    // Advance the performance overlay through Off -> Minimal -> Default -> Full
    // -> Off ... — bound to the overlay hotkey (Ctrl+Alt+O / Select+L1+R1+X).
    // This drives the same persisted setting shown in Settings > Overlay, so the
    // selector there stays in sync; the overlay layer is shown whenever the
    // resulting profile is not Off.
    void cycleOverlayMode()
    {
        auto mode = static_cast<StreamingPreferences::OverlayMode>(
            (static_cast<int>(m_Preferences->overlayMode) + 1)
            % (static_cast<int>(StreamingPreferences::OM_FULL) + 1));
        // Apply to the live session preferences (which may be a per-game clone).
        m_Preferences->overlayMode = mode;
        // Persist on the GLOBAL singleton — not the session clone — so Settings >
        // Overlay stays in sync and we never write per-game overrides to the
        // global store.
        auto* global = StreamingPreferences::get();
        global->overlayMode = mode;
        global->save();
        m_OverlayManager.setOverlayState(Overlay::OverlayDebug,
            m_Preferences->overlayMode != StreamingPreferences::OM_OFF);
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

signals:
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
    SessionTelemetrySampler* m_TelemetrySampler       = nullptr;
    HueSyncManager*          m_HueSyncManager         = nullptr;

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
