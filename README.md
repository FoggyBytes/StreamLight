## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](.badges/downloads.svg)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-09-09 170725" src="https://github.com/user-attachments/assets/6f2aca2f-8df6-4ff3-847f-a71cb450fbc8" />
</div>

**StreamLight** is the client half of the FoggyBytes streaming duo: a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with a gamepad-first interface and native integration with its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak).

The streaming engine is untouched from upstream Moonlight — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c`. What is new sits around it: the interface, and everything the two apps can do together over a local TCP bridge — host link matching, host metrics in the overlay, the store each game comes from, session quality reports, remote power-off and Windows Update, Tailscale, and signing a woken host in with its PIN from the sofa.

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-09-09 170804" src="https://github.com/user-attachments/assets/554bb031-2b48-4696-951f-e9d1ed5b98b3" />
</div>

## ✅ Compatibility

Windows 10 and 11. Works as an ordinary Moonlight-compatible client against any **Sunshine / Apollo / Vibeshine / Vibepollo** host, and unlocks its paired feature set when [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak) is running on the host.

> 🔐 **The bridge is authenticated.** Every command StreamLight sends is signed with its existing Moonlight identity certificate; the host approves each client once, via a 4-digit PIN shown on both screens. **Streaming never depends on it** — without approval you stream normally and simply lose the paired features. Each host card shows its state as a badge (AUTHORIZED / PENDING / DENIED).

> ⚠️ **Not affiliated with or endorsed by the Moonlight project.** StreamLight is an independent fork. For upstream Moonlight support, use the [official client](https://github.com/moonlight-stream/moonlight-qt).

## 🔥 Features

Everything below is in the current release, whichever version first introduced it.

**🕹️ Gamepad-first, keyboard-equal**
- Every action is reachable from the pad: D-pad across host tabs, library, settings tabs and dialogs, with a clickable prompt bar along the bottom
- **Prompts follow the device in your hands** — touch the keyboard and each glyph becomes the key to press; pick the pad back up and they return to that controller's own icons (Xbox / PlayStation / Nintendo, auto-detected or forced)
- **Rebindable shortcuts** — every in-stream keyboard hotkey and all three controller combos, in *Settings → Shortcuts*. Defaults are **LB + RB + A** quit, **+ X** performance overlay, **+ B** stream settings, chosen to stay clear of Steam's overlay

**🏠 Home and the host page**
- **Home** is your hosts as tabs under the wordmark, the selected one filling the screen: name, state, addresses, stream settings and actions at once. **LT / RT** move between hosts, **LB / RB** between that host's profiles
- **The host page** puts the library down the left at full height and the game in the spotlight beside it — cover, name, store, and the right verb (*Resume* if it is already running, *Play* if not)
- **Last played** *(5.7.0+)* — the game you last streamed on that host fills the right of its card, with the hours behind it and how long ago you left it. **Play again** starts it without opening the library, and the same game sits first on the host page under *Last played*
- **Play time** *(5.7.0+)* — how long you have streamed each game, beside the store on every row. Filed under the game's name, so it survives a reinstall, and resettable from the per-game panel
- **Per-host backgrounds** — a colour you pick or a picture of your own, with the card's gradient derived from it
- **Your accent colour** — five presets or any hex code. Status colours never follow it: online stays green, a pending link change amber, *Shutdown* red
- **Time, date and battery** in the top right of every screen, in the clock and date format you read

**🎬 In-stream**
- **Performance overlay, built line by line** — eleven lines to choose from, switched on and off *on the overlay itself* in Settings, plus corner, text colour, font size and transparency. Minimal / Default / Full remain as starting points
- **Stream Settings panel** — change resolution, frame rate, bitrate, HDR and frame pacing **while streaming**, applied with a brief reconnect and host-agnostic. It takes the corner the performance overlay is not using
- **Custom resolutions and frame rates** — any width and height, and any rate, not just the presets, from Settings or the in-stream panel
- **Your display's own values are offered too** *(5.5.0+)* — the resolution and refresh rate this machine reports appear in the pickers, so a 165 Hz or 16:10 screen needs nothing typed in. Marked with a dot in *Settings* and with the words *this display* in the in-stream panel, which offers the same list
- **Frame pacing** — Off or On, evening frames out with the same software pacer Moonlight uses
- **Fractional V-Sync** *(5.6.0+)* — shows each frame for a whole number of refreshes instead of once per refresh, so 60 FPS on a 120 Hz screen becomes one frame every two. Needs V-Sync and frame pacing, and a screen running at an exact multiple of the frame rate: at 60 FPS that is 120 / 180 / 240 Hz, and a 144 Hz screen wants 72 FPS. Off by default

**⚙️ Settings and profiles**
- Ten tabs, pill-style selectors instead of dropdowns, inline subtitles instead of tooltips, and a bitrate slider with hold-to-accelerate and a **Default** prompt
- **Per-host profiles** — up to three named profiles per host, each overriding resolution, frame rate, bitrate, HDR, codec, display mode, V-Sync, frame pacing, fractional V-Sync, audio, link matching, launch wait and Hue. Switchable from Home or the host page
- **Per-game overrides** on top of the active profile, for the settings that vary by title
- A setting that cannot act says so wherever you meet it — greyed, with the reason on the line beneath, in Settings, in the profile and in the per-game dialog alike
- Every change is written to disk the moment you make it
- **Update from the app** *(5.8.0+)* — *Settings → About* downloads a newer release when there is one and checks it against GitHub's checksum; *Install now* opens the installer, which reopens StreamLight when it finishes, after a single Windows permission prompt. A newer version is also announced at startup

**🎯 Windows Xbox app integration**
- Branded tile artwork in the Windows 11 Xbox app's "My apps" section, seeded during setup and re-applied automatically whenever the Xbox app overwrites it

**💡 Philips Hue Sync**
- Optional: starts Hue Sync on this PC when a session begins and closes it when it ends, silently, with the install path resolved from the registry

## 🔗 Paired Features (with StreamTweak)

These cross the bridge and need both apps. The version shown is the **minimum StreamTweak** on the host.

All of them are switched on **per host**, in **Settings → StreamTweak** — a host added from StreamLight 5.2.0 on starts off, and hosts you were already using StreamTweak with are switched on for you on first run. Streaming itself is never affected either way.

- **Host link matching** *(8.1.0+)* — before each launch StreamLight measures the wired link that actually reaches that host, asks the host to come down to it, and starts the stream only once the host confirms. A host running faster than the client sends each frame as a burst the slower link cannot drain, and the packets that die first are the few carrying audio: the symptom is sound cutting out while the picture stays perfect. The client decides the speed because only the client knows its own connection; the host keeps the permission and the restore
- **Seamless launch** *(8.1.0+, opt-in)* — with **Wait for the game to appear** on, the stream window stays hidden until the host reports the game is really on screen, so you watch the game's cover art instead of the host's desktop rearranging itself. **B** or **Esc** reveals the host at any moment
- **Remote PIN unlock** *(8.1.0+)* — after a **Wake**, if the host comes up at its lock screen, a controller-navigable number pad takes its Windows PIN. The session carrying the PIN is never shown and never recorded on the host; wrong attempts stop at three, since Windows suspends the PIN after a few failures
- **Host metrics in the overlay** *(4.4.0+)* — GPU %, encoder %, GPU temperature, VRAM, CPU and network TX, hidden entirely when StreamTweak is unreachable
- **Store badges** *(5.0.0+)* — which store the selected game comes from, its mark beside its name on the host page: Steam, Epic, GOG, Ubisoft, Xbox, Battle.net and EA App
- **Session quality reporting** *(5.2.0+)* — FPS, drops, RTT, jitter, decode latency and bitrate sent every second; StreamTweak turns them into a grade and charts
- **Delivered vs target bitrate** *(8.0.0+)* — StreamLight reports the rate it was told to aim for, so the host can show what it actually delivered against it. Neither side can work that out alone
- **Remote host power-off** *(7.2.0+)* — a **Power…** chooser for the host, this PC, or both, on an authorized host only
- **Remote Windows Update** *(7.3.0+)* — scan, classify and install updates on the host, rebooting only if required, with a backgroundable progress view. Updates can also be installed before a shutdown
- **Remote session pause** *(6.0.0+)* — the Pause button on StreamTweak's dashboard ends the stream client-side
- **Tailscale in one tile** *(6.3.0+)* — a host reachable both on the LAN and over Tailscale stays a single tile that tracks both addresses and uses whichever is available, with an option to force the `100.x` endpoint. Pairs with the **Auto-start Tailscale** toggle, so opening StreamLight is enough to stream from anywhere

## ✨ What's New in 5.8.0 — Keep Up

StreamLight updates itself from *Settings*, *Play again* behaves like the rest of the host card and ends where a game started from the library ends, and waking a host now says how long each step is taking. Client-side, works with any host.

- **Update now, in *Settings → About*.** It appears only when a newer release is out, downloads the installer and checks it against the checksum GitHub publishes; **Install now** then opens the installer, which reopens StreamLight when it finishes — never before you press it, and with the one Windows permission prompt any install into *Program Files* asks for. If something goes wrong it stays on screen and says what, and the same button retries. A newer version is also announced when the app starts, unless you tick *Don't remind me* for it. Versions before 5.8.0 cannot do this, so this one is installed by hand
- **Play again greys out with the card.** It stayed lit and pressable while the link speed was changing — the one button that did not follow the host's state — and on an offline host it vanished instead of greying, leaving the game it names on screen with nothing to do about it
- **A greyed button can no longer be pressed with the controller.** During a link speed change the mouse was blocked and the D-pad was not, so **A** on a dimmed *Open* started a stream into an adapter that was still renegotiating
- **Play again ends on the host's page**, with that game at the top, instead of back on the host list — a session started from the card now ends exactly where one started from the library does. Coming back from there also asks whether to put the host's link back: that question is only ever raised on the way from a host's page to the host list, and a game launched from the card never made that trip
- **Waking a host is timed, step by step.** The host appearing on the network and StreamTweak coming up each count up in tenths of a second while they are the one being waited on, and keep the figure once they are done — so a slow wake says which part was slow rather than only which part is still running. The first step is called *Wake signal sent* now, not *Magic packet sent*
- **A wake that cannot be sent stops there** and says so, rather than waiting out the full two and a half minutes for a host that was never told anything. One that gives up leaves the dialog on screen with the step that ran out and how long it was waited on, where it used to close with nothing said

*Older releases are in [changelog.txt](changelog.txt).*

## 🏗️ Architecture

A Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline — FFmpeg, D3D11VA, DXVA2, libplacebo — and the protocol, `moonlight-common-c`, are upstream's, and they track Moonlight's **development branch** rather than its releases: upstream has not tagged one since v6.1.0 in September 2024, while its master branch is still moving. As of 5.2.0 our copy of `moonlight-common-c` is identical to master's. The UI layer is ours.

Integration with StreamTweak runs over a TCP bridge on **port 47998** (LAN, line-delimited ASCII), carrying link speed (`NETINFO`, `SETSPEED`), host metrics (`STATS`), store data (`APPSTORES`), telemetry (`SESSIONDATA`), Tailscale presence, launch state (`GAMESTATE`), lock state, and the power and Windows Update commands. Each command is preceded by an `AUTH1` line signing it with the client's Moonlight certificate (RSA-SHA256); a one-time `ENROLL` registers the client with the host for approval.

```
StreamLight (Qt, client PC)
    │  TCP port 47998
    ▼
StreamTweak (WinUI 3, host PC)  →  Named Pipe  →  StreamTweakService (LocalSystem)
                                                           │
                                                           ▼
                                                NIC speed via CIM/WMI
                                                Host assets via filesystem
                                                Windows Update via WUA
```

## 📝 Installation

Download the latest installer from the [Releases](https://github.com/FoggyBytes/StreamLight/releases) page and run it.

Settings — paired hosts, video / audio / input preferences, client certificate — live under `HKCU\Software\FoggyBytes\StreamLight`, and box art is cached in `%LOCALAPPDATA%\FoggyBytes\StreamLight`. Upgrades from 5.4.0 onward keep everything.

Up to 5.3.0 both lived under `Moonlight Game Streaming Project\Moonlight` — upstream Moonlight's own store, shared with it. 5.4.0 moved out of it and does not migrate anything, so the upgrade to 5.4.0 resets settings and pairing once. The old store is left untouched: an older StreamLight, or a Moonlight installation, still finds its data there.

## 🙏 Support the Project
[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/foggypunk)

## 🤝 Acknowledgements

- [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak) — the host-side companion, designed in lockstep with StreamLight
- [**Moonlight**](https://github.com/moonlight-stream/moonlight-qt) — the open-source client this fork is built on; full credit to its contributors
- [**Sunshine**](https://github.com/LizardByte/Sunshine) — the streaming host that started it all
- [**Apollo**](https://github.com/ClassicOldSong/Apollo) — community-driven Sunshine fork
- [**Vibeshine**](https://github.com/Nonary/vibeshine) and [**Vibepollo**](https://github.com/Nonary/Vibepollo) — fully supported

## License
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

StreamLight is released under the GPL v3 License, in accordance with the upstream Moonlight license.
