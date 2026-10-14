#include "steamartwork.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QtEndian>

#include <utility>

namespace {

// The four pictures, and the suffix Steam expects after the appid for each.
struct Slot {
    const char* resource;
    const char* suffix;
};
const Slot k_Slots[] = {
    { ":/steam/capsule.png", "p" },
    { ":/steam/header.png",  "" },
    { ":/steam/hero.png",    "_hero" },
    { ":/steam/logo.png",    "_logo" },
};
constexpr int k_SlotCount = int(sizeof(k_Slots) / sizeof(k_Slots[0]));

// SHA-256 (hex) of every official picture shipped BEFORE the ones in the build, so that a
// newer release replaces them as its own. ⚠️ When the pictures change, add the outgoing
// four here — otherwise the old ones read as the user's and are never updated.
const QList<QByteArray> k_OlderOfficial = {
};

// Steam stores a picture the user set by hand as .png or .jpg; either means "theirs".
const char* const k_OtherExtensions[] = { ".jpg", ".jpeg" };

enum SlotState { SlotMissing, SlotOurs, SlotOlder, SlotCustom };

QByteArray sha256(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QString steamDir()
{
    // Written by Steam itself for the user who installed or ran it, with forward slashes.
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString path = reg.value(QStringLiteral("SteamPath")).toString();
    return path.isEmpty() || !QDir(path).exists() ? QString() : QDir::cleanPath(path);
}

/*
 * shortcuts.vdf is Valve's BINARY KeyValues: a type byte, a NUL-terminated key, the value.
 * 0x00 opens a nested map, closed by 0x08; 0x01 is a NUL-terminated string; 0x02 a 32-bit
 * little-endian int. The file is one map, "shortcuts", holding one map per shortcut ("0",
 * "1"…) with appid, AppName, Exe, StartDir, icon… and a nested "tags" map. Key case varies
 * across Steam versions (Exe / exe), hence the case-insensitive compares.
 *
 * The other types never seen in this file are still skipped by size where it is known, so
 * a future one does not misalign everything after it; an unknown one stops the read.
 */
struct Shortcut {
    bool hasAppId = false;
    quint32 appId = 0;
    QString exe;
};

bool readCString(const QByteArray& d, int& pos, QByteArray& out)
{
    const int end = d.indexOf('\0', pos);
    if (end < 0) return false;
    out = d.mid(pos, end - pos);
    pos = end + 1;
    return true;
}

// depth 0 = the file, 1 = inside "shortcuts", 2 = inside one shortcut, deeper = its tags.
bool readMap(const QByteArray& d, int& pos, int depth, Shortcut* current, QList<Shortcut>& out)
{
    while (pos < d.size()) {
        const quint8 type = quint8(d.at(pos++));
        if (type == 0x08) return true;

        QByteArray key;
        if (!readCString(d, pos, key)) return false;

        switch (type) {
        case 0x00:
            if (depth == 1) {
                Shortcut s;
                if (!readMap(d, pos, depth + 1, &s, out)) return false;
                out.append(s);
            }
            else if (!readMap(d, pos, depth + 1, nullptr, out)) {
                return false;
            }
            break;
        case 0x01: {
            QByteArray value;
            if (!readCString(d, pos, value)) return false;
            if (current && key.compare("exe", Qt::CaseInsensitive) == 0) {
                current->exe = QString::fromUtf8(value);
            }
            break;
        }
        case 0x02: case 0x03: case 0x04: case 0x06:
            if (pos + 4 > d.size()) return false;
            if (current && type == 0x02 && key.compare("appid", Qt::CaseInsensitive) == 0) {
                current->appId = qFromLittleEndian<quint32>(d.constData() + pos);
                current->hasAppId = true;
            }
            pos += 4;
            break;
        case 0x07: case 0x0a:
            if (pos + 8 > d.size()) return false;
            pos += 8;
            break;
        default:
            return false;
        }
    }
    // The file's own map may end at EOF without its 0x08.
    return depth == 0;
}

SlotState slotState(const QString& base, const QByteArray& current)
{
    for (const char* ext : k_OtherExtensions) {
        if (QFileInfo::exists(base + QLatin1String(ext))) return SlotCustom;
    }
    QFile png(base + QStringLiteral(".png"));
    if (!png.exists()) return SlotMissing;
    if (!png.open(QIODevice::ReadOnly)) return SlotCustom;   // cannot tell: leave it alone
    const QByteArray hash = sha256(png.readAll());
    if (hash == current) return SlotOurs;
    if (k_OlderOfficial.contains(hash.toHex())) return SlotOlder;
    return SlotCustom;
}

QString slotBase(const QString& gridDir, quint32 appId, int slot)
{
    return gridDir + QLatin1Char('/') + QString::number(appId) + QLatin1String(k_Slots[slot].suffix);
}

} // namespace

SteamArtwork* SteamArtwork::get(QQmlEngine*)
{
    static SteamArtwork* instance = new SteamArtwork();
    return instance;
}

SteamArtwork::SteamArtwork(QObject* parent)
    : QObject(parent)
{
    for (const Slot& s : k_Slots) {
        QFile f(QLatin1String(s.resource));
        const QByteArray data = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        if (data.isEmpty()) qWarning() << "SteamArtwork: missing resource" << s.resource;
        m_Images.append(data);
        m_Hashes.append(sha256(data));
    }
}

QList<SteamArtwork::Target> SteamArtwork::findTargets(bool* steamFound) const
{
    QList<Target> targets;
    const QString steam = steamDir();
    const QDir userdata(steam + QStringLiteral("/userdata"));
    *steamFound = !steam.isEmpty() && userdata.exists();
    if (!*steamFound) return targets;

    // Ours by file name, not by full path: a shortcut made to the installed copy still
    // counts when a build elsewhere is running, and the other way round.
    const QString ownName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();

    const QStringList accounts = userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& account : accounts) {
        const QString configDir = userdata.filePath(account) + QStringLiteral("/config");
        QFile vdf(configDir + QStringLiteral("/shortcuts.vdf"));
        if (!vdf.open(QIODevice::ReadOnly)) continue;

        const QByteArray data = vdf.readAll();
        QList<Shortcut> shortcuts;
        int pos = 0;
        if (!readMap(data, pos, 0, nullptr, shortcuts)) {
            qWarning() << "SteamArtwork: could not read" << vdf.fileName();
            continue;
        }
        for (const Shortcut& s : std::as_const(shortcuts)) {
            QString exe = s.exe.trimmed();
            if (exe.startsWith(QLatin1Char('"'))) exe = exe.mid(1);
            if (exe.endsWith(QLatin1Char('"'))) exe.chop(1);
            if (QFileInfo(exe).fileName().compare(ownName, Qt::CaseInsensitive) != 0) continue;
            if (!s.hasAppId) {
                qInfo() << "SteamArtwork: shortcut to" << exe << "in account" << account
                        << "has no appid, skipped";
                continue;
            }
            targets.append({ configDir + QStringLiteral("/grid"), s.appId });
        }
    }
    return targets;
}

void SteamArtwork::refresh()
{
    bool steamFound = false;
    m_Targets = findTargets(&steamFound);
    m_Writable = 0;
    m_Custom = 0;

    for (const Target& t : std::as_const(m_Targets)) {
        for (int i = 0; i < k_SlotCount; ++i) {
            switch (slotState(slotBase(t.gridDir, t.appId, i), m_Hashes[i])) {
            case SlotMissing:
            case SlotOlder:  ++m_Writable; break;
            case SlotCustom: ++m_Custom;   break;
            case SlotOurs:                 break;
            }
        }
    }

    m_State = !steamFound          ? NoSteam
            : m_Targets.isEmpty()  ? NoShortcut
            : m_Writable > 0       ? Ready
            :                        Applied;
    qInfo() << "SteamArtwork:" << m_Targets.size() << "shortcut(s)," << m_Writable
            << "picture(s) to write," << m_Custom << "of the user's own";
    emit changed();
}

QString SteamArtwork::summary() const
{
    switch (m_State) {
    case NoSteam:    return tr("Steam is not installed");
    case NoShortcut: return tr("Add StreamLight to Steam as a non-Steam game first");
    case Ready:      return tr("Library and Big Picture images for the StreamLight shortcut");
    default:         return m_Custom > 0 ? tr("Applied · your own images are kept") : tr("Applied");
    }
}

QString SteamArtwork::apply()
{
    refresh();
    if (m_State == NoShortcut) return summary();
    if (m_State != Ready) return tr("Nothing to change");

    int written = 0;
    int failed = 0;
    for (const Target& t : std::as_const(m_Targets)) {
        QDir().mkpath(t.gridDir);
        for (int i = 0; i < k_SlotCount; ++i) {
            const QString base = slotBase(t.gridDir, t.appId, i);
            const SlotState st = slotState(base, m_Hashes[i]);
            if (st != SlotMissing && st != SlotOlder) continue;

            QSaveFile out(base + QStringLiteral(".png"));
            if (out.open(QIODevice::WriteOnly) && out.write(m_Images[i]) == m_Images[i].size() && out.commit()) {
                ++written;
            }
            else {
                ++failed;
                qWarning() << "SteamArtwork: could not write" << out.fileName();
            }
        }
    }
    refresh();

    if (failed > 0) return tr("Couldn't write to Steam's folder");
    return written > 0 ? tr("Done · restart Steam if the images don't show") : tr("Nothing to change");
}
