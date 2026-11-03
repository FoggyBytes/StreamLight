#include "eventwatch.h"

#include "servicelog.h"

#include <QRegularExpression>

#include <vector>

namespace
{

const wchar_t k_ShutdownQuery[] = L"*[System[Provider[@Name='User32'] and EventID=1074]]";
const wchar_t k_RecentShutdownQuery[] =
        L"*[System[Provider[@Name='User32'] and EventID=1074 and TimeCreated[timediff(@SystemTime) <= 600000]]]";
const wchar_t k_BootQuery[] = L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Boot'] and EventID=27]]";

QString renderXml(EVT_HANDLE event)
{
    DWORD used = 0;
    DWORD properties = 0;
    EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &used, &properties);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || used == 0)
        return QString();
    std::vector<wchar_t> buffer(used / sizeof(wchar_t) + 1);
    if (!EvtRender(nullptr, event, EvtRenderEventXml, used, buffer.data(), &used, &properties))
        return QString();
    return QString::fromWCharArray(buffer.data());
}

QString dataField(const QString& xml, const QString& name)
{
    const QRegularExpression pattern(QStringLiteral("<Data Name=['\"]%1['\"]>([^<]*)</Data>")
                                             .arg(QRegularExpression::escape(name)));
    const QRegularExpressionMatch match = pattern.match(xml);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

}

EventWatch::EventWatch(std::function<void(bool, const QString&)> onShutdownRequested, std::function<void(int)> onBoot)
    : m_OnShutdown(std::move(onShutdownRequested)),
      m_OnBoot(std::move(onBoot))
{
    m_ShutdownSubscription = EvtSubscribe(nullptr, nullptr, L"System", k_ShutdownQuery, nullptr, this,
                                          &EventWatch::onShutdownEvent, EvtSubscribeToFutureEvents);
    if (!m_ShutdownSubscription)
        ServiceLog::write(QStringLiteral("events: cannot watch event 1074 (%1)").arg(GetLastError()));
    m_BootSubscription = EvtSubscribe(nullptr, nullptr, L"System", k_BootQuery, nullptr, this,
                                      &EventWatch::onBootEvent, EvtSubscribeToFutureEvents);
    if (!m_BootSubscription)
        ServiceLog::write(QStringLiteral("events: cannot watch Kernel-Boot 27 (%1)").arg(GetLastError()));
}

EventWatch::~EventWatch()
{
    if (m_ShutdownSubscription)
        EvtClose(m_ShutdownSubscription);
    if (m_BootSubscription)
        EvtClose(m_BootSubscription);
}

bool EventWatch::isRestartType(const QString& type)
{
    wchar_t* text = nullptr;
    const int length = LoadStringW(GetModuleHandleW(L"user32.dll"), 708, reinterpret_cast<LPWSTR>(&text), 0);
    const QString restart = length > 0 ? QString::fromWCharArray(text, length) : QString();
    return (!restart.isEmpty() && type.compare(restart, Qt::CaseInsensitive) == 0)
           || type.compare(QLatin1String("restart"), Qt::CaseInsensitive) == 0;
}

bool EventWatch::latestShutdownIsRestart(QString* type)
{
    type->clear();
    EVT_HANDLE query = EvtQuery(nullptr, L"System", k_RecentShutdownQuery, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!query)
        return false;
    EVT_HANDLE event = nullptr;
    DWORD returned = 0;
    if (EvtNext(query, 1, &event, 1000, 0, &returned) && returned == 1) {
        *type = dataField(renderXml(event), QStringLiteral("param5"));
        EvtClose(event);
    }
    EvtClose(query);
    return isRestartType(*type);
}

DWORD WINAPI EventWatch::onShutdownEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID context, EVT_HANDLE event)
{
    if (action != EvtSubscribeActionDeliver)
        return ERROR_SUCCESS;
    auto* self = static_cast<EventWatch*>(context);
    const QString xml = renderXml(event);
    const QString type = dataField(xml, QStringLiteral("param5"));
    ServiceLog::write(QStringLiteral("events: 1074 \"%1\" from %2").arg(type, dataField(xml, QStringLiteral("param1"))));
    self->m_OnShutdown(isRestartType(type), type);
    return ERROR_SUCCESS;
}

DWORD WINAPI EventWatch::onBootEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID context, EVT_HANDLE event)
{
    if (action != EvtSubscribeActionDeliver)
        return ERROR_SUCCESS;
    auto* self = static_cast<EventWatch*>(context);
    const int bootType = dataField(renderXml(event), QStringLiteral("BootType")).toInt();
    ServiceLog::write(QStringLiteral("events: Kernel-Boot 27, boot type %1").arg(bootType));
    self->m_OnBoot(bootType);
    return ERROR_SUCCESS;
}
