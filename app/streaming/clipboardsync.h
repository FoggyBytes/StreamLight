#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <string>
#include <vector>

#include "../StreamTweakBridge.h"

/**
 * The clipboard shared with StreamTweak while a stream runs (6.3.0, §79). Text only, up to
 * MaxTextBytes, encrypted with a key that lasts one stream. Windows only; elsewhere every
 * method does nothing.
 *
 * Who triggers what:
 * - client → host: the loop of the session calls poll() on every wake, and poll() reads the
 *   system clipboard's sequence number itself. ⚠️ Not SDL_CLIPBOARDUPDATE: on Windows SDL3
 *   only checks the clipboard when its window regains focus (§79.5b, measured).
 * - host → client: STATS carries the host's sequence number; when it moves,
 *   onHostClipboardSeq() asks CLIPGET. Also on every focus loss, so an Alt+Tab right after a
 *   copy in the stream finds the text already here. What the host had on its clipboard
 *   before the stream is never fetched — the number first heard counts as already seen.
 * - at the start: what was copied on this device since the previous stream (or since
 *   StreamLight started) is sent once the key arrives. Not unconditionally — every launch
 *   would otherwise overwrite the host's clipboard.
 *
 * Both sides mark what they write on the other's behalf with a private clipboard format, so
 * the change it causes is recognised as ours and never bounces back.
 *
 * Passwords (text carrying a password manager's marker) travel flagged as sensitive and are
 * written here with the same markers; they are cleared when the host's clipboard is emptied,
 * after 60 s, and at the end of the stream. A password copied on the host in the last second
 * of a stream is not fetched at all.
 *
 * ⚠️ Clipboard CONTENT is never logged, at any level — lengths and outcomes only.
 */
class ClipboardSync : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxTextBytes = 32 * 1024;

    /** Called once from main(): what StreamLight found on the clipboard when it started. */
    static void captureBaseline();

    ClipboardSync(const QString& hostAddress, QObject* parent);
    ~ClipboardSync() override;

    /** Asks for the session key (CLIPKEY). Main thread, once the stream is up. */
    void start();

    /** From the session's event loop, on every wake. Cheap; throttled inside. */
    void poll();

    void onFocusGained();
    void onFocusLost();

    /**
     * After the event loop has returned: last look at the host's clipboard, drop any password
     * we hold, CLIPEND. Synchronous, because the Qt event loop is not running there.
     */
    void finish();

public slots:
    /** Every STATS reply's "clip". The first one is only where the host stands. */
    void onHostClipboardSeq(qint64 hostSeq);

signals:
    /** Something the user should see once, in the stream's status line. */
    void notice(const QString& text);

private:
    void onKeyReply(const QString& reply);
    void sendLocal();
    void sendSealed(const std::string& utf8, bool sensitive);
    void onSetReply(const QString& reply);
    void fetchHost();
    void onGetReply(const QString& reply, bool allowSensitive);
    void clearHeldPassword(const char* why);
    void deactivate(bool forGood);
    void noticeTooLarge();

    QString m_Host;
    StreamTweakBridge m_Bridge;
    void* m_Owner = nullptr;                  // HWND of a message-only window: the clipboard owner
    std::string m_Uid;
    std::vector<unsigned char> m_Key;

    bool m_Starting = false;
    bool m_Active = false;
    bool m_Dead = false;                      // the host said no, or cannot: stop asking
    bool m_Finishing = false;
    quint64 m_RetryAtMs = 0;                  // after an unanswered CLIPKEY
    quint64 m_LastPollMs = 0;

    unsigned long m_LastLocalSeq = 0;         // this device's clipboard, last seen
    qint64 m_LastHostSeq = -1;                // the host's, last reported by STATS
    qint64 m_LastFetchedHostSeq = -1;         // the host's, last written here

    bool m_SendBusy = false, m_SendAgain = false;
    bool m_GetBusy = false, m_GetAgain = false;

    bool m_LastSentSensitive = false;         // the host holds a password we sent
    unsigned long m_HeldPasswordSeq = 0;      // our clipboard right after writing a host password
    QTimer m_PasswordTimer;

    unsigned long m_TooLargeLocalSeq = 0;     // notify once per content, not once per poll
    qint64 m_TooLargeHostSeq = -1;

    static unsigned long s_BaselineSeq;
};
