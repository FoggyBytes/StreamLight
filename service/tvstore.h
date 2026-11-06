#pragma once

#include <QString>
#include <QStringList>

// The paired TV, kept by the service in %ProgramData%\FoggyBytes\StreamLight\service\tv.json.
// The key controls the TV and nothing else; the folder is the service's (ACL set by the setup).
struct TvRecord
{
    QString name;
    QString ip;         // last address it answered on; the MAC is what identifies it
    QString mac;
    QString key;        // the pairing's client-key
    QString cert;       // 8.5.0: SHA-256 of the TV's TLS certificate, kept at pairing ("" on ws://, or
                        // from 8.4.0 until the first connection records it) — WebOsTv::connect
    int input = 0;      // HDMI input this PC is on, 0 = not known yet
    QStringList inputs; // the TV's own names for HDMI 1, 2…, read at pairing ("PC", "LG Soundbar")

    // The rules (see PowerPolicy). Sleep, wake and the screen are not among them since 03/11/2026.
    bool onAtStart = true;          // the PC starts → TV on, on our input
    bool offAtShutdown = true;      // the PC shuts down → TV off (a restart never turns it off)
    bool offOnlyIfShowingPc = true; // never turn off a TV that is showing something else

    bool isPaired() const { return !mac.isEmpty() && !key.isEmpty(); }
};

namespace TvStore
{
QString folder();
// 8.5.0: where service.log goes — %ProgramData%\FoggyBytes\StreamLight\logs, NOT folder(). The
// setup closes folder() to everyone but the service (tv.json holds the TV's key) and leaves this
// one readable by users, so the app can copy the log next to its own (LogsAndCache::copyServiceLog).
QString logFolder();
TvRecord load();
bool save(const TvRecord& record, QString* error);
}
