#include "servicecore.h"

#include "network.h"
#include "servicelog.h"
#include "webostv.h"

#include <QCoreApplication>
#include <QJsonArray>

namespace
{

// A service that started within this long of boot is a boot start (see PowerPolicy).
constexpr ULONGLONG k_BootStartMs = 5 * 60 * 1000;

QJsonObject fail(const QString& error)
{
    return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), error } };
}

}

ServiceCore::~ServiceCore()
{
    stop();
}

bool ServiceCore::start(bool echo)
{
    ServiceLog::open(TvStore::folder() + QStringLiteral("/service.log"), echo);
    ServiceLog::write(QStringLiteral("service: start, version %1, %2")
                              .arg(QCoreApplication::applicationVersion(),
                                   echo ? QStringLiteral("in the foreground") : QStringLiteral("as a service")));
    if (!TvNetwork::startup()) {
        ServiceLog::write(QStringLiteral("service: Winsock did not start"));
        return false;
    }
    m_StartedAtBoot = GetTickCount64() < k_BootStartMs;
    {
        std::lock_guard<std::recursive_mutex> lock(m_Lock);
        apply(TvStore::load());
    }
    m_Events = std::make_unique<EventWatch>(
            [this](bool restart, const QString& type) { shutdownRequested(QStringLiteral("event 1074"), restart, type); },
            [this](int bootType) { booted(bootType); });
    m_Pipe = std::make_unique<ControlPipe>(
            [this](const QJsonObject& request, const ControlPipe::Send& event) { return handle(request, event); });
    if (!m_Pipe->start())
        ServiceLog::write(QStringLiteral("service: no pipe, StreamLight cannot reach the service"));
    m_Started = true;
    return true;
}

void ServiceCore::stop()
{
    if (!m_Started)
        return;
    m_Started = false;
    m_Pipe.reset();
    m_Events.reset();   // its callbacks reach into the policy
    {
        std::lock_guard<std::recursive_mutex> lock(m_Lock);
        m_Policy.reset();
    }
    ServiceLog::write(QStringLiteral("service: stopped"));
}

bool ServiceCore::hasTv() const
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    return m_Tv.isPaired();
}

void ServiceCore::apply(const TvRecord& tv)
{
    const bool rebuilding = m_Policy != nullptr || m_Started;
    m_Policy.reset();
    m_Tv = tv;
    if (!tv.isPaired()) {
        ServiceLog::write(QStringLiteral("service: no TV paired"));
        return;
    }
    m_Policy = std::make_unique<PowerPolicy>(tv, m_StartedAtBoot && !rebuilding);
}

// ── Power notices ─────────────────────────────────────────────────────────────

void ServiceCore::suspending()
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (m_Policy)
        m_Policy->suspending();
}

void ServiceCore::resumed()
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (m_Policy)
        m_Policy->resumed();
}

void ServiceCore::shutdownRequested(const QString& source, bool restart, const QString& type)
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (m_Policy)
        m_Policy->shutdownRequested(source, restart, type);
}

void ServiceCore::booted(int bootType)
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (m_Policy)
        m_Policy->booted(bootType);
}

// ── The pipe ──────────────────────────────────────────────────────────────────

QJsonObject ServiceCore::handle(const QJsonObject& request, const ControlPipe::Send& event)
{
    const QString command = request.value(QStringLiteral("cmd")).toString();
    if (command != QLatin1String("status"))
        ServiceLog::write(QStringLiteral("pipe: %1").arg(command));
    if (command == QLatin1String("status"))
        return status();
    if (command == QLatin1String("discover"))
        return discover();
    if (command == QLatin1String("pair"))
        return pair(request, event);
    if (command == QLatin1String("set"))
        return set(request);
    if (command == QLatin1String("forget"))
        return forget();
    if (command == QLatin1String("tv_on"))
        return tvCommand(true);
    if (command == QLatin1String("tv_off"))
        return tvCommand(false);
    if (command == QLatin1String("restart"))
        return restartAnnounced();
    return fail(QStringLiteral("unknown command"));
}

QJsonObject ServiceCore::status() const
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    QJsonObject reply {
        { QStringLiteral("version"), QCoreApplication::applicationVersion() },
        { QStringLiteral("paired"), m_Tv.isPaired() },
    };
    if (!m_Tv.isPaired())
        return reply;
    reply.insert(QStringLiteral("name"), m_Tv.name);
    reply.insert(QStringLiteral("ip"), m_Tv.ip);
    reply.insert(QStringLiteral("input"), m_Tv.input);
    reply.insert(QStringLiteral("inputs"), QJsonArray::fromStringList(m_Tv.inputs));
    reply.insert(QStringLiteral("onAtStart"), m_Tv.onAtStart);
    reply.insert(QStringLiteral("offAtShutdown"), m_Tv.offAtShutdown);
    reply.insert(QStringLiteral("offOnlyIfShowingPc"), m_Tv.offOnlyIfShowingPc);
    if (m_Policy) {
        reply.insert(QStringLiteral("on"), m_Policy->tv().connected());
        reply.insert(QStringLiteral("showing"), Ssap::hdmiFromAppId(m_Policy->tv().showing()));
    }
    return reply;
}

QJsonObject ServiceCore::discover()
{
    QJsonArray tvs;
    for (const TvNetwork::Candidate& c : TvNetwork::ssdpSearch(3000)) {
        if (c.mac.isEmpty())
            continue;
        QString name = TvNetwork::friendlyName(c.location, 1500);
        tvs.append(QJsonObject {
            { QStringLiteral("ip"), c.ip },
            { QStringLiteral("mac"), c.mac },
            { QStringLiteral("name"), name.isEmpty() ? QStringLiteral("LG TV") : name },
        });
    }
    ServiceLog::write(QStringLiteral("pipe: discover found %1 TV(s)").arg(tvs.size()));
    return { { QStringLiteral("ok"), true }, { QStringLiteral("tvs"), tvs } };
}

QJsonObject ServiceCore::pair(const QJsonObject& request, const ControlPipe::Send& event)
{
    const QString ip = request.value(QStringLiteral("ip")).toString();
    if (ip.isEmpty())
        return fail(QStringLiteral("no address"));

    // The TV prompt can take minutes: no lock while it waits. The policy keeps running meanwhile.
    WebOsTv session;
    QString error;
    if (!session.connect(ip, 5000, &error))
        return fail(QStringLiteral("cannot reach the TV: %1").arg(error));
    const QString mac = TvNetwork::arpResolve(ip);
    if (mac.isEmpty())
        return fail(QStringLiteral("the TV did not answer on the local network"));
    QString key;
    {
        std::lock_guard<std::recursive_mutex> lock(m_Lock);
        if (TvNetwork::sameMac(m_Tv.mac, mac))
            key = m_Tv.key;     // the same TV again: it may not even ask
    }
    if (!session.registerClient(&key, 180000, [&event] { event({ { QStringLiteral("event"), QStringLiteral("prompt") } }); },
                                &error, [this] { return m_Pipe && m_Pipe->clientGone(); })) {
        ServiceLog::write(QStringLiteral("pipe: pairing %1 ended: %2").arg(ip, error));
        return fail(error.contains(QStringLiteral("denied"), Qt::CaseInsensitive)
                    ? QStringLiteral("denied on the TV") : error);
    }

    TvRecord tv;
    {
        std::lock_guard<std::recursive_mutex> lock(m_Lock);
        // Pairing the same TV again keeps the rules chosen for it.
        if (TvNetwork::sameMac(m_Tv.mac, mac))
            tv = m_Tv;
    }
    tv.ip = ip;
    tv.mac = mac;
    tv.key = key;
    tv.name = request.value(QStringLiteral("name")).toString(tv.name.isEmpty() ? QStringLiteral("LG TV") : tv.name);

    const WebOsTv::Reply inputs = session.request(QString::fromLatin1(Ssap::InputList));
    tv.inputs.clear();
    for (const QJsonValue& device : inputs.payload.value(QStringLiteral("devices")).toArray()) {
        const QJsonObject d = device.toObject();
        const int n = Ssap::hdmiFromAppId(d.value(QStringLiteral("appId")).toString());
        if (n <= 0)
            continue;
        while (tv.inputs.size() < n)
            tv.inputs.append(QStringLiteral("HDMI %1").arg(tv.inputs.size() + 1));
        tv.inputs[n - 1] = d.value(QStringLiteral("label")).toString(QStringLiteral("HDMI %1").arg(n));
    }
    // The TV is almost certainly showing this PC while it is being paired from it: its foreground
    // app is our input. Otherwise the input the TV calls "PC", otherwise what was set before.
    const int showing = Ssap::hdmiFromAppId(session.request(QString::fromLatin1(Ssap::ForegroundApp))
                                                    .payload.value(QStringLiteral("appId")).toString());
    if (showing > 0)
        tv.input = showing;
    else if (tv.input <= 0 && tv.inputs.contains(QStringLiteral("PC")))
        tv.input = tv.inputs.indexOf(QStringLiteral("PC")) + 1;
    session.close();

    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (!TvStore::save(tv, &error))
        return fail(QStringLiteral("could not save the pairing: %1").arg(error));
    ServiceLog::write(QStringLiteral("pipe: paired %1 (%2), HDMI %3").arg(tv.name, tv.mac).arg(tv.input));
    apply(tv);
    QJsonObject reply = status();
    reply.insert(QStringLiteral("ok"), true);
    return reply;
}

QJsonObject ServiceCore::set(const QJsonObject& request)
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (!m_Tv.isPaired())
        return fail(QStringLiteral("no TV paired"));
    // From the file, not m_Tv: the controller may have saved a newer address or key meanwhile.
    TvRecord tv = TvStore::load();
    if (request.contains(QStringLiteral("input")))
        tv.input = qBound(1, request.value(QStringLiteral("input")).toInt(), 4);
    if (request.contains(QStringLiteral("onAtStart")))
        tv.onAtStart = request.value(QStringLiteral("onAtStart")).toBool();
    if (request.contains(QStringLiteral("offAtShutdown")))
        tv.offAtShutdown = request.value(QStringLiteral("offAtShutdown")).toBool();
    if (request.contains(QStringLiteral("offOnlyIfShowingPc")))
        tv.offOnlyIfShowingPc = request.value(QStringLiteral("offOnlyIfShowingPc")).toBool();
    QString error;
    if (!TvStore::save(tv, &error))
        return fail(error);
    if (m_Policy) {
        m_Tv = tv;
        m_Policy->updateRules(tv);
    }
    else {
        apply(tv);
    }
    QJsonObject reply = status();
    reply.insert(QStringLiteral("ok"), true);
    return reply;
}

QJsonObject ServiceCore::forget()
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    QString error;
    if (!TvStore::save(TvRecord(), &error))
        return fail(error);
    ServiceLog::write(QStringLiteral("pipe: TV forgotten"));
    apply(TvRecord());
    QJsonObject reply = status();
    reply.insert(QStringLiteral("ok"), true);
    return reply;
}

QJsonObject ServiceCore::tvCommand(bool on)
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (!m_Policy)
        return fail(QStringLiteral("no TV paired"));
    // By hand, the TV is turned off whatever it shows: that is what was asked.
    if (on)
        m_Policy->tv().turnOn(QStringLiteral("from StreamLight"));
    else
        m_Policy->tv().turnOff(QStringLiteral("from StreamLight"), false);
    return { { QStringLiteral("ok"), true } };
}

QJsonObject ServiceCore::restartAnnounced()
{
    std::lock_guard<std::recursive_mutex> lock(m_Lock);
    if (m_Policy)
        m_Policy->restartAnnounced();
    return { { QStringLiteral("ok"), true } };
}
