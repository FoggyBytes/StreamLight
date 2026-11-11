#pragma once

#include <QList>

/**
 * The charge of Microsoft's wireless pads, for the top-right corner (8.7.0).
 *
 * <p>SDL cannot give it on Windows: it drives Xbox pads through XInput, and XInput has battery
 * information only for the Xbox 360 receivers — for an Xbox One or Series pad on the Xbox Wireless
 * Adapter XInputGetBatteryInformation answers "disconnected" (measured on FOGGY, 11/11/2026), and
 * SDL's XInput backend never sets the connection state either. So the reading comes from
 * Windows.Gaming.Input, which has it (the adapter's Medium arrives as 700 of 1000 mWh). For a pad
 * on Bluetooth Low Energy Windows.Gaming.Input is wrong (100 of 1000 for a pad Windows itself
 * shows at 75 %), and the percentage is taken from the Bluetooth device's battery property, the
 * one Settings shows.</p>
 */
namespace XboxPadPower
{
    struct Reading
    {
        int  percent  = -1;    // 0–100, -1 not known
        bool charging = false;
        bool full     = false; // on a charger and full
    };

    /**
     * The wireless Microsoft pads Windows sees, with a battery. False when Windows.Gaming.Input
     * could not be asked at all — the caller then keeps what SDL says. Windows only; elsewhere
     * always false. Fills the list in Windows' order; may be empty right after the first call of
     * the process, while Windows is still enumerating.
     */
    bool read(QList<Reading>* out);
}
