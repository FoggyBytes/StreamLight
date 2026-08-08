#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QString>
#include <QStringList>

#include <functional>

/**
 * StreamTweakBridge
 *
 * Sends one-shot TCP commands to StreamTweak on the host PC (port 47998).
 *
 * Commands:
 *   NETINFO / SETSPEED — host link-speed matching (StreamTweak 8.1.0+); see below.
 *              They replace PREPARE, which applied a target the host itself configured.
 *   SHUTDOWN — asks the host to power off. Destructive; StreamTweak only honours
 *              it from an approved client (verified AUTH1 signature). Fire-and-forget.
 *   SHUTDOWN_UPDATE — like SHUTDOWN, but the host installs any pending Windows
 *              updates before powering off ("Update and shut down"). Same approval
 *              requirement as SHUTDOWN. Fire-and-forget.
 *   UPDATESTATE — asks whether the host has updates waiting for a reboot.
 *              StreamTweak replies with {"pending":true|false}.
 *   STATUS   — queries the current NIC speed from StreamTweak.
 *              StreamTweak replies with the link speed in Mbps (e.g. "1000").
 *   STATS    — requests real-time host metrics (GPU %, encoder %, temperature,
 *              VRAM used, CPU %, network TX). StreamTweak replies with a JSON
 *              object, e.g. {"gpu":45,"gpu_enc":80,"gpu_temp":72,"vram_used":4200,
 *              "cpu":30,"net_tx":18}, or "STATS_UNAVAILABLE".
 *
 * The query commands (STATUS, STATS, NETINFO, APPSTORES, TAILSCALE, UPDATESTATE)
 * take a per-call completion callback that receives the trimmed response line, or an
 * empty string on error/timeout.
 *
 * Each query owns its own socket and its own callback, so concurrent requests
 * never cross-talk. Responses are buffered until the protocol's '\n' terminator
 * arrives (or the peer closes), so replies split across multiple TCP segments
 * (large APPSTORES payloads) are no longer truncated. A per-request watchdog
 * guarantees the callback fires exactly once even if the host connects but never
 * sends a newline-terminated reply.
 */
class StreamTweakBridge : public QObject
{
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(const QString&)>;

    explicit StreamTweakBridge(QObject* parent = nullptr);

    // installUpdates: send SHUTDOWN_UPDATE ("Update and shut down") instead of SHUTDOWN.
    void sendShutdown(const QString& hostAddress, bool installUpdates = false);

    /**
     * Asynchronously queries the NIC speed from StreamTweak.
     * Invokes onResult with the response (e.g. "1000"), or "" on error/timeout.
     */
    void requestStatus(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Asynchronously requests real-time host metrics from StreamTweak.
     * Invokes onResult with a JSON payload on success, or "" on error/timeout.
     */
    void requestStats(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Asks how far along the launch is on the host: whether the game's window has appeared,
     * whether something else is holding the screen, or whether there is nothing to wait for.
     * Invokes onResult with a JSON payload, or "" on error/timeout / a host that predates
     * the command — which the caller must read as "no curtain", never as "keep waiting".
     */
    void requestGameState(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Asynchronously requests the store map for all managed apps from StreamTweak.
     * Invokes onResult with a JSON object mapping app names to store names,
     * e.g. {"Cyberpunk 2077":"Steam","Fortnite":"Epic Games"}, or "" on
     * error/timeout / StreamTweak unreachable.
     */
    void requestAppStores(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Asynchronously asks whether the host has Windows updates waiting for a reboot.
     * Invokes onResult with {"pending":true|false}, or "" on error/timeout / legacy
     * host (pre-7.2.1 hosts reply "ERR"). Informational only — used to hint the user
     * in the Power dialog.
     */
    void requestUpdateState(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Remote PIN unlock (StreamTweak 8.1.0+).
     *
     *  - requestLockState: is a lock or logon screen up on the host? Replies
     *    {"v":1,"locked":true|false}, or "" / "ERR" on a host that predates the verb —
     *    which the caller must read as "no unlock flow available", never as "not locked".
     *
     *  - sendUnlockBegin / sendUnlockEnd: tell the host that the session we are about to
     *    open is plumbing for a PIN unlock, so it stays out of the session history and
     *    skips the side effects of a real session (spatial audio, managed apps, game
     *    capture). The host cannot tell such a session apart by looking at it — only we
     *    know why we opened it. Fire-and-forget.
     */
    void requestLockState(const QString& hostAddress, ResponseCallback onResult);
    void sendUnlockBegin(const QString& hostAddress);
    void sendUnlockEnd(const QString& hostAddress);

    /**
     * Remote "Update host" feature (drives Windows Update Agent on the host).
     *  - sendUpdateCheck: start an async scan. Fire-and-forget ("OK" discarded).
     *  - sendUpdateNow:   install the scanned updates for the given scope ("SEC"/"ALL")
     *                     and reboot if required. Destructive; authenticated.
     *  - requestUpdateProgress: poll the job state; onResult gets the JSON snapshot,
     *                     or "" on error/timeout (host unreachable / rebooting).
     */
    void sendUpdateCheck(const QString& hostAddress);
    void sendUpdateNow(const QString& hostAddress, const QString& scope);
    void requestUpdateProgress(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Host link-speed matching (StreamTweak 8.1.0+).
     *
     *  - requestNetInfo: describes the host's managed wired adapter — current speed,
     *    the speeds it supports (in Mbps, each with the driver's display string as an
     *    opaque key), whether client control is permitted, and whether a session is
     *    already running. Replies "" / "ERR" on hosts older than 8.1.0, which is how
     *    the client detects that the feature is unavailable.
     *
     *  - sendSetSpeed: asks the host to run at `mbps` before we connect. The reply is
     *    an acknowledgement, not a completion: the change takes seconds (the adapter
     *    renegotiates), so poll requestNetInfo until `state` is "idle" at the requested
     *    speed. Replies OK / ERR_NOT_ALLOWED / ERR_UNSUPPORTED / ERR_BUSY /
     *    ERR_NOT_LAN / ERR_NO_ADAPTER.
     *
     * Never block a launch on either of these: a tuning feature that prevents
     * streaming is worse than no tuning feature.
     */
    void requestNetInfo(const QString& hostAddress, ResponseCallback onResult);
    void sendRestore(const QString& hostAddress, ResponseCallback onResult);
    void sendSetSpeed(const QString& hostAddress, quint64 mbps, ResponseCallback onResult);

    /**
     * The host's most recent finished session (StreamTweak 8.1.0+), as JSON:
     * {"v":1,"has":true,"ago":"3d ago","duration":"1m 1s","grade":"Excellent",
     *  "grade_color":"#4ade80","rtt_ms":1,"rtt_peak_ms":3,"host_latency_ms":1.3,
     *  "drops_pct":0.6,"games":[{"name":…,"cover":"<base64 png>"}]}
     *
     * Note this is the HOST's last session and not necessarily one of ours — StreamTweak
     * logs whatever streamed and does not record which client it belonged to.
     *
     * Replies "" / "ERR" on older hosts, which is how the client detects the feature is
     * unavailable and simply draws nothing. The reply carries thumbnail images and is by
     * far the largest one the bridge produces; sendRawRequest accumulates until the '\n'
     * terminator, so it is already segment-safe.
     */
    void requestLastSession(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Asynchronously asks the host whether Tailscale is installed and active.
     * Invokes onResult with the host's Tailscale IPv4 address (e.g.
     * "100.64.1.2"), "NOT_DETECTED" if Tailscale is not present, or "" on
     * error/timeout / StreamTweak unreachable.
     */
    void requestTailscale(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Queries the host's bridge capabilities (unauthenticated). Invokes onResult
     * with e.g. "CAPS1 auth=required" / "CAPS1 auth=optional", or "" on legacy
     * hosts (which don't understand CAPS) and on unreachable hosts.
     */
    void requestCaps(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Enrolls this client's Moonlight identity certificate with StreamTweak so the
     * host user can approve it (trust-on-first-use). Invokes onResult with
     * "ENROLLED" / "PENDING" / "DENIED", or "" on error / unreachable / legacy host.
     * Sent unauthenticated — it is the bootstrap step that establishes trust.
     */
    void enroll(const QString& hostAddress, const QString& pin, ResponseCallback onResult);

    /**
     * Sends a SESSIONDATA batch to StreamTweak. The payload must be a compact
     * JSON string (no embedded newlines). Fire-and-forget; response is discarded.
     */
    void sendSessionData(const QString& hostAddress, const QString& jsonPayload);

    /**
     * Synchronous variant of sendSessionData. Blocks until the data is written
     * or the timeout expires. Use only for the final flush at session end, where
     * the Qt event loop is not running and async sockets would never fire.
     */
    void sendSessionDataSync(const QString& hostAddress, const QString& jsonPayload);

    static constexpr quint16 BridgePort = 47998;

    // Per-request watchdog: how long to wait for a newline-terminated reply
    // before giving up and invoking the callback with an empty string. Generous
    // for a loopback/LAN request that normally completes in a few milliseconds.
    static constexpr int ResponseTimeoutMs = 3000;

private:
    void sendCommand(const QString& hostAddress, const QString& command);

    // Sends an authenticated command (prepends the AUTH1 signature line) and
    // delivers the host's reply to onResult exactly once (also on error/timeout).
    void sendRequest(const QString& hostAddress,
                     const QString& command,
                     ResponseCallback onResult);

    // General primitive: opens a dedicated socket, writes each entry in `lines`
    // followed by '\n', buffers the reply until '\n' (or the peer closes), and
    // invokes onResult exactly once. Used for authenticated commands and for the
    // unauthenticated ENROLL bootstrap alike.
    void sendRawRequest(const QString& hostAddress,
                        const QStringList& lines,
                        ResponseCallback onResult);

    // Builds the "AUTH1 <uniqueId> <unixMillis> <base64 sig>" line authenticating
    // `command`. Signs "<uniqueId>\n<unixMillis>\n<command>" with the Moonlight
    // identity private key (RSA-SHA256, PKCS#1 v1.5). Empty string on failure.
    static QString    buildAuthLine(const QString& command);
    static QByteArray signPayload(const QByteArray& payload);
};
