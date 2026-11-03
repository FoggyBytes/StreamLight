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
TvRecord load();
bool save(const TvRecord& record, QString* error);
}
