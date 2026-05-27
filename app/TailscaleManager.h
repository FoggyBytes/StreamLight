#pragma once

#include <QString>

// TailscaleManager — fire-and-forget Tailscale launcher.
//
// Unlike HueSyncManager, this class holds no process state and never terminates
// Tailscale. The "Auto-start Tailscale" preference controls only whether
// StreamLight launches Tailscale at boot when it is not already running. Once
// launched, Tailscale lives independently of StreamLight: closing StreamLight
// does not stop Tailscale, and toggling the preference OFF at runtime does not
// stop Tailscale either (it only affects the next StreamLight launch).
//
// Supported install: official Windows installer from https://tailscale.com/download.
// The Microsoft Store package is not supported.
class TailscaleManager
{
public:
    // Returns the absolute path to tailscale-ipn.exe, or an empty string if not found.
    // Searches the Windows Uninstall registry for "Tailscale" first, then falls back
    // to the well-known install location under Program Files.
    static QString discoverExecutable();

    // Returns true if at least one tailscale-ipn.exe process is currently running.
    // Used to avoid launching a duplicate when the user already started Tailscale
    // manually (or from a previous StreamLight session).
    static bool isAlreadyRunning();

    // Launches Tailscale (tailscale-ipn.exe) as a detached background process.
    // Returns true on success. Fire-and-forget: StreamLight does not keep a handle
    // to the spawned process and is not responsible for its lifetime.
    static bool launch(const QString& exePath);
};
