#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

/**
 * Everything the launch curtain shows, in one place.
 *
 * <p>There is one curtain and QML draws all of it, because the stream window stays hidden
 * until the launch is over. This class is what it reads: the state, and the wording with it.
 * The wording is English here rather than qsTr in QML because it is composed alongside the
 * state that produces it — and StreamLight ships English-only, so nothing is lost.</p>
 */
class LaunchCurtain : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool         active   READ active   NOTIFY changed)
    Q_PROPERTY(QString      gameName READ gameName NOTIFY changed)
    Q_PROPERTY(QUrl         coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(QColor       accent   READ accent   NOTIFY changed)
    Q_PROPERTY(QColor       deep     READ deep     NOTIFY changed)
    Q_PROPERTY(QString      title    READ title    NOTIFY changed)
    Q_PROPERTY(QString      detail   READ detail   NOTIFY changed)
    Q_PROPERTY(QString      warning  READ warning  NOTIFY changed)
    Q_PROPERTY(qreal        progress READ progress NOTIFY changed)
    // No `steps` property: the QML used to draw a checklist of them and no longer does — one
    // line and a prompt replaced it. ⚠️ m_Steps itself stays, and is not dead: `progress`, and
    // with it the cover's brightness, is computed from how many have completed.

public:
    explicit LaunchCurtain(QObject* parent = nullptr);

    bool         active()   const { return m_Active; }
    QString      gameName() const { return m_GameName; }
    QUrl         coverUrl() const { return m_CoverUrl; }
    QColor       accent()   const { return m_Accent; }
    QColor       deep()     const { return m_Deep; }
    QString      title()    const { return m_Title; }
    QString      detail()   const { return m_Detail; }
    QString      warning()  const { return m_Warning; }
    qreal        progress() const { return m_Progress; }

    /** The launch is starting. Called once, before anything else. */
    void begin(const QString& gameName);

    /**
     * The box art, which only QML knows: it comes from the app model, and NvApp itself
     * carries no artwork. Safe to call before or after begin(); the colours are derived the
     * moment it arrives.
     */
    Q_INVOKABLE void setCover(const QUrl& coverUrl);

    /** The host is renegotiating its link; `detail` is "2.5 Gbps → 1 Gbps". */
    void onLinkStage(const QString& detail);

    /** The link handshake ended, one way or another. */
    void onLinkFinished(bool changed, const QString& warning);

    /** A new phase from the host's launch watcher (values of LaunchPhase). */
    void onLaunchPhase(int phase, const QString& foreground, qint64 elapsedMs);

    /** The curtain comes down. */
    void finish();

signals:
    void changed();

private:
    struct Step
    {
        QString label;
        int     limitSec;      ///< when the app stops waiting — real behaviour
        int     expectedSec;   ///< only paces the colour ramp; decides nothing
        int     state = 0;     ///< 0 pending, 1 active, 2 done, 3 failed
        qint64  startedMs = -1;
        qint64  endedMs = -1;
    };

    void  tick();
    void  setStepActive(int index);
    void  completeStep(int index, int state);
    void  recompute();
    qint64 nowMs() const;
    void  extractCoverColours(const QUrl& coverUrl);

    void ensureLaunchSteps();

    QList<Step>  m_Steps;
    QTimer       m_Ticker;
    qint64       m_StartedMs = 0;

    // Which slot each step landed in, or -1 while it doesn't exist. A step is only created
    // once there is something real to report about it — the Desktop entry launches nothing,
    // so it must never be told a game is being waited for.
    int m_LinkIdx   = -1;
    // Target speed pulled out of the matcher's "<from> → <to>" stage line, so the line that
    // follows can name the speed the link settled on. Empty when there was no renegotiation.
    QString m_LinkTarget;
    int m_LaunchIdx = -1;
    int m_WindowIdx = -1;

    bool     m_Active = false;
    QString  m_GameName;
    QUrl     m_CoverUrl;
    QColor   m_Accent = QColor(0x3a, 0x4a, 0x5c);
    QColor   m_Deep   = QColor(0x10, 0x14, 0x1c);
    QString  m_Title;
    QString  m_Detail;
    QString  m_Warning;
    qreal    m_Progress = 0.0;
};
