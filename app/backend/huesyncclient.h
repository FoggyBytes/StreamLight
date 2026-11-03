#pragma once

#include <QString>

/*
 * 8.4.0: Hue Sync on THIS device, for the Hue Sync tab of the StreamTweak section.
 *
 * Only one thing is done with it: "take the lights back" — close Hue Sync and start it again, so
 * that it starts syncing (its own "Start sync at startup") and the bridge, which streams to one
 * area at a time, hands this device the lights. Measured 03/11/2026: a Hue Sync that another device
 * displaced does not resume by itself, and nothing on the host can give the lights back.
 *
 * ⚠️ This replaces nothing of the integration removed in 8.4.0 (the old HueSyncManager, started and
 * closed with every stream): StreamLight no longer opens or closes Hue Sync around a stream.
 */
class HueSyncClient
{
public:
    // HueSyncStarter.exe (Hue Sync's own silent launcher) or HueSync.exe, from the Uninstall
    // entry named "Hue Sync", else the usual folders. Empty when Hue Sync is not installed.
    static QString executable();

    static bool running();

    // Closes Hue Sync if it is open, then starts it again. False when it is not installed or
    // could not be started.
    static bool restart();
};
