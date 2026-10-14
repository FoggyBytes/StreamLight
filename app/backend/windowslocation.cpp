#include "windowslocation.h"

#include <QtGlobal>

#include <thread>

#ifdef Q_OS_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.devices.geolocation.h>
#pragma comment(lib, "runtimeobject")

// WRL on the ABI interfaces rather than C++/WinRT: C++/WinRT under C++17 pulls in MSVC's
// experimental coroutine headers, which the toolchain refuses (STL1011) and is about to remove.
// This needs nothing past C++11 and links against runtimeobject only.
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::Event;
using Microsoft::WRL::Wrappers::HStringReference;
using Microsoft::WRL::Wrappers::RoInitializeWrapper;
namespace Geo = ABI::Windows::Devices::Geolocation;
namespace Fnd = ABI::Windows::Foundation;

namespace
{
    bool readPosition(double* lat, double* lon)
    {
        ComPtr<Geo::IGeolocator> locator;
        if (FAILED(Fnd::ActivateInstance(HStringReference(RuntimeClass_Windows_Devices_Geolocation_Geolocator).Get(),
                                         &locator))) {
            return false;
        }

        // A position up to six hours old is as good as a new one for the sun, and lets Windows
        // answer from its cache without waking the radios. TimeSpan counts 100 ns ticks.
        const Fnd::TimeSpan maxAge  { 6LL * 3600 * 10000000 };
        const Fnd::TimeSpan timeout { 15LL * 10000000 };
        ComPtr<Fnd::IAsyncOperation<Geo::Geoposition*>> op;
        if (FAILED(locator->GetGeopositionAsyncWithAgeAndTimeout(maxAge, timeout, &op))) {
            return false;   // location off or not allowed for desktop apps: refused at once
        }

        Event done(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS));
        if (!done.IsValid()) {
            return false;
        }
        Fnd::AsyncStatus status = Fnd::AsyncStatus::Started;
        HANDLE h = done.Get();
        const HRESULT hr = op->put_Completed(
            Callback<Fnd::IAsyncOperationCompletedHandler<Geo::Geoposition*>>(
                [h, &status](Fnd::IAsyncOperation<Geo::Geoposition*>*, Fnd::AsyncStatus s) -> HRESULT {
                    status = s;
                    SetEvent(h);
                    return S_OK;
                }).Get());
        if (FAILED(hr)) {
            return false;
        }
        // The operation times out by itself at 15 s; this is only a backstop.
        if (WaitForSingleObjectEx(h, 20000, FALSE) != WAIT_OBJECT_0 || status != Fnd::AsyncStatus::Completed) {
            ComPtr<Fnd::IAsyncInfo> info;
            if (SUCCEEDED(op.As(&info))) info->Cancel();
            return false;
        }

        ComPtr<Geo::IGeoposition> pos;
        ComPtr<Geo::IGeocoordinate> coord;
        if (FAILED(op->GetResults(&pos)) || !pos || FAILED(pos->get_Coordinate(&coord)) || !coord) {
            return false;
        }
        // Point.Position: IGeocoordinate's own Latitude/Longitude are deprecated since 8.1.
        ComPtr<Geo::IGeocoordinateWithPoint> withPoint;
        ComPtr<Geo::IGeopoint> point;
        Geo::BasicGeoposition p {};
        if (FAILED(coord.As(&withPoint)) || FAILED(withPoint->get_Point(&point)) || !point
                || FAILED(point->get_Position(&p))) {
            return false;
        }
        *lat = p.Latitude;
        *lon = p.Longitude;
        return p.Latitude >= -90.0 && p.Latitude <= 90.0 && p.Longitude >= -180.0 && p.Longitude <= 180.0;
    }
}
#endif

void WindowsLocation::request(std::function<void(bool, double, double)> done)
{
#ifdef Q_OS_WIN32
    // A thread of its own rather than the pool: it enters a Windows Runtime apartment, and a
    // pool thread may already carry COM state of some other kind.
    std::thread([done]() {
        double lat = 0, lon = 0;
        bool ok = false;
        {
            RoInitializeWrapper ro(RO_INIT_MULTITHREADED);
            ok = readPosition(&lat, &lon);
        }
        done(ok, lat, lon);
    }).detach();
#else
    done(false, 0, 0);
#endif
}
