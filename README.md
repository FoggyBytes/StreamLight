## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](.badges/downloads.svg)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-09-18 222853" src="https://github.com/user-attachments/assets/99e8f5da-d31c-47e8-80b0-a40b1118acc8" />
</div>

**StreamLight** is the client half of the FoggyBytes streaming duo: a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with a gamepad-first interface and native integration with its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak).

The streaming engine is upstream Moonlight's — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c` — with Nonary's VRR pacing added in 6.0.0. What is new sits around it: the interface, and everything the two apps can do together over a local TCP bridge — host link matching, host metrics in the overlay, the store each game comes from, session quality reports, remote power-off and Windows Update, Tailscale, and signing a woken host in with its PIN from the sofa.

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-09-18 222938" src="https://github.com/user-attachments/assets/db14c1d5-e703-472d-a136-f636625252dc" />
</div>

## ✅ Compatibility

Windows 10 and 11. Works as an ordinary Moonlight-compatible client against any **Sunshine / Apollo / Vibeshine / Vibepollo** host, and unlocks its paired feature set when [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak) is running on the host.

> 🔐 **The bridge is authenticated.** Every command StreamLight sends is signed with its existing Moonlight identity certificate; the host approves each client once, via a 4-digit PIN shown on both screens. **Streaming never depends on it** — without approval you stream normally and simply lose the paired features. Each host card shows its state as a badge (AUTHORIZED / PENDING / DENIED).

> ⚠️ **Not affiliated with or endorsed by the Moonlight project.** StreamLight is an independent fork. For upstream Moonlight support, use the [official client](https://github.com/moonlight-stream/moonlight-qt).

## 🔥 Features

Everything below is in the current release, whichever version first introduced it.

**🕹️ Gamepad-first, keyboard-equal**
- Every action is reachable from the pad: D-pad across host tabs, library, settings tabs and dialogs, with a clickable prompt bar along the bottom
- **Prompts follow the device in your hands** — touch the keyboard and each glyph becomes the key to press; pick the pad back up and they return to that controller's own icons (Xbox / PlayStation / Nintendo, auto-detected or forced). *Settings → Session* can pin them to the controller or to the keyboard *(6.3.0+)*, for Steam Input and the Steam Deck, where one pad also sends clicks and keys
- **Rebindable shortcuts** — every in-stream keyboard hotkey and all three controller combos, in *Settings → Shortcuts*. Defaults are **LB + RB + A** quit, **+ X** performance overlay, **+ B** stream settings, chosen to stay clear of Steam's overlay

**🏠 Home and the host page**
- **Home** is your hosts as tabs under the wordmark. The selected one's card takes the left of the screen — name, state, addresses, stream settings and actions at once — and its game stands beside it. **LT / RT** move between hosts, **LB / RB** between that host's profiles
- **Moving background** *(6.0.0+)* — waves in your accent colour rise over the lower three quarters of Home, Settings and the PIN pad, twice as fast while a host is streaming. Still with *Reduce animations*, paused behind dialogs
- **Opening animation** *(6.0.0+)* — the waves rise, the icon fades in and a band of light writes STREAMLIGHT before Home appears; any button skips it, and *Settings → Session* turns it off
- **The host page** puts the library down the left at full height and the game in the spotlight beside it — cover, name, store, and the right verb (*Resume* if it is already running, *Play* if not)
- **All, Games and Apps** *(5.9.0+, All 6.1.0+)* — **LB / RB** switch the host page between everything you can launch, the games, and everything else: Desktop, Virtual Display, Steam Big Picture and the host's controls, which open with *Open* and never count hours. The page reopens on the tab you last chose on that host *(6.3.0+)*, All the first time; **LT / RT** move the host's profile
- **Move between Games and Apps** *(6.1.0+)* — the **right stick click** or **M** moves the selected entry to the other tab, remembered per host. The host's own controls stay on Apps
- **Remote Input and Remote Monitor** *(5.9.0+)* — the host controls of Vibeshine and Vibepollo 2.0 open beside a running game, and their results and confirmation requests appear as messages and Yes/No questions rather than errors
- **Last played** *(5.7.0+)* — the game you last streamed on that host fills the right of its card, with how long ago you left it and the hours you have played it in total. **Play again** starts it without opening the library, and the same game sits first on the host page under *Last played*
- **Play time** *(5.7.0+)* — how long you have streamed each game, beside the store on every row. Filed under the game's name, so it survives a reinstall, and resettable from the per-game panel
- **Pinned games** *(6.0.0+)* — **Start** on the pad (Menu / Options / +) or **P** pins the selected game: pinned games sit under their own heading right after *Last played*, alphabetically, marked with a pin on the row. Kept per host and by name, so a pin survives a reinstall
- **Resume from Home** *(6.0.0+)* — while a host is streaming, its card shows the game in progress, marked *Streaming now*, with **Resume** in place of *Play again*
- **Per-host backgrounds** — a colour you pick or a picture of your own, with the card's gradient derived from it, and *(6.0.0+)* how much of the moving background shows through the card, from 70% to 100%
- **Your accent colour** — five presets or any hex code. Status colours never follow it: online stays green, a pending link change amber, *Shutdown* red
- **Time, date and battery** in the top right of every screen, in the clock and date format you read

**🎬 In-stream**
- **Performance overlay, built line by line** — thirteen lines to choose from, switched on and off *on the overlay itself* in Settings, plus corner, text colour, font size and transparency. Minimal / Default / Full remain as starting points
- **Stream Settings panel** — change resolution, frame rate, bitrate, HDR and frame pacing **while streaming**, applied with a brief reconnect and host-agnostic. It takes the corner the performance overlay is not using
- **Custom resolutions and frame rates** — any width and height, and any rate, not just the presets, from Settings or the in-stream panel
- **Your display's own values are offered too** *(5.5.0+)* — the resolution and refresh rate this machine reports appear in the pickers, so a 165 Hz or 16:10 screen needs nothing typed in. Marked with a dot in *Settings* and with the words *this display* in the in-stream panel, which offers the same list
- **Frame pacing** — Off or On, evening frames out with the same software pacer Moonlight uses
- **Fractional V-Sync** *(5.6.0+)* — shows each frame for a whole number of refreshes instead of once per refresh, so 60 FPS on a 120 Hz screen becomes one frame every two. Needs V-Sync and frame pacing, and a screen running at an exact multiple of the frame rate: at 60 FPS that is 120 / 180 / 240 Hz, and a 144 Hz screen wants 72 FPS. Off by default
- **VRR** *(6.0.0+, experimental)* — on a variable-refresh display each frame is shown as soon as it is ready, with three timing profiles (*Low latency*, *Balanced*, *Smooth*) and *Reduce judder*. Built on Nonary's VRR work for Moonlight; needs V-Sync, runs borderless, and switches Fractional V-Sync off while it is on

**⚙️ Settings and profiles**
- Ten tabs, pill-style selectors instead of dropdowns, inline subtitles instead of tooltips, and a bitrate slider with hold-to-accelerate and a **Default** prompt
- **Per-host profiles** — up to three named profiles per host, each overriding resolution, frame rate, bitrate, HDR, codec, display mode, V-Sync, frame pacing, fractional V-Sync, VRR, audio, link matching, launch wait and Hue. Switchable from Home or the host page
- **Per-game overrides** on top of the active profile, for the settings that vary by title
- **Profiles in tabs** *(6.0.0+)* — the host profile and per-game dialogs are split into Settings' own tabs, one section at a time, switched with **LB / RB** drawn at the ends of the strip, each tab showing how many of its values are changed
- **Inherited values look inherited** *(6.0.0+)* — the accent marks only what a profile or a game changes, every row says where its value comes from, **Y** gives a row back, and **LT / RT** switch profile anywhere in the dialog. **X** names a profile from eight ready-made names, or your own with a keyboard
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
- **Seamless launch** *(8.1.0+, opt-in)* — with **Wait for the game to appear** on, the stream window stays hidden until the host reports the game is really on screen, so you watch the game's cover art instead of the host's desktop rearranging itself. **B** or **Esc** reveals the host at any moment. A resume never waits: the game is already there
- **Remote PIN unlock** *(8.1.0+)* — after a **Wake**, if the host comes up at its lock screen, a controller-navigable number pad takes its Windows PIN. The session carrying the PIN is never shown and never recorded on the host; wrong attempts stop at three, since Windows suspends the PIN after a few failures
- **Host metrics in the overlay** *(4.4.0+)* — GPU %, encoder %, GPU temperature, VRAM, CPU and network TX, hidden entirely when StreamTweak is unreachable
- **Store badges** *(5.0.0+)* — which store the selected game comes from, its mark beside its name on the host page: Steam, Epic, GOG, Ubisoft, Xbox, Battle.net and EA App
- **Session quality reporting** *(5.2.0+)* — FPS, drops, RTT, jitter, decode latency and bitrate sent every second; StreamTweak turns them into a grade and charts
- **Delivered vs target bitrate** *(8.0.0+)* — StreamLight reports the rate it was told to aim for, so the host can show what it actually delivered against it. Neither side can work that out alone
- **Remote host power-off** *(7.2.0+)* — a **Power…** chooser for the host, this PC, or both, on an authorized host only
- **Sleep and restart** *(8.6.0+)* — one row per machine in the Power chooser, each offering only what that machine supports
- **Remote Windows Update** *(7.3.0+)* — scan, classify and install updates on the host, rebooting only if required, with a backgroundable progress view. Updates can also be installed before a shutdown
- **Remote session pause** *(6.0.0+)* — the Pause button on StreamTweak's dashboard ends the stream client-side
- **Tailscale in one tile** *(6.3.0+)* — a host reachable both on the LAN and over Tailscale stays a single tile that tracks both addresses and uses whichever is available, with an option to force the `100.x` endpoint. Pairs with the **Auto-start Tailscale** toggle, so opening StreamLight is enough to stream from anywhere
- **Shared clipboard** *(8.7.0+)* — text copied on the host pastes on this device and the other way round while you stream, up to 32 KB, encrypted with a key that lasts one stream. Passwords from a password manager are cleared on the other side when the original is, within 60 seconds anyway. Off until you turn it on under *Clipboard* in *Settings → StreamTweak*; the host has its own switch

## ✨ What's New in 6.3.0 — Where You Left It

The shared clipboard needs **StreamTweak 8.7.0** on the host; the rest is client-side, any host.

- **Shared clipboard** — copy on one side, paste on the other while you stream: encrypted, text up to 32 KB, passwords cleared within 60 seconds. Turn it on in *Settings → StreamTweak*
- **Button prompts: Auto, Controller or Keyboard & mouse** — pin the prompts to the controller when Steam Input sends clicks and keys from the same pad
- **Resume shows the stream at once** — from Home or the host page, even with *Wait for the game to appear* on
- **The host page reopens on your last tab** — All, Games or Apps, per host, also after closing StreamLight
- **Controller prompts sooner** — from the first screen with a pad connected, and on the stick and triggers too

*Older releases are in [changelog.txt](changelog.txt).*

## 🏗️ Architecture

A Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline — FFmpeg, D3D11VA, DXVA2, libplacebo — and the protocol, `moonlight-common-c`, are upstream's, and they track Moonlight's **development branch** rather than its releases: upstream has not tagged one since v6.1.0 in September 2024, while its master branch is still moving. As of 5.9.0 our copy of `moonlight-common-c` is identical to master's again, and upstream's application fixes are carried over as they land. The VRR pacing *(6.0.0+)* is Nonary's, imported unchanged so that it can be kept in step with the original. The UI layer is ours.

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
- [**Nonary's VRR work for Moonlight**](https://github.com/Nonary/moonlight-qt) — the VRR pacing in 6.0.0 is Chase Payne's code, imported as it is (tag `v6.1.0-vrr17.1`) together with its test harness
- [**Sunshine**](https://github.com/LizardByte/Sunshine) — the streaming host that started it all
- [**Apollo**](https://github.com/ClassicOldSong/Apollo) — community-driven Sunshine fork
- [**Vibeshine**](https://github.com/Nonary/vibeshine) and [**Vibepollo**](https://github.com/Nonary/Vibepollo) — fully supported

## License
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

StreamLight is released under the GPL v3 License, in accordance with the upstream Moonlight license.
