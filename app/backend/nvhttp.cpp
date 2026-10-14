#include "nvcomputer.h"
#include "identitymanager.h"
#include "pyrowaveudpprobe.h"
#include <Limelight.h>

#include <QUdpSocket>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <QDebug>
#include <QUuid>
#include <QtNetwork/QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QXmlStreamReader>
#include <QSslKey>
#include <QImageReader>
#include <QtEndian>
#include <QNetworkProxy>

#define FAST_FAIL_TIMEOUT_MS 2000
#define REQUEST_TIMEOUT_MS 5000
#define LAUNCH_TIMEOUT_MS 120000
#define RESUME_TIMEOUT_MS 30000
#define QUIT_TIMEOUT_MS 30000

NvHTTP::NvHTTP(NvAddress address, uint16_t httpsPort, QSslCertificate serverCert, bool useTrueUid, QNetworkAccessManager* nam) :
    m_Nam(nam ? nam : new QNetworkAccessManager(this)),
    m_ServerCert(serverCert),
    m_UseTrueUid(useTrueUid)
{
    m_BaseUrlHttp.setScheme("http");
    m_BaseUrlHttps.setScheme("https");

    setAddress(address);
    setHttpsPort(httpsPort);

    // Never use a proxy server
    QNetworkProxy noProxy(QNetworkProxy::NoProxy);
    m_Nam->setProxy(noProxy);
}

NvHTTP::NvHTTP(NvComputer* computer, QNetworkAccessManager* nam) :
    NvHTTP(computer->activeAddress, computer->activeHttpsPort, computer->serverCert, !computer->isNvidiaServerSoftware, nam)
{
}

void NvHTTP::setServerCert(QSslCertificate serverCert)
{
    m_ServerCert = serverCert;
}

void NvHTTP::setAddress(NvAddress address)
{
    Q_ASSERT(!address.isNull());

    m_Address = address;

    m_BaseUrlHttp.setHost(address.address());
    m_BaseUrlHttps.setHost(address.address());

    m_BaseUrlHttp.setPort(address.port());
}

void NvHTTP::setHttpsPort(uint16_t port)
{
    m_BaseUrlHttps.setPort(port);
}

void NvHTTP::setTrueUid(bool useTrueUid)
{
    m_UseTrueUid = useTrueUid;
}

NvAddress NvHTTP::address()
{
    return m_Address;
}

QSslCertificate NvHTTP::serverCert()
{
    return m_ServerCert;
}

uint16_t NvHTTP::httpPort()
{
    return m_BaseUrlHttp.port();
}

uint16_t NvHTTP::httpsPort()
{
    return m_BaseUrlHttps.port();
}

QVector<int>
NvHTTP::parseQuad(QString quad)
{
    QVector<int> ret;

    // Return an empty vector for old GFE versions
    // that were missing GfeVersion.
    if (quad.isEmpty()) {
        return ret;
    }

    QStringList parts = quad.split(".");
    ret.reserve(parts.length());
    for (int i = 0; i < parts.length(); i++)
    {
        ret.append(parts.at(i).toInt());
    }

    return ret;
}

int
NvHTTP::getCurrentGame(QString serverInfo)
{
    // GFE 2.8 started keeping currentgame set to the last game played. As a result, it no longer
    // has the semantics that its name would indicate. To contain the effects of this change as much
    // as possible, we'll force the current game to zero if the server isn't in a streaming session.
    QString serverState = getXmlString(serverInfo, "state");
    if (serverState.endsWith("_SERVER_BUSY"))
    {
        return getXmlString(serverInfo, "currentgame").toInt();
    }
    else
    {
        return 0;
    }
}

QString
NvHTTP::getServerInfo(NvLogLevel logLevel, bool fastFail)
{
    QString serverInfo;

    // Check if we have a pinned cert and HTTPS port for this host yet
    if (!m_ServerCert.isNull() && httpsPort() != 0)
    {
        try
        {
            // Always try HTTPS first, since it properly reports
            // pairing status (and a few other attributes).
            serverInfo = openConnectionToString(m_BaseUrlHttps,
                                                "serverinfo",
                                                nullptr,
                                                fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS,
                                                logLevel);
            // Throws if the request failed
            verifyResponseStatus(serverInfo);
        }
        catch (const GfeHttpResponseException& e)
        {
            if (e.getStatusCode() == 401)
            {
                // Certificate validation error, fallback to HTTP
                serverInfo = openConnectionToString(m_BaseUrlHttp,
                                                    "serverinfo",
                                                    nullptr,
                                                    fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS,
                                                    logLevel);
                verifyResponseStatus(serverInfo);
            }
            else
            {
                // Rethrow real errors
                throw e;
            }
        }
    }
    else
    {
        // Only use HTTP prior to pairing or fetching HTTPS port
        serverInfo = openConnectionToString(m_BaseUrlHttp,
                                            "serverinfo",
                                            nullptr,
                                            fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS,
                                            logLevel);
        verifyResponseStatus(serverInfo);

        // Populate the HTTPS port
        uint16_t httpsPort = getXmlString(serverInfo, "HttpsPort").toUShort();
        if (httpsPort == 0) {
            httpsPort = DEFAULT_HTTPS_PORT;
        }
        setHttpsPort(httpsPort);

        // If we just needed to determine the HTTPS port, we'll try again over
        // HTTPS now that we have the port number
        if (!m_ServerCert.isNull()) {
            return getServerInfo(logLevel, fastFail);
        }
    }

    return serverInfo;
}

// PyroWave Test (7.0.0): verbatim from Nonary's tree at f96b0524.
PyroWaveLink::Result NvHTTP::probePyroWaveUdp(int kbps, int packetSize, const std::atomic<bool>& cancelled,
                                           bool useHandshake, int burstFps, int paceKbps)
{
    if (m_ServerCert.isNull() || httpsPort() == 0 || kbps < 5000 || kbps > 3000000 ||
        packetSize < 256 || packetSize > 1392 ||
        (burstFps && (burstFps < 10 || burstFps > 500 || paceKbps < kbps || paceKbps > 9999999))) {
        throw std::runtime_error("Invalid or unauthenticated PyroWave UDP probe");
    }
    QUdpSocket socket;
    const QHostAddress host = PyroWaveUdp::bindReceiver(socket, m_BaseUrlHttps.host(), cancelled);
    QUrl probeUrl = m_BaseUrlHttps;
    probeUrl.setHost(host.toString());
    socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024);
    const QByteArray token = QUuid::createUuid().toRfc4122().toHex();
    PyroWaveUdp::ProbeHandshake handshake(socket, host, token, cancelled);
    PyroWaveLink::Result result;
    result.requestedKbps = kbps;
    result.expected = uint64_t(kbps) * PyroWaveLink::durationMs / (8 * (packetSize + 134));
    std::vector<int64_t> arrivals(result.expected, -1);
    std::vector<double> readDelays;
    readDelays.reserve(result.expected);
    PyroWaveUdp::ReceiverTiming timing(socket);
    result.kernelArrivalTimestamps = timing.usesKernelTimestamps();
    bool missingTimestamp = false;
    quint64 authenticatedPackets = 0;
    const auto receive = [&] {
        char packet[2048];
        // Bound a callback so HTTP completion/timeouts cannot starve if other
        // traffic arrives continuously. Cancellation still drains without grading.
        for (int count = 0; count < 4096 && socket.hasPendingDatagrams(); ++count) {
            QHostAddress source;
            quint16 sourcePort;
            qint64 arrivalUs;
            const auto bytes = timing.readDatagram(packet, sizeof(packet), &source, arrivalUs, &sourcePort);
            if (bytes < 0) break;
            if (bytes != packetSize + 48 ||
                (useHandshake ? !handshake.acceptsSource(source, sourcePort) : source != host) ||
                memcmp(packet, token.constData(), 32) != 0 || cancelled.load()) continue;
            handshake.receivedPacket();
            ++authenticatedPackets;
            const auto seq = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packet + 32));
            if (seq < arrivals.size() && arrivals[seq] < 0) {
                if (arrivalUs < 0) { missingTimestamp = true; continue; }
                arrivals[seq] = arrivalUs;
                readDelays.push_back(timing.lastReadDelayUs() / 1000.0);
            }
        }
    };
    connect(&socket, &QUdpSocket::readyRead, &socket, receive);
    QTimer poll;
    connect(&poll, &QTimer::timeout, &socket, receive);
    poll.start(1);
    // ReadyRead continues draining UDP inside openConnection's event loop while
    // the host sends; the reliable response gives the count including lost tails.
    const std::function<QString(QNetworkReply*)> receiveHeaders = useHandshake ?
        std::function<QString(QNetworkReply*)>([&](QNetworkReply* response) {
            handshake.start(response->rawHeader("X-PyroWave-Udp-Port"));
            return handshake.errorString();
        }) : std::function<QString(QNetworkReply*)>();
    QNetworkReply* networkReply;
    try {
        networkReply = openConnection(probeUrl, "pyrowave-udp-probe",
            (QString("kbps=%1&port=%2&packetsize=%3&token=%4") + (useHandshake ? "&handshake=1" : "") +
             (burstFps ? QString("&fps=%1&pacekbps=%2").arg(burstFps).arg(paceKbps) : QString()))
                .arg(kbps).arg(socket.localPort()).arg(packetSize).arg(QString::fromLatin1(token)),
            6000, NVLL_ERROR, receiveHeaders);
    }
    catch (const QtNetworkReplyException& error) {
        if (error.getError() != QNetworkReply::TimeoutError || cancelled.load()) throw;
        const QString detail = useHandshake ? handshake.timeoutDescription(authenticatedPackets) :
            tr("Legacy probe to %1, receiver port %2, authenticated packets %3")
                .arg(host.toString()).arg(socket.localPort()).arg(authenticatedPackets);
        const QString message = tr("PyroWave UDP test timed out after 6000 ms: %1. No bandwidth recommendation is available.").arg(detail);
        qWarning().noquote() << message;
        throw QtNetworkReplyException(QNetworkReply::TimeoutError, message);
    }
    const QString reply = QString::fromUtf8(networkReply->readAll());
    delete networkReply;
    if (useHandshake && !handshake.started()) {
        throw std::runtime_error("Update Vibeshine to support the PyroWave UDP handshake");
    }
    if (!handshake.errorString().isEmpty()) throw std::runtime_error(handshake.errorString().toStdString());
    verifyResponseStatus(reply);
    bool validExpected = false, validSent = false, validElapsed = false;
    const auto expected = getXmlString(reply, "expected").toUInt(&validExpected);
    result.sent = getXmlString(reply, "sent").toUInt(&validSent);
    result.senderMs = getXmlString(reply, "elapsedMs").toDouble(&validElapsed);
    if (!validExpected || expected != result.expected || !validSent || result.sent > expected ||
        !validElapsed || !std::isfinite(result.senderMs)) {
        throw std::runtime_error("Invalid PyroWave UDP probe result");
    }
    // Optional host send diagnostics; older hosts omit them.
    bool validRetries = false, validSendError = false;
    const auto retries = getXmlString(reply, "sendRetries").toUInt(&validRetries);
    const auto sendError = getXmlString(reply, "lastSendError").toInt(&validSendError);
    if (validRetries) result.hostSendRetries = retries;
    if (validSendError) result.hostLastSendError = sendError;
    // Allow a bounded reordered tail; its delay is still scored against the
    // sender schedule, never forgiven as good throughput.
    QEventLoop drain;
    QTimer::singleShot(100, &drain, &QEventLoop::quit);
    drain.exec();
    receive();
    if (missingTimestamp) {
        throw std::runtime_error("PyroWave UDP packets arrived without valid kernel timestamps; calibration cannot grade delivery timing");
    }
    // Burst-v1 uses this exact wire charge for its 1 ms groups. Keep the
    // final group's holes visible even when its last packet arrived.
    const int burstWireBytes = packetSize + 48 + 8 +
        (host.protocol() == QAbstractSocket::IPv6Protocol ? 40 : 20) + 38;
    const uint32_t burstGroupPackets = (std::max)(1, paceKbps / 8 / burstWireBytes);
    PyroWaveLink::summarize(result, arrivals, burstFps, burstGroupPackets);
    if (!readDelays.empty()) {
        std::sort(readDelays.begin(), readDelays.end());
        result.receiverReadDelayP99Ms = readDelays[(readDelays.size() - 1) * 99 / 100];
    }
    return result;
}

void
NvHTTP::startApp(QString verb,
                 bool isGfe,
                 int appId,
                 PSTREAM_CONFIGURATION streamConfig,
                 bool sops,
                 bool localAudio,
                 int gamepadMask,
                 bool persistGameControllersOnDisconnect,
                 bool clientVrrRequested,
                 QString& rtspSessionUrl,
                 bool& virtualDisplayReady)
{
    int riKeyId;

    memcpy(&riKeyId, streamConfig->remoteInputAesIv, sizeof(riKeyId));
    riKeyId = qFromBigEndian(riKeyId);

    QString response =
            openConnectionToString(m_BaseUrlHttps,
                                   verb,
                                   "appid="+QString::number(appId)+
                                   "&mode="+QString::number(streamConfig->width)+"x"+
                                   QString::number(streamConfig->height)+"x"+
                                   // Using an FPS value over 60 causes SOPS to default to 720p60,
                                   // so force it to 0 to ensure the correct resolution is set. We
                                   // used to use 60 here but that locked the frame rate to 60 FPS
                                   // on GFE 3.20.3. We don't need this hack for Sunshine.
                                   QString::number((streamConfig->fps > 60 && isGfe) ? 0 : streamConfig->fps)+
                                   "&additionalStates=1&sops="+QString::number(sops ? 1 : 0)+
                                   "&rikey="+QByteArray(streamConfig->remoteInputAesKey, sizeof(streamConfig->remoteInputAesKey)).toHex()+
                                   "&rikeyid="+QString::number(riKeyId)+
                                   ((streamConfig->supportedVideoFormats & VIDEO_FORMAT_MASK_10BIT) ?
                                       "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0" :
                                        "")+
                                   "&localAudioPlayMode="+QString::number(localAudio ? 1 : 0)+
                                   "&surroundAudioInfo="+QString::number(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(streamConfig->audioConfiguration))+
                                   "&remoteControllersBitmap="+QString::number(gamepadMask)+
                                   "&gcmap="+QString::number(gamepadMask)+
                                   "&gcpersist="+QString::number(persistGameControllersOnDisconnect ? 1 : 0)+
                                   // Tells VRR-aware hosts that this client paces playback from
                                   // RTP timestamps, so they can capture with precise frame timing
                                   // (Vibeshine: 1000 Hz virtual display in its Automatic mode).
                                   // Other hosts ignore unknown launch parameters.
                                   (clientVrrRequested ? "&clientVrrRequested=1" : "")+
                                   LiGetLaunchUrlQueryParameters(),
                                   LAUNCH_TIMEOUT_MS);

    qInfo() << "Launch response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);

    rtspSessionUrl = getXmlString(response, "sessionUrl0");

    // Hosts that capture a virtual display (Vibeshine, Apollo, Sunshine with the driver)
    // announce it here. GFE and hosts without it simply omit the element, which reads as
    // false — so the flag only ever narrows a later diagnosis, it never invents one.
    virtualDisplayReady = getXmlString(response, "VirtualDisplayDriverReady")
                              .compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

void
NvHTTP::quitApp()
{
    QString response =
            openConnectionToString(m_BaseUrlHttps,
                                   "cancel",
                                   nullptr,
                                   QUIT_TIMEOUT_MS);

    qInfo() << "Quit response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);

    // Newer GFE versions will just return success even if quitting fails
    // if we're not the original requester.
    if (getCurrentGame(getServerInfo(NvHTTP::NVLL_ERROR)) != 0) {
        // Generate a synthetic GfeResponseException letting the caller know
        // that they can't kill someone else's stream.
        throw GfeHttpResponseException(599, "");
    }
}

QVector<NvDisplayMode>
NvHTTP::getDisplayModeList(QString serverInfo)
{
    QXmlStreamReader xmlReader(serverInfo);
    QVector<NvDisplayMode> modes;

    while (!xmlReader.atEnd()) {
        while (xmlReader.readNextStartElement()) {
            auto name = xmlReader.name();
            if (name == QString("DisplayMode")) {
                modes.append(NvDisplayMode());
            }
            else if (!modes.isEmpty()) {
                if (name == QString("Width")) {
                    modes.last().width = xmlReader.readElementText().toInt();
                }
                else if (name == QString("Height")) {
                    modes.last().height = xmlReader.readElementText().toInt();
                }
                else if (name == QString("RefreshRate")) {
                    modes.last().refreshRate = xmlReader.readElementText().toInt();
                }
            }
        }
    }

    return modes;
}

QVector<NvApp>
NvHTTP::getAppList()
{
    QString appxml = openConnectionToString(m_BaseUrlHttps,
                                            "applist",
                                            nullptr,
                                            REQUEST_TIMEOUT_MS,
                                            NvLogLevel::NVLL_ERROR);
    verifyResponseStatus(appxml);

    QXmlStreamReader xmlReader(appxml);
    QVector<NvApp> apps;
    while (!xmlReader.atEnd()) {
        while (xmlReader.readNextStartElement()) {
            auto name = xmlReader.name();
            if (name == QString("App")) {
                // We must have a valid app before advancing to the next one
                if (!apps.isEmpty() && !apps.last().isInitialized()) {
                    qWarning() << "Invalid applist XML";
                    throw std::runtime_error("Invalid applist XML");
                }
                apps.append(NvApp());
            }
            else if (!apps.isEmpty()) {
                if (name == QString("AppTitle")) {
                    // If an app has no name, Sunshine may send us <AppTitle/>,
                    // which readElementText() returns as a null QString.
                    // We want to treat this as an empty QString instead, so we
                    // will explicitly convert it. An empty string will satisfy
                    // NvApp's isInitialized() check.
                    QString title = xmlReader.readElementText();
                    if (title.isNull()) {
                        title = "";
                    }
                    apps.last().name = title;
                }
                else if (name == QString("ID")) {
                    apps.last().id = xmlReader.readElementText().toInt();
                }
                else if (name == QString("UUID")) {
                    apps.last().uuid = xmlReader.readElementText();
                }
                else if (name == QString("IsHdrSupported")) {
                    apps.last().hdrSupported = xmlReader.readElementText() == "1";
                }
                else if (name == QString("IsAppCollectorGame")) {
                    apps.last().isAppCollectorGame = xmlReader.readElementText() == "1";
                }
            }
        }
    }

    // Vibeshine / Vibepollo 2.0 pad their control titles with leading spaces so they sort
    // first in a client that alphabetises the list. We sort and group those ourselves, so the
    // padding would only show up as a crooked label. Ordinary apps keep their name untouched:
    // it is the key play time and the "last played" record are stored under.
    for (NvApp& app : apps) {
        if (hostControlKind(app.id, app.uuid, app.name) != HostControl::None) {
            app.name = app.name.trimmed();
        }
    }

    return apps;
}

void
NvHTTP::verifyResponseStatus(QString xml)
{
    QXmlStreamReader xmlReader(xml);

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name() == QString("root"))
        {
            // Status code can be 0xFFFFFFFF in some rare cases on GFE 3.20.3, and
            // QString::toInt() will fail in that case, so use QString::toUInt()
            // and cast the result to an int instead.
            int statusCode = (int)xmlReader.attributes().value("status_code").toUInt();
            if (statusCode == 200)
            {
                // Successful
                return;
            }
            else
            {
                QString statusMessage = xmlReader.attributes().value("status_message").toString();
                if (statusCode != 401) {
                    // 401 is expected for unpaired PCs when we fetch serverinfo over HTTPS
                    qWarning() << "Request failed:" << statusCode << statusMessage;
                }
                if (statusCode == -1 && statusMessage == "Invalid") {
                    // Special case handling an audio capture error which GFE doesn't
                    // provide any useful status message for.
                    statusCode = 418;
                    statusMessage = tr("Missing audio capture device. Reinstalling GeForce Experience should resolve this error.");
                }
                throw GfeHttpResponseException(statusCode, statusMessage);
            }
        }
    }

    throw GfeHttpResponseException(-1, "Malformed XML (missing root element)");
}

QImage
NvHTTP::getBoxArt(int appId)
{
    QNetworkReply* reply = openConnection(m_BaseUrlHttps,
                                          "appasset",
                                          "appid="+QString::number(appId)+
                                          "&AssetType=2&AssetIdx=0",
                                          REQUEST_TIMEOUT_MS,
                                          NvLogLevel::NVLL_VERBOSE);
    QImage image = QImageReader(reply).read();
    delete reply;

    return image;
}

QByteArray
NvHTTP::getXmlStringFromHex(QString xml,
                            QString tagName)
{
    return QByteArray::fromHex(getXmlString(xml, tagName).toUtf8());
}

QString
NvHTTP::getXmlString(QString xml,
                     QString tagName)
{
    QXmlStreamReader xmlReader(xml);

    while (!xmlReader.atEnd())
    {
        if (xmlReader.readNext() != QXmlStreamReader::StartElement)
        {
            continue;
        }

        if (xmlReader.name() == tagName)
        {
            return xmlReader.readElementText();
        }
    }

    return QString();
}

void NvHTTP::handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors)
{
    bool ignoreErrors = true;

    if (m_ServerCert.isNull()) {
        // We should never make an HTTPS request without a cert
        Q_ASSERT(!m_ServerCert.isNull());
        return;
    }

    for (const QSslError& error : errors) {
        if (m_ServerCert != error.certificate()) {
            ignoreErrors = false;
            break;
        }
    }

    if (ignoreErrors) {
        reply->ignoreSslErrors(errors);
    }
}

QString
NvHTTP::openConnectionToString(QUrl baseUrl,
                               QString command,
                               QString arguments,
                               int timeoutMs,
                               NvLogLevel logLevel)
{
    QNetworkReply* reply = openConnection(baseUrl, command, arguments, timeoutMs, logLevel);
    QString ret;

    QTextStream stream(reply);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif

    ret = stream.readAll();
    delete reply;

    return ret;
}

QNetworkReply*
NvHTTP::openConnection(QUrl baseUrl,
                       QString command,
                       QString arguments,
                       int timeoutMs,
                       NvLogLevel logLevel,
                       const std::function<QString(QNetworkReply*)>& receiveHeaders)
{
    // The receiveHeaders hook and the "pyrowave-udp-probe" branches below are the PyroWave Test's
    // (7.0.0), as in Nonary's tree at f96b0524. Every other request runs exactly as before.

    // Port must be set
    Q_ASSERT(baseUrl.port(0) != 0);

    // Build a URL for the request
    QUrl url(baseUrl);
    url.setPath("/" + command);

    // Use a placeholder UID for GFE allow them to quit games for each other.
    url.setQuery("uniqueid=" + (m_UseTrueUid ? IdentityManager::get()->getUniqueId() : "0123456789ABCDEF") +
                 "&uuid=" + QUuid::createUuid().toRfc4122().toHex() +
                 ((arguments != nullptr) ? ("&" + arguments) : ""));

    QNetworkRequest request(url);

    // Add our client certificate
    request.setSslConfiguration(IdentityManager::get()->getSslConfig());

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Disable HTTP/2 (GFE 3.22 doesn't like it) and Qt 6 enables it by default
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    // Use fine-grained idle timeouts to avoid calling QNetworkAccessManager::clearAccessCache(),
    // which tears down the NAM's global thread each time. We must not keep persistent connections
    // or GFE will puke.
    request.setAttribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute, 0);
#endif

    auto sslErrorsConnection = connect(m_Nam, &QNetworkAccessManager::sslErrors, this, &NvHTTP::handleSslErrors);
    QNetworkReply* reply = m_Nam->get(request);

    // Run the request with a timeout if requested
    QEventLoop loop;
    QString headerError;
    bool completeProbeBody = false;
    if (command == "pyrowave-udp-probe") {
        connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            completeProbeBody = PyroWaveUdp::completeCloseDelimitedResponse(*reply);
            if (completeProbeBody) loop.quit();
        });
    }
    if (receiveHeaders) {
        connect(reply, &QNetworkReply::metaDataChanged, &loop, [&] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 200 && status < 300) {
                headerError = receiveHeaders(reply);
                if (!headerError.isEmpty()) reply->abort();
            }
        });
    }
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
    if (timeoutMs) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }
    if (logLevel >= NvLogLevel::NVLL_VERBOSE) {
        qInfo() << "Executing request:" << url.toString();
    }
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    // Abort the request if it timed out
    if (!reply->isFinished() && !completeProbeBody)
    {
        if (logLevel >= NvLogLevel::NVLL_ERROR) {
            qWarning() << "Aborting timed out request for" << url.toString();
            if (command == "pyrowave-udp-probe") {
                qWarning() << "PyroWave probe HTTP progress: status"
                           << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                           << "body bytes" << reply->bytesAvailable()
                           << "content length" << reply->rawHeader("Content-Length")
                           << "transfer encoding" << reply->rawHeader("Transfer-Encoding")
                           << "connection" << reply->rawHeader("Connection");
            }
        }
        reply->abort();
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    // If we couldn't use fine-grained connection idle timeouts, kill them all now
    m_Nam->clearAccessCache();
#endif
    disconnect(sslErrorsConnection);

    if (!headerError.isEmpty()) {
        delete reply;
        throw std::runtime_error(headerError.toStdString());
    }

    // Handle error
    if (reply->error() != QNetworkReply::NoError)
    {
        // This authenticated endpoint returns a plain-text explanation for
        // rejected tests (for example, when a streaming session is active).
        // Preserve it instead of Qt's generic "Bad Request" error/URL.
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (command == "pyrowave-udp-probe" && httpStatus >= 400) {
            QString reason = QString::fromUtf8(reply->read(1024)).trimmed();
            if (reason.isEmpty()) {
                reason = tr("The host rejected the bandwidth test (HTTP %1).").arg(httpStatus);
            }
            qWarning() << "PyroWave UDP test rejected:" << httpStatus << reason;
            GfeHttpResponseException exception(httpStatus,
                tr("PyroWave UDP test: %1").arg(reason));
            delete reply;
            throw exception;
        }
        if (logLevel >= NvLogLevel::NVLL_ERROR) {
            qWarning() << command << "request failed with error:" << reply->error();
        }

        if (reply->error() == QNetworkReply::SslHandshakeFailedError) {
            // This will trigger falling back to HTTP for the serverinfo query
            // then pairing again to get the updated certificate.
            GfeHttpResponseException exception(401, "Server certificate mismatch");
            delete reply;
            throw exception;
        }
        else if (reply->error() == QNetworkReply::OperationCanceledError) {
            QtNetworkReplyException exception(QNetworkReply::TimeoutError, "Request timed out");
            delete reply;
            throw exception;
        }
        else {
            QtNetworkReplyException exception(reply->error(), reply->errorString());
            delete reply;
            throw exception;
        }
    }

    // A complete close-delimited probe body may precede HTTP EOF on older
    // handshake hosts. The caller reads/validates it and deletes the reply,
    // closing that connection; all ordinary requests still require finished().
    return reply;
}
