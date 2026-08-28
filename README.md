## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](.badges/downloads.svg)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-08-18 172148" src="https://github.com/user-attachments/assets/22709ba2-af9b-44d8-a87c-9b69c950d477" />
</div>

**StreamLight** is the client half of the FoggyBytes streaming duo: a fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with a gamepad-first interface and native integration with its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak).

The streaming engine is untouched from upstream Moonlight — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c`. What is new sits around it: the interface, and everything the two apps can do together over a local TCP bridge — host link matching, host metrics in the overlay, store badges on covers, session quality reports, remote power-off and Windows Update, Tailscale, and signing a woken host in with its PIN from the sofa.

<div align="center">
  <img width="960" height="540" alt="Immagine 2026-08-18 172213" src="https://github.com/user-attachments/assets/d99d94e9-6b12-4f77-9f37-4ac9633d82f3" />
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
- **Performance overlay, built line by line** — twelve lines to choose from, switched on and off *on the overlay itself* in Settings, plus corner, text colour, font size and transparency. Minimal / Default / Full remain as starting points
- **Stream Settings panel** — change resolution, frame rate, bitrate, HDR and frame pacing **while streaming**, applied with a brief reconnect and host-agnostic. It takes the corner the performance overlay is not using
- **Custom resolutions** — any width and height, not just the presets, from Settings or the in-stream panel
- **Frame pacing** — Off or On, evening frames out with the same software pacer Moonlight uses

**⚙️ Settings and profiles**
- Nine tabs, pill-style selectors instead of dropdowns, inline subtitles instead of tooltips, and a bitrate slider with hold-to-accelerate and a **Default** prompt
- **Per-host profiles** — up to three named profiles per host, each overriding resolution, frame rate, bitrate, HDR, codec, display mode, V-Sync, frame pacing, audio, link matching, launch wait and Hue. Switchable from Home or the host page
- **Per-game overrides** on top of the active profile, for the settings that vary by title
- A setting that cannot act says so wherever you meet it — greyed, with the reason on the line beneath, in Settings, in the profile and in the per-game dialog alike
- Every change is written to disk the moment you make it

**🎯 Windows Xbox app integration**
- Branded tile artwork in the Windows 11 Xbox app's "My apps" section, seeded during setup and re-applied automatically whenever the Xbox app overwrites it

**💡 Philips Hue Sync**
- Optional: starts Hue Sync on this PC when a session begins and closes it when it ends, silently, with the install path resolved from the registry

## 🔗 Paired Features (with StreamTweak)

These cross the bridge and need both apps. The version shown is the **minimum StreamTweak** on the host.

All of them are switched on **per host**, in **Settings → StreamTweak** — a host added from 5.2.0 on starts off, and hosts you were already using StreamTweak with are switched on for you on first run. Streaming itself is never affected either way.

- **Host link matching** *(8.1.0+)* — before each launch StreamLight measures the wired link that actually reaches that host, asks the host to come down to it, and starts the stream only once the host confirms. A host running faster than the client sends each frame as a burst the slower link cannot drain, and the packets that die first are the few carrying audio: the symptom is sound cutting out while the picture stays perfect. The client decides the speed because only the client knows its own connection; the host keeps the permission and the restore
- **Seamless launch** *(8.1.0+, opt-in)* — with **Wait for the game to appear** on, the stream window stays hidden until the host reports the game is really on screen, so you watch the game's cover art instead of the host's desktop rearranging itself. **B** or **Esc** reveals the host at any moment
- **Remote PIN unlock** *(8.1.0+)* — after a **Wake**, if the host comes up at its lock screen, a controller-navigable number pad takes its Windows PIN. The session carrying the PIN is never shown and never recorded on the host; wrong attempts stop at three, since Windows suspends the PIN after a few failures
- **Host session report** *(8.1.0+)* — the host's last finished session on its card: grade, age, duration, RTT and peak, host frame latency, drop rate, and the covers of what was played
- **Host metrics in the overlay** *(4.4.0+)* — GPU %, encoder %, GPU temperature, VRAM, CPU and network TX, hidden entirely when StreamTweak is unreachable
- **Store badges on covers** *(5.0.0+)* — Steam, Epic, GOG, Ubisoft, Xbox, Battle.net and EA App
- **Session quality reporting** *(5.2.0+)* — FPS, drops, RTT, jitter, decode latency and bitrate sent every second; StreamTweak turns them into a grade and charts
- **Delivered vs target bitrate** *(8.0.0+)* — StreamLight reports the rate it was told to aim for, so the host can show what it actually delivered against it. Neither side can work that out alone
- **Remote host power-off** *(7.2.0+)* — a **Power…** chooser for the host, this PC, or both, on an authorized host only
- **Remote Windows Update** *(7.3.0+)* — scan, classify and install updates on the host, rebooting only if required, with a backgroundable progress view. Updates can also be installed before a shutdown
- **Remote session pause** *(6.0.0+)* — the Pause button on StreamTweak's dashboard ends the stream client-side
- **Tailscale in one tile** *(6.3.0+)* — a host reachable both on the LAN and over Tailscale stays a single tile that tracks both addresses and uses whichever is available, with an option to force the `100.x` endpoint. Pairs with the **Auto-start Tailscale** toggle, so opening StreamLight is enough to stream from anywhere

## ✨ What's New in 5.2.0 — Hands Off

StreamLight stops taking the presentation of frames into its own hands. Everything it used to do differently from Moonlight between the decoder and your screen is gone, and what remains is Moonlight's own code doing Moonlight's own thing — as are the protocol underneath it and the video libraries alongside it. Alongside that, everything StreamLight can only do with StreamTweak on the host now lives behind one switch per host, in a Settings tab of its own. All client-side, and it works with any host.

- **A StreamTweak tab in Settings.** Everything StreamLight can do only when StreamTweak is installed on the machine you stream from is listed in one place, with a switch for each of your hosts that turns all of it on or off at once, and the download link that used to sit in About. Each row says whether StreamTweak is actually answering on that host — asked once while the tab is open, so a host that has it tells you and a host that does not stays quiet. Nowhere else in the app goes looking
- **The settings that need StreamTweak grey themselves out**, with the reason and the host's name, when that host has the integration switched off — *Host link speed* under Network and *Wait for the game to appear* under Session. That second one now also states the dependency in its own description: it always had one, and was the only such setting never to mention it, so it could be switched on and quietly do nothing
- **Waking a host whose StreamTweak switch is off no longer waits for StreamTweak.** The dialog shows two steps instead of three and closes itself the moment the host is on the network, rather than spinning for a minute under the words *StreamTweak ready* on a machine that was never going to say them
- **The Settings tab strip breathes again.** Every label is a shade smaller and less letter-spaced, and the StreamTweak tab is its mark rather than its name — eleven characters was the longest label in the app and it sat flush against SHORTCUTS, the second longest
- **The buttons in the StreamTweak and About tabs look like the rest of Settings at last** — the same fill, which follows the accent colour you picked, the same corner rounding and the same focus outline. They had a grey of their own and a thinner ring, and they were the only two places in the app that answered the mouse at all
- **Now everything answers the mouse.** Move the pointer over a button anywhere in Settings and its outline brightens — the same outline, in the same place, that the pad and the keyboard light up in the accent colour, so the two never say the same thing in two different ways. In the segmented pickers, where the outline belongs to the box around the options rather than to any one of them, the option under the pointer lightens instead; the one already chosen stays as it is, because hovering a choice you have already made has nothing to tell you
- **The focus outline steps aside while you are driving with the mouse**, everywhere it used to stay lit. Only the two GitHub buttons behaved that way before, so the picker you last touched with the pad kept its ring while you pointed at something else entirely — two things claiming to be where you were. The pointing hand, likewise, now appears only on what can actually be clicked
- **The mouse pointer gets out of the way when you pick up the controller**, and comes back the moment you move the mouse or press a key. ⚠️ It can only do that over StreamLight's own window: left on the desktop or over another application, the pointer belongs to Windows and nothing an application asks for reaches it there
- **The PIN pad no longer tells you the PIN was wrong when the host simply stopped answering.** A correct PIN could be refused three times over a network blip or a StreamTweak restart and end at *Too many attempts*, locking you out of your own machine. That case now says so, and does not count against your attempts
- **StreamLight no longer knocks on the StreamTweak port forever** on hosts that do not run it. Two checks — one every two seconds, another every two and a half — ran on every paired host for as long as the app was open, and kept running while you were streaming
- **The last session shown on a host's card refreshes while you are on the home screen.** Leaving a host's page does not end the session on the host, so the panel used to keep describing the session before it until something else happened to ask again
- **The banded stripes across a host's card are gone.** Two gradients were stacked there — the host's colour and a dark veil for the text — and blending them threw away most of the shades available: measured across the left 60% of a card, the colour underneath offers 48 distinct values and the blend offered eight. That is not a gradient with stripes in it, it is a flat colour that jumps every couple of hundred pixels. The two are now one gradient and the part that was nearly flat is exactly flat: the widest run of one unchanging shade drops from 46 pixels to 16, and the card draws in two fewer passes when it has no picture behind it
- **Frame pacing is now identical to Moonlight's.** The built-in half-rate cadence — which held each frame on screen for two refreshes when your display ran at twice the stream's frame rate — has been removed. It was added in 3.4.0 to smooth 60 FPS on a 120 Hz screen, and it worked; but holding frames that way makes them queue up inside the graphics driver rather than replace one another, and how deep that queue settles is decided in the first seconds of a stream and never changes again. That is the drifting delay behind [issue #9](https://github.com/FoggyBytes/StreamLight/issues/9), and two attempts to fix it from the inside both had to be withdrawn. Frames are now presented the moment they are ready, and any evening out is done by the same software pacer Moonlight uses
- **Frame pacing is now Off or On.** *Automatic* and *Software* both meant "let the software pacer even the frames out" once there was no hardware path left, and *Hardware* meant nothing at all. Your setting carries over: Automatic and Software become On; Hardware becomes On if you have V-Sync enabled and Off if you do not, since the pacer has nothing to work from without it
- **Refresh rate switching has been removed**, and with it *Match frame rate*. Your screen now always goes to the highest refresh rate your frame rate divides into while streaming — what Moonlight does, and what StreamLight did before 5.1.0. Match frame rate existed as the way out of the half-rate cadence; with the cadence gone there is nothing left for it to avoid
- ⚠️ **The consequence, stated plainly:** a 60 FPS stream on a 120 Hz screen can judder on camera pans again, exactly as it did before 3.4.0 and as it does on Moonlight today. If that affects you, halve the refresh rate at the driver level — NVIDIA Profile Inspector on NVIDIA, Special-K elsewhere — which is where it has always worked best
- **The Cadence line, and the measurement behind it, have been removed.** It read statistics from the display pipeline on every single frame and wrote its summary to the log from inside the render loop, where writing to a file means taking a lock and flushing. It also charged its own cost to the *rendering time* figure it existed to explain — and it wrote most often exactly when something was going wrong, so it was at its most expensive during the very stutter it was meant to describe
- **The performance overlay has no Frame pacing line any more.** It reported which mechanism was really running, and with the half-rate cadence gone the only states left to report are the two the Frame pacing setting already states — upstream Moonlight has never had such a line. If you had picked Full in the Overlay tab you still have Full
- **The game's cover on the host page is no longer left stretched** when you come back from a stream. Moving to another game and back has always cleared it, so StreamLight now does exactly that by itself the moment the window returns. 5.1.2 said it had fixed this and had only found part of it: the drawing fault it corrected was real, but it was not the whole cause, and this repairs what is left while the rest is still being chased
- **The code that talks to your host is Moonlight's current version again** — StreamLight had been six months behind on it. The error correction that rebuilds packets lost on the way now uses the processor's own instructions for the job instead of a hand-rolled dispatch layer, replies from a host that are malformed or absurdly long are turned away rather than parsed, and Windows socket failures are reported for what they are instead of collapsing into one generic error
- **The video libraries have moved up for the first time since March** — FFmpeg goes up a major version and the Vulkan rendering library moves with it, bringing five months of decoder and renderer fixes
- **The picture is asked for at full colour range** — all 256 levels per channel instead of the 16-235 broadcast range, which keeps more detail in dark scenes and reduces banding. Moonlight made this its default in August, and the FFmpeg update above is what makes it safe: full-range 8-bit decoding on the Direct3D 11 path was broken in the version we shipped before. If your host answers with limited range instead, StreamLight follows what it actually sends rather than assuming
- **The cover on the launch screen is the same picture as the one on the host page**, rounded corners and shadow included — one shared piece of code instead of two
- **The host profiles and per-game settings panels always fit on screen**, with a scroll bar that stays visible whenever there is more below. Their chrome was sized in fixed pixels while their contents scaled, so on a large display they grew past the top and bottom edges and took their buttons with them
- **The profile name field fits its row again** at every screen size, for the same reason

## ✨ What's New in 5.1.3 — As It Was

Frame pacing goes back to doing exactly what 5.1.0 did, the busy spinner is drawn by us now, and a line of text that repeated the one above it is gone. All client-side, and it works with any host.

- **The frame queue cap introduced in 5.1.2 is gone.** It limited how many frames the driver was allowed to queue ahead of the display, on the path where StreamLight holds each frame for a whole number of V-blanks. On every machine measured before and after, the build carrying it read worse than the build without, so pacing is back to what 5.1.0 did
- **The busy spinner is drawn by StreamLight now**, everywhere it turns up — launching, quitting, waking a host, pairing, finding hosts, checking for updates. The stock one built its arc from flat-ended segments with nothing behind them, so its outline was never really a circle and its two ends read as corners stuck to the ring, at every size
- **Every spinner is sized against the text beside it** instead of by hand, so the proportion is the same in all of them. They ranged from just right to nearly twice the size of their own label, and the one in the Windows Update view was fixed at a size that never grew on a high-resolution display
- **Cover art on the host page has rounded corners whatever shape it arrives in.** The rounding was applied to the frame rather than to the picture, so a cover that was not exactly 2:3 sat inside that frame keeping its own square corners — two games side by side on the same screen could look different
- **The launch screen no longer says "window open on host"** beneath "Loading" — the title already said it, in fewer words

*Older releases are in [changelog.txt](changelog.txt).*

## 🏗️ Architecture

A Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c` — is identical to upstream, and it follows Moonlight's development branch rather than its last tagged release, which has not moved since September 2024. The UI layer is ours.

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

Settings — paired hosts, video / audio / input preferences, client certificate — live under `HKCU\Software\Moonlight Game Streaming Project\Moonlight`, the same place upstream Moonlight uses, so an upgrade keeps everything. Box art is cached in `%LOCALAPPDATA%\Moonlight Game Streaming Project\Moonlight`.

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
