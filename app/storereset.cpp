#include "storereset.h"

#include <QDebug>
#include <QSettings>
#include <QStringList>

// Bumped when the shape or the location of the store changes. 1 was the era of the
// shared "Moonlight Game Streaming Project\Moonlight" store; 2 is our own.
#define SER_STOREVERSION "storeversion"
#define STORE_VERSION 2

// The store StreamLight used up to and including 5.3.0 — Moonlight's own.
#define LEGACY_ORG "Moonlight Game Streaming Project"
#define LEGACY_APP "Moonlight"

namespace
{
    bool s_Probed = false;
    bool s_WasReset = false;

    // Keys that only StreamLight ever writes. Moonlight has none of them, so finding
    // one in the shared store proves a StreamLight install lived there — which is the
    // difference between "your settings were reset" (true) and "welcome, new user"
    // (also true, for someone who merely has Moonlight installed). Telling a first-time
    // user that their settings were reset is worse than telling nobody.
    //
    // Deliberately several, spanning 3.x to 5.3.x, because a single key dates the
    // release that introduced it and would miss everyone older.
    const char* const LEGACY_FINGERPRINT[] = {
        "framepacingmode",     // 3.4.x
        "huesync",             // 4.x
        "matchhostlinkspeed",  // 4.6 / 5.0
        "waitforgame",         // 5.0
        "overlayitems",        // 5.1
        "overlayposition",     // 5.1
        "glyphset",
        "hidehostips",
        "tailscaleautostart",
        "clockformat",
        "matchrefreshrate",    // 5.2.1
    };

    bool legacyStreamLightStorePresent()
    {
        // ⚠️ The format has to be passed EXPLICITLY. QSettings::setDefaultFormat() —
        // which main.cpp calls for portable mode — only applies to the constructors
        // that take no organization/application; QSettings(org, app) is always
        // NativeFormat. Written the short way, this probe reads the REGISTRY on a
        // portable install while the rest of the app reads the INI next to the exe,
        // so a portable upgrade would silently miss its notice (or invent one from a
        // Moonlight install that has nothing to do with it). Measured, not reasoned:
        // fileName() came back as \HKEY_CURRENT_USER\... with the redirection active.
        QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope,
                         QStringLiteral(LEGACY_ORG), QStringLiteral(LEGACY_APP));

        for (const char* key : LEGACY_FINGERPRINT) {
            if (legacy.contains(QLatin1String(key))) {
                qInfo() << "Legacy shared store carries StreamLight key:" << key;
                return true;
            }
        }

        // The accent colour lives in its own group and predates some of the keys above.
        if (legacy.childGroups().contains(QStringLiteral("theme"))) {
            qInfo() << "Legacy shared store carries StreamLight's theme group";
            return true;
        }

        return false;
    }
}

void StoreReset::probe()
{
    Q_ASSERT(!s_Probed);
    s_Probed = true;

    QSettings settings;

    if (settings.contains(QStringLiteral(SER_STOREVERSION))) {
        // Either a later launch, or the notice has already been acknowledged.
        s_WasReset = false;
        return;
    }

    s_WasReset = legacyStreamLightStorePresent();

    if (!s_WasReset) {
        // A genuinely new install. Stamp the marker now so the probe never runs again;
        // there is no notice pending to lose by writing it here.
        settings.setValue(QStringLiteral(SER_STOREVERSION), STORE_VERSION);
        settings.sync();
    }
    else {
        // The marker is NOT written yet, on purpose: it is written when the user
        // acknowledges the notice, so a crash before that shows it again next time.
        qInfo() << "Settings were reset: upgraded from a build sharing Moonlight's store";
    }
}

bool StoreReset::settingsWereReset()
{
    Q_ASSERT(s_Probed);
    return s_WasReset;
}

void StoreReset::acknowledge()
{
    if (!s_WasReset) {
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral(SER_STOREVERSION), STORE_VERSION);
    settings.sync();

    s_WasReset = false;
}
