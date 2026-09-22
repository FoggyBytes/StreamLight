#include "clipboardsync.h"
#include "clipboardcrypto.h"
#include "backend/identitymanager.h"

#include <SDL.h>

#include <QStringList>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

unsigned long ClipboardSync::s_BaselineSeq = 0;

namespace
{
    // A password received from the host lives on this clipboard at most this long.
    constexpr int PasswordLifetimeMs = 60 * 1000;

    // The session loop can wake a thousand times a second on mouse input; the clipboard does
    // not need reading that often.
    constexpr quint64 PollIntervalMs = 150;

    // Timeout of each of the two blocking requests at the end of a stream.
    constexpr int FinishTimeoutMs = 1000;

#ifdef Q_OS_WIN
    // ⚠️ The same names StreamTweak registers (ClipboardShare.cs). The first is ours: what
    // either side writes on the other's behalf. The other four are the password managers'
    // markers, verified in their sources (§79.5) — any one of them makes a text sensitive.
    UINT fmtOwnTag()       { static const UINT f = RegisterClipboardFormatW(L"FoggyBytes.ClipboardSync"); return f; }
    UINT fmtExclude()      { static const UINT f = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"); return f; }
    UINT fmtHistory()      { static const UINT f = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory"); return f; }
    UINT fmtCloud()        { static const UINT f = RegisterClipboardFormatW(L"CanUploadToCloudClipboard"); return f; }
    UINT fmtViewerIgnore() { static const UINT f = RegisterClipboardFormatW(L"Clipboard Viewer Ignore"); return f; }

    enum class Kind { Text, Empty, Own, NoText, TooLarge, Busy };

    struct Snapshot
    {
        Kind kind;
        std::string utf8;
        bool sensitive = false;
    };

    bool openClipboard(HWND owner)
    {
        // Another app may hold it for a moment: ~100 ms, then give up until the next poll.
        for (int i = 0; i < 10; i++) {
            if (OpenClipboard(owner))
                return true;
            Sleep(10);
        }
        return false;
    }

    // -1 when the format is absent or unreadable.
    long long readDword(UINT format)
    {
        if (!IsClipboardFormatAvailable(format))
            return -1;
        HANDLE h = GetClipboardData(format);
        if (!h || GlobalSize(h) < sizeof(DWORD))
            return -1;
        const void* p = GlobalLock(h);
        if (!p)
            return -1;
        long long v = *static_cast<const DWORD*>(p);
        GlobalUnlock(h);
        return v;
    }

    Snapshot readLocal(HWND owner)
    {
        if (!openClipboard(owner))
            return { Kind::Busy };

        Snapshot s { Kind::NoText };
        if (CountClipboardFormats() == 0) {
            s.kind = Kind::Empty;
        }
        else if (IsClipboardFormatAvailable(fmtOwnTag())) {
            s.kind = Kind::Own;
        }
        else if (HANDLE h = IsClipboardFormatAvailable(CF_UNICODETEXT) ? GetClipboardData(CF_UNICODETEXT) : nullptr) {
            const size_t maxChars = GlobalSize(h) / sizeof(wchar_t);
            if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
                // Past MaxTextBytes characters the UTF-8 form is too large anyway: stop looking
                // there rather than converting a 50 MB copy to find out.
                const size_t limit = (std::min)(maxChars, static_cast<size_t>(ClipboardSync::MaxTextBytes) + 1);
                size_t len = 0;
                while (len < limit && w[len] != L'\0')
                    len++;
                if (len == limit && maxChars > limit) {
                    s.kind = Kind::TooLarge;
                }
                else if (len == 0) {
                    s.kind = Kind::Empty;
                }
                else {
                    s.utf8 = QString::fromWCharArray(w, static_cast<int>(len)).toStdString();
                    s.kind = Kind::Text;
                }
                GlobalUnlock(h);
            }
            if (s.kind == Kind::Text) {
                s.sensitive = IsClipboardFormatAvailable(fmtExclude())
                           || IsClipboardFormatAvailable(fmtViewerIgnore())
                           || readDword(fmtHistory()) == 0
                           || readDword(fmtCloud()) == 0;
            }
        }
        CloseClipboard();
        return s;
    }

    bool put(UINT format, const void* data, size_t size)
    {
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!g)
            return false;
        void* p = GlobalLock(g);
        if (!p) {
            GlobalFree(g);
            return false;
        }
        memcpy(p, data, size);
        GlobalUnlock(g);
        if (!SetClipboardData(format, g)) {
            GlobalFree(g);   // the system takes ownership only on success
            return false;
        }
        return true;
    }

    // Returns the clipboard sequence number after the write, or 0 when it failed.
    DWORD writeLocal(HWND owner, const QString& text, bool sensitive)
    {
        // Windows apps expect CRLF; whatever the other side sent, normalise it.
        QString crlf = text;
        crlf.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
        std::wstring w = crlf.toStdWString();

        if (!openClipboard(owner))
            return 0;
        bool ok = EmptyClipboard() && put(CF_UNICODETEXT, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        if (ok) {
            const DWORD zero = 0;
            put(fmtOwnTag(), &zero, sizeof(zero));
            if (sensitive) {
                // Out of Win+V history, off the cloud clipboard, ignored by clipboard monitors.
                put(fmtExclude(), &zero, sizeof(zero));
                put(fmtViewerIgnore(), &zero, sizeof(zero));
                put(fmtHistory(), &zero, sizeof(zero));
                put(fmtCloud(), &zero, sizeof(zero));
            }
        }
        CloseClipboard();
        SecureZeroMemory(&w[0], w.size() * sizeof(wchar_t));
        return ok ? GetClipboardSequenceNumber() : 0;
    }

    bool clearIfUnchanged(HWND owner, DWORD seq)
    {
        if (GetClipboardSequenceNumber() != seq || !openClipboard(owner))
            return false;
        // Checked again inside: someone may have copied between the two reads.
        bool cleared = GetClipboardSequenceNumber() == seq && EmptyClipboard();
        CloseClipboard();
        return cleared;
    }
#endif
}

void ClipboardSync::captureBaseline()
{
#ifdef Q_OS_WIN
    s_BaselineSeq = GetClipboardSequenceNumber();
#endif
}

ClipboardSync::ClipboardSync(const QString& hostAddress, QObject* parent)
    : QObject(parent),
      m_Host(hostAddress),
      m_Bridge(this),
      m_Uid(IdentityManager::get()->getUniqueId().toStdString())
{
#ifdef Q_OS_WIN
    // A message-only window, owned by the main thread, which pumps its messages — Windows SENDS
    // the owner WM_DESTROYCLIPBOARD when another app empties the clipboard.
    m_Owner = CreateWindowExW(0, L"STATIC", L"StreamLight clipboard", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, nullptr, nullptr);
#endif
    m_PasswordTimer.setSingleShot(true);
    m_PasswordTimer.setInterval(PasswordLifetimeMs);
    connect(&m_PasswordTimer, &QTimer::timeout, this, [this]() { clearHeldPassword("60 s elapsed"); });
}

ClipboardSync::~ClipboardSync()
{
    // finish() normally ran; this is the belt for a session torn down without it.
    clearHeldPassword("session destroyed");
    std::fill(m_Key.begin(), m_Key.end(), 0);
#ifdef Q_OS_WIN
    if (m_Owner)
        DestroyWindow(static_cast<HWND>(m_Owner));
#endif
}

void ClipboardSync::start()
{
#ifdef Q_OS_WIN
    if (m_Starting || m_Active || m_Dead || m_Finishing)
        return;
    m_Starting = true;
    m_Bridge.requestClipKey(m_Host, [this](const QString& reply) { onKeyReply(reply); });
#endif
}

void ClipboardSync::onKeyReply(const QString& reply)
{
#ifdef Q_OS_WIN
    m_Starting = false;
    if (reply.startsWith(QLatin1String("KEY "))) {
        QByteArray wrapped = QByteArray::fromBase64(reply.mid(4).toLatin1());
        std::vector<unsigned char> key;
        const std::string pem = IdentityManager::get()->getPrivateKey().toStdString();
        if (ClipboardCrypto::unwrapKey(pem, std::vector<unsigned char>(wrapped.begin(), wrapped.end()), key)) {
            m_Key.swap(key);
            m_Active = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: shared with the host for this stream");

            // What was copied here since the previous stream — or since StreamLight started —
            // goes over once. Anything older stays where it is.
            m_LastLocalSeq = GetClipboardSequenceNumber();
            if (m_LastLocalSeq != s_BaselineSeq)
                sendLocal();
            return;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: session key did not open");
        m_Dead = true;
    }
    else if (reply.isEmpty()) {
        // No answer at all: the host may just be slow. Try again in a while.
        m_RetryAtMs = SDL_GetTicks64() + 10000;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: no answer to CLIPKEY, retrying in 10 s");
    }
    else {
        // ERR_NOT_ALLOWED: off on the host. ERR: a StreamTweak older than 8.7.0.
        m_Dead = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: not shared by the host (%s)",
                    qPrintable(reply.left(24)));
    }
#else
    Q_UNUSED(reply);
#endif
}

void ClipboardSync::poll()
{
#ifdef Q_OS_WIN
    const quint64 now = SDL_GetTicks64();
    if (!m_Active) {
        if (!m_Starting && !m_Dead && !m_Finishing && m_RetryAtMs != 0 && now >= m_RetryAtMs) {
            m_RetryAtMs = 0;
            start();
        }
        return;
    }
    if (now - m_LastPollMs < PollIntervalMs)
        return;
    m_LastPollMs = now;

    const DWORD seq = GetClipboardSequenceNumber();
    if (seq != m_LastLocalSeq) {
        m_LastLocalSeq = seq;
        sendLocal();
    }
#endif
}

void ClipboardSync::onFocusGained()
{
    // Back in the stream: whatever was copied meanwhile must reach the host before a Ctrl+V.
    m_LastPollMs = 0;
    poll();
}

void ClipboardSync::onFocusLost()
{
    // Leaving the stream: fetch now rather than waiting up to a second for STATS.
    if (m_Active)
        fetchHost();
}

void ClipboardSync::sendLocal()
{
#ifdef Q_OS_WIN
    if (!m_Active)
        return;
    if (m_SendBusy) {
        m_SendAgain = true;
        return;
    }

    Snapshot s = readLocal(static_cast<HWND>(m_Owner));
    switch (s.kind) {
    case Kind::Busy:
        m_LastLocalSeq = 0;   // read it again on the next poll
        return;
    case Kind::Own:           // we wrote it for the host: do not send it back
    case Kind::NoText:
        return;
    case Kind::Empty:
        // A password manager empties the clipboard once its timeout expires. If the host holds
        // a password we sent, that is its cue to drop it too.
        if (m_LastSentSensitive) {
            m_LastSentSensitive = false;
            sendSealed(std::string(), false);
        }
        return;
    case Kind::TooLarge:
        break;
    case Kind::Text:
        if (s.utf8.size() <= static_cast<size_t>(MaxTextBytes)) {
            sendSealed(s.utf8, s.sensitive);
            m_LastSentSensitive = s.sensitive;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: %u bytes to the host%s",
                        static_cast<unsigned>(s.utf8.size()), s.sensitive ? " (sensitive)" : "");
            std::fill(s.utf8.begin(), s.utf8.end(), '\0');
            return;
        }
        break;
    }

    // Too large: once per content, not on every poll.
    if (m_TooLargeLocalSeq != m_LastLocalSeq) {
        m_TooLargeLocalSeq = m_LastLocalSeq;
        noticeTooLarge();
    }
#endif
}

void ClipboardSync::sendSealed(const std::string& utf8, bool sensitive)
{
    std::vector<unsigned char> sealedBytes;
    if (!ClipboardCrypto::seal(m_Key, utf8, sensitive, ClipboardCrypto::aad(true, m_Uid), sealedBytes))
        return;
    QString b64 = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(sealedBytes.data()),
                                                 static_cast<int>(sealedBytes.size())).toBase64());
    m_SendBusy = true;
    m_Bridge.sendClipSet(m_Host, b64, [this](const QString& reply) { onSetReply(reply); });
}

void ClipboardSync::onSetReply(const QString& reply)
{
    m_SendBusy = false;
    if (reply == QLatin1String("ERR_TOO_LARGE")) {
        noticeTooLarge();
    }
    else if (reply == QLatin1String("ERR_NO_KEY")) {
        // The host let the key go (it expires when a client goes quiet): ask for a new one.
        deactivate(false);
        start();
        return;
    }
    else if (reply == QLatin1String("ERR_NOT_ALLOWED")) {
        deactivate(true);
        return;
    }
    else if (reply != QLatin1String("OK")) {
        // ERR_LOCKED, ERR_BUSY, no answer: the next change will try again.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: host did not take it (%s)",
                    qPrintable(reply.left(24)));
    }
    if (m_SendAgain) {
        m_SendAgain = false;
        sendLocal();
    }
}

void ClipboardSync::onHostClipboardSeq(qint64 hostSeq)
{
    // Until the key arrives, and for the first number after, this is only where the host's
    // clipboard stands: fetching it would overwrite this device's clipboard at every launch.
    // Marking it as fetched keeps the focus-loss CLIPGET from bringing it over either.
    if (m_LastHostSeq < 0 || !m_Active) {
        m_LastHostSeq = hostSeq;
        m_LastFetchedHostSeq = hostSeq;
        return;
    }
    if (hostSeq == m_LastHostSeq)
        return;
    m_LastHostSeq = hostSeq;
    fetchHost();
}

void ClipboardSync::fetchHost()
{
    // No number from STATS yet: nothing tells a copy made in this stream from what the host
    // had before it, so wait for one rather than guess.
    if (!m_Active || m_LastHostSeq < 0)
        return;
    if (m_GetBusy) {
        m_GetAgain = true;
        return;
    }
    m_GetBusy = true;
    m_Bridge.requestClipGet(m_Host, [this](const QString& reply) {
        m_GetBusy = false;
        onGetReply(reply, true);
        if (m_GetAgain) {
            m_GetAgain = false;
            fetchHost();
        }
    });
}

void ClipboardSync::onGetReply(const QString& reply, bool allowSensitive)
{
#ifdef Q_OS_WIN
    if (reply.startsWith(QLatin1String("CLIP "))) {
        const QStringList parts = reply.split(QLatin1Char(' '));
        if (parts.size() != 3)
            return;
        const qint64 seq = parts[1].toLongLong();
        if (seq == m_LastFetchedHostSeq)
            return;   // already here: a focus loss asks again even when nothing changed

        QByteArray sealedB64 = QByteArray::fromBase64(parts[2].toLatin1());
        std::string utf8;
        bool sensitive = false;
        if (!ClipboardCrypto::open(m_Key, std::vector<unsigned char>(sealedB64.begin(), sealedB64.end()),
                                   ClipboardCrypto::aad(false, m_Uid), utf8, sensitive)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: host clipboard did not authenticate");
            return;
        }
        if (sensitive && !allowSensitive) {
            std::fill(utf8.begin(), utf8.end(), '\0');
            return;
        }

        const DWORD after = writeLocal(static_cast<HWND>(m_Owner), QString::fromStdString(utf8), sensitive);
        const size_t bytes = utf8.size();
        std::fill(utf8.begin(), utf8.end(), '\0');
        if (after == 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: could not write the host's clipboard here");
            return;
        }
        m_LastFetchedHostSeq = seq;
        m_LastLocalSeq = after;           // our own write: nothing to send back
        m_LastSentSensitive = false;      // whatever we had sent is no longer on this clipboard
        if (sensitive) {
            m_HeldPasswordSeq = after;
            m_PasswordTimer.start();
        }
        else {
            m_HeldPasswordSeq = 0;
            m_PasswordTimer.stop();
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: %u bytes from the host%s",
                    static_cast<unsigned>(bytes), sensitive ? " (sensitive)" : "");
    }
    else if (reply == QLatin1String("EMPTY")) {
        // Emptied on the host — by its password manager, typically: drop the copy we hold.
        clearHeldPassword("emptied on the host");
    }
    else if (reply == QLatin1String("ERR_TOO_LARGE")) {
        if (m_TooLargeHostSeq != m_LastHostSeq) {
            m_TooLargeHostSeq = m_LastHostSeq;
            noticeTooLarge();
        }
    }
    else if (reply == QLatin1String("ERR_NO_KEY") && !m_Finishing) {
        deactivate(false);
        start();
    }
    else if (reply == QLatin1String("ERR_NOT_ALLOWED")) {
        deactivate(true);
    }
    // OWN (ours), NOTEXT (an image, files…), ERR_LOCKED, ERR_BUSY, no answer: nothing to do.
#else
    Q_UNUSED(reply);
    Q_UNUSED(allowSensitive);
#endif
}

void ClipboardSync::clearHeldPassword(const char* why)
{
#ifdef Q_OS_WIN
    m_PasswordTimer.stop();
    if (m_HeldPasswordSeq == 0)
        return;
    const DWORD seq = m_HeldPasswordSeq;
    m_HeldPasswordSeq = 0;
    if (clearIfUnchanged(static_cast<HWND>(m_Owner), seq))
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: password cleared (%s)", why);
#else
    Q_UNUSED(why);
#endif
}

void ClipboardSync::deactivate(bool forGood)
{
    m_Active = false;
    m_Dead = m_Dead || forGood;
    std::fill(m_Key.begin(), m_Key.end(), 0);
    m_Key.clear();
    if (forGood)
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: turned off on the host");
}

void ClipboardSync::noticeTooLarge()
{
    emit notice(QStringLiteral("Clipboard too large to share"));
}

void ClipboardSync::finish()
{
#ifdef Q_OS_WIN
    m_Finishing = true;
    // Same rule as fetchHost(): without a number from STATS, what the host holds may predate
    // the stream.
    if (m_Active && m_LastHostSeq >= 0) {
        // What was copied on the host in the last moments of the stream — except a password,
        // which would outlive the stream here.
        onGetReply(m_Bridge.requestSync(m_Host, QStringLiteral("CLIPGET"), FinishTimeoutMs), false);
    }
    clearHeldPassword("stream ended");

    // The next stream sends only what is copied from here on.
    s_BaselineSeq = GetClipboardSequenceNumber();

    if (m_Active) {
        m_Bridge.requestSync(m_Host, QStringLiteral("CLIPEND"), FinishTimeoutMs);
        deactivate(false);
    }
#endif
}
