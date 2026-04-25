## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](https://img.shields.io/github/downloads/FoggyBytes/StreamLight/total?label=Downloads&color=orange)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

**StreamLight** is the official FoggyBytes fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with native [StreamTweak](https://github.com/FoggyBytes/StreamTweak) integration. It adds host NIC control, live host metrics in the overlay, store badges on game covers, and session quality reporting — all from the client side.

## ✅ Compatibility

Windows 10 and 11. Requires [StreamTweak](https://github.com/FoggyBytes/StreamTweak) running on the host PC for StreamTweak-specific features.

> ⚠️ **Not affiliated with or endorsed by the Moonlight project.** StreamLight is an independent fork. For upstream Moonlight support, use the [official client](https://github.com/moonlight-stream/moonlight-qt).

## 🔥 Features

**🔗 NIC Control from the Client**
- Right-click any paired host to send the speed-change command before connecting, with a built-in 10-second countdown and auto-revert if no connection follows within 30 seconds
- Show current host NIC speed without starting a stream

**🖥️ Host Metrics in Overlay** *(requires StreamTweak 4.4.0+)*
- Live GPU %, encoder %, GPU temperature, VRAM used/total, CPU %, and network TX displayed in the performance overlay
- Section hidden entirely when StreamTweak is unreachable — no visual clutter

**🎮 Store Badges on Game Covers** *(requires StreamTweak 5.0.0+)*
- Per-game store badge (icon + name) overlaid bottom-right on each cover: Steam, Epic Games, GOG, Ubisoft Connect, Xbox, Battle.net, EA App
- Fetched live from the host via the APPSTORES command on the TCP bridge

**📋 Session Quality Report** *(requires StreamTweak 5.2.0+)*
- Client-side metrics (FPS, frame drops, RTT, jitter, decode latency, bitrate) streamed to StreamTweak every second during the session
- StreamTweak uses this data to generate a quality grade (Excellent / Good / Poor) and sparkline charts visible in the Logs tab

**⏸️ Remote Session Pause** *(requires StreamTweak 6.0.0+)*
- StreamTweak's Home page shows a Pause button when a session is active; pressing it terminates the stream on the client side exactly as if the user had pressed Pause locally
- Signal piggybacked on the existing per-second STATS channel — no new connection or protocol change required

**💡 Philips Hue Sync Integration**
- Optional toggle in Settings to automatically start Philips Hue Sync on the client PC when a streaming session begins and close it when the session ends
- Launched silently in the background using HueSyncStarter.exe — no window appears; install path resolved via the Windows Uninstall registry, so custom install locations are supported

**🎨 Visual Identity**
- UI fully aligned with StreamTweak — color palette, spacing, and component styling match across both apps
- App list sort order: Desktop first, Steam Big Picture second, all others alphabetically

## ✨ What's New in 2.3.1 — "The Live Charts Update"

- **Session telemetry send interval** — SESSIONDATA is now sent once per second instead of every 10 seconds; StreamTweak's live session charts on the Home page update every second as intended *(requires StreamTweak 6.1.0+)*

<details>
<summary>2.3.0 — "The Remote Pause Update"</summary>

- **Remote session pause** — StreamTweak's Home page now shows a Pause button when a session is active; pressing it stops the stream on the client side with no UI changes required in StreamLight *(requires StreamTweak 6.0.0+)*
</details>

For full version history see [changelog.txt](changelog.txt).

## 🏗️ Architecture

StreamLight communicates with StreamTweak over a plain TCP bridge on **port 47998** (LAN). Commands: `PREPARE`, `RESTORE`, `STATUS`, `STATS`, `APPSTORES`. The same bridge is used to send NIC commands from the client side and to receive host metrics and store data.

```
StreamLight (Qt, client PC)
    │  TCP port 47998
    ▼
StreamTweak (WinUI3, host PC)  →  Named Pipe  →  StreamTweakService (LocalSystem)
                                                          │
                                                          ▼
                                               NIC speed via CIM/WMI
```

## 📝 Installation

1. Go to the **Releases** page of this repository.
2. Download the latest installer and run it.

## 🙏 Support the Project
[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/foggypunk)

## 🤝 Acknowledgements

- [**Moonlight**](https://github.com/moonlight-stream/moonlight-qt) — the open-source streaming client this fork is built on; full credit to the Moonlight contributors
- [**Sunshine**](https://github.com/LizardByte/Sunshine) — the streaming host that started it all
- [**Apollo**](https://github.com/ClassicOldSong/Apollo) — community-driven Sunshine fork
- [**Vibeshine**](https://github.com/Nonary/vibeshine) and [**Vibepollo**](https://github.com/Nonary/Vibepollo) — fully supported since v2.5.2

## License
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

StreamLight is released under the GPL v3 License, in accordance with the upstream Moonlight license.
