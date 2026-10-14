#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class QQmlEngine;

/**
 * Settings → Interface → "Steam artwork" (7.0.0, issue #38).
 *
 * StreamLight added to Steam as a non-Steam game shows up in the library and in Big Picture
 * as a grey tile with its name. Steam looks for a shortcut's pictures in
 * `<Steam>\userdata\<account>\config\grid\`, by file name: `<appid>p.png` (capsule, 600×900),
 * `<appid>.png` (header, 920×430), `<appid>_hero.png` (3840×1240) and `<appid>_logo.png`.
 * This puts ours there. The same four files are installed next to the exe, in
 * "Steam artwork", for anyone who prefers to assign them by hand.
 *
 * ⚠️ Three things it never does:
 *   · write shortcuts.vdf. It is READ, to find our shortcut and its appid; Steam keeps that
 *     file in memory and rewrites it on exit, so a change made while it runs would be lost —
 *     which is also why the shortcut's icon is left alone (it lives in that file, and Steam
 *     already takes it from the exe);
 *   · compute the appid. Steam has stored it in the file since 2020; a shortcut without one
 *     is skipped rather than guessed;
 *   · replace a picture the user chose. A file is written only where there is none, or where
 *     the one there is byte-for-byte an official StreamLight picture (current or older —
 *     k_OlderOfficial). Anything else, including a .jpg, is theirs.
 *
 * Every Steam account on the PC whose shortcuts point at a StreamLight.exe gets them. Read
 * when asked (refresh), not watched: QML refreshes when the Interface tab opens.
 */
class SteamArtwork : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    enum State {
        NoSteam,     // Steam is not installed for this Windows user
        NoShortcut,  // Steam is, but no shortcut points at StreamLight.exe
        Ready,       // at least one picture can be written or updated
        Applied      // nothing left to write: ours are there, or the user's own
    };
    Q_ENUM(State)

    static SteamArtwork* get(QQmlEngine* engine = nullptr);

    int state() const { return m_State; }
    QString summary() const;

    Q_INVOKABLE void refresh();
    // Returns the sentence to show under the button: what was done, or why not.
    Q_INVOKABLE QString apply();

signals:
    void changed();

private:
    explicit SteamArtwork(QObject* parent = nullptr);

    struct Target {
        QString gridDir;
        quint32 appId;
    };

    QList<Target> findTargets(bool* steamFound) const;

    QList<QByteArray> m_Images;   // the four pictures, in k_Slots order
    QList<QByteArray> m_Hashes;   // their SHA-256
    QList<Target> m_Targets;
    int m_State = NoSteam;
    int m_Writable = 0;
    int m_Custom = 0;
};
