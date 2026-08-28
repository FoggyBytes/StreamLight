#include "refreshratematch.h"

#include <QtGlobal>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <SDL_syswm.h>
#endif

namespace
{
    // True when the last apply() actually replaced upstream's choice. Everything the
    // teardown path does is gated on it, so with the setting off — or with nothing to
    // match — this file never touches the display at all.
    bool s_ModeWritten = false;

#ifdef Q_OS_WIN32
    // The display the stream window was on, captured while the window still existed.
    WCHAR s_DisplayDevice[CCHDEVICENAME] = {};

    // Undo a display mode change that outlived the stream window.
    //
    // SDL is supposed to put the panel back when an exclusive-fullscreen window is
    // destroyed, and normally does. It does not always: with the match enabled, a
    // 60 FPS stream leaves a 120 Hz panel sitting at 60 Hz after the session ends
    // (issue #9). The case does not exist without the match, because upstream always
    // picks the highest refresh rate, which on such a panel is the desktop mode
    // already — there is nothing to put back.
    //
    // Rather than guess which of SDL's restore paths was missed, ask Windows. A mode
    // set for fullscreen is temporary and does not touch the saved settings, so if the
    // display is not running what the user has saved for it, the leftover change is
    // ours and we undo it. When SDL got it right this is a no-op, which is what keeps
    // it from introducing a mode change of its own.
    void restoreDesktopDisplayModeIfChanged(const WCHAR* deviceName)
    {
        DEVMODEW currentMode = {};
        DEVMODEW savedMode = {};

        currentMode.dmSize = sizeof(currentMode);
        savedMode.dmSize = sizeof(savedMode);

        if (!EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &currentMode) ||
            !EnumDisplaySettingsW(deviceName, ENUM_REGISTRY_SETTINGS, &savedMode)) {
            return;
        }

        // Both readings have to actually carry a resolution and a refresh rate before
        // they can be compared. Testing a field the driver never filled in would have
        // us "restoring" the display after every single session.
        const DWORD requiredFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        if ((currentMode.dmFields & requiredFields) != requiredFields ||
            (savedMode.dmFields & requiredFields) != requiredFields) {
            return;
        }

        if (currentMode.dmPelsWidth == savedMode.dmPelsWidth &&
            currentMode.dmPelsHeight == savedMode.dmPelsHeight &&
            currentMode.dmDisplayFrequency == savedMode.dmDisplayFrequency) {
            return;
        }

        // A null DEVMODE means "go back to the settings saved for this display".
        LONG result = ChangeDisplaySettingsExW(deviceName, nullptr, nullptr, 0, nullptr);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Display was left at %lux%lu@%lu Hz after the stream - restoring the desktop mode "
                    "%lux%lu@%lu Hz (result %ld)",
                    currentMode.dmPelsWidth, currentMode.dmPelsHeight, currentMode.dmDisplayFrequency,
                    savedMode.dmPelsWidth, savedMode.dmPelsHeight, savedMode.dmDisplayFrequency,
                    result);
    }
#endif
}

void RefreshRateMatch::apply(SDL_Window* window, int fps, bool enabled)
{
    // Cleared on every call, including the ones that return early: this also runs when
    // the window moves to another display mid-session, and a stale true from an earlier
    // display would have the teardown path restoring a display we never changed.
    s_ModeWritten = false;

    if (!enabled || window == nullptr || fps <= 0) {
        return;
    }

    // Upstream's decision, read back rather than recomputed. Whatever it settled on is
    // the resolution we stay at; only the refresh rate is up for discussion.
    SDL_DisplayMode chosen;
    if (SDL_GetWindowDisplayMode(window, &chosen) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Refresh rate match: SDL_GetWindowDisplayMode() failed: %s",
                    SDL_GetError());
        return;
    }

    int displayIndex = SDL_GetWindowDisplayIndex(window);
    if (displayIndex < 0) {
        return;
    }

    // The LOWEST refresh rate the stream FPS divides, which is the opposite of what
    // upstream wants and the whole point of the setting: 60 FPS on a panel offering
    // 60/100/120 lands on 60, so every streamed frame is shown for exactly one refresh.
    //
    // Format is preferred, not required. Modes at one resolution can differ in pixel
    // format, and silently dropping to a lower-bpp one to gain the rate would be a
    // trade the user never asked for — but if the panel only offers the right rate in
    // another format, having the rate is still what was asked for.
    SDL_DisplayMode best = {};
    bool bestMatchesFormat = false;
    for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(displayIndex, i, &mode) != 0) {
            continue;
        }
        if (mode.w != chosen.w || mode.h != chosen.h) {
            continue;
        }
        if (mode.refresh_rate <= 0 || mode.refresh_rate % fps != 0) {
            continue;
        }

        bool matchesFormat = (mode.format == chosen.format);
        if (best.refresh_rate == 0 ||
                (matchesFormat && !bestMatchesFormat) ||
                (matchesFormat == bestMatchesFormat && mode.refresh_rate < best.refresh_rate)) {
            best = mode;
            bestMatchesFormat = matchesFormat;
        }
    }

    if (best.refresh_rate == 0) {
        // Nothing to match: a 60 Hz-only panel with a 60 FPS stream is already right,
        // and a panel with no multiple of the FPS at all cannot be helped. Either way
        // upstream's choice stands, which is the same behaviour as the setting being off.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Refresh rate match: no %d Hz multiple at %dx%d, keeping %d Hz",
                    fps, chosen.w, chosen.h, chosen.refresh_rate);
        return;
    }

    if (best.refresh_rate == chosen.refresh_rate) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Refresh rate match: already at %d Hz for %d FPS",
                    chosen.refresh_rate, fps);
        return;
    }

    if (SDL_SetWindowDisplayMode(window, &best) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Refresh rate match: SDL_SetWindowDisplayMode() failed: %s",
                    SDL_GetError());
        return;
    }

    s_ModeWritten = true;

    // ⚠️ "will use" and not "is using": like upstream's own line just above this one in
    // the log, this is a mode recorded on the window. It reaches the panel only in
    // exclusive fullscreen, when the window is shown.
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Refresh rate match: %dx%d will use %d Hz instead of %d Hz for %d FPS",
                best.w, best.h, best.refresh_rate, chosen.refresh_rate, fps);
}

#ifdef Q_OS_WIN32

void RefreshRateMatch::rememberDisplay(SDL_Window* window)
{
    s_DisplayDevice[0] = L'\0';

    if (!s_ModeWritten || window == nullptr) {
        return;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo) && wmInfo.subsystem == SDL_SYSWM_WINDOWS) {
        HMONITOR monitor = MonitorFromWindow(wmInfo.info.win.window, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo)) {
            SDL_memcpy(s_DisplayDevice, monitorInfo.szDevice, sizeof(s_DisplayDevice));
            s_DisplayDevice[CCHDEVICENAME - 1] = L'\0';
        }
    }
}

void RefreshRateMatch::restoreIfChanged()
{
    if (s_DisplayDevice[0] == L'\0') {
        return;
    }

    restoreDesktopDisplayModeIfChanged(s_DisplayDevice);

    // One shot. A second session starts by recording its own display, and nothing
    // should be able to restore twice off one recording.
    s_DisplayDevice[0] = L'\0';
    s_ModeWritten = false;
}

#else

void RefreshRateMatch::rememberDisplay(SDL_Window*) {}
void RefreshRateMatch::restoreIfChanged() {}

#endif
