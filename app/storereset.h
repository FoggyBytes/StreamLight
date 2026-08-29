#pragma once

// StreamLight moved to its own settings store in 5.4.0 (see the block in main.cpp
// for why). Nothing is migrated, so the first launch after that upgrade looks
// exactly like a fresh install: no hosts, no pairing, default settings.
//
// A silent reset reads as a fault, and this app never shows its changelog at
// runtime — so the only channel that reaches the user at the moment it matters is
// a one-time notice in the app itself. This is the thing that decides whether to
// show it.
//
// The probe is READ-ONLY against the old store. StreamLight must never write to
// Moonlight's settings again, not even a marker: that store may belong to a live
// Moonlight installation, and a downgrade to 5.3.0 has to find it untouched.
namespace StoreReset
{
    // Call once at startup, AFTER the portable-mode QSettings format/path has been
    // decided (main.cpp) and BEFORE anything constructs QSettings for real. Doing it
    // in the other order probes the wrong file in portable mode.
    void probe();

    // True when this launch is the first one on the new store AND the old shared
    // store shows evidence that StreamLight — not just Moonlight — ran against it.
    // Stays true until acknowledge(), so a crash before the user sees the notice
    // does not swallow it.
    bool settingsWereReset();

    // The user has seen the notice. Writes the store-version marker, after which
    // settingsWereReset() is false forever.
    void acknowledge();
}
