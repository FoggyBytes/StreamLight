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
 *   PREPARE  — sent from the PC context menu before the user launches the stream.
 *              StreamTweak sets the NIC to 1 Gbps pre-emptively so no disconnect
 *              occurs during the session.
 *   RESTORE  — sent after the session ends as an explicit fallback.
 *              StreamTweak's log monitor handles the normal case; this covers
 *              edge cases where the log event is delayed or missed.
 *   SHUTDOWN — asks the host to power off. Destructive; StreamTweak only honours
 *              it from an approved client (verified AUTH1 signature). Fire-and-forget.
 *   STATUS   — queries the current NIC speed from StreamTweak.
 *              StreamTweak replies with the link speed in Mbps (e.g. "1000").
 *   STATS    — requests real-time host metrics (GPU %, encoder %, temperature,
 *              VRAM used, CPU %, network TX). StreamTweak replies with a JSON
 *              object, e.g. {"gpu":45,"gpu_enc":80,"gpu_temp":72,"vram_used":4200,
 *              "cpu":30,"net_tx":18}, or "STATS_UNAVAILABLE".
 *
 * PREPARE/RESTORE are fire-and-forget. The query commands (STATUS, STATS,
 * APPSTORES, SESSIONID, TAILSCALE) take a per-call completion callback that
 * receives the trimmed response line, or an empty string on error/timeout.
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

    void sendPrepare(const QString& hostAddress);
    void sendRestore(const QString& hostAddress);
    void sendShutdown(const QString& hostAddress);

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
     * Asynchronously requests the store map for all managed apps from StreamTweak.
     * Invokes onResult with a JSON object mapping app names to store names,
     * e.g. {"Cyberpunk 2077":"Steam","Fortnite":"Epic Games"}, or "" on
     * error/timeout / StreamTweak unreachable.
     */
    void requestAppStores(const QString& hostAddress, ResponseCallback onResult);

    /**
     * Asynchronously requests the active session ID from StreamTweak.
     * Invokes onResult with the ID string, "NONE" if no session is active,
     * or "" on error/timeout.
     */
    void requestSessionId(const QString& hostAddress, ResponseCallback onResult);

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
