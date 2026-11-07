#include "release.h"
#include "common.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>

namespace up {

namespace {

const wchar_t* const k_Host = L"api.github.com";
const wchar_t* const k_Path = L"/repos/FoggyBytes/StreamLight/releases/latest";
constexpr qint64 k_MaxBody = 1024 * 1024;

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};

/*
 * WinHTTP has already checked the certificate the ordinary way, against every root this user
 * trusts. This builds the chain again with only the machine's roots (Root and AuthRoot, which
 * only administrators change): a root the user — or something running as the user — added to
 * their own store must not be able to stand in for GitHub in front of an elevated process.
 */
bool chainsToMachineRoot(PCCERT_CONTEXT cert)
{
    HCERTSTORE roots = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr);
    if (!roots) return false;
    for (const wchar_t* name : { L"ROOT", L"AuthRoot" }) {
        HCERTSTORE s = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                     CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG
                                     | CERT_STORE_OPEN_EXISTING_FLAG, name);
        if (s) {
            CertAddStoreToCollection(roots, s, 0, 0);
            CertCloseStore(s, 0);
        }
    }

    CERT_CHAIN_ENGINE_CONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hExclusiveRoot = roots;
    HCERTCHAINENGINE engine = nullptr;
    bool ok = false;
    if (CertCreateCertificateChainEngine(&cfg, &engine)) {
        LPSTR usage[] = { const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH) };
        CERT_CHAIN_PARA para = {};
        para.cbSize = sizeof(para);
        para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        para.RequestedUsage.Usage.cUsageIdentifier = 1;
        para.RequestedUsage.Usage.rgpszUsageIdentifier = usage;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        if (CertGetCertificateChain(engine, cert, nullptr, cert->hCertStore, &para, 0, nullptr, &chain)) {
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {};
            ssl.cbSize = sizeof(ssl);
            ssl.dwAuthType = AUTHTYPE_SERVER;
            ssl.pwszServerName = const_cast<wchar_t*>(k_Host);
            CERT_CHAIN_POLICY_PARA policy = {};
            policy.cbSize = sizeof(policy);
            policy.pvExtraPolicyPara = &ssl;
            CERT_CHAIN_POLICY_STATUS status = {};
            status.cbSize = sizeof(status);
            ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)
                    && status.dwError == 0;
            if (!ok) log(QStringLiteral("certificate: machine chain refused (0x%1)")
                         .arg(quint32(status.dwError), 8, 16, QLatin1Char('0')));
            CertFreeCertificateChain(chain);
        }
        CertFreeCertificateChainEngine(engine);
    }
    CertCloseStore(roots, 0);
    return ok;
}

bool get(QByteArray& body)
{
    Handle session(WinHttpOpen(L"StreamLight-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { log(QStringLiteral("http: open failed %1").arg(GetLastError())); return false; }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    Handle connect(WinHttpConnect(session, k_Host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect) { log(QStringLiteral("http: connect failed %1").arg(GetLastError())); return false; }
    Handle request(WinHttpOpenRequest(connect, L"GET", k_Path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) { log(QStringLiteral("http: request failed %1").arg(GetLastError())); return false; }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request, nullptr)) {
        log(QStringLiteral("http: send/receive failed %1").arg(GetLastError()));
        return false;
    }

    PCCERT_CONTEXT cert = nullptr;
    DWORD size = sizeof(cert);
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &size) || !cert) {
        log(QStringLiteral("http: no server certificate"));
        return false;
    }
    const bool trusted = chainsToMachineRoot(cert);
    CertFreeCertificateContext(cert);
    if (!trusted) return false;

    DWORD code = 0;
    size = sizeof(code);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    if (code != 200) { log(QStringLiteral("http: status %1").arg(code)); return false; }

    body.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) return false;
        if (available == 0) break;
        if (body.size() + qint64(available) > k_MaxBody) { log(QStringLiteral("http: reply too large")); return false; }
        const qsizetype at = body.size();
        body.resize(at + qsizetype(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + at, available, &read)) return false;
        body.resize(at + qsizetype(read));
    }
    return true;
}

} // namespace

bool fetchLatestRelease(Release& out, QString* reason)
{
    QByteArray body;
    if (!get(body)) { if (reason) *reason = QStringLiteral("network"); return false; }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        log(QStringLiteral("release: not JSON"));
        if (reason) *reason = QStringLiteral("release");
        return false;
    }
    const QJsonObject obj = doc.object();
    out.tag = obj.value(QStringLiteral("tag_name")).toString();
    const QList<int> tagVersion = parseVersion(out.tag);
    if (tagVersion.isEmpty()) {
        log(QStringLiteral("release: unreadable tag"));
        if (reason) *reason = QStringLiteral("release");
        return false;
    }
    out.version = out.tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? out.tag.mid(1) : out.tag;

    static const QRegularExpression pattern(QStringLiteral("^StreamLight_(\\d+(?:\\.\\d+){1,3})_Installer\\.exe$"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    for (const QJsonValue& v : obj.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QRegularExpressionMatch m = pattern.match(name);
        if (!m.hasMatch() || parseVersion(m.captured(1)) != tagVersion) continue;
        const QString digest = asset.value(QStringLiteral("digest")).toString();
        if (!digest.startsWith(QLatin1String("sha256:"), Qt::CaseInsensitive)) break;
        const QString sha = digest.mid(7).trimmed().toLower();
        if (!hex.match(sha).hasMatch()) break;
        out.assetName = name;
        out.sha256 = sha.toLatin1();
        log(QStringLiteral("release %1, asset %2").arg(out.tag, out.assetName));
        return true;
    }
    log(QStringLiteral("release %1: no installer with a digest").arg(out.tag));
    if (reason) *reason = QStringLiteral("release");
    return false;
}

} // namespace up
