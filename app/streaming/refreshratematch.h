#pragma once

#include "SDL_compat.h"

// Runs the display at the stream's own frame rate, for people whose panel would
// otherwise show a 60 FPS stream on a 120 Hz refresh and judder on the 2:1 ratio.
//
// ⚠️ This is deliberately built as a POST-PASS over upstream's own mode selection,
// not as a change to it. Session::updateOptimalWindowDisplayMode() runs untouched
// and picks what Moonlight master would pick; apply() then reads that choice back
// and, if the user asked for it and the panel can do it, replaces the refresh rate
// with the lowest multiple of the stream FPS at the same resolution.
//
// The reason that works without a second modeset is timing: SDL_SetWindowDisplayMode()
// only RECORDS the mode a fullscreen window will use — the mode reaches the driver
// later, at SDL_SetWindowFullscreen() (in practice at the reveal, since 5.0.0 creates
// the window hidden). apply() runs inside that same window of time, so the panel still
// sees exactly one transition.
//
// The previous incarnation of this feature (5.1.0 - 5.1.3, "Refresh rate switching")
// reached into upstream's two search loops instead, which is what made it expensive to
// carry and easy to get wrong. Nothing here modifies engine code.
namespace RefreshRateMatch
{
    // Call immediately after updateOptimalWindowDisplayMode() has stored its choice.
    // A no-op when 'enabled' is false, when the panel has no mode the FPS divides, or
    // when upstream already picked that rate — in every one of those cases upstream's
    // selection stands untouched.
    void apply(SDL_Window* window, int fps, bool enabled);

    // Windows only (a no-op elsewhere). Note which display the stream window is on
    // while the window still exists, so restoreIfChanged() can check it afterwards.
    // Records nothing unless apply() actually wrote a mode.
    void rememberDisplay(SDL_Window* window);

    // Windows only (a no-op elsewhere). Call after the window is destroyed and the
    // video subsystem is down: if a fullscreen mode change of ours outlived them, put
    // the desktop mode back.
    void restoreIfChanged();
}
