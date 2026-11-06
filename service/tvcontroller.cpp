#include "tvcontroller.h"

#include "network.h"
#include "servicelog.h"

namespace
{

// How long a wake may take: at boot the network itself can need half of it.
constexpr int k_TurnOnMs = 45000;
constexpr int k_WolEveryMs = 1500;
constexpr int k_IdleCheckMs = 30000;

}

TvController::TvController(const TvRecord& tv)
    : m_Tv(tv),
      m_Input(tv.input)
{
    // m_LastCheck is left unstarted: the first idle check runs at once, so a TV that is on is
    // connected — and shown on — within a second of the service starting or a new pairing.
    m_Worker = std::thread(&TvController::run, this);
}

TvController::~TvController()
{
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        m_Stop = true;
        if (m_Pending && m_Pending->done)
            m_Pending->done->set_value(false);
        m_Pending.reset();
    }
    m_Wake.notify_all();
    if (m_Worker.joinable())
        m_Worker.join();
    m_Session.close();
}

void TvController::turnOn(const QString& why)
{
    post({ Action::On, why, false, 0, nullptr });
}

void TvController::turnOff(const QString& why, bool onlyIfShowingPc)
{
    post({ Action::Off, why, onlyIfShowingPc, 6000, nullptr });
}

bool TvController::turnOffNow(const QString& why, bool onlyIfShowingPc, int budgetMs)
{
    auto done = std::make_shared<std::promise<bool>>();
    std::future<bool> result = done->get_future();
    post({ Action::Off, why, onlyIfShowingPc, budgetMs, done });
    if (result.wait_for(std::chrono::milliseconds(budgetMs)) != std::future_status::ready) {
        ServiceLog::write(QStringLiteral("tv: %1: still working after %2 ms, Windows goes on").arg(why).arg(budgetMs));
        return false;
    }
    return result.get();
}

void TvController::post(Job job)
{
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        if (m_Stop)
            return;
        if (m_Pending && m_Pending->done)
            m_Pending->done->set_value(false);
        m_Pending = std::move(job);
        m_Cancel = true;    // the operation running now gives way at its next step
    }
    m_Wake.notify_all();
}

bool TvController::pause(int ms)
{
    std::unique_lock<std::mutex> lock(m_Lock);
    m_Wake.wait_for(lock, std::chrono::milliseconds(ms), [this] { return m_Cancel || m_Stop; });
    return !cancelled();
}

void TvController::run()
{
    while (!m_Stop) {
        std::optional<Job> job;
        {
            std::unique_lock<std::mutex> lock(m_Lock);
            m_Wake.wait_for(lock, std::chrono::seconds(1), [this] { return m_Pending || m_Stop; });
            if (m_Stop)
                break;
            if (m_Pending) {
                job = std::move(m_Pending);
                m_Pending.reset();
                m_Cancel = false;
            }
        }
        if (!job) {
            idle();
            continue;
        }
        QElapsedTimer clock;
        clock.start();
        ServiceLog::write(QStringLiteral("tv: %1 (%2)").arg(job->action == Action::On ? QStringLiteral("turn on")
                                                                                       : QStringLiteral("turn off"),
                                                          job->why));
        const bool ok = job->action == Action::On ? doTurnOn(*job) : doTurnOff(*job);
        ServiceLog::write(QStringLiteral("tv: %1 after %2 ms").arg(ok ? QStringLiteral("done") : QStringLiteral("not done"))
                                  .arg(clock.elapsed()));
        if (job->done)
            job->done->set_value(ok);
        m_LastCheck.restart();
    }
}

void TvController::idle()
{
    if (m_Session.isOpen()) {
        if (!m_Session.pump(0))
            ServiceLog::write(QStringLiteral("tv: connection closed by the TV"));
    }
    // Every half minute: open the connection if the TV has come on by itself (the remote, the
    // TV's own timer), so it is warm when Windows next shuts down. With the TV in standby this
    // costs one refused register.
    if (m_LastCheck.isValid() && m_LastCheck.elapsed() < k_IdleCheckMs)
        return;
    m_LastCheck.restart();
    if (m_Session.isOpen()) {
        const WebOsTv::Reply power = m_Session.request(QString::fromLatin1(Ssap::PowerState), QJsonObject(), 3000);
        if (!power.ok)
            m_Session.close();
    }
    else if (ensureSession(2000, false)) {
        ServiceLog::write(QStringLiteral("tv: the TV is on (connected at idle check), showing %1").arg(m_Foreground));
    }
}

bool TvController::ensureSession(int timeoutMs, bool allowSsdp)
{
    if (m_Session.isOpen())
        return true;

    QElapsedTimer clock;
    clock.start();
    QStringList candidates;
    if (!m_Tv.ip.isEmpty())
        candidates.append(m_Tv.ip);
    for (const QString& ip : TvNetwork::addressesFor(m_Tv.mac)) {
        if (!candidates.contains(ip))
            candidates.append(ip);
    }
    if (allowSsdp) {
        for (const TvNetwork::Candidate& c : TvNetwork::ssdpSearch(1500)) {
            if (TvNetwork::sameMac(c.mac, m_Tv.mac) && !candidates.contains(c.ip))
                candidates.append(c.ip);
        }
    }

    for (const QString& ip : candidates) {
        const int left = timeoutMs - static_cast<int>(clock.elapsed());
        if (left <= 0 || cancelled())
            return false;
        if (!TvNetwork::portOpen(ip, 3001, qMin(left, 400)) && !TvNetwork::portOpen(ip, 3000, qMin(left, 400)))
            continue;
        // The address must still belong to the TV: DHCP may have given it to something else.
        if (!TvNetwork::sameMac(TvNetwork::arpResolve(ip), m_Tv.mac))
            continue;
        QString error;
        bool certChanged = false;
        if (!m_Session.connect(ip, qMax(left, 1000), &error, m_Tv.cert, &certChanged)) {
            if (certChanged) {
                // The same MAC, another certificate: the key is not sent until the TV is paired
                // again from StreamLight, which shows why (8.5.0, security review).
                if (!m_CertChanged.exchange(true))
                    ServiceLog::write(QStringLiteral("tv: %1 presented a different certificate — not connecting; "
                                                     "pair the TV again from StreamLight").arg(ip));
                return false;
            }
            continue;
        }
        m_CertChanged = false;
        // A pairing from 8.4.0 kept no certificate: the first TLS connection records it.
        const bool pinNow = m_Tv.cert.isEmpty() && !m_Session.certHash().isEmpty();
        QString key = m_Tv.key;
        bool prompted = false;
        if (!m_Session.registerClient(&key, 4000, [&prompted] { prompted = true; }, &error)) {
            m_Session.close();
            if (prompted)
                ServiceLog::write(QStringLiteral("tv: the TV is asking to allow StreamLight again — pair it again from StreamLight"));
            return false;   // in standby the TV closes the connection here
        }
        if (key != m_Tv.key || ip != m_Tv.ip || pinNow) {
            m_Tv.key = key;
            m_Tv.ip = ip;
            if (pinNow) {
                m_Tv.cert = m_Session.certHash();
                ServiceLog::write(QStringLiteral("tv: certificate recorded for this TV"));
            }
            // Only these fields: the rules in the file may have changed since this copy was made.
            TvRecord stored = TvStore::load();
            if (TvNetwork::sameMac(stored.mac, m_Tv.mac)) {
                stored.key = key;
                stored.ip = ip;
                if (stored.cert.isEmpty())
                    stored.cert = m_Tv.cert;
                QString saveError;
                if (!TvStore::save(stored, &saveError))
                    ServiceLog::write(QStringLiteral("tv: could not save the new address: %1").arg(saveError));
            }
        }
        m_Foreground.clear();
        if (!m_Session.subscribe(QString::fromLatin1(Ssap::ForegroundApp),
                                 [this](const QJsonObject& payload) {
                                     const QString app = payload.value(QStringLiteral("appId")).toString();
                                     if (!app.isEmpty() && app != m_Foreground) {
                                         if (!m_Foreground.isEmpty())
                                             ServiceLog::write(QStringLiteral("tv: now showing %1").arg(app));
                                         m_Foreground = app;
                                         std::lock_guard<std::mutex> lock(m_ShowingLock);
                                         m_Showing = app;
                                     }
                                 },
                                 &error))
            ServiceLog::write(QStringLiteral("tv: no foreground subscription: %1").arg(error));
        return true;
    }
    return false;
}

bool TvController::doTurnOn(const Job& job)
{
    Q_UNUSED(job)
    QElapsedTimer clock;
    clock.start();
    QString lastState;
    int packets = 0;
    int attempts = 0;
    for (;;) {
        if (cancelled())
            return false;
        if (clock.elapsed() > k_TurnOnMs) {
            ServiceLog::write(QStringLiteral("tv: did not come on within %1 s (%2 Wake-on-LAN packets)")
                                      .arg(k_TurnOnMs / 1000).arg(packets));
            return false;
        }
        // SSDP every fifth round: the TV may have a new address the ARP cache does not know yet.
        if (!m_Session.isOpen() && !ensureSession(2000, ++attempts % 5 == 0)) {
            const int sent = TvNetwork::sendWakeOnLan(m_Tv.mac);
            // 8.5.0: with no network up yet nothing goes out at all, silently — say when it does.
            if (sent > 0 && packets == 0)
                ServiceLog::write(QStringLiteral("tv: first Wake-on-LAN out %1 ms into turning on (%2 s after Windows started)")
                                          .arg(clock.elapsed()).arg(GetTickCount64() / 1000));
            packets += sent;
            if (!pause(k_WolEveryMs))
                return false;
            continue;
        }
        const WebOsTv::Reply power = m_Session.request(QString::fromLatin1(Ssap::PowerState), QJsonObject(), 3000);
        if (!power.ok) {
            m_Session.close();
            continue;
        }
        const QString state = power.payload.value(QStringLiteral("state")).toString();
        if (state != lastState) {
            ServiceLog::write(QStringLiteral("tv: power state %1").arg(state));
            lastState = state;
        }
        if (state == QLatin1String("Active") && !power.payload.contains(QStringLiteral("processing")))
            break;
        if (state == QLatin1String("Screen Off"))
            m_Session.request(QString::fromLatin1(Ssap::TurnOnScreen), QJsonObject(), 3000);
        else if (state != QLatin1String("Active"))
            packets += TvNetwork::sendWakeOnLan(m_Tv.mac);
        if (!pause(state == QLatin1String("Screen Off") ? 500 : k_WolEveryMs))
            return false;
    }
    if (packets > 0)
        ServiceLog::write(QStringLiteral("tv: on after %1 ms, %2 Wake-on-LAN packets").arg(clock.elapsed()).arg(packets));

    const int input = m_Input;
    if (input <= 0) {
        ServiceLog::write(QStringLiteral("tv: this PC's input is not known; left on %1").arg(m_Foreground));
        return true;
    }
    const QString ours = Ssap::hdmiAppId(input);
    m_Session.pump(0);
    if (m_Foreground == ours) {
        ServiceLog::write(QStringLiteral("tv: already on HDMI %1").arg(input));
        return true;
    }
    // Right after a wake webOS can refuse the first launch while it finishes starting: try twice.
    for (int i = 0; i < 2 && !cancelled(); i++) {
        WebOsTv::Reply reply = m_Session.request(QString::fromLatin1(Ssap::Launch), { { QStringLiteral("id"), ours } }, 6000);
        if (!reply.ok)
            reply = m_Session.request(QString::fromLatin1(Ssap::SwitchInput),
                                      { { QStringLiteral("inputId"), Ssap::hdmiInputId(input) } }, 6000);
        if (reply.ok) {
            ServiceLog::write(QStringLiteral("tv: switched to HDMI %1 (was %2)").arg(input).arg(m_Foreground));
            return true;
        }
        ServiceLog::write(QStringLiteral("tv: switch to HDMI %1 failed: %2").arg(input).arg(reply.error));
        if (!pause(1000))
            return false;
    }
    return false;
}

bool TvController::doTurnOff(const Job& job)
{
    QElapsedTimer clock;
    clock.start();
    const int budget = job.budgetMs > 0 ? job.budgetMs : 6000;
    if (!ensureSession(qMin(1200, budget / 2), false)) {
        ServiceLog::write(QStringLiteral("tv: not reachable, already off"));
        return true;
    }
    const int input = m_Input;
    if (job.onlyIfShowingPc && input > 0) {
        m_Session.pump(0);
        if (m_Foreground.isEmpty()) {
            m_Foreground = m_Session.request(QString::fromLatin1(Ssap::ForegroundApp), QJsonObject(), 1000)
                                   .payload.value(QStringLiteral("appId")).toString();
        }
        if (m_Foreground != Ssap::hdmiAppId(input)) {
            ServiceLog::write(QStringLiteral("tv: showing %1, not this PC: left on").arg(m_Foreground));
            return true;
        }
    }
    const int left = qMax(budget - static_cast<int>(clock.elapsed()), 500);
    const WebOsTv::Reply power = m_Session.request(QString::fromLatin1(Ssap::PowerState), QJsonObject(), qMin(left, 1000));
    const QString state = power.payload.value(QStringLiteral("state")).toString();
    bool ok = true;
    if (!power.ok) {
        ServiceLog::write(QStringLiteral("tv: no power state (%1): treated as off").arg(power.error));
    }
    // POWER, like the remote, is the only way that also sends the HDMI-CEC standby to the devices
    // on the TV (a soundbar on SIMPLINK). It is a toggle: pressed only on a TV known to be on.
    else if (state == QLatin1String("Active")) {
        ok = pressPower();
        if (!ok) {
            ServiceLog::write(QStringLiteral("tv: POWER failed, falling back to turnOff"));
            ok = m_Session.request(QString::fromLatin1(Ssap::TurnOff), QJsonObject(), 1500).ok;
        }
    }
    else if (state == QLatin1String("Screen Off")) {
        ok = m_Session.request(QString::fromLatin1(Ssap::TurnOff), QJsonObject(), 1500).ok;
    }
    else {
        ServiceLog::write(QStringLiteral("tv: power state %1: nothing to do").arg(state));
    }
    m_Session.close();
    return ok;
}

bool TvController::pressPower()
{
    QString error;
    if (m_Session.pressButton(QStringLiteral("POWER"), &error))
        return true;
    ServiceLog::write(QStringLiteral("tv: POWER: %1").arg(error));
    return false;
}
