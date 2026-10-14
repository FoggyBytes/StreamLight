#pragma once

#include <functional>

/**
 * This PC's position, from Windows (7.0.0, 13/10/2026): only for the automatic theme's
 * sunrise and sunset (Theme, Auto → Sunrise & sunset).
 *
 * Windows decides what it gives: its location service (Wi-Fi, GPS where there is one), or the
 * "default location" the user can set in Windows when the PC cannot find itself — which is what
 * a desktop on a cable gets. Nothing here turns location on or changes any Windows setting:
 * when location is off, desktop apps are not allowed it, or Windows has no position, the answer
 * is simply "not available" and the theme falls back to set hours.
 *
 * Coarse on purpose (a few kilometres): a city's worth of precision moves sunrise by seconds.
 */
namespace WindowsLocation
{
    /**
     * Asks once, off the calling thread: a request can take seconds (up to the 15 s timeout)
     * and must never hold up the UI. @p done runs on a worker thread — marshal back before
     * touching anything else. ok = false when location is off, denied or unavailable.
     */
    void request(std::function<void(bool ok, double latitude, double longitude)> done);
}
