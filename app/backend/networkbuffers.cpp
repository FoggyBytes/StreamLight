// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "networkbuffers.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QNetworkInterface>
#include <QUdpSocket>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>

#include <thread>
#include <vector>
#endif

namespace {

// moonlight-common-c asks for RTP_RECV_PACKETS_BUFFERED_PYROWAVE (8192)
// packets of packetSize + MAX_RTP_HEADER_SIZE (1392 + 16) bytes.
constexpr qint64 k_RequiredBytes = 8192LL * (1392 + 16);
// Headroom above the request, so a larger packet size still fits
constexpr qint64 k_RecommendedBytes = 32LL * 1024 * 1024;

const char* const k_ConfFile = "/etc/sysctl.d/60-moonlight-pyrowave.conf";

qint64 readRmemMax()
{
#ifdef Q_OS_LINUX
    QFile file(QStringLiteral("/proc/sys/net/core/rmem_max"));
    if (file.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const qint64 value = file.readAll().trimmed().toLongLong(&ok);
        if (ok) {
            return value;
        }
    }
#endif
    return 0;
}

QString rootScript()
{
    return QStringLiteral("sysctl -w net.core.rmem_max=%1 && printf 'net.core.rmem_max = %1\\n' > %2")
        .arg(k_RecommendedBytes).arg(QString::fromLatin1(k_ConfFile));
}

// pkexec runs on the host, so a containerized build reaches it through the
// container's host bridge. Flatpak builds have no such permission.
QStringList applyCommand()
{
#ifdef Q_OS_LINUX
    if (QFile::exists(QStringLiteral("/.flatpak-info"))) {
        return {};
    }
    QStringList command = { QStringLiteral("pkexec"), QStringLiteral("sh"), QStringLiteral("-c"), rootScript() };
    const bool container = QFile::exists(QStringLiteral("/run/.containerenv")) ||
                           !qEnvironmentVariableIsEmpty("CONTAINER_ID");
    if (container) {
        for (const auto& bridge : { QStringLiteral("distrobox-host-exec"), QStringLiteral("host-spawn") }) {
            if (!QStandardPaths::findExecutable(bridge).isEmpty()) {
                command.prepend(bridge);
                return command;
            }
        }
        if (!QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn")).isEmpty()) {
            command.prepend(QStringLiteral("--host"));
            command.prepend(QStringLiteral("flatpak-spawn"));
            return command;
        }
        return {};
    }
    if (QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()) {
        return {};
    }
    return command;
#else
    return {};
#endif
}

#ifdef Q_OS_WIN
// Settings that size an adapter's receive descriptor ring: the standard NDIS
// keyword, and the Realtek USB driver's ring length and in-flight USB
// transfers. A ring below 256 descriptors overflows on a PyroWave burst.
const wchar_t* const k_ReceiveKeywords[] = { L"*ReceiveBuffers", L"ReceiveBufferLen", L"PendingReceives" };
constexpr int k_MinimumReceiveDescriptors = 256;
// Raise to the driver's maximum, but not to absurd rings some drivers allow.
constexpr int k_MaximumReceiveDescriptors = 2048;
const wchar_t* const k_NetClassKey =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";

struct ReceiveSetting {
    QString adapterName;
    QString adapterDescription;
    QString keyword;
    QString displayName;
    int current = 0;
    int maximum = 0;
    int target = 0;
};

bool readRegistryInt(HKEY key, const wchar_t* subKey, const wchar_t* name, int& value)
{
    wchar_t text[64] = {};
    DWORD size = sizeof(text);
    DWORD type = 0;
    if (RegGetValueW(key, subKey, name, RRF_RT_REG_SZ | RRF_RT_REG_DWORD, &type, text, &size) != ERROR_SUCCESS) {
        return false;
    }
    if (type == REG_DWORD) {
        value = int(*reinterpret_cast<DWORD*>(text));
        return true;
    }
    bool ok = false;
    value = QString::fromWCharArray(text).trimmed().toInt(&ok);
    return ok;
}

QString readRegistryString(HKEY key, const wchar_t* subKey, const wchar_t* name)
{
    wchar_t text[256] = {};
    DWORD size = sizeof(text);
    if (RegGetValueW(key, subKey, name, RRF_RT_REG_SZ, nullptr, text, &size) != ERROR_SUCCESS) {
        return {};
    }
    return QString::fromWCharArray(text);
}

// The adapter's driver key under the network class, found by its interface GUID.
HKEY openAdapterKey(const QString& interfaceGuid)
{
    HKEY classKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k_NetClassKey, 0, KEY_READ, &classKey) != ERROR_SUCCESS) {
        return nullptr;
    }
    HKEY found = nullptr;
    for (DWORD index = 0; found == nullptr; ++index) {
        wchar_t name[64] = {};
        DWORD nameLength = ARRAYSIZE(name);
        if (RegEnumKeyExW(classKey, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        HKEY adapterKey = nullptr;
        if (RegOpenKeyExW(classKey, name, 0, KEY_READ, &adapterKey) != ERROR_SUCCESS) {
            continue;
        }
        if (readRegistryString(adapterKey, nullptr, L"NetCfgInstanceId").compare(interfaceGuid, Qt::CaseInsensitive) == 0) {
            found = adapterKey;
        }
        else {
            RegCloseKey(adapterKey);
        }
    }
    RegCloseKey(classKey);
    return found;
}

std::vector<ReceiveSetting> scanReceiveSettings()
{
    std::vector<ReceiveSetting> settings;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_UNSPEC,
                                      GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST |
                                      GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
    }
    if (result != NO_ERROR) {
        return settings;
    }
    for (auto adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); adapter != nullptr; adapter = adapter->Next) {
        // Wired adapters in use. Wi-Fi rarely exposes these rings, and PyroWave
        // bitrates need a wired link anyway.
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType != IF_TYPE_ETHERNET_CSMACD) {
            continue;
        }
        const HKEY adapterKey = openAdapterKey(QString::fromLatin1(adapter->AdapterName));
        if (adapterKey == nullptr) {
            continue;
        }
        for (const wchar_t* keyword : k_ReceiveKeywords) {
            const std::wstring params = std::wstring(L"Ndi\\Params\\") + keyword;
            ReceiveSetting setting;
            if (!readRegistryInt(adapterKey, params.c_str(), L"max", setting.maximum)) {
                continue;
            }
            // An absent value means the driver's default is in effect.
            if (!readRegistryInt(adapterKey, nullptr, keyword, setting.current) &&
                    !readRegistryInt(adapterKey, params.c_str(), L"default", setting.current)) {
                continue;
            }
            if (setting.current >= qMin(setting.maximum, k_MinimumReceiveDescriptors)) {
                continue;
            }
            setting.adapterName = QString::fromWCharArray(adapter->FriendlyName);
            setting.adapterDescription = QString::fromWCharArray(adapter->Description);
            setting.keyword = QString::fromWCharArray(keyword);
            const QString description = readRegistryString(adapterKey, params.c_str(), L"ParamDesc");
            // Indirect "@driver.inf,..." strings need a resource lookup; the keyword reads fine.
            setting.displayName = description.isEmpty() || description.startsWith(QLatin1Char('@')) ?
                setting.keyword : description;
            setting.target = qMin(setting.maximum, k_MaximumReceiveDescriptors);
            settings.push_back(setting);
        }
        RegCloseKey(adapterKey);
    }
    return settings;
}

QString quotePowerShell(const QString& value)
{
    return QLatin1Char('\'') + QString(value).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
}

// Sets every flagged value without a restart, then restarts each adapter once.
QString windowsScript(const std::vector<ReceiveSetting>& settings)
{
    QStringList lines = { QStringLiteral("$ErrorActionPreference = 'Stop'") };
    QStringList adapters;
    for (const auto& setting : settings) {
        lines << QStringLiteral("Set-NetAdapterAdvancedProperty -InterfaceDescription %1 -RegistryKeyword %2 -RegistryValue %3 -NoRestart")
                     .arg(quotePowerShell(setting.adapterDescription), quotePowerShell(setting.keyword))
                     .arg(setting.target);
        if (!adapters.contains(setting.adapterDescription)) {
            adapters << setting.adapterDescription;
        }
    }
    for (const auto& adapter : adapters) {
        lines << QStringLiteral("Restart-NetAdapter -InterfaceDescription %1 -Confirm:$false").arg(quotePowerShell(adapter));
    }
    return lines.join(QStringLiteral("; "));
}
#endif

}

NetworkBuffers::NetworkBuffers(QObject* parent)
    : QObject(parent), m_ApplyCommand(applyCommand())
{
    refresh();
}

bool NetworkBuffers::receiveBufferTooSmall()
{
#ifdef Q_OS_WIN
    return !scanReceiveSettings().empty();
#else
    const qint64 current = readRmemMax();
    return current > 0 && current < k_RequiredBytes;
#endif
}

int NetworkBuffers::wiredLinkMbps(bool* wirelessConnected)
{
    int fastest = 0;
    bool wireless = false;
#if defined(Q_OS_LINUX)
    const QDir netDir(QStringLiteral("/sys/class/net"));
    for (const QString& name : netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = netDir.filePath(name);
        // Virtual interfaces (bridges, VPNs, containers) have no device
        if (!QFile::exists(path + QStringLiteral("/device"))) {
            continue;
        }
        QFile state(path + QStringLiteral("/operstate"));
        if (!state.open(QIODevice::ReadOnly) || state.readAll().trimmed() != "up") {
            continue;
        }
        if (QFile::exists(path + QStringLiteral("/wireless")) ||
                QFile::exists(path + QStringLiteral("/phy80211"))) {
            wireless = true;
            continue;
        }
        QFile speed(path + QStringLiteral("/speed"));
        if (speed.open(QIODevice::ReadOnly)) {
            bool ok = false;
            const int mbps = speed.readAll().trimmed().toInt(&ok);
            if (ok && mbps > fastest) {
                fastest = mbps;
            }
        }
    }
#elif defined(Q_OS_WIN)
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_UNSPEC,
                                      GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST |
                                      GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
    }
    if (result == NO_ERROR) {
        for (auto adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) {
                continue;
            }
            if (adapter->IfType == IF_TYPE_IEEE80211) {
                wireless = true;
            }
            else if (adapter->IfType == IF_TYPE_ETHERNET_CSMACD) {
                const int mbps = int(adapter->ReceiveLinkSpeed / 1000000);
                if (mbps > fastest) {
                    fastest = mbps;
                }
            }
        }
    }
#endif
    if (wirelessConnected != nullptr) {
        *wirelessConnected = wireless;
    }
    return fastest;
}

int NetworkBuffers::routedWiredLinkMbps(const QHostAddress& host)
{
    if (host.isNull()) return 0;
    QUdpSocket route;
    route.connectToHost(host, 9);
    if (!route.waitForConnected(200)) return 0;
    const QHostAddress source = route.localAddress();
    for (const auto& netInterface : QNetworkInterface::allInterfaces()) {
        if (!(netInterface.flags() & QNetworkInterface::IsUp) ||
            (netInterface.flags() & QNetworkInterface::IsLoopBack)) continue;
        bool ownsSource = false;
        for (const auto& entry : netInterface.addressEntries()) {
            if (entry.ip() == source) { ownsSource = true; break; }
        }
        if (!ownsSource) continue;
#ifdef Q_OS_LINUX
        const QString path = QStringLiteral("/sys/class/net/") + netInterface.name();
        if (!QFile::exists(path + QStringLiteral("/device")) ||
            QFile::exists(path + QStringLiteral("/wireless")) ||
            QFile::exists(path + QStringLiteral("/phy80211"))) return 0;
        QFile duplex(path + QStringLiteral("/duplex"));
        if (!duplex.open(QIODevice::ReadOnly) || duplex.readAll().trimmed() != "full") return 0;
        QFile speed(path + QStringLiteral("/speed"));
        if (!speed.open(QIODevice::ReadOnly)) return 0;
        bool valid = false;
        const int mbps = speed.readAll().trimmed().toInt(&valid);
        return valid && mbps > 0 ? mbps : 0;
#elif defined(Q_OS_WIN)
        ULONG size = 16 * 1024;
        std::vector<unsigned char> buffer;
        ULONG result = ERROR_BUFFER_OVERFLOW;
        for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
            buffer.resize(size);
            result = GetAdaptersAddresses(AF_UNSPEC,
                                          GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST |
                                          GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                          nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
        }
        if (result != NO_ERROR) return 0;
        for (auto adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); adapter; adapter = adapter->Next) {
            if (adapter->OperStatus == IfOperStatusUp && adapter->IfType == IF_TYPE_ETHERNET_CSMACD &&
                (adapter->IfIndex == ULONG(netInterface.index()) || adapter->Ipv6IfIndex == ULONG(netInterface.index()))) {
                return int(adapter->ReceiveLinkSpeed / 1'000'000);
            }
        }
#endif
        return 0;
    }
    return 0;
}

QString NetworkBuffers::launchWarning()
{
    if (!receiveBufferTooSmall()) {
        return {};
    }
#ifdef Q_OS_WIN
    return tr("This PC's network adapter has a small receive buffer, so PyroWave frames may lose detail. Fix it in Settings, below the video codec.");
#else
    return tr("Linux limits this PC's network receive buffer, so PyroWave frames may lose packets. Fix it in Settings, below the video codec.");
#endif
}

bool NetworkBuffers::canApply() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return !m_ApplyCommand.isEmpty();
#endif
}

int NetworkBuffers::recommendedMb() const
{
    return int(k_RecommendedBytes / (1024 * 1024));
}

QString NetworkBuffers::fixDescription() const
{
#ifdef Q_OS_WIN
    return tr("Raises the adapter's receive buffers to its maximum and restarts it, which briefly drops the network. Asks for administrator permission.");
#else
    return tr("Raises net.core.rmem_max now and saves it for future boots. Asks for your password.");
#endif
}

QString NetworkBuffers::manualHint() const
{
#ifdef Q_OS_WIN
    return tr("Run this in PowerShell as administrator:");
#else
    return tr("Run this in a terminal (Konsole on Steam Deck):");
#endif
}

QString NetworkBuffers::manualCommand() const
{
#ifdef Q_OS_WIN
    return m_WindowsScript;
#else
    return QStringLiteral("sudo sysctl -w net.core.rmem_max=%1 && echo 'net.core.rmem_max = %1' | sudo tee %2")
        .arg(k_RecommendedBytes).arg(QString::fromLatin1(k_ConfFile));
#endif
}

void NetworkBuffers::refresh()
{
#ifdef Q_OS_WIN
    const auto settings = scanReceiveSettings();
    m_NeedsFix = !settings.empty();
    m_WindowsScript = windowsScript(settings);
    QStringList details;
    QString adapter;
    for (const auto& setting : settings) {
        adapter = setting.adapterName;
        details << tr("%1 %2 of %3").arg(setting.displayName).arg(setting.current).arg(setting.maximum);
    }
    m_ProblemText = m_NeedsFix ?
        tr("The network adapter \"%1\" has a small receive buffer (%2). PyroWave bursts overflow it, so frames lose detail and still images shimmer.")
            .arg(adapter, details.join(QStringLiteral(", "))) :
        QString();
#else
    m_CurrentBytes = readRmemMax();
    m_NeedsFix = m_CurrentBytes > 0 && m_CurrentBytes < k_RequiredBytes;
    m_ProblemText = m_NeedsFix ?
        tr("Linux limits this PC's network receive buffer to %1 KB. PyroWave needs about %2 MB, or frames arrive with missing packets.")
            .arg(currentKb()).arg(recommendedMb()) :
        QString();
#endif
    emit changed();
}

void NetworkBuffers::apply()
{
    if (m_Busy || !canApply() || !m_NeedsFix) {
        return;
    }

    m_Busy = true;
    m_Message = tr("Waiting for authorization…");
    emit changed();

#ifdef Q_OS_WIN
    // UAC and the adapter restart block, so run them off the UI thread.
    const QString script = m_WindowsScript;
    QPointer<NetworkBuffers> self(this);
    std::thread([self, script]() {
        const QByteArray encoded = QByteArray(reinterpret_cast<const char*>(script.utf16()),
                                              script.size() * 2).toBase64();
        const std::wstring parameters =
            L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand " +
            QString::fromLatin1(encoded).toStdWString();
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas";
        info.lpFile = L"powershell.exe";
        info.lpParameters = parameters.c_str();
        info.nShow = SW_HIDE;
        const bool started = ShellExecuteExW(&info) && info.hProcess != nullptr;
        const bool cancelled = !started && GetLastError() == ERROR_CANCELLED;
        DWORD exitCode = 1;
        if (started) {
            WaitForSingleObject(info.hProcess, 120000);
            GetExitCodeProcess(info.hProcess, &exitCode);
            CloseHandle(info.hProcess);
        }
        QMetaObject::invokeMethod(qApp, [self, started, cancelled, exitCode]() {
            if (self) {
                self->finishApply(started, cancelled, exitCode);
            }
        }, Qt::QueuedConnection);
    }).detach();
#else
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        m_Busy = false;
        refresh();
        if (status == QProcess::NormalExit && exitCode == 0 && !m_NeedsFix) {
            m_Message = tr("Receive buffer limit raised to %1 MB and saved for future boots. Reconnect any running stream.")
                            .arg(recommendedMb());
        }
        else if (exitCode == 126) {
            m_Message = tr("Authorization was cancelled.");
        }
        else if (exitCode == 127) {
            m_Message = tr("No password prompt is available here (for example in Gaming Mode). Switch to Desktop Mode, or run the command below in a terminal.");
        }
        else {
            m_Message = tr("The change failed (exit code %1). Run the command below in a terminal instead.").arg(exitCode);
            if (!error.isEmpty()) {
                m_Message += QStringLiteral("\n") + error;
            }
        }
        emit changed();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        m_Busy = false;
        m_Message = tr("Could not start the authorization helper. Run the command below in a terminal instead.");
        emit changed();
    });

    const QString program = m_ApplyCommand.first();
    process->start(program, m_ApplyCommand.mid(1));
#endif
}

void NetworkBuffers::finishApply(bool started, bool cancelled, unsigned long exitCode)
{
    m_Busy = false;
    refresh();
    if (started && exitCode == 0 && !m_NeedsFix) {
        m_Message = tr("Receive buffers raised. The adapter restarted; reconnect any running stream.");
    }
    else if (cancelled) {
        m_Message = tr("Authorization was cancelled.");
    }
    else {
        m_Message = tr("The change failed (exit code %1). Run the command below instead.").arg(exitCode);
    }
    emit changed();
}

void NetworkBuffers::copyCommand()
{
    QGuiApplication::clipboard()->setText(manualCommand());
#ifdef Q_OS_WIN
    m_Message = tr("Command copied. Paste it into PowerShell opened as administrator.");
#else
    m_Message = tr("Command copied. Paste it into a terminal (Konsole on Steam Deck) and enter your password.");
#endif
    emit changed();
}
