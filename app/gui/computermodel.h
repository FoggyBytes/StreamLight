#include "backend/computermanager.h"
#include "streaming/session.h"
#include "../StreamTweakBridge.h"
#include "backend/boxartmanager.h"

#include <QAbstractListModel>
#include <QTimer>
#include <QHash>
#include <QVariantMap>

class ComputerModel : public QAbstractListModel
{
    Q_OBJECT

    /*
     * The range of a host card's opacity, in percent (6.0.0). Here, once, because three
     * places need it and must agree: the setter clamps to it, data() applies the default to
     * hosts that never set one, and the slider in StageBackgroundDialog is drawn from it.
     *
     * ⚠️ The floor is 70 — Marcello's call after trying it (18/09/2026); it was 60 at first.
     * The reason for having a floor at all is the text, not the card. The card never
     * disappears at any value — it keeps its border and its contents — but what the name and
     * the fields are read against is the card's own SCRIM, and the opacity thins that too:
     * the lower it goes, the more the moving waves behind cut across the host's name.
     */
    Q_PROPERTY(int stageOpacityMin READ stageOpacityMin CONSTANT)
    Q_PROPERTY(int stageOpacityDefault READ stageOpacityDefault CONSTANT)

    /*
     * Moves every time the rows change places (6.3.1). The list is sorted by name, so a host
     * renamed, discovered, removed or cloned for Tailscale shifts every row after it — and a
     * row number held across that names a different host. Anything that has to outlive a
     * single call (a wake, an update, a dialog left open) keeps the host's uuid instead, and
     * reads its row back through indexOfUuid() in a binding that also reads this, so the
     * binding re-runs exactly when the answer can have changed.
     */
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY layoutRevisionChanged)

    enum Roles
    {
        NameRole = Qt::UserRole,
        OnlineRole,
        PairedRole,
        BusyRole,
        WakeableRole,
        StatusUnknownRole,
        ServerSupportedRole,
        DetailsRole,
        AddressRole,
        GpuModelRole,
        IsTailscaleCloneRole,
        PhysicalAddressRole,
        TailscaleAddressRole,
        HasTailscaleRole,
        TailscaleActiveRole,
        ProfileCountRole,
        ActiveProfileSlotRole,
        ActiveProfileNameRole,
        StageImageRole,
        StageOpacityRole,
        StreamTweakEnabledRole,
        AsleepRole,         // put to sleep by this client, not polled until Wake (6.2.0)
        PyroWaveTestRole,   // 0 no PyroWave · 1 PyroWave, host too old for the test · 2 testable (7.0.0)
        ServerRestartRole   // the streaming server's name while a Library Sync and restart restarts it, else "" (8.0.0)
    };

public:
    explicit ComputerModel(QObject* object = nullptr);

    static constexpr int StageOpacityMin     = 70;
    // Where every host starts — an existing one on its first 6.0.0 launch and a new one
    // alike, because both arrive with stageOpacity 0 (Marcello, 18/09/2026: 88, then 85, then 90).
    static constexpr int StageOpacityDefault = 90;
    int stageOpacityMin() const     { return StageOpacityMin; }
    int stageOpacityDefault() const { return StageOpacityDefault; }
    int layoutRevision() const      { return m_LayoutRevision; }

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager);

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

    /** The host at this row, as something that stays true when the rows move. "" if none. */
    Q_INVOKABLE QString uuidAt(int computerIndex) const;
    /** The row this host occupies now, or -1 once it is gone. See layoutRevision. */
    Q_INVOKABLE int indexOfUuid(const QString& uuid) const;

    Q_INVOKABLE void deleteComputer(int computerIndex);

    Q_INVOKABLE QString generatePinString();

    Q_INVOKABLE void pairComputer(int computerIndex, QString pin);

    Q_INVOKABLE void testConnectionForComputer(int computerIndex);

    Q_INVOKABLE void wakeComputer(int computerIndex);

    Q_INVOKABLE void renameComputer(int computerIndex, QString name);

    Q_INVOKABLE Session* createSessionForCurrentGame(int computerIndex);


    // Asks the host (via StreamTweak) to power off. Requires the host to have
    // approved this client; fire-and-forget over the authenticated bridge.
    // installUpdates: install pending Windows updates before powering off.
    Q_INVOKABLE void shutdownHost(int computerIndex, bool installUpdates = false);

    /**
     * Host power modes (StreamTweak 8.6.0+). requestPowerCaps answers with
     * powerCapsReceived(index, supported, modes, wakeLan): `supported` is false for a host
     * that predates POWERCAPS, whose only mode is the old SHUTDOWN — the dialog shows it as
     * Shut down and the caller uses shutdownHost(). powerHost sends POWER and reports
     * powerHostResult(index, mode, ok) once the host has answered.
     */
    Q_INVOKABLE void requestPowerCaps(int computerIndex);
    Q_INVOKABLE void powerHost(int computerIndex, const QString& mode, bool installUpdates);

    Q_INVOKABLE void requestStreamTweakStatus(int computerIndex);

    /**
     * Asks the host whether it has Windows updates waiting for a reboot.
     * Emits updateStateReceived(computerIndex, pending) when the response arrives
     * (pending=false on legacy/unreachable hosts). Drives the Power dialog hint.
     */
    Q_INVOKABLE void requestUpdateState(int computerIndex);

    /**
     * Remote PIN unlock.
     *
     * requestLockState asks whether a lock or logon screen is up; the answer arrives as
     * lockStateReceived(index, supported, locked). `supported` is separate on purpose: a
     * host that predates the command answers nothing, and reading that as "not locked"
     * would send us straight past the PIN pad into a session nobody can drive.
     *
     * markUnlockSession declares the session we are about to open as unlock plumbing, so
     * the host keeps it out of its history and skips its session-start side effects.
     */
    Q_INVOKABLE void requestLockState(int computerIndex);

    /*
     * 7.0.2: after a Quit session, wait for the host to close the game, then ask StreamTweak
     * (GAMEALIVE, 9.2.3+) whether the game it launched is still running. The streaming server
     * can say the app has ended while the game lives on — Vibeshine cannot terminate an Xbox
     * app game it started through explorer.exe, and its serverinfo then shows nothing — so this
     * is the only question that sees it. Emits gameStillRunningAfterQuit only on a clear yes;
     * an older host, the integration off, or "known":false say nothing. By uuid, never by row:
     * the answer comes seconds later (§80.2).
     * 8.2.0: a yes is asked again until 32 s after the Quit, and only a yes at the end is said —
     * a game can take longer than 8 s to go (§98.5).
     */
    Q_INVOKABLE void checkGameAfterQuit(const QString& hostUuid, const QString& appName);
    Q_INVOKABLE void markUnlockSession(int computerIndex, bool begin);

    /**
     * Runs the link-speed match on its own, with no session behind it. Used right after a
     * remote unlock: the speed cannot be matched before the host has logged in — the adapter
     * would go down for a session lasting as long as typing a PIN — so it is done here, and
     * the host card only calls itself ready once this finishes.
     *
     * Emits linkMatchProgress(index, true, detail) while it runs and (index, false, "") when
     * it ends, whatever the outcome: like every other path through LinkMatcher, failure is
     * reported and then ignored.
     */
    Q_INVOKABLE void matchHostLinkSpeed(int computerIndex);

    /**
     * Remote "Update host" feature. startUpdateCheck kicks off an async scan;
     * startUpdateInstall installs the scanned set for a scope ("SEC"/"ALL") and reboots
     * if required; requestUpdateProgress polls the job state and emits
     * updateProgressReceived(computerIndex, state) where state is the parsed JSON
     * (phase/percent/message/updates/counts), or {"phase":"IDLE"} when unreachable.
     */
    Q_INVOKABLE void startUpdateCheck(int computerIndex);
    Q_INVOKABLE void startUpdateInstall(int computerIndex, const QString& scope);
    Q_INVOKABLE void requestUpdateProgress(int computerIndex);

    /**
     * Enrolls this client with the host's StreamTweak and reports the access state.
     * Emits streamTweakAuthReceived(computerIndex, state) where state is one of
     * "authorized" / "pending" / "denied" / "none" ("none" = StreamTweak absent,
     * legacy, or unreachable). Triggers the approval prompt on the host on first
     * contact. Used to drive the per-host access badge and the "Request access"
     * option.
     */
    Q_INVOKABLE void requestStreamTweakAuth(int computerIndex);

    /**
     * Requests the store map for all StreamTweak-managed apps from the host.
     * Emits appStoresReceived(computerIndex, storesMap) when the response arrives.
     * If StreamTweak is unreachable the map will be empty.
     * Returns the cached map immediately if already fetched for this host.
     */
    Q_INVOKABLE void requestAppStores(int computerIndex);

    /**
     * Returns the last successfully fetched store map for this computer,
     * or an empty QVariantMap if no fetch has succeeded yet.
     */
    Q_INVOKABLE QVariantMap getCachedAppStores(int computerIndex) const;

    /**
     * Probes the host's StreamTweak for its Tailscale (100.x) endpoint and, if found,
     * records it on the host (unified tile). Safe to call repeatedly; no-op if the host
     * is unreachable or has no Tailscale.
     */
    Q_INVOKABLE void refreshTailscale(int computerIndex);

    /** Sets this host's stage picture (7.0.0: a picture only); an empty path clears it. */
    Q_INVOKABLE void setHostStageBackground(int computerIndex, const QString& imagePath);


    /**
     * How opaque this host's card is on Home, in percent (6.0.0). Clamped to
     * [StageOpacityMin, 100] — see the note on the property for why the floor is where it is.
     * Stored on the host with the rest of its backdrop.
     */
    Q_INVOKABLE void setHostStageOpacity(int computerIndex, int percent);

    /**
     * Wake-on-LAN (8.1.0, issue #45): { reported, extra } as "AA:BB:CC:DD:EE:FF" strings, empty
     * when unknown / not set. "reported" is the MAC /serverinfo gave — the card the host
     * streams on — and "extra" one the user added for another card of the same PC.
     */
    Q_INVOKABLE QVariantMap hostWakeMacs(int computerIndex) const;
    /**
     * Sets the extra MAC from what the user typed: twelve hex digits, with or without ':' '-'
     * or '.' between them; empty clears it. False (and nothing stored) when it is not a MAC,
     * or is all zeros or a multicast address, which no card has.
     */
    Q_INVOKABLE bool setHostWakeMac(int computerIndex, const QString& text);

    /**
     * The last three pictures chosen as a host background, newest first, for any host
     * (7.0.0) — the Background panel offers them so a pad can pick a picture without the
     * file dialog. Files that no longer exist are dropped on read.
     */
    Q_INVOKABLE QStringList recentStagePictures() const;
    /** Puts @p imagePath at the head of recentStagePictures() (max 3). */
    Q_INVOKABLE void rememberStagePicture(const QString& imagePath);

    /**
     * The StreamTweak integration, per host. Everything that talks to the bridge is gated
     * on this: the probes, link matching, remote power and Windows Update, the PIN unlock,
     * the last-session panel, store badges, host metrics, the launch curtain, telemetry.
     * Streaming is never gated either way.
     *
     * setStreamTweakEnabled persists immediately and emits dataChanged, so every binding
     * reading the role follows in the same frame — turning it off has to take the features
     * away now, not on the next visit to the screen.
     */
    Q_INVOKABLE bool streamTweakEnabled(int computerIndex) const;

    // NvComputer::heldAsleep: this client put the host to sleep and has not woken it since.
    bool heldAsleep(int computerIndex) const;
    // The gate on every bridge call: the integration is on AND the host is not held asleep.
    bool bridgeAllowed(int computerIndex) const;
    Q_INVOKABLE void setStreamTweakEnabled(int computerIndex, bool enabled);

    /**
     * One CAPS query, so the Settings tab can tell each host apart: is StreamTweak actually
     * answering on this machine? Emits streamTweakPresenceReceived(index, found).
     *
     * ⚠️ Deliberately NOT gated on streamTweakEnabled, and deliberately called from nowhere
     * else: it is the one question the tab exists to answer, and a host that has the
     * integration switched off is exactly the host the user needs an answer about. Every
     * other bridge call in this class refuses when the switch is off.
     */
    Q_INVOKABLE void probeStreamTweakPresence(int computerIndex);

    // Wired link speed of the interface that actually routes to this host — the value the
    // host is asked to match. Returns {status, mbps, adapter, reason, usable}; `usable` is
    // false whenever the path is Wi-Fi, a tunnel, or the adapter reports no rate, and
    // `reason` is written for the UI so the feature is never inert without saying why.
    Q_INVOKABLE QVariantMap probeLocalLink(int computerIndex);

    // One-shot NETINFO so the host card can say whether a switch is coming. Cached by the
    // caller: the permission rarely changes, and the current speed is already kept live by
    // the 2 s STATUS poll. Emits hostNetInfoReceived; silently does nothing on hosts older
    // than StreamTweak 8.1.0.
    Q_INVOKABLE void requestHostNetInfo(int computerIndex);

    /// Asks the host to put its link speed back. The host never decides this for itself — it
    /// holds the streaming speed until told — so this is the only thing that ends a switch,
    /// sent when the user answers the prompt on returning to the host list, or picks the
    /// Options tile.
    Q_INVOKABLE void restoreHostLink(int computerIndex);

    /**
     * StreamLight 8.0's host section (StreamTweak 10.0.0) — HostSectionScreen.qml.
     *
     * requestHostSection asks one read: "DASHBOARD", "APPS", "NVIDIA", "STATS" for the
     * Dashboard's numbers, or "UPDATESTATE" for updates waiting for a restart — or one of the
     * Sessions tab's, in their fixed shapes: "SESSIONS <offset> <count> ALL|MINE",
     * "SESSION <id>", "SERIES <id> <track>", "COVER <id> <n>". The signal carries the command
     * as sent. Emits hostSectionReceived with the parsed JSON, an EMPTY map when
     * the host did not answer or predates the verb.
     *
     * setHostSetting sends one write. Only the three shapes the host accepts get through —
     * "SETAUDIO ON|OFF", "SETAPP <16 hex digits> ON|OFF", "SETSENTINEL ON|OFF" — so nothing
     * from QML can put a path or a free command on the wire. Emits hostSettingResult with the
     * host's reply ("" on timeout, "ERR" when refused here).
     */
    Q_INVOKABLE void requestHostSection(int computerIndex, const QString& verb);
    Q_INVOKABLE void setHostSetting(int computerIndex, const QString& command);

    /** True if Tailscale is installed on THIS (client) PC (so the host's Tailscale
     *  endpoint is actually usable from here). Drives the greyed "Tailscale" option. */
    Q_INVOKABLE bool clientHasTailscale() const;

    /**
     * Pins the host's active connection to its Tailscale endpoint for the next session
     * (used by the host's "Tailscale" option). Returns false if no Tailscale endpoint is
     * known. Call clearTailscalePreferences() when returning to the host list.
     */
    Q_INVOKABLE bool prepareTailscaleSession(int computerIndex);

    /** Clears any Tailscale session pin on all hosts (poller reverts to LAN-first). */
    Q_INVOKABLE void clearTailscalePreferences();

    // ── Per-host streaming profiles (StreamLight 4.0.0) ──────────────────────
    Q_INVOKABLE int hostProfileCount(int computerIndex) const;
    Q_INVOKABLE int hostActiveProfile(int computerIndex) const;       // slot, or -1
    Q_INVOKABLE QString hostActiveProfileName(int computerIndex) const;
    Q_INVOKABLE void setHostActiveProfile(int computerIndex, int slot);
    Q_INVOKABLE void cycleHostProfile(int computerIndex, int dir);    // -1 prev / +1 next
    Q_INVOKABLE QString hostProfileName(int computerIndex, int slot) const;
    Q_INVOKABLE void setHostProfileName(int computerIndex, int slot, const QString& name);
    Q_INVOKABLE QVariantMap hostProfileSettings(int computerIndex, int slot) const;
    // What a profile row set to "Global" will actually run at. A profile sits directly on
    // top of the global settings, so unlike the per-game dialog there is no middle level
    // to fold in. Formatted for display; see inheritedValueLabels() in settings/appsettings.h.
    Q_INVOKABLE QVariantMap globalLabels() const;
    Q_INVOKABLE void setHostProfileSettings(int computerIndex, int slot, const QVariantMap& ov);
    Q_INVOKABLE int addHostProfile(int computerIndex);                // new slot, or -1
    Q_INVOKABLE void removeHostProfile(int computerIndex, int slot);

    // The active profile's override as a QVariantMap (keys: width/height/fps/
    // bitrate/hdr/codec/framepacing/audio/hue). Empty when no profile is active.
    Q_INVOKABLE QVariantMap hostActiveOverride(int computerIndex) const;

    // ── Last played (5.7.0) ──────────────────────────────────────────────────────────────
    /**
     * What the host card's Last played block is drawn from: the game this client last
     * streamed on this host, its artwork, and how that session went.
     *
     * Everything comes off local disk — the play-time record and the box art cache — so it
     * answers with a host that has no StreamTweak, and with no host reachable at all.
     *
     * Returns an EMPTY map when there is nothing to show, and the card then draws nothing:
     * no game ever streamed here, the record was reset, or the game is no longer in the
     * host's app list. That last one is the launchability gate, and it costs nothing extra —
     * resolving the artwork already needs the app id, and only the app list has it.
     */
    Q_INVOKABLE QVariantMap lastPlayedFor(int computerIndex) const;

    // ── Now streaming (6.0.0) ────────────────────────────────────────────────────────────
    /**
     * What the host card shows INSTEAD of Last played while the host has a session up: the
     * running entry's name and artwork, found through `currentGameId` in the app list — the
     * same source the host page's STREAMING tag reads, so the two screens cannot disagree
     * about what is running.
     *
     * Returns an EMPTY map when the host is not online, has nothing running, or runs an id
     * its app list does not carry. The card then falls back to Last played, whose button
     * already opens the host page while the host is busy — never a Resume that could not
     * find what to resume.
     */
    Q_INVOKABLE QVariantMap runningAppFor(int computerIndex) const;
    // The Library tab's cover for the host's app of that name (8.0.0): this client's own box
    // art, the host page's picture, fetched from the server when it is not on disk yet ("" until
    // then; libraryCoverReady says when to ask again). "" for a name the server does not list.
    Q_INVOKABLE QString libraryCover(int computerIndex, const QString& name);

    /// "2 h ago", "yesterday", "3 days ago" — the wording the host used to send with its own
    /// last-session reply, kept identical now that the client works it out for itself.
    static QString formatAgo(const QDateTime& utcStamp);

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);
    /**
     * The magic packet is away (or could not be sent at all — no MAC on record, every
     * send refused). Says nothing about the host: it has not had time to hear it yet.
     */
    void wakeCompleted(int computerIndex, bool sent);
    void streamTweakStatusReceived(int computerIndex, QString status);
    void streamTweakAuthReceived(int computerIndex, QString state, QString pin);

    // Answer to probeStreamTweakPresence(). `found` is false for a host that is offline,
    // unreachable, or simply not running StreamTweak — the tab says which from what it
    // already knows about the host, so this stays a single bit.
    // `clip` is the host's clipboard sharing from the same CAPS reply (6.3.0, §79): "on",
    // "off", or "" for a StreamTweak older than 8.7.0 (and whenever `found` is false).
    void streamTweakPresenceReceived(int computerIndex, bool found, QString clip);

    /** @param info {allowsLinkControl, currentMbps} — empty map on hosts without NETINFO. */
    void hostNetInfoReceived(int computerIndex, QVariantMap info);

    void appStoresReceived(int computerIndex, QVariantMap stores);
    void updateStateReceived(int computerIndex, bool pending);
    void powerCapsReceived(int computerIndex, bool supported, QStringList modes, bool wakeLan);
    void powerHostResult(int computerIndex, QString mode, bool ok);
    /** supported=false means the host does not know LOCKSTATE — not that it is unlocked. */
    void lockStateReceived(int computerIndex, bool supported, bool locked);
    void gameStillRunningAfterQuit(QString hostUuid, QString gameName);
    /** detail is the change being made ("2.5 Gbps → 1 Gbps"), empty once finished. */
    void linkMatchProgress(int computerIndex, bool running, QString detail);
    void updateProgressReceived(int computerIndex, QVariantMap state);

    // StreamLight 8.0's host section.
    void hostSectionReceived(int computerIndex, QString verb, QVariantMap data);
    void hostSettingResult(int computerIndex, QString command, QString reply);
    void libraryCoverReady(int computerIndex);
    // From the CAPS reply that starts every authorisation check: whether the host has the
    // host-section verbs ("host=1", StreamTweak 10.0.0) and its version ("ver=", "" before it).
    void streamTweakCapsReceived(int computerIndex, bool hostSection, QString version);

    void layoutRevisionChanged();

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
    BoxArtManager m_BoxArtManager;      // the Library tab's covers (libraryCover)
    static void rememberStreamTweakSeen(const QString& uuid);
    void bumpLayoutRevision();
    // One GAMEALIVE of checkGameAfterQuit(); asks again while the game is still closing (8.2.0).
    void askGameAliveAfterQuit(const QString& hostUuid, const QString& appName, int attempt);

    // A Library Sync and restart in progress on a host (8.0.0), followed from here and not
    // from the section, so Home keeps saying "Restarting" after the section is left: for the
    // minute the server is down, Moonlight's polls see the host offline. Keyed by uuid.
    struct LibraryJob { QString server; QString phase; qint64 sinceMs = 0; };
    QHash<QString, LibraryJob> m_LibraryJobs;
    QTimer m_LibraryJobTimer;
    void noteLibraryState(const QString& uuid, const QVariantMap& state);
    void pollLibraryJobs();

    QVector<NvComputer*> m_Computers;
    int m_LayoutRevision = 0;
    // The theme the stage pictures were last served for: each theme has its own (13/10/2026),
    // so a theme change repaints every card.
    bool m_StageLight = false;
    ComputerManager* m_ComputerManager;
    StreamTweakBridge m_streamTweakBridge;
    // Keyed by host UUID, not list index: the index shifts whenever the model is
    // reset (host added/removed, Tailscale clone inserted), which would otherwise
    // associate a cached store map with the wrong host.
    QHash<QString, QVariantMap> m_appStoresCache;
    // Per-host (UUID) 4-digit confirmation PIN, generated on first enrollment and
    // reused across re-polls while the host approval is pending; cleared once the
    // host approves/denies so a fresh attempt gets a new PIN.
    QHash<QString, QString> m_streamTweakPins;
};
