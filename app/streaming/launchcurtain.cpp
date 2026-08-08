#include "launchcurtain.h"
#include "../backend/coverpalette.h"
#include "../backend/launchgate.h"

#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QVariantMap>
#include <QtMath>

namespace
{
    // The two budgets the user is told about. They are the real behaviour: past these the
    // app stops waiting and shows the stream regardless.
    constexpr int LinkLimitSec   = 40;   // matches LinkMatcher's own ceiling
    constexpr int LaunchLimitSec = 90;   // matches the host's LaunchWatcher

    // Only used to pace the colour ramp, never to decide anything. Measured from real
    // launches: a link renegotiation is ~20 s (4 s down plus the host's 15 s settle), a
    // store takes a handful of seconds to obey, and the window follows shortly after.
    constexpr int LinkExpectedSec   = 20;
    constexpr int LaunchExpectedSec = 6;
    constexpr int WindowExpectedSec = 4;

    constexpr int StepPending = 0, StepActive = 1, StepDone = 2, StepFailed = 3;
}

LaunchCurtain::LaunchCurtain(QObject* parent)
    : QObject(parent)
{
    // One second, integer seconds. Tenths would be noise on a screen read from across a
    // room, and in the renderer each update costs a text surface rebuild.
    m_Ticker.setInterval(1000);
    m_Ticker.setSingleShot(false);
    connect(&m_Ticker, &QTimer::timeout, this, &LaunchCurtain::tick);
}

qint64 LaunchCurtain::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

void LaunchCurtain::setCover(const QUrl& coverUrl)
{
    m_CoverUrl = coverUrl;
    extractCoverColours(coverUrl);

    // Says whether the artwork arrived at all, whether it was a real file or the
    // placeholder the model hands back while a cover is still downloading, and what came
    // out. Without this, "the background isn't coloured" has three different causes that
    // all look identical on screen.
    qInfo().nospace() << "Launch curtain: cover " << coverUrl.toString()
                      << (coverUrl.isLocalFile() ? " [local]" : " [not a local file]")
                      << " accent=" << m_Accent.name() << " deep=" << m_Deep.name();
    emit changed();
}

void LaunchCurtain::begin(const QString& gameName)
{
    m_Active    = true;
    m_GameName  = gameName;
    m_Warning.clear();
    m_StartedMs = nowMs();
    m_Progress  = 0.0;

    // No steps yet, on purpose. Each one appears when there is something real behind it:
    // the link step only if a renegotiation actually happens, the launch steps only once
    // the host says it is running something. The Desktop entry runs nothing at all, and
    // used to be shown a "Game launched" counter it could never satisfy.
    m_Steps.clear();
    m_LinkIdx = m_LaunchIdx = m_WindowIdx = -1;

    // The game's name is the heading right above this, so nothing here repeats it: the title
    // says what is happening to it and reads as a progression — preparing connection, starting
    // up, launching on host, loading, ready.
    //
    // The detail line only exists where it carries a fact the title does not: the speeds during
    // a renegotiation, the link it settled on, the window being up. Everywhere else it is
    // cleared rather than filled — restating the title in more words is what the spinner beside
    // it already does, and narrating that we are about to reveal the host describes something
    // the user is watching happen.
    m_Title  = QStringLiteral("Starting up");
    m_Detail.clear();

    m_Ticker.start();
    recompute();
}

void LaunchCurtain::ensureLaunchSteps()
{
    if (m_LaunchIdx >= 0) return;
    m_Steps.append({ QStringLiteral("Game launched"),    LaunchLimitSec, LaunchExpectedSec });
    m_LaunchIdx = m_Steps.size() - 1;
    m_Steps.append({ QStringLiteral("Window on screen"), LaunchLimitSec, WindowExpectedSec });
    m_WindowIdx = m_Steps.size() - 1;
}

void LaunchCurtain::onLinkStage(const QString& detail)
{
    // First word from the matcher: there is a renegotiation to wait for after all, so the
    // step appears now and takes the lead.
    if (m_LinkIdx < 0) {
        m_Steps.prepend({ QStringLiteral("Link matched"), LinkLimitSec, LinkExpectedSec });
        m_LinkIdx = 0;
        if (m_LaunchIdx >= 0) m_LaunchIdx++;
        if (m_WindowIdx >= 0) m_WindowIdx++;
        setStepActive(m_LinkIdx);
    }

    // Named for what it achieves rather than what it does: "matching the host link speed" is
    // the mechanism, and the mechanism is only interesting to the person who wrote it. The
    // speeds stay in the detail line, where they are evidence rather than jargon.
    m_Title  = QStringLiteral("Preparing connection");
    m_Detail = detail;

    // "<from> → <to>": kept so the line after this one can name the speed we ended on. The
    // same project builds that string (LinkMatcher), so the separator is ours to rely on —
    // and if it ever stops matching, the worst case is a "link ready" with no figure.
    const int arrow = detail.lastIndexOf(QChar(0x2192));
    m_LinkTarget = (arrow >= 0) ? detail.mid(arrow + 1).trimmed() : QString();

    recompute();
}

void LaunchCurtain::onLinkFinished(bool changed, const QString& warning)
{
    if (m_LinkIdx >= 0) {
        completeStep(m_LinkIdx, warning.isEmpty() ? StepDone : StepFailed);
    }

    // Not fatal by design: a link that didn't switch never stops a launch, so it is a line
    // of text and nothing more.
    m_Warning = warning;

    m_Title  = QStringLiteral("Starting up");
    if (changed) {
        m_Detail = m_LinkTarget.isEmpty()
                       ? QStringLiteral("link ready")
                       : QStringLiteral("link ready — %1").arg(m_LinkTarget);
    }
    else {
        // Nothing was renegotiated, so there is no fact to report here.
        m_Detail.clear();
    }
    recompute();
}

void LaunchCurtain::onLaunchPhase(int phase, const QString& foreground, qint64 elapsedMs)
{
    Q_UNUSED(elapsedMs);

    switch (static_cast<LaunchPhase>(phase)) {
    case LaunchPhase::Launching:
        // Now we know the host is actually running something, so the two steps earn their
        // place. Nothing creates them before this: a Desktop session never gets here.
        ensureLaunchSteps();
        setStepActive(m_LaunchIdx);
        m_Title  = QStringLiteral("Launching on host");
        m_Detail.clear();
        break;

    case LaunchPhase::GameWindow:
        ensureLaunchSteps();
        completeStep(m_LaunchIdx, StepDone);
        setStepActive(m_WindowIdx);
        m_Title  = QStringLiteral("Loading");
        m_Detail = QStringLiteral("window open on host");
        break;

    case LaunchPhase::Ready:
        // The host can skip straight here — a game that opens full screen in one go never
        // passes through GameWindow — so finish whatever is still open rather than assuming
        // the steps arrived in order.
        ensureLaunchSteps();
        completeStep(m_LaunchIdx, StepDone);
        completeStep(m_WindowIdx, StepDone);
        m_Title  = QStringLiteral("Ready");
        m_Detail.clear();
        break;

    case LaunchPhase::NeedsAttention:
        m_Title  = QStringLiteral("%1 needs you").arg(
                       foreground.isEmpty() ? QStringLiteral("Something") : foreground);
        // The title is already an instruction and the curtain is lifting as it is read, so
        // saying that we are showing the host describes what the user is watching happen.
        m_Detail.clear();
        break;

    case LaunchPhase::Timeout:
        m_Title  = QStringLiteral("Taking too long");
        // ⚠️ Tied to LaunchWatcher's 90-second cap on the host (StreamTweak §39). If that cap
        // ever moves, this figure has to move with it — it is the one number here that can
        // silently start lying.
        m_Detail = QStringLiteral("no answer after 90s");
        break;

    default:
        break;
    }

    recompute();
}

void LaunchCurtain::finish()
{
    m_Ticker.stop();
    m_Active = false;
    emit changed();
}

void LaunchCurtain::setStepActive(int index)
{
    if (index < 0 || index >= m_Steps.size()) return;
    Step& s = m_Steps[index];
    if (s.state == StepDone || s.state == StepFailed) return;
    if (s.state != StepActive) {
        s.state = StepActive;
        s.startedMs = nowMs();
    }
}

void LaunchCurtain::completeStep(int index, int state)
{
    if (index < 0 || index >= m_Steps.size()) return;
    Step& s = m_Steps[index];
    if (s.state == StepDone || s.state == StepFailed) return;
    if (s.startedMs < 0) s.startedMs = nowMs();
    s.endedMs = nowMs();
    s.state = state;
}

void LaunchCurtain::tick()
{
    recompute();
}

void LaunchCurtain::recompute()
{
    // Progress is what the cover's colour follows, and it cannot be elapsed-over-total:
    // nobody knows how long a game takes to load — the same title measured 9 s warm and
    // 27 s cold on the same machine. So it is milestones, with a creep inside the current
    // one paced by an expectation that decides nothing else.
    int done = 0;
    qreal partial = 0.0;

    for (const Step& s : m_Steps) {
        if (s.state == StepDone || s.state == StepFailed) {
            done++;
        }
        else if (s.state == StepActive && s.startedMs > 0 && s.expectedSec > 0) {
            const qreal secs = (nowMs() - s.startedMs) / 1000.0;
            partial = qBound(0.0, secs / s.expectedSec, 1.0);
        }
    }

    const int total = m_Steps.size();
    m_Progress = total > 0 ? qBound(0.0, (done + partial) / total, 1.0) : 0.0;

    emit changed();
}


void LaunchCurtain::extractCoverColours(const QUrl& coverUrl)
{
    // The logic moved to CoverPalette when the host stage needed the same thing from a
    // picture the user chose. Two copies of "find the dominant colour and tidy it up" would
    // have drifted the first time either was tuned, and the whole point of the pair is that
    // the two screens look like one design.
    if (!coverUrl.isLocalFile()) {
        CoverPalette::neutral(m_Accent, m_Deep);
        return;
    }

    CoverPalette::fromImageFile(coverUrl.toLocalFile(), m_Accent, m_Deep);
}
