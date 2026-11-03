// StreamLightService.exe — see service.pro for what it is and why it is separate.
//
//   (no arguments)   the Windows service, started by the service manager
//   --run            the same engine in the foreground, for development (servicehost.h)
//   --console <cmd>  one step against the TV per run, printed as it happens: how the protocol was
//                    measured on real TVs, kept as the diagnosis tool. It reads and writes the same
//                    tv.json as the service.

#include "controlpipe.h"
#include "network.h"
#include "servicehost.h"
#include "tvstore.h"
#include "webostv.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

#include <cstdio>

namespace
{

QElapsedTimer s_Clock;

void say(const QString& line)
{
    std::fprintf(stdout, "[%6lld ms] %s\n", static_cast<long long>(s_Clock.elapsed()), line.toUtf8().constData());
    std::fflush(stdout);
}

QString compact(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void sayReply(const char* what, const WebOsTv::Reply& reply)
{
    say(QStringLiteral("%1: %2 %3").arg(QLatin1String(what),
                                        reply.ok ? QStringLiteral("ok") : QStringLiteral("FAILED (%1)").arg(reply.error),
                                        compact(reply.payload)));
}

// Connects to the paired TV and registers with its key. The address is checked against the MAC
// first, so a DHCP reshuffle never has us talking to another device.
bool openPaired(TvRecord& tv, WebOsTv& session, int connectMs, bool quiet = false)
{
    QStringList candidates;
    if (!tv.ip.isEmpty())
        candidates.append(tv.ip);
    for (const QString& ip : TvNetwork::addressesFor(tv.mac)) {
        if (!candidates.contains(ip))
            candidates.append(ip);
    }
    for (const QString& ip : candidates) {
        if (!TvNetwork::portOpen(ip, 3001, 400) && !TvNetwork::portOpen(ip, 3000, 400))
            continue;
        const QString mac = TvNetwork::arpResolve(ip);
        if (!TvNetwork::sameMac(mac, tv.mac)) {
            if (!quiet)
                say(QStringLiteral("%1 answers with MAC %2, not the TV's; skipped").arg(ip, mac));
            continue;
        }
        QString error;
        if (!session.connect(ip, connectMs, &error)) {
            if (!quiet)
                say(QStringLiteral("connect to %1 failed: %2").arg(ip, error));
            continue;
        }
        QString key = tv.key;
        if (!session.registerClient(&key, 5000, [] { say(QStringLiteral("the TV is asking to allow StreamLight again")); }, &error)) {
            say(QStringLiteral("register on %1 failed: %2").arg(session.endpoint(), error));
            session.close();
            return false;
        }
        if (!quiet)
            say(QStringLiteral("connected and registered: %1").arg(session.endpoint()));
        if (key != tv.key || ip != tv.ip) {
            tv.key = key;
            tv.ip = ip;
            TvStore::save(tv, &error);
        }
        return true;
    }
    return false;
}

TvRecord pairedOrFail()
{
    const TvRecord tv = TvStore::load();
    if (!tv.isPaired())
        say(QStringLiteral("no TV paired yet: run 'pair <ip>' first"));
    return tv;
}

int discover()
{
    say(QStringLiteral("SSDP search for webOS TVs (3 s)…"));
    for (const TvNetwork::Candidate& c : TvNetwork::ssdpSearch(3000))
        say(QStringLiteral("  SSDP %1  MAC %2  %3  %4").arg(c.ip, c.mac, c.server, c.location));
    say(QStringLiteral("neighbours with port 3001 or 3000 open…"));
    for (const TvNetwork::Candidate& c : TvNetwork::probeNeighbours(300))
        say(QStringLiteral("  ARP  %1  MAC %2").arg(c.ip, c.mac));
    return 0;
}

int pair(const QString& ip)
{
    WebOsTv session;
    QString error;
    if (!session.connect(ip, 5000, &error)) {
        say(QStringLiteral("connect failed: %1").arg(error));
        return 1;
    }
    say(QStringLiteral("connected: %1").arg(session.endpoint()));
    const QString mac = TvNetwork::arpResolve(ip);
    say(QStringLiteral("MAC %1").arg(mac.isEmpty() ? QStringLiteral("(no ARP answer)") : mac));

    QString key;
    if (!session.registerClient(&key, 180000,
                                [] { say(QStringLiteral(">>> The TV is asking: press Allow (Consenti) with the remote. Waiting up to 3 minutes.")); },
                                &error)) {
        say(QStringLiteral("pairing failed: %1").arg(error));
        return 1;
    }
    say(QStringLiteral("paired, key of %1 characters").arg(key.size()));

    TvRecord tv;
    tv.ip = ip;
    tv.mac = mac;
    tv.key = key;
    // The TV is almost certainly showing this PC right now: its foreground app is our input.
    const WebOsTv::Reply foreground = session.request(QString::fromLatin1(Ssap::ForegroundApp));
    sayReply("foreground app", foreground);
    tv.input = Ssap::hdmiFromAppId(foreground.payload.value(QStringLiteral("appId")).toString());
    say(tv.input ? QStringLiteral("this PC looks to be on HDMI %1").arg(tv.input)
                 : QStringLiteral("the TV is not showing an HDMI input; input left unknown"));
    if (!TvStore::save(tv, &error)) {
        say(QStringLiteral("could not save the pairing: %1").arg(error));
        return 1;
    }
    say(QStringLiteral("saved in %1").arg(TvStore::folder()));
    return 0;
}

int info()
{
    TvRecord tv = pairedOrFail();
    if (!tv.isPaired())
        return 1;
    WebOsTv session;
    if (!openPaired(tv, session, 4000)) {
        say(QStringLiteral("the TV is not reachable"));
        return 1;
    }
    sayReply("power state", session.request(QString::fromLatin1(Ssap::PowerState)));
    sayReply("foreground app", session.request(QString::fromLatin1(Ssap::ForegroundApp)));
    const WebOsTv::Reply inputs = session.request(QString::fromLatin1(Ssap::InputList));
    if (!inputs.ok) {
        sayReply("inputs", inputs);
    }
    else {
        for (const QJsonValue& d : inputs.payload.value(QStringLiteral("devices")).toArray()) {
            const QJsonObject o = d.toObject();
            say(QStringLiteral("  input %1 \"%2\" connected=%3 app=%4").arg(
                    o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("label")).toString(),
                    o.value(QStringLiteral("connected")).toBool() ? QStringLiteral("yes") : QStringLiteral("no"),
                    o.value(QStringLiteral("appId")).toString()));
        }
    }
    say(QStringLiteral("stored input: HDMI %1").arg(tv.input));
    return 0;
}

int simple(const char* uri, const char* what)
{
    TvRecord tv = pairedOrFail();
    if (!tv.isPaired())
        return 1;
    WebOsTv session;
    if (!openPaired(tv, session, 4000)) {
        say(QStringLiteral("the TV is not reachable"));
        return 1;
    }
    const WebOsTv::Reply reply = session.request(QString::fromLatin1(uri), QJsonObject(), 4000);
    sayReply(what, reply);
    return reply.ok ? 0 : 1;
}

int input(TvRecord& tv, WebOsTv& session, int n)
{
    const WebOsTv::Reply foreground = session.request(QString::fromLatin1(Ssap::ForegroundApp));
    if (foreground.payload.value(QStringLiteral("appId")).toString() == Ssap::hdmiAppId(n)) {
        say(QStringLiteral("already on HDMI %1, nothing to do").arg(n));
        return 0;
    }
    WebOsTv::Reply reply = session.request(QString::fromLatin1(Ssap::Launch),
                                           { { QStringLiteral("id"), Ssap::hdmiAppId(n) } }, 6000);
    sayReply("launch HDMI app", reply);
    if (!reply.ok) {
        reply = session.request(QString::fromLatin1(Ssap::SwitchInput),
                                { { QStringLiteral("inputId"), Ssap::hdmiInputId(n) } }, 6000);
        sayReply("switchInput", reply);
    }
    Q_UNUSED(tv)
    return reply.ok ? 0 : 1;
}

int turnOn(int wanted)
{
    TvRecord tv = pairedOrFail();
    if (!tv.isPaired())
        return 1;
    const int hdmi = wanted ? wanted : tv.input;

    WebOsTv session;
    QString lastState;
    int packets = 0;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 30000) {
        if (!session.isOpen() && !openPaired(tv, session, 2000, true)) {
            packets += TvNetwork::sendWakeOnLan(tv.mac);
            say(QStringLiteral("not reachable: Wake-on-LAN sent (%1 packets so far)").arg(packets));
            QThread::msleep(1500);
            continue;
        }
        const WebOsTv::Reply power = session.request(QString::fromLatin1(Ssap::PowerState), QJsonObject(), 3000);
        if (!power.ok) {
            say(QStringLiteral("power state failed (%1); reconnecting").arg(power.error));
            session.close();
            continue;
        }
        const QString state = power.payload.value(QStringLiteral("state")).toString();
        if (state != lastState) {
            say(QStringLiteral("power state: %1").arg(compact(power.payload)));
            lastState = state;
        }
        if (state == QLatin1String("Active") && !power.payload.contains(QStringLiteral("processing")))
            break;
        if (state == QLatin1String("Screen Off")) {
            sayReply("turn screen on", session.request(QString::fromLatin1(Ssap::TurnOnScreen), QJsonObject(), 3000));
            QThread::msleep(500);
            continue;
        }
        if (state != QLatin1String("Active")) {
            packets += TvNetwork::sendWakeOnLan(tv.mac);
            say(QStringLiteral("Wake-on-LAN sent (%1 packets so far)").arg(packets));
        }
        QThread::msleep(1500);
    }
    if (lastState != QLatin1String("Active")) {
        say(QStringLiteral("the TV did not come on within 30 s"));
        return 1;
    }
    say(QStringLiteral("TV on after %1 ms").arg(clock.elapsed()));
    if (hdmi == 0) {
        say(QStringLiteral("no input known: left where it is"));
        return 0;
    }
    const int result = input(tv, session, hdmi);
    say(QStringLiteral("done after %1 ms").arg(clock.elapsed()));
    return result;
}

int switchTo(int n)
{
    TvRecord tv = pairedOrFail();
    if (!tv.isPaired())
        return 1;
    WebOsTv session;
    if (!openPaired(tv, session, 4000)) {
        say(QStringLiteral("the TV is not reachable"));
        return 1;
    }
    return input(tv, session, n);
}

// One request over the service's pipe; every line of the answer printed, up to the one with "ok".
int pipeRequest(const QString& json)
{
    HANDLE pipe = CreateFileW(ControlPipe::Name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        say(QStringLiteral("cannot open the pipe (%1): is the service running?").arg(GetLastError()));
        return 1;
    }
    const QByteArray line = json.toUtf8() + '\n';
    DWORD written = 0;
    WriteFile(pipe, line.constData(), static_cast<DWORD>(line.size()), &written, nullptr);
    QByteArray buffer;
    char chunk[4096];
    int result = 1;
    for (bool done = false; !done;) {
        DWORD read = 0;
        if (!ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0)
            break;
        buffer.append(chunk, static_cast<int>(read));
        int newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const QByteArray reply = buffer.left(newline);
            buffer.remove(0, newline + 1);
            say(QString::fromUtf8(reply));
            const QJsonObject o = QJsonDocument::fromJson(reply).object();
            if (o.contains(QStringLiteral("ok"))) {
                result = o.value(QStringLiteral("ok")).toBool() ? 0 : 1;
                done = true;
            }
        }
    }
    CloseHandle(pipe);
    return result;
}

void usage()
{
    std::fprintf(stderr,
                 "StreamLightService            run as the Windows service\n"
                 "StreamLightService --run      run the service in the foreground (Ctrl+C stops)\n"
                 "StreamLightService --console <command>\n"
                 "  discover          find webOS TVs on this network\n"
                 "  pair <ip>         pair with the TV at <ip> (press Allow on the TV)\n"
                 "  info              power state, foreground app, software, volume, inputs\n"
                 "  on [hdmi]         turn the TV on (Wake-on-LAN if needed) and switch input\n"
                 "  off               turn the TV off\n"
                 "  screen-off        turn only the screen off\n"
                 "  screen-on         turn the screen back on\n"
                 "  input <hdmi>      switch to HDMI <hdmi>\n"
                 "  wake <mac>        send Wake-on-LAN to <mac> (no pairing needed)\n"
                 "  button <name>     press a remote button (POWER, HOME…) through the pointer channel\n"
                 "  pipe <json>       send one request to the running service, as StreamLight does\n");
}

int runConsole(const QStringList& args)
{
    const QString command = args.value(0);
    if (command == QLatin1String("discover"))
        return discover();
    if (command == QLatin1String("pair") && args.size() == 2)
        return pair(args.at(1));
    if (command == QLatin1String("info"))
        return info();
    if (command == QLatin1String("on"))
        return turnOn(args.value(1).toInt());
    if (command == QLatin1String("off"))
        return simple(Ssap::TurnOff, "turn off");
    if (command == QLatin1String("screen-off"))
        return simple(Ssap::TurnOffScreen, "turn screen off");
    if (command == QLatin1String("screen-on"))
        return simple(Ssap::TurnOnScreen, "turn screen on");
    if (command == QLatin1String("input") && args.size() == 2)
        return switchTo(args.at(1).toInt());
    if (command == QLatin1String("button") && args.size() == 2) {
        TvRecord tv = pairedOrFail();
        if (!tv.isPaired())
            return 1;
        WebOsTv session;
        if (!openPaired(tv, session, 4000)) {
            say(QStringLiteral("the TV is not reachable"));
            return 1;
        }
        QString error;
        const bool pressed = session.pressButton(args.at(1).toUpper(), &error);
        say(pressed ? QStringLiteral("button %1 pressed").arg(args.at(1).toUpper())
                    : QStringLiteral("button failed: %1").arg(error));
        return pressed ? 0 : 1;
    }
    if (command == QLatin1String("pipe") && args.size() == 2)
        return pipeRequest(args.at(1));
    if (command == QLatin1String("wake") && args.size() == 2) {
        const int sent = TvNetwork::sendWakeOnLan(args.at(1));
        say(QStringLiteral("Wake-on-LAN to %1: %2 packets").arg(TvNetwork::normaliseMac(args.at(1))).arg(sent));
        return sent > 0 ? 0 : 1;
    }
    usage();
    return 2;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    s_Clock.start();
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty())
        return ServiceHost::runAsService();
    if (args.value(0) == QLatin1String("--run"))
        return ServiceHost::runInForeground();
    if (args.value(0) != QLatin1String("--console")) {
        usage();
        return 2;
    }
    if (!TvNetwork::startup()) {
        say(QStringLiteral("Winsock did not start"));
        return 1;
    }
    return runConsole(args.mid(1));
}
