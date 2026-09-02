#pragma once

// The resolution and frame-rate values the UI offers (StreamLight 5.5.0).
//
// One source for three pickers — the Settings screen, the host-profile panel and the
// per-game panel — plus the inherited-value labels those two panels print. Before this,
// each of the three carried its own literal [30, 60, 90, 120] and its own resolution
// table, and appsettings.cpp carried a fourth copy of the preset NAMES; adding the
// display's own values to one of them would have made the four disagree.
//
// The presets are the four each picker already offered. On top of them go the values the
// client's own displays report, which is what issue #13 was about: a 165 Hz panel had no
// way to ask for 165, and the number only ever appeared because StreamLight used to read
// Moonlight's settings store.
//
// ⚠️ The native values are a snapshot, pushed in by SystemProperties::refreshDisplays()
// and read on the main thread only. There is no locking here and none is needed: the
// push happens once, during startAsyncLoad(), before any picker exists.

#include <QList>
#include <QSize>
#include <QString>

namespace VideoOptions
{
    // Called by SystemProperties::refreshDisplays() with what SDL reported. Refresh rates
    // arrive already normalised (58-62 -> 60, 28-32 -> 30), so a 59.94 Hz panel does not
    // produce a second pill next to the 60 preset.
    //
    // The two lists are NOT index-parallel: a display over 8192 px contributes its refresh
    // rate and not its resolution. Nothing here pairs them, so that does not matter.
    void setNativeDisplays(const QList<QSize>& resolutions, const QList<int>& refreshRates);

    // How many displays the snapshot came from. 0 when SDL never got as far as asking.
    int displayCount();

    // Presets plus natives: deduplicated, ascending (by pixel count for resolutions).
    QList<int> frameRates();
    QList<QSize> resolutions();

    bool isNativeFrameRate(int fps);
    bool isNativeResolution(const QSize& resolution);

    /*
     * "1080p" for a preset, "1600p" for a native one, "2560x1600" for anything else.
     *
     * ⚠️ A native falls back to WxH when another offered entry shares its height — an
     * ultrawide 3440x1440 beside the 1440p preset would otherwise put the same word on two
     * different pills. The label is derived from the whole list rather than from the size
     * alone for exactly that reason, so callers must not try to shortcut it.
     */
    QString resolutionLabel(const QSize& resolution);
    QString frameRateLabel(int fps);

    // What the two Settings rows print underneath their titles: the distinct native values,
    // e.g. "2560x1600" and "165 Hz". Empty when there is nothing to report.
    QString nativeResolutionHint();
    QString nativeRefreshHint();
}
