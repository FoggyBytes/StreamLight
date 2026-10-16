## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](.badges/downloads.svg)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

> ⚠️ **Windows SmartScreen and Smart App Control.** StreamLight is not code-signed yet.
> - **SmartScreen** may warn you when you run the installer: choose *More info → Run anyway*.
> - **Smart App Control**, on the Windows 11 PCs where it is turned on, blocks StreamLight outright and offers no per-app exception. The only way to run it there is to turn Smart App Control off (*Windows Security → App & browser control → Smart App Control settings*).
>
> Keeping Smart App Control on is a perfectly reasonable choice. Code signing is being looked into, and this note will be updated if that changes. The full source is in this repository.

<div align="center">
  <img width="3840" height="2160" alt="StreamLight in the light theme" src=".github/images/streamlight.png" />
</div>

**StreamLight** is the client half of the FoggyBytes streaming duo: a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with a gamepad-first interface and native integration with its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak).

The streaming engine is upstream Moonlight's — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c` — with Nonary's VRR pacing added in 6.0.0 and his PyroWave codec in 6.4.0. What is new sits around it: the interface, and everything the two apps can do together over a local TCP bridge — host link matching, host metrics in the overlay, the store each game comes from, session quality reports, remote power-off and Windows Update, Tailscale, and signing a woken host in with its PIN from the sofa.

<div align="center">
  <img width="3840" height="2160" alt="StreamLight in the dark theme" src=".github/images/streamdark.png" />
</div>

## ✅ Compatibility

Windows 10 and 11. Works as an ordinary Moonlight-compatible client against any **Sunshine / Apollo / Vibeshine / Vibepollo** host, and unlocks its paired feature set when [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak) is running on the host.

> 🔐 **The bridge is authenticated.** Every command StreamLight sends is signed with its existing Moonlight identity certificate; the host approves each client once, via a 4-digit PIN shown on both screens. **Streaming never depends on it** — without approval you stream normally and simply lose the paired features. Each host card shows its state as a badge (AUTHORIZED / PENDING / DENIED).

> ⚠️ **Not affiliated with or endorsed by the Moonlight project.** StreamLight is an independent fork. For upstream Moonlight support, use the [official client](https://github.com/moonlight-stream/moonlight-qt).

## 🔥 Features

Everything below is in the current release, whichever version first introduced it.

**🕹️ Gamepad-first, keyboard-equal**
- Every action is reachable from the pad: D-pad across host tabs, library, settings tabs and dialogs, with a clickable prompt bar along the bottom
- **Prompts follow the device in your hands** — touch the keyboard and each glyph becomes the key to press; pick the pad back up and they return to that controller's own icons (Xbox / PlayStation / Nintendo / Steam, auto-detected or forced; Steam *(6.5.1+)*). *Settings → Interface* can pin them to the controller or to the keyboard *(6.3.0+)*, for Steam Input and the Steam Deck, where one pad also sends clicks and keys — and on the controller a trackpad mapped to the mouse points without taking the focus away from the pad *(6.4.1+)*
- **Rebindable shortcuts** — every in-stream keyboard hotkey and all four controller combos, in *Settings → Shortcuts*. Defaults are **LB + RB + A** quit (closes the game on the host), **+ Y** pause (leaves it running, to resume), **+ X** performance overlay, **+ B** stream settings, chosen to stay clear of Steam's overlay
- **Navigation sounds** *(7.0.0+)* — soft sounds for move, confirm and back from the controller and the keyboard, never from the mouse; off in *Settings → Interface*
- **DualSense haptics** *(7.0.0+)* — the fine vibration games play through the DualSense's actuators reaches the controller in your hands, over USB or Bluetooth, from a Vibeshine or Vibepollo 2.0.1+ host with its *DualSense USB audio and haptics* component

**🏠 Home and the host page**
- **Home** is your hosts as tabs under the wordmark. The selected one's card takes the left of the screen — name, state, addresses, stream settings and actions at once — and its game stands beside it. **LB / RB** move between hosts, **LT / RT** between that host's profiles — as on the host page *(7.0.0+)*
- **Moving background** *(6.0.0+)* — waves in your accent colour rise over the lower three quarters of Home, Settings and the PIN pad, twice as fast while a host is streaming. Still with *Reduce animations*, paused behind dialogs
- **Opening animation** *(6.0.0+)* — the waves rise, the icon fades in and a band of light writes STREAMLIGHT before Home appears; any button skips it, and *Settings → Interface* turns it off
- **The host page** puts the library down the left at full height and the game in the spotlight beside it — cover, name, store, and the right verb (*Resume* if it is already running, *Play* if not)
- **All, Games and Apps** *(5.9.0+, All 6.1.0+)* — **LB / RB** switch the host page between everything you can launch, the games, and everything else: Desktop, Virtual Display, Steam Big Picture and the host's controls, which open with *Open* and never count hours. The page reopens on the tab you last chose on that host *(6.3.0+)*, All the first time; **LT / RT** move the host's profile
- **Move between Games and Apps** *(6.1.0+)* — the **right stick click** or **M** moves the selected entry to the other tab, remembered per host. The host's own controls stay on Apps
- **Remote Input and Remote Monitor** *(5.9.0+)* — the host controls of Vibeshine and Vibepollo 2.0 open beside a running game, and their results and confirmation requests appear as messages and Yes/No questions rather than errors
- **Last played** *(5.7.0+)* — the game you last streamed on that host fills the right of its card, with how long ago you left it and the hours you have played it in total. **Play again** starts it without opening the library, and the same game sits first on the host page under *Last played*
- **Play time** *(5.7.0+)* — how long you have streamed each game, beside the store on every row. Filed under the game's name, so it survives a reinstall, and resettable from the per-game panel
- **Pinned games** *(6.0.0+)* — **Start** on the pad (Menu / Options / +) or **P** pins the selected game: pinned games sit under their own heading right after *Last played*, alphabetically, marked with a pin on the row. Kept per host and by name, so a pin survives a reinstall
- **Resume from Home** *(6.0.0+)* — while a host is streaming, its card shows the game in progress, marked *Streaming now*, with **Resume** in place of *Play again*
- **Per-host backgrounds** — *(7.0.0+: a small popup that previews on the card)* a picture of your own, and *(6.0.0+)* how much of the moving background shows through the card, from 70% to 100%. Without a picture the card wears your accent colour, in a gradient
- **Your accent colour** *(7.0.0+: a palette of 24, previewed live)* — or any hex code. Status colours never follow it: online stays green, a pending link change amber, *Shutdown* red
- **Light or dark** *(7.0.0+)* — *Dark*, *Light*, *System* or *Auto* (sunrise and sunset, or two set hours), switched from Home with the right stick click, and an interface scale of 90%, 100% or 110%
- **Time, date and battery** in the top right of every screen, in the clock and date format you read

**🎬 In-stream**
- **Performance overlay, built line by line** — thirteen lines to choose from, switched on and off *on the overlay itself* in Settings, plus corner, text colour, font size and transparency. Minimal / Default / Full remain as starting points
- **Stream Settings panel** — change resolution, frame rate, bitrate, HDR and frame pacing **while streaming**, applied with a brief reconnect and host-agnostic. It takes the corner the performance overlay is not using
- **Custom resolutions and frame rates** — any width and height, and any rate, not just the presets, from Settings or the in-stream panel
- **Your display's own values are offered too** *(5.5.0+)* — the resolution and refresh rate this machine reports appear in the pickers, so a 165 Hz or 16:10 screen needs nothing typed in. Marked with a dot in *Settings* and with the words *this display* in the in-stream panel, which offers the same list
- **Frame pacing** — Off or On, evening frames out with the same software pacer Moonlight uses
- **Fractional V-Sync** *(5.6.0+)* — shows each frame for a whole number of refreshes instead of once per refresh, so 60 FPS on a 120 Hz screen becomes one frame every two. Needs V-Sync and frame pacing, and a screen running at an exact multiple of the frame rate: at 60 FPS that is 120 / 180 / 240 Hz, and a 144 Hz screen wants 72 FPS. Off by default
- **VRR** *(6.0.0+, experimental)* — on a variable-refresh display each frame is shown as soon as it is ready, with three timing profiles (*Low latency*, *Balanced*, *Smooth*) and *Reduce judder*. Built on Nonary's VRR work for Moonlight; needs V-Sync, runs borderless, and switches Fractional V-Sync off while it is on
- **PyroWave** *(6.4.0+)* — a fifth video codec for wired networks: every frame is coded on its own, so decoding takes well under a millisecond and a lost packet blurs part of one picture instead of breaking the next ones. Needs hundreds of Mbps and a Vibeshine or Vibepollo 2.0 host; other hosts stream H.264
- **PyroWave Test** *(7.0.0+)* — from the host's Options: measures the wired network to the host and this device's decoder, offers a bitrate for the target you pick, and lets later PyroWave streams send their frames at the pace it found. Needs Vibeshine or Vibepollo 2.0.1+

**⚙️ Settings and profiles**
- Nine tabs, pill-style selectors instead of dropdowns, inline subtitles instead of tooltips, and a bitrate slider with hold-to-accelerate and a **Default** prompt
- **Per-host profiles** — up to three named profiles per host, each overriding resolution, frame rate, bitrate, HDR, YUV 4:4:4, codec, display mode, V-Sync, frame pacing, fractional V-Sync, VRR, audio, link matching, launch wait and Hue. Switchable from Home or the host page
- **Per-game overrides** on top of the active profile, for the settings that vary by title
- **Profiles in tabs** *(6.0.0+)* — the host profile and per-game dialogs are split into Settings' own tabs, one section at a time, switched with **LB / RB** drawn at the ends of the strip, each tab showing how many of its values are changed
- **Inherited values look inherited** *(6.0.0+)* — the accent marks only what a profile or a game changes, every row says where its value comes from, **Y** gives a row back, and **LT / RT** switch profile anywhere in the dialog. **X** names a profile from eight ready-made names, or your own with a keyboard
- A setting that cannot act says so wherever you meet it — greyed, with the reason on the line beneath, in Settings, in the profile and in the per-game dialog alike
- Every change is written to disk the moment you make it
- **Logs and cache** *(6.5.0+)* — *Settings → About* shows where logs, crash dumps and covers are kept and how much space they take, opens each folder and clears them after a confirmation. Logs and crash dumps can move to another folder, and the running log follows at once
- **Update from the app** *(5.8.0+)* — *Settings → About* downloads a newer release when there is one and checks it against GitHub's checksum; *Install now* opens the installer, which reopens StreamLight when it finishes, after a single Windows permission prompt. A newer version is also announced at startup

**🎯 Xbox app and Steam integration**
- Branded tile artwork in the Windows 11 Xbox app's "My apps" section, seeded during setup and re-applied automatically whenever the Xbox app overwrites it
- **Steam artwork** *(7.0.0+)* — added to Steam as a non-Steam game, StreamLight gets its own cover, header, background and logo in the library and in Big Picture from *Settings → Interface → Steam artwork*, on every Steam account on the PC; pictures you chose yourself are left alone. The images are also in the *Steam artwork* folder of the install, for assigning by hand

**💡 Philips Hue Sync**
- Optional: starts Hue Sync on this PC when a session begins and closes it when it ends, silently, with the install path resolved from the registry

## 🔗 Paired Features (with StreamTweak)

These cross the bridge and need both apps. The version shown is the **minimum StreamTweak** on the host.

All of them are switched on **per host**, in **Settings → StreamTweak** — a host added from StreamLight 5.2.0 on starts off, and hosts you were already using StreamTweak with are switched on for you on first run. Streaming itself is never affected either way.

- **Host link matching** *(8.1.0+)* — before each launch StreamLight measures the wired link that actually reaches that host, asks the host to come down to it, and starts the stream only once the host confirms. A host running faster than the client sends each frame as a burst the slower link cannot drain, and the packets that die first are the few carrying audio: the symptom is sound cutting out while the picture stays perfect. The client decides the speed because only the client knows its own connection; the host keeps the permission and the restore
- **Seamless launch** *(8.1.0+, opt-in)* — with **Wait for the game to appear** on, the stream window stays hidden until the host reports the game is really on screen, so you watch the game's cover art instead of the host's desktop rearranging itself. **B** or **Esc** reveals the host at any moment. A resume never waits: the game is already there
- **Remote PIN unlock** *(8.1.0+)* — after a **Wake**, or *(7.0.0+)* from the **Unlock** button of any host sitting at its lock screen and before any launch on it, a controller-navigable number pad takes its Windows PIN. The session carrying the PIN is never shown and never recorded on the host; wrong attempts stop at three, since Windows suspends the PIN after a few failures
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

## ✨ What's New in 7.0.1 — Clean Exit

Client-side, any host.

- **Quit or pause** — **LB + RB + A** / **Ctrl+Alt+Shift+Q** now ends the stream *and* the game on the host; the new **LB + RB + Y** / **Ctrl+Alt+Shift+P** ends the stream and leaves the game running, to resume
- **Command-line launches follow your profiles** — `StreamLight.exe stream` applies the host's active profile and the game's own settings, closes the game when the stream ends (`--no-quit-after` keeps it), and shows the cover on the launch screen
- **Visual C++ Runtime in the installer** — added only when the PC's is older than the one StreamLight 7.0 needs; without it the app could close as soon as it opened
- **Opens again on PCs updated since 2024** — a plugin left by an old version stopped 7.0.0 at startup; the installer now empties Qt's plugin folders before copying the new ones

## ✨ What's New in 7.0.0 — In a New Light

Client-side, any host.

- **Light theme** — *Dark*, *Light*, *System* or *Auto* (sunrise and sunset from the Windows location, or two set hours) in Settings → Interface; a soft grey rather than white, every text checked for contrast, the launch screen stays dark
- **Theme toggle on Home** — the right stick click (**T** on the keyboard) switches between Dark and Light; each theme keeps its own accent and host pictures
- **Accent palette** — twenty-four colours in a popup that previews each one on the whole interface; **A** keeps it, **B** puts back the one you had
- **Interface scale** — text and controls at 90%, 100% or 110%, on top of the automatic fit to the window
- **Host background popup** — a picture or one of the last three and the card's opacity, beside a card that stays whole as the preview; without a picture the card wears your accent, in a gradient
- **Host page on the waves** — the same moving background as Home, or the selected game's cover as before, in Settings → Interface; the launch screen keeps the game's cover
- **VRR frame rate** — with VRR on, the rate your display can hold joins the frame rate choices and is selected for you
- **Right-stick scrolling** — Settings, About, profiles and the host page scroll without moving the focus, with a *Scroll* prompt when there is something to scroll; a held D-pad now repeats
- **A new launch screen** — the page fades to black and the launch rises out of it: host and stream settings along the top, the game at the lower left, and a bar that fills step by step — *Connect*, *Launch*, and *Load* when it waits for the game; quitting wears the same look
- **Navigation sounds** — soft sounds for move, confirm and back, from the controller and the keyboard only; off in Settings → Interface
- **PIN unlock whenever the host is locked** — however it was switched on, its card says **Unlock** instead of *Open* and its page stays closed until it is signed in; a game launched on it gets the PIN pad first and starts by itself afterwards; **Enter** now signs in
- **DualSense haptics** — the fine vibration games play through the DualSense's actuators reaches your controller, over USB or Bluetooth; needs Vibeshine or Vibepollo 2.0.1+ on the host with its optional *DualSense USB audio and haptics* component, every other host keeps ordinary rumble
- **Steam artwork** — a cover, header, background and logo for StreamLight's non-Steam shortcut in the library and in Big Picture, applied from Settings → Interface; your own pictures are kept
- **PyroWave Test** — a tile in the host's Options measures the wired network and this device's decoder and offers a bitrate for the target you pick; later PyroWave streams send their frames at the pace it found. Needs Vibeshine or Vibepollo 2.0.1+; *All Apps* left Options, as it opened the same page as *Open*
- **What's new in About**, and a **NEW** mark beside what this release added
- **Motion** — sliding focus and selections, springy popups, pages that zoom and slide; *Reduce animations* turns it all off

*Older releases are in [changelog.txt](changelog.txt).*

## 🏗️ Architecture

A Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline — FFmpeg, D3D11VA, DXVA2, libplacebo — and the protocol, `moonlight-common-c`, are upstream's, and they track Moonlight's **development branch** rather than its releases — for two years there was none after v6.1.0, and as of 6.5.0 the security fixes and libraries of Moonlight 6.2.0 (October 2026) are in. As of 6.4.0 our copy of `moonlight-common-c` is master's plus Nonary's PyroWave commits, and upstream's application fixes are carried over as they land. The VRR pacing *(6.0.0+)* and PyroWave *(6.4.0+)* are Nonary's, imported unchanged so that they can be kept in step with the original. The UI layer is ours.

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
- [**Nonary's VRR work for Moonlight**](https://github.com/Nonary/moonlight-qt) — the VRR pacing in 6.0.0 and PyroWave support in 6.4.0 are Chase Payne's code, imported as it is (tag `v6.1.0-vrr18`) together with its test harness; the PyroWave codec itself is [Hans-Kristian Arntzen's](https://github.com/Themaister/pyrowave). The DualSense haptics and the PyroWave Test's measurements in 7.0.0 are his too, from the `master` branch
- [**SAxense**](https://github.com/egormanga/SAxense) — Sdore's (Egor Vorontsov) research into the DualSense's Bluetooth haptics report, which the Bluetooth path is built on; MPL-2.0, kept with its notices in [`third-party/saxense`](third-party/saxense)
- [**Sunshine**](https://github.com/LizardByte/Sunshine) — the streaming host that started it all
- [**Apollo**](https://github.com/ClassicOldSong/Apollo) — community-driven Sunshine fork
- [**Vibeshine**](https://github.com/Nonary/vibeshine) and [**Vibepollo**](https://github.com/Nonary/Vibepollo) — fully supported

## License
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

StreamLight is released under the GPL v3 License, in accordance with the upstream Moonlight license.
