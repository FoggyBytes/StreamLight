## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](.badges/downloads.svg)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-08-29 122012" src="https://github.com/user-attachments/assets/86a91fa4-0ee6-42d3-841b-2d64e8358171" />
</div>

**StreamLight** is the client half of the FoggyBytes streaming duo: a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with a gamepad-first interface and native integration with its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak).

The streaming engine is untouched from upstream Moonlight — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c`. What is new sits around it: the interface, and everything the two apps can do together over a local TCP bridge — host link matching, host metrics in the overlay, the store each game comes from, session quality reports, remote power-off and Windows Update, Tailscale, and signing a woken host in with its PIN from the sofa.

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-08-29 122048" src="https://github.com/user-attachments/assets/a5f90088-5c5d-4d7b-90ff-13370a02c268" />
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
- **Host session report** *(8.1.0+)* — the host's last finished session on its card: grade, age, duration, RTT and peak, host frame latency, drop rate, and the covers of what was played
- **Host metrics in the overlay** *(4.4.0+)* — GPU %, encoder %, GPU temperature, VRAM, CPU and network TX, hidden entirely when StreamTweak is unreachable
- **Store badges** *(5.0.0+)* — which store the selected game comes from, its mark beside its name on the host page: Steam, Epic, GOG, Ubisoft, Xbox, Battle.net and EA App
- **Session quality reporting** *(5.2.0+)* — FPS, drops, RTT, jitter, decode latency and bitrate sent every second; StreamTweak turns them into a grade and charts
- **Delivered vs target bitrate** *(8.0.0+)* — StreamLight reports the rate it was told to aim for, so the host can show what it actually delivered against it. Neither side can work that out alone
- **Remote host power-off** *(7.2.0+)* — a **Power…** chooser for the host, this PC, or both, on an authorized host only
- **Remote Windows Update** *(7.3.0+)* — scan, classify and install updates on the host, rebooting only if required, with a backgroundable progress view. Updates can also be installed before a shutdown
- **Remote session pause** *(6.0.0+)* — the Pause button on StreamTweak's dashboard ends the stream client-side
- **Tailscale in one tile** *(6.3.0+)* — a host reachable both on the LAN and over Tailscale stays a single tile that tracks both addresses and uses whichever is available, with an option to force the `100.x` endpoint. Pairs with the **Auto-start Tailscale** toggle, so opening StreamLight is enough to stream from anywhere

## ✨ What's New in 5.6.0 — Half Rate

A 60 FPS stream on a 120 Hz screen can be shown one frame every two refreshes — what NVIDIA Profile Inspector and Special-K were being used for. Client-side, works with any host, and with the switch off nothing behaves differently from 5.5.0.

- **Fractional V-Sync.** A switch in *Settings → Video* holds each frame for a whole number of refreshes instead of presenting once per refresh. Off by default, and it needs V-Sync and *Frame pacing* on — with either off the row greys out and says which one
- **Only where the numbers divide.** The screen has to run at an exact multiple of the stream's frame rate — and it is the *ratio* that matters, not the screen. At 60 FPS that means 120, 180 or 240 Hz; a **144 Hz** screen is 2.4× at 60 and does nothing, but 2× at **72 FPS**, which the Custom pill on the frame rate row will give you. The log says at the start of every stream whether it applied and what it decided on
- **Overridable per host profile**, on the row directly under *Frame pacing*, since that and V-Sync are what decide whether it can act. A profile missing either greys the row and names the one to fix — and keeps the choice, which starts working the day that profile has both. Deliberately not a per-game override: its conditions live on the profile, so a per-game copy could hold a value it had no way to satisfy
- **A *Presentation cadence* line for the overlay** — how many refreshes each frame was really held for, how deep the queue to the display is sitting, and how long presenting a frame blocked. It measures the same way with the switch off, so the two states can be compared on the same numbers
- **Every on/off setting is now a pair of *Off / On* pills** instead of a switch — the control the host profile and per-game panels have always used, so a yes-or-no answer looks the same wherever it is asked. They also grow with the interface scale, which the switches did not: a switch is sized by the theme rather than by us, so on a handheld every row grew around a control that stayed desktop-sized
- ⚠️ **This is not *Frame pacing → Hardware***, the setting 5.1.3 was the last to carry. That one also switched frame pacing off and left presentation as the only thing timing the stream, which is where the drifting delay of [issue #9](https://github.com/FoggyBytes/StreamLight/issues/9) came from. This one leaves frame pacing running and requires it

## ✨ What's New in 5.5.0 — Your Display

The resolution and frame rate pickers offer what your own screen can actually do, and a frame rate outside the presets can be typed in. Client-side, works with any host.

- **The frame rates your display can run.** A 165 Hz screen is offered 165, beside the usual 30 / 60 / 90 / 120, and a small dot marks the values that came from your display rather than from the list. Nothing changes on a 60 Hz screen — the row comes out exactly as before
- **A Custom pill beside the frame rate**, the one the resolution row has always had, for a rate no display reports or a deliberate cap. It is in the host profile and per-game panels too, so a rate you set globally can be overridden anywhere
- **Your display's own resolution is offered as well** — a 16:10 or ultrawide panel no longer has to be entered by hand
- **Resolution and Frame rate are two rows** instead of one, each showing what was found on this display underneath its name. Both custom-value windows say the same thing, so there is a number to aim at before typing one
- **The in-stream panel offers the same list.** *Stream Settings* kept its own copy of the presets, so a resolution or frame rate you could pick in *Settings* was not necessarily there mid-session. It reads the same values now, your display's own included, and says *this display* on the row carrying them
- **A frame rate outside the presets stays put.** Until 5.4.0 such a value could only arrive from the settings store shared with Moonlight; the in-stream *Stream Settings* panel did not know what to do with one and quietly fell back to 120, which it then wrote back the next time anything was applied. Reported in [issue #13](https://github.com/FoggyBytes/StreamLight/issues/13)
- ⚠️ **Match refresh rate has been removed**, one release after it arrived. Put to the test against the tools it was meant to replace, on the hardware it was meant to help, it lost: taking a 120 Hz panel down to 60 brought occasional stutters, slightly worse control latency and clearly worse rendering times, where leaving the panel high and presenting at half rate did not. If your screen runs faster than your stream, halving the refresh at the driver level — NVIDIA Profile Inspector, or Special-K elsewhere — remains the way to do it. It was off by default, and it is gone from *Settings* and from host profiles

## ✨ What's New in 5.4.0 — Own House

StreamLight keeps its settings in its own place instead of sharing Moonlight's. Client-side, works with any host.

> ⚠️ **Read before updating.** This version starts from scratch once: no hosts, default settings. Pair each host again; if you use StreamTweak, approve this device on the host **and** switch the integration back on for each host in *Settings* — a newly paired host starts with it off, so the bridge stays silent until you do. Nothing is deleted — the old settings stay where they were, so 5.3.0 still finds them. If you normally wake your host from StreamLight, wake it *before* updating: Wake-on-LAN needs the host's stored address, and that goes with the rest.

- **Its own settings store.** Installed side by side, StreamLight and Moonlight used to share one — the same registry key and the same cache folder — so each one overwrote the other's resolution, bitrate, V-Sync and codec every time it saved
- **Per-host settings survive having Moonlight installed.** Moonlight rewrites the whole host list whenever it saves, dropping every field it does not know about: the Tailscale address, the stage artwork, the pinned address, the per-host StreamTweak switch. Hosts that kept forgetting their Tailscale endpoint were this
- **A one-time notice on first launch** says the settings were reset, what to do about it, and that nothing was thrown away — an empty host list with no explanation reads as a fault
- **"Restart now" actually restarts.** Answering *Yes* to the Tailscale restart prompt used to close StreamLight and leave nothing running: the replacement started while the old one was still shutting down, saw it was there, and stepped aside
- **GUI mode asks to restart.** *Windowed / Maximized / Fullscreen* is read when the window is built, so it only takes effect on the next launch — it now says so instead of appearing to do nothing. *No* keeps the choice for next time
- **LB/RB switch profiles on a host with only one.** The cycle runs through *Global* too, so one profile is already two stops — but the shoulders stayed dead until a second profile existed, while the on-screen hints said they would work
- **No more black stripe down the right edge of the large cover.** The drop shadow draws a black backing plate behind the artwork, and it was coming out a couple of pixels wider than the picture laid over it — which is why the stripe arrived with 5.3.0 and showed at some window sizes and not others. Reported in [issue #11](https://github.com/FoggyBytes/StreamLight/issues/11)

## ✨ What's New in 5.3.0 — One Scale

The interface is drawn at one scale from end to end, and the settings you override per host or per game now say what they inherit. Client-side, works with any host.

- **Every override says what it inherits.** The first option in the per-game and host profile panels carries the value it stands for — *Global · 1080p* instead of *Global* — so choosing it no longer means leaving for *Settings* to find out what it meant. Where a host profile is active, the per-game panel names that profile and the value it hands down
- **And the row stops offering that value twice.** With *Global* at 1080p the resolution row reads *Global · 1080p | 720p | 1440p | 4K*: the option that would only repeat the inherited answer is gone, and comes back as soon as it stops being a repetition. A value you pinned yourself is never hidden
- **One scale, everywhere.** *Settings* was written in fixed pixels while the host page and every dialog already grew with the window, so on a large screen the same row came out one size there and another in a dialog opened on top of it. The segmented pickers and the pill buttons follow now too — they were the last controls left at a fixed size
- **The per-game and host profile panels are wider**, using the space they were leaving empty at both sides rather than getting taller
- **The cover no longer comes back deformed from a stream.** The drop shadow's automatic padding was resizing the effect under the artwork; the shadow is now cast by a separate silhouette and the picture never passes through a padded pass. Reported in [issue #10](https://github.com/FoggyBytes/StreamLight/issues/10)
- **The version in the corner is read from the app itself** — it had been typed by hand at every release, and 5.2.1 shipped saying 5.2.0

## ✨ What's New in 5.2.1 — One To One

The refresh rate match is back, as an addition on top of Moonlight's presentation code rather than a change to it. Client-side, works with any host, and with the switch off nothing behaves differently from 5.2.0.

- **Match refresh rate.** A switch in *Settings → Video* runs your display at the stream's frame rate for the session and puts it back when the stream ends, so 60 FPS on a 120 Hz screen is shown one frame per refresh instead of two — the judder 5.2.0 warned about, without NVIDIA Profile Inspector or Special-K. Off by default
- **It acts only in Fullscreen.** Borderless and Windowed keep the desktop refresh rate, so there the switch greys out and says why rather than pretending. Your setting is kept and comes back with Fullscreen
- **Overridable per host profile**, on the row directly under *Display mode*, since that is what decides whether it can act at all. A profile that is not on Fullscreen greys it out and names the reason — and keeps the choice, which starts working the day that profile goes back to Fullscreen
- **The streaming engine is untouched.** The display mode is chosen exactly as Moonlight chooses it; the setting only edits that choice in the moment between the window being made and it going fullscreen, so the panel still sees a single change and nothing in the picture path is different

## ✨ What's New in 5.2.0 — Hands Off

StreamLight stops taking the presentation of frames into its own hands: between the decoder and your screen it now runs Moonlight's own code, with Moonlight's current protocol and video libraries alongside it. And everything that needs StreamTweak on the host moves behind one switch per host, in a Settings tab of its own.

- **A StreamTweak tab in Settings.** Everything StreamLight can do only with StreamTweak on the host, listed in one place, with a switch for each of your hosts and the download link that used to sit in About. Each row says whether StreamTweak is actually answering on that host
- **Frame pacing is Moonlight's again.** The half-rate cadence added in 3.4.0 smoothed 60 FPS on a 120 Hz screen by holding each frame for two refreshes — but holding frames that way makes them pile up in the graphics driver, and that is the drifting delay behind [issue #9](https://github.com/FoggyBytes/StreamLight/issues/9). Frames are now presented the moment they are ready, and any evening out is the same software pacer Moonlight uses
- **Frame pacing is now Off or On.** *Automatic* and *Software* both meant the same thing once there was no hardware path left, and *Hardware* meant nothing at all. Your setting carries over
- ⚠️ **Refresh rate switching has been removed**, and with it *Match frame rate*. Your screen goes to the highest refresh rate your frame rate divides into, as Moonlight does. Stated plainly: a 60 FPS stream on a 120 Hz screen can judder on camera pans again, and the fix is to halve the refresh rate at the driver level — NVIDIA Profile Inspector, or Special-K elsewhere
- **The code that talks to your host is Moonlight's current version again**, after six months behind — error correction on the processor's own instructions, malformed replies turned away rather than parsed, and Windows socket failures reported for what they are
- **The video libraries move up for the first time since March** — FFmpeg goes up a major version and the Vulkan renderer with it
- **The picture is asked for at full colour range** — all 256 levels per channel instead of the 16-235 broadcast range, which keeps detail in dark scenes. If your host answers with limited range instead, StreamLight follows what it actually sends
- **Everything in Settings answers the mouse now.** The outline under the pointer brightens, in a neutral colour so it can never be confused with the accent the pad and keyboard use, and the focus ring steps aside while you are pointing. The pointer itself gets out of the way when you pick up the controller
- **The settings that need StreamTweak grey themselves out**, naming the host, when that host has the integration switched off — and *Wait for the game to appear* now states the dependency it always had
- **The performance overlay has no Frame pacing line any more**, and the *Cadence* line added in 5.1.2 is gone with the measurement behind it. Nothing else about your overlay changes
- **Fixes** — the host-page cover is no longer left stretched after a stream, the PIN pad no longer blames your PIN when the host simply stopped answering, the banded stripes across host cards are gone, a stream that ends with no picture points at the host's graphics drivers rather than your own firewall, and StreamLight stops knocking on the StreamTweak port on hosts that do not run it

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
