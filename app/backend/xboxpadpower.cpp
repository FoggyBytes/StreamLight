#include "xboxpadpower.h"

#include <QtGlobal>
#include <QRegularExpression>
#include <QString>

#include <string>
#include <cwchar>

#ifdef Q_OS_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.gaming.input.h>
#include <windows.devices.power.h>
#include <windows.system.power.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#pragma comment(lib, "runtimeobject")
#pragma comment(lib, "cfgmgr32")

// WRL on the ABI interfaces, as backend/windowslocation.cpp: C++/WinRT under C++17 is refused by
// the toolchain (STL1011).
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Gi  = ABI::Windows::Gaming::Input;
namespace Pwr = ABI::Windows::Devices::Power;
namespace Sys = ABI::Windows::System::Power;
namespace Fnd = ABI::Windows::Foundation;

namespace
{
    const UINT16 VENDOR_MICROSOFT = 0x045E;

    // The battery level a Bluetooth device reports, the one Settings shows:
    // {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2 — a byte, 0–100.
    const DEVPROPKEY BLUETOOTH_BATTERY = {
        { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } }, 2 };

    QStringList presentIds(const wchar_t* enumerator)
    {
        const ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
        for (int attempt = 0; attempt < 3; attempt++) {
            ULONG length = 0;
            if (CM_Get_Device_ID_List_SizeW(&length, enumerator, flags) != CR_SUCCESS || length == 0) {
                return {};
            }
            std::wstring buffer(length, L'\0');
            const CONFIGRET cr = CM_Get_Device_ID_ListW(enumerator, buffer.data(), length, flags);
            if (cr == CR_BUFFER_SMALL) {
                continue;   // a device arrived between the two calls
            }
            if (cr != CR_SUCCESS) {
                return {};
            }
            QStringList ids;
            for (const wchar_t* p = buffer.c_str(); *p; p += wcslen(p) + 1) {
                ids.append(QString::fromWCharArray(p));
            }
            return ids;
        }
        return {};
    }

    /**
     * The Xbox pads connected over Bluetooth Low Energy, as product id → battery percentage, one
     * entry per pad. Connected = its HID-over-GATT node (service 0x1812) is present: the device
     * node itself stays present while a paired pad is off.
     */
    QList<QPair<int, int>> blePads()
    {
        static const QRegularExpression hid(
            QStringLiteral("^BTHLEDEVICE\\\\\\{00001812-0000-1000-8000-00805F9B34FB\\}_DEV_VID&02045E_PID&([0-9A-F]{4})_REV&[0-9A-F]{4}_([0-9A-F]{12})\\\\"),
            QRegularExpression::CaseInsensitiveOption);

        QList<QPair<int, int>> pads;
        const QStringList devices = presentIds(L"BTHLE");
        for (const QString& id : presentIds(L"BTHLEDEVICE")) {
            const QRegularExpressionMatch m = hid.match(id);
            if (!m.hasMatch()) {
                continue;
            }
            const int pid = m.captured(1).toInt(nullptr, 16);
            const QString prefix = QStringLiteral("BTHLE\\DEV_") + m.captured(2).toUpper() + QStringLiteral("\\");
            int percent = -1;
            for (const QString& dev : devices) {
                if (!dev.startsWith(prefix, Qt::CaseInsensitive)) {
                    continue;
                }
                DEVINST inst = 0;
                std::wstring wid = dev.toStdWString();
                if (CM_Locate_DevNodeW(&inst, wid.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
                    break;
                }
                DEVPROPTYPE type = 0;
                BYTE value = 0;
                ULONG size = sizeof(value);
                if (CM_Get_DevNode_PropertyW(inst, &BLUETOOTH_BATTERY, &type, &value, &size, 0) == CR_SUCCESS
                        && type == DEVPROP_TYPE_BYTE && value <= 100) {
                    percent = value;
                }
                break;
            }
            pads.append(qMakePair(pid, percent));
        }
        return pads;
    }

    int optionalInt(Fnd::IReference<INT32>* ref, int fallback)
    {
        INT32 v = 0;
        return ref != nullptr && SUCCEEDED(ref->get_Value(&v)) ? v : fallback;
    }
}

bool XboxPadPower::read(QList<Reading>* out)
{
    out->clear();

    ComPtr<Gi::IRawGameControllerStatics> statics;
    if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Gaming_Input_RawGameController).Get(),
                                      IID_PPV_ARGS(&statics)))) {
        return false;
    }
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<Gi::RawGameController*>> controllers;
    if (FAILED(statics->get_RawGameControllers(&controllers))) {
        return false;
    }
    unsigned count = 0;
    if (FAILED(controllers->get_Size(&count))) {
        return false;
    }

    QList<QPair<int, int>> ble = blePads();

    for (unsigned i = 0; i < count; i++) {
        ComPtr<Gi::IRawGameController> raw;
        if (FAILED(controllers->GetAt(i, &raw))) {
            continue;
        }
        UINT16 vendor = 0, product = 0;
        raw->get_HardwareVendorId(&vendor);
        raw->get_HardwareProductId(&product);
        if (vendor != VENDOR_MICROSOFT) {
            continue;
        }

        ComPtr<Gi::IGameController> controller;
        boolean wireless = false;
        if (FAILED(raw.As(&controller)) || FAILED(controller->get_IsWireless(&wireless)) || !wireless) {
            continue;   // on a cable, or not a pad Windows can say anything about
        }

        ComPtr<Gi::IGameControllerBatteryInfo> batteryInfo;
        ComPtr<Pwr::IBatteryReport> report;
        if (FAILED(raw.As(&batteryInfo)) || FAILED(batteryInfo->TryGetBatteryReport(&report)) || !report) {
            continue;
        }
        Sys::BatteryStatus status = Sys::BatteryStatus_NotPresent;
        report->get_Status(&status);
        if (status == Sys::BatteryStatus_NotPresent) {
            continue;   // a virtual pad, or one with nothing to report
        }

        ComPtr<Fnd::IReference<INT32>> remaining, fullCharge;
        report->get_RemainingCapacityInMilliwattHours(&remaining);
        report->get_FullChargeCapacityInMilliwattHours(&fullCharge);
        const int rem = optionalInt(remaining.Get(), -1);
        const int full = optionalInt(fullCharge.Get(), -1);

        Reading r;
        r.percent = rem >= 0 && full > 0 ? qBound(0, rem * 100 / full, 100) : -1;
        r.charging = status == Sys::BatteryStatus_Charging;
        r.full = status == Sys::BatteryStatus_Idle;

        // On Bluetooth Low Energy the Bluetooth property is the right figure: one per pad.
        for (int b = 0; b < ble.size(); b++) {
            if (ble[b].first == product) {
                if (ble[b].second >= 0) {
                    r.percent = ble[b].second;
                }
                ble.removeAt(b);
                break;
            }
        }
        out->append(r);
    }
    return true;
}

#else

bool XboxPadPower::read(QList<Reading>* out)
{
    out->clear();
    return false;
}

#endif
