## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](https://img.shields.io/github/downloads/FoggyBytes/StreamLight/total?label=Downloads&color=orange)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

**StreamLight** is the client-side half of the FoggyBytes streaming duo: the official FoggyBytes fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with native integration for its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak). It adds host NIC control, live host metrics in the overlay, store badges on game covers, session-quality reporting and a Tailscale dual-tile workflow — all driven from the client over a local TCP bridge to StreamTweak. From **3.0.0** the entire UI has been redesigned from the ground up with a flat, gamepad-first look inspired by the Xbox and Steam Big Picture interfaces, while the underlying streaming engine (FFmpeg / D3D11VA / DXVA2 / libplacebo / `moonlight-common-c`) is unchanged from 2.3.1.

<div align="center">
  <img width="960" height="540" alt="StreamLight 3.0 — Home screen with portrait host cards, inline Add Hosts tile and bottom status bar" src="https://github.com/user-attachments/assets/c4000b75-0700-48e3-b715-db1622ebbf68" />
</div>

## ✅ Compatibility

Windows 10 and 11. Works as a standalone Moonlight-compatible client against any Sunshine / Apollo / Vibeshine / Vibepollo host, **and** unlocks its full feature set when paired with [**StreamTweak 6.3.0**](https://github.com/FoggyBytes/StreamTweak) on the host PC (Tailscale dual-tile, live NIC speed transitions, host metrics overlay, store badges, session-quality reporting, remote pause).

> ⚠️ **Not affiliated with or endorsed by the Moonlight project.** StreamLight is an independent fork. For upstream Moonlight support, use the [official client](https://github.com/moonlight-stream/moonlight-qt).

## 🔥 Features

**🕹️ Gamepad-First UI** *(new in 3.0.0)*
- Every action is reachable from the pad: D-pad navigation across host cards, app grid, settings tabs, and dialogs
- Contextual gamepad prompts in a clickable bottom status bar (A / B / X / Y / LB / RB), every prompt also clickable with the mouse
- Glyphs swap automatically between **Xbox** and **PlayStation** icons based on the connected controller (DualSense / DualShock / Xbox / generic)
- App list sort order: Desktop first, Steam Big Picture second, all others alphabetically

**🏠 Home & App Grid**
- Portrait host cards with per-host coloured gradient headers and an inline **+ Add Hosts** tile
- Per-card **Options** dropdown: View All Apps, Test Network, Rename, Delete, View Details, **StreamTweak Streaming Mode**, Wake
- Compact one-row header in the app grid: `hostname · ONLINE · IP · NIC speed · resolution · FPS`
- 200×267 covers with store badges and a tight bright-green focus border; the running app's cover gets a thicker, pulsing border
- App-name tooltip appears **instantly** below the cover, anchored outside the focus frame

**⚙️ Settings**
- 6 tabs (Video / Audio / Input / Decoder / Network / Session) with bold uppercase labels and LB/RB tab cycling
- Pill-style **segmented selectors** replace every dropdown — D-pad ◀ / ▶ pick the value, no popup, no focus leak
- Inline subtitles replace hover tooltips; legacy copy reviewed and shortened
- **Bitrate slider** with hold-to-accelerate, `Mbps` unit, and an **X / □ "Default"** prompt that restores the recommended value
- **Auto-start Tailscale on launch** *(new in 3.0.0)* — a Network-tab toggle that launches `tailscale-ipn.exe` in the background at every startup so remote hosts can be reached via their `100.x.y.z` Tailscale IP without keeping the Tailscale tray manually open. Pairs naturally with the Tailscale dual-tile workflow on Home (see Paired Features below). Requires the official Tailscale Windows installer from [tailscale.com/download](https://tailscale.com/download); the Microsoft Store package is not supported

**🎯 Windows Xbox App Integration** *(new in 3.0.0)*
- **Branded tile artwork** in the Windows 11 Xbox app's "My apps" section: StreamLight no longer shows the default grey square — a 1024×1024 dark-green gradient PNG with the swoosh glow is automatically placed in the tile, matching the visual quality of Steam, Epic Games Store, GOG GALAXY, Ubisoft Connect, EA App, and Battle.net
- **Pre-population on install** — an optional task during setup ("Add an icon to the Xbox app's 'My apps' section", default ON) seeds the `CustomLibraryManagement` manifest entry and tile PNG so StreamLight appears in the Xbox app on first open, no manual "+" click required
- **Runtime self-heal** — a `QFileSystemWatcher` re-applies the branded PNG within ~250 ms whenever the Xbox app rewrites the tile (e.g. user manually adds StreamLight via "+" while StreamLight is running, or Xbox app refreshes its metadata)
- **Boot-time check** — at every launch StreamLight verifies the tile PNG against the embedded artwork and refreshes it if needed (one-shot, idempotent, ~50 ms)

**💡 Philips Hue Sync Integration**
- Optional toggle in Settings to automatically start Philips Hue Sync on the client PC when a session begins and close it when it ends
- Launched silently in the background via `HueSyncStarter.exe` — no window appears; install path resolved through the Windows Uninstall registry, custom install locations supported

## 🔗 Paired Features (with StreamTweak)

These features cross the bridge and require both apps. The version next to each one is the **minimum** StreamTweak version on the host side.

- **NIC Control from the Client** *(StreamTweak 1.0+)* — Options → **StreamTweak Streaming Mode** sends `PREPARE` to the host before connecting, with a 10-second countdown and 30-second auto-revert if no connection follows. Current host NIC speed is shown on every host card and per-host header, **polled live every 2 seconds** — watch the host transition 2.5 Gbps → 1 Gbps when Streaming Mode kicks in, and back on inactivity
- **Host Metrics in Overlay** *(StreamTweak 4.4.0+)* — live GPU %, encoder %, GPU temperature, VRAM used/total, CPU %, and network TX in the performance overlay; section hidden entirely when StreamTweak is unreachable
- **Store Badges on Game Covers** *(StreamTweak 5.0.0+)* — per-game badge overlaid on each cover (Steam, Epic, GOG, Ubisoft, Xbox, Battle.net, EA App), fetched via the `APPSTORES` command
- **Session Quality Reporting** *(StreamTweak 5.2.0+)* — FPS, drops, RTT, jitter, decode latency, bitrate streamed every second; StreamTweak generates the quality grade and sparkline charts in its Logs tab
- **Remote Session Pause** *(StreamTweak 6.0.0+)* — Pause button on StreamTweak's Home page terminates the stream on the client side; signal piggybacked on the existing per-second `STATS` channel
- **Tailscale Dual-Tile** *(StreamTweak 6.3.0+, flagship of this release pair)* — after a successful pairing via LAN IP, StreamLight queries the new `TAILSCALE` bridge command. If StreamTweak detects a Tailscale `100.x.y.z` adapter on the host, a one-time popup offers to create a second tile pinned to that address. The LAN tile and the Tailscale tile coexist permanently; the Tailscale tile is **address-pinned**, so mDNS rediscovery on the LAN cannot collapse it back onto the parent. The underlying Moonlight UUID and server certificate are shared with the parent, so no second pairing is required. Combined with the new **Auto-start Tailscale on launch** Settings toggle on the client side, the full round-trip is automatic: open StreamLight → Tailscale comes up → the remote tile is live → one click streams from anywhere

## ✨ What's New in 3.0.0 — "The Big UI Update"

A full rewrite of the UI layer with the streaming engine of 2.3.1 left untouched. Settings storage is unchanged from 2.x — Qt continues to use `HKCU\Software\Moonlight Game Streaming Project\Moonlight`; upgrades preserve paired hosts, preferences and certificates transparently with no migration step.

**Navigation**
- Old sidebar and toolbar removed; every screen is full-width content
- Contextual gamepad prompts in a clickable bottom status bar (A / B / X / Y / LB / RB)
- Glyphs swap automatically between **Xbox** and **PlayStation** icons based on the connected controller
- B from Settings returns to the page that opened it (Home or the host's app grid)
- Min window size raised to 1280×720

**Home**
- Portrait host cards with per-host coloured gradients
- Inline **+ Add Hosts** tile + small square **Refresh** button next to the last host
- Per-card **Options** dropdown (View All Apps, Test Network, Rename, Delete, View Details, StreamTweak Streaming Mode, Wake) — no background dim
- **Live NIC-speed polling every 2 s** — watch the host transition 2.5 Gbps → 1 Gbps when StreamTweak Streaming Mode kicks in (and back, on inactivity timeout)
- **Tailscale dual-tile** *(requires StreamTweak 6.3.0+)* — when a host is paired via its LAN IP and StreamTweak reports Tailscale is installed, a one-time popup offers to add a second tile pinned to the host's `100.x.y.z` address. Internally `NvComputer` gained two new persisted fields (`aliasSuffix`, `isAddressPinned`); `ComputerManager`'s `m_KnownHosts` is now keyed by `storageKey()` (uuid + optional `#suffix`) instead of plain uuid, with the underlying Moonlight UUID and server certificate shared with the parent so no second pairing is needed
- Redesigned **Pair dialog** with a hero-size bright-green PIN, gamepad-friendly Cancel
- Redesigned **Add Host dialog** with a large IP TextField and D-pad navigation

**App grid (per host)**
- Compact one-row header: `hostname · ONLINE · IP · NIC speed · resolution · FPS`
- 200×267 covers with store badges and a tight bright-green focus border
- App name tooltip appears **instantly** below the cover, anchored outside the frame
- The running streaming session is marked by a **thicker, pulsing** green border on its cover; in-cover Play / Stop overlay removed in favour of A "Resume" / X "Stop" prompts in the status bar

**Settings**
- 6 tabs (Video / Audio / Input / Decoder / Network / Session) with bold uppercase labels and LB/RB tab cycling
- Pill-style **segmented selectors** replace every dropdown — D-pad ◀ / ▶ pick the value, no popup, no focus leak
- Inline subtitles replace hover tooltips (instant, no delay); legacy copy reviewed, shortened, non-Windows references removed
- **Auto-scroll on focus**: D-pad navigation keeps the focused row in view; scrollbar adapts dynamically to the active tab's content
- **Bitrate slider** with hold-to-accelerate (longer hold = faster delta), `Mbps` unit, **X / □ "Default"** status-bar prompt to restore the recommended bitrate
- **New: Auto-start Tailscale on launch** *(Network tab)* — when enabled, StreamLight launches `tailscale-ipn.exe` in the background at every startup so remote hosts can be reached via their `100.x.y.z` Tailscale IP without keeping the Tailscale tray manually open. The toggle persists across sessions and pairs naturally with the Tailscale dual-tile workflow on Home. Switching it ON shows a one-shot RESTART REQUIRED dialog; switching it OFF does **not** terminate the running Tailscale instance — a brief notice informs the user that Tailscale will keep running and auto-start is disabled for future launches. Skipped silently when Tailscale is already running (e.g. started manually or pinned to Windows startup). The official Windows installer from [tailscale.com/download](https://tailscale.com/download) is required; the Microsoft Store package is not supported
- Initial focus lands on the first control of the active tab instead of needing several D-pad presses

**Visual identity**
- Unified bright green `#00E676` accent across the whole UI (focus cursor, TabBar underline, ONLINE badge, slider handle, accent borders, hyperlinks)
- Ambient vertical gradient background (charcoal → deep green) on every section
- DM Sans + JetBrains Mono fonts embedded
- Mouse and gamepad focus indicators are visually consistent

**Windows Xbox App integration**
- Branded tile artwork in the Windows 11 Xbox app's "My apps" section — a 1024×1024 dark-green gradient PNG with the swoosh glow, matching the visual quality of the curated launchers
- **Pre-population on install** — a new optional task "Add an icon to the Xbox app's 'My apps' section" (default ON) seeds the `CustomLibraryManagement` manifest entry and tile PNG under `%LocalAppData%`, so StreamLight already appears in "My apps" when the Xbox app is opened. Runs as the original non-elevated user via Inno Setup's `runasoriginaluser` flag so the manifest lands in the correct user profile
- **Runtime self-heal** — a `QFileSystemWatcher` on the `CustomLibraryManagement` and `Images` folders re-applies the branded PNG within ~250 ms whenever the Xbox app rewrites the tile
- **Boot-time check** — at every launch StreamLight verifies the tile PNG against the embedded artwork and refreshes it if needed (one-shot, idempotent, ~50 ms)
- **Diagnostic log** at `%TEMP%\streamlight-xbox-tile.log` records each step (`registerEntry` / `applyIfRegistered` / watcher events) for troubleshooting

**Under the hood**
- **Single-instance guard** — launching StreamLight while it is already running brings the existing window to the foreground (de-minimized, raised, activated) and exits the second invocation immediately; uses a per-user `QLocalServer` named pipe (`StreamLight-SingleInstance`); CLI subcommands (`stream` / `quit` / `pair` / `list`) remain re-entrant
- Streaming engine (FFmpeg / D3D11VA / DXVA2 / libplacebo / `moonlight-common-c`) untouched — same protocol and decoder pipeline as 2.3.1
- `ComputerModel` exposes new `address` and `gpuModel` roles to QML
- `SdlGamepadKeyNavigation` exposes `controllerType` (xbox / ps / switch / generic) and `simulateKey()` for click-to-pad bridging
- 7 dead UI files + 11 orphan SVG icons removed from the build

⚠️ Requires StreamTweak 6.3.0 or later on the host PC for the Tailscale dual-tile workflow. Earlier versions (6.1.0+) remain fully compatible but won't expose the Tailscale endpoint — StreamLight will only show the LAN tile in that case.

For full version history see [changelog.txt](changelog.txt).

## 🏗️ Architecture

StreamLight is a Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline (FFmpeg / D3D11VA / DXVA2 / libplacebo / `moonlight-common-c`) is identical to upstream Moonlight; the UI layer was rewritten for 3.0.

Integration with StreamTweak happens over a plain TCP bridge on **port 47998** (LAN, line-delimited ASCII). Commands sent by StreamLight: `PREPARE`, `RESTORE`, `STATUS`, `STATS`, `APPSTORES`, `TAILSCALE`. The same bridge is used to send NIC commands from the client and to receive host metrics, store data, Tailscale presence, and remote-pause signals.

```
StreamLight (Qt, client PC)
    │  TCP port 47998
    ▼
StreamTweak (WinUI 3, host PC)  →  Named Pipe  →  StreamTweakService (LocalSystem)
                                                           │
                                                           ▼
                                                NIC speed via CIM/WMI
                                                Host assets via filesystem
```

## 📝 Installation

1. Go to the **Releases** page of this repository.
2. Download the latest installer (`StreamLight_3.0.0_Installer.exe`) and run it.

Settings (paired hosts, video / audio / input preferences, client certificate) are stored under `HKCU\Software\Moonlight Game Streaming Project\Moonlight` — the same location used by upstream Moonlight and StreamLight 2.x. Upgrades from 2.x preserve all your hosts and preferences automatically. Box-art cache lives in `%LOCALAPPDATA%\Moonlight Game Streaming Project\Moonlight`.

## 🙏 Support the Project
[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/foggypunk)

## 🤝 Acknowledgements

- [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak) — the host-side companion, designed in lockstep with StreamLight
- [**Moonlight**](https://github.com/moonlight-stream/moonlight-qt) — the open-source streaming client this fork is built on; full credit to the Moonlight contributors
- [**Sunshine**](https://github.com/LizardByte/Sunshine) — the streaming host that started it all
- [**Apollo**](https://github.com/ClassicOldSong/Apollo) — community-driven Sunshine fork
- [**Vibeshine**](https://github.com/Nonary/vibeshine) and [**Vibepollo**](https://github.com/Nonary/Vibepollo) — fully supported since v2.5.2

## License
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

StreamLight is released under the GPL v3 License, in accordance with the upstream Moonlight license.
