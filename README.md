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
- **Performance overlay, built line by line** — fourteen lines to choose from, switched on and off *on the overlay itself* in Settings, plus corner, text colour, font size and transparency. Minimal / Default / Full remain as starting points. The last of them, *Cadence*, measures what the display really did with the frame pacing and records it in the log — the one to switch on before reporting a stutter. On Intel graphics the V-blank counts are left out of it, since the driver misreports them; the rest is measured separately and still shown
- **Stream Settings panel** — change resolution, frame rate, bitrate, HDR and frame pacing **while streaming**, applied with a brief reconnect and host-agnostic. It takes the corner the performance overlay is not using
- **Custom resolutions** — any width and height, not just the presets, from Settings or the in-stream panel
- **Frame pacing** — Off, Automatic, Software or Hardware, the last locking the cadence in the GPU on a display running a whole multiple of the stream
- **Refresh rate switching** — what your screen does while streaming in Fullscreen: Off, Automatic, Highest, or Match frame rate

**⚙️ Settings and profiles**
- Nine tabs, pill-style selectors instead of dropdowns, inline subtitles instead of tooltips, and a bitrate slider with hold-to-accelerate and a **Default** prompt
- **Per-host profiles** — up to three named profiles per host, each overriding resolution, frame rate, bitrate, HDR, codec, display mode, refresh rate switching, V-Sync, frame pacing, audio, link matching, launch wait and Hue. Switchable from Home or the host page
- **Per-game overrides** on top of the active profile, for the settings that vary by title
- A setting that cannot act says so wherever you meet it — greyed, with the reason on the line beneath, in Settings, in the profile and in the per-game dialog alike
- Every change is written to disk the moment you make it

**🎯 Windows Xbox app integration**
- Branded tile artwork in the Windows 11 Xbox app's "My apps" section, seeded during setup and re-applied automatically whenever the Xbox app overwrites it

**💡 Philips Hue Sync**
- Optional: starts Hue Sync on this PC when a session begins and closes it when it ends, silently, with the install path resolved from the registry

## 🔗 Paired Features (with StreamTweak)

These cross the bridge and need both apps. The version shown is the **minimum StreamTweak** on the host.

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

## ✨ What's New in 5.1.3 — As It Was

Frame pacing goes back to doing exactly what 5.1.0 did, the busy spinner is drawn by us now, and a line of text that repeated the one above it is gone. All client-side, and it works with any host.

- **The frame queue cap introduced in 5.1.2 is gone.** It limited how many frames the driver was allowed to queue ahead of the display, on the path where StreamLight holds each frame for a whole number of V-blanks. On every machine measured before and after, the build carrying it read worse than the build without, so pacing is back to what 5.1.0 did
- **The busy spinner is drawn by StreamLight now**, everywhere it turns up — launching, quitting, waking a host, pairing, finding hosts, checking for updates. The stock one built its arc from flat-ended segments with nothing behind them, so its outline was never really a circle and its two ends read as corners stuck to the ring, at every size
- **Every spinner is sized against the text beside it** instead of by hand, so the proportion is the same in all of them. They ranged from just right to nearly twice the size of their own label, and the one in the Windows Update view was fixed at a size that never grew on a high-resolution display
- **Cover art on the host page has rounded corners whatever shape it arrives in.** The rounding was applied to the frame rather than to the picture, so a cover that was not exactly 2:3 sat inside that frame keeping its own square corners — two games side by side on the same screen could look different
- **The launch screen no longer says "window open on host"** beneath "Loading" — the title already said it, in fewer words
- **On Intel graphics the Cadence line leaves out the V-blank counts.** The driver's presentation counter there disagrees with its own panel: it reports one V-blank per frame where two is the only physical answer, and flags every frame as a cadence slip. The wait and queue figures beside it are measured separately and are still shown

## ✨ What's New in 5.1.2 — Smoother Again

A stutter that 5.1.1 introduced, an overlay that blinked once a second, four things that were being drawn or said wrong, a note in Settings that shows up only when it applies to you, and a way to see what your screen is really doing with the frame pacing. All of it is client-side and works with any host.

- **The overlay can show what your screen is really doing** — a new *Cadence* line measures what the display did with the frame pacing it was asked for: how long each frame was actually held, how deep the queue of frames waiting to be handed over sat, and how long the handover took. Every other line prints something already known; this one switches a measurement on, and with it a record in the log. ⚠️ It is what to turn on before reporting a stutter — *Settings → Overlay*, last line, or use the Full preset
- **The stutter some people saw in 5.1.1 is gone.** That release fixed the half-refresh case by doing two things at once, and only one of them was right. Capping how many frames the driver may hold ahead of the display was right, and it stays — that is what stopped the latency building up and never coming back. Waiting for the screen *before* drawing the next frame was wrong, and it is gone: it left the picture running on the screen's rhythm while frames kept arriving on the stream's, and wherever those two didn't line up the result ranged from jitter you only notice on a slow camera pan to heavy stuttering. If 5.1.1 felt worse to you rather than better, this is why
- **The performance overlay no longer blinks about once a second.** It is rebuilt in the background at that rate, and the old one was being taken down before the new one was made — so any frame drawn in between had nothing to draw and the overlay vanished for it. The old one now stays up until its replacement is ready, and a frame that lands on the swap itself redraws what it drew last rather than nothing
- **The game's cover on the host page is no longer stretched and cut off** when you come back from a stream. It only ever happened to *Desktop* and *Steam Big Picture*, because their artwork is the only one that is not the usual portrait shape, and moving to another game and back was the one thing that cleared it. Two drawing steps had been sharing a single pass — rounding the corners and casting the shadow — and were disagreeing about how large the picture was; they are now done one at a time
- **The covers in the last-session panel are sharp on a dense screen** — they were drawn from the small thumbnail the host sends inside its report. It is sized for the panel's own layout and stretched rather than fitted, so on a 4K screen it was blown up almost threefold, making the covers drawn biggest the ones drawn softest, and any cover that is not the usual portrait shape arrived squashed. StreamLight now draws the full-size artwork it already holds for that host — the very file the game list is showing — and keeps the host's thumbnail only as the fallback for a game it has never opened or one since removed from the library
- **Dark backdrops no longer step through visible bands** — a host background picture, and the blurred cover behind the host page, sit under a dark veil that keeps the text readable. Darkening a picture that far leaves it only a couple of dozen distinct shades, so anything smooth in it breaks into stripes. The veil is now dithered the way the app's own gradients already were: a pattern half a shade deep, far too fine to see, and exactly what stops the stripes forming
- **The host card no longer promises a link-speed change it will not get.** For around half a minute after a session ends the host still counts one as running and turns down any request to change its adapter, so starting another game inside that window announced *on launch → 1 Gbps* and then quietly did not do it. The card and the Options tile now stay silent while the host reports itself busy, and the tile gives that as the reason. ⚠️ The wait itself belongs to the host — **StreamTweak 8.1.2** does away with it, so the match simply happens; against an older host this at least stops a promise that was never going to be kept
- **Settings → Video now tells you when you are on the half-refresh arrangement** — at 60 FPS on a 120 Hz screen with frame pacing set to Automatic or Hardware, each frame is held for two refreshes, and how much delay that comes with is settled when the stream starts rather than being anything the app gets to choose. The note sits under *Frame Pacing* and appears only when that is what your settings will actually do, pointing at *Match frame rate*, which avoids the arrangement instead of managing it

## ✨ What's New in 5.1.1 — Frame Pacing Latency

The soft-controls problem behind [issue #9](https://github.com/FoggyBytes/StreamLight/issues/9), found and fixed. Client-side, and it works with any host.

- **Streaming at half your screen's refresh rate no longer builds up a frame of latency** — on a 120 Hz screen fed 60 FPS the controls would start feeling soft, on and off, while every meter said the stream was perfect: the picture flawless, the frame rate exact. Frames were queueing three deep inside the graphics driver, and once that queue was full each one had to wait for room before it could be handed over. Where it settled was decided in the first seconds of the stream and never changed again — which is why reconnecting cured it sometimes and not others. StreamLight now waits for the screen to be ready *before* it draws a frame rather than after, so nothing queues. Measured across a whole session, the time a frame spends waiting on the display went from **6.68 ms to 0.57 ms**, with the cadence it exists to protect untouched: 60 frames out of 60, held exactly two refreshes each
- **Your screen goes back to its normal refresh rate when the stream ends** — with *Refresh rate switching* set to **Match frame rate**, a 60 FPS stream left a 120 Hz screen at 60 Hz once you returned to the host list, and it stayed there until you changed it by hand. It never came up before 5.1.0 because the highest refresh rate is usually the one the desktop is already on, so there was nothing to put back
- **The launch and quit screens stand on the game's own artwork** — the same picture the host page is already showing. Starting a game used to cross onto a gradient mixed from two colours sampled out of the cover, so the background changed under you at the one moment when nothing else should move. Quitting shows the artwork of what is being closed. With **Reduce animations** on, all three keep the plain gradient in your accent colour
- Thanks to [@Soladus](https://github.com/FoggyBytes/StreamLight/issues/9), whose measurements on three different machines pinned the latency to a 2:1 ratio and nothing else, and who then found a way to trigger it on demand — without that the cause would still be a guess

## ✨ What's New in 5.1.0 — Custom Overlay

The performance overlay stops being three fixed answers and becomes yours. Alongside it, a say in what your screen's refresh rate does while streaming, a clock on every screen, and the dialogs brought back in line with the rest of the app.

- **You build the overlay on the overlay itself** — Settings → Overlay draws the box as it will look while streaming, in the real face, colours and transparency, and every one of its thirteen lines is switched on or off *in place*. A line you turn off stays where it is, faded, so nothing jumps around while you work. The arrangement it replaced put switches in one place and a preview in another, and the preview held nothing focusable — with a pad you were toggling blind
- **Drawn to scale against your own stream**, so the box takes the same share of the panel that it will take of the screen. The same overlay is a third of the size on a 4K stream as on a 1080p one, which is why one person calls it enormous and another cannot read it
- **Both corners, both boxes** — the settings also draw the in-stream Stream Settings panel, in whichever corner the overlay is not using. Two corners and not three is deliberate: with a centre there would be no opposite side for it to take
- **You choose what your screen's refresh rate does** — *Settings → Video → Refresh rate switching*, for Fullscreen: **Off**, **Highest** (the long-standing behaviour), **Match frame rate**, or **Automatic**, which follows your frame pacing choice. It matters most in the commonest handheld and TV case there is — a 60 FPS stream on a 120 Hz screen, where each frame is held for two refreshes while host and client run on two clocks nobody synchronises, and the wait before a frame reaches the screen drifts. Felt as the controls going soft, on and off, with nothing on any meter to explain it
- **Display mode and V-Sync are overridable per host profile too** — they are what the two rows above depend on, and a profile could previously hold one of them while having no way to state its condition. A setting that cannot act now says so wherever you meet it, with the reason on the line beneath
- Thanks to [@Soladus](https://github.com/FoggyBytes/StreamLight/issues/9), whose measurements on two different handhelds established that the drift needs a 2:1 ratio and nothing else — the refresh rate choice exists because of that testing, not a hunch
- **The host page uses its right-hand side** — library down the left at full height, the spotlight a column beside it: **seven titles on screen instead of four and a half**, and a cover two and a half times the size. The cover sits in a fixed portrait frame, so an off-format one gets empty space rather than cropped edges, and the column stops changing width from game to game. Long titles shrink and wrap rather than being cut off
- **A cover that is only a thumbnail is fetched again** — nothing used to expire one, so a cover downloaded before the host learned where to find the proper artwork was kept for ever
- **Profiles switch from the host page too** — LB and RB either side of the profile badge, Q and E on the keyboard, with the settings badges below following as you cycle
- **Every dialog is drawn at the same scale as the page behind it** — they were written in fixed pixels while the pages grow with the window, so on a large screen a dialog came out at roughly half the size of what it was covering. They also share one surface, border, spacing and button again, with a dimmed backdrop behind all of them
- **The clock is on every screen** — time, date and, on a laptop or handheld, the battery, in the top right of Home, the host page and Settings alike. The wordmark opposite is now set at the clock's own size, so the top row reads as one band
- **Large gradients no longer band** — the background and the host card are dithered as they are drawn rather than having noise laid over them afterwards
- **X gives up on a launch** and takes you back where you started it. B has always shown you the host, which answers *what is it waiting for*; this answers *I don't want it any more*. The prompt appears after five seconds, so a launch that simply works never offers a way out of itself
- **New controller defaults** — quit **LB + RB + A**, overlay **+ X**, stream settings **+ B**. Select and Start are gone from the defaults because Steam's overlay binds combos on them. A combo you had already rebound is untouched
- Settings are written the moment you change them, renaming a host stops at the 15 characters Windows itself allows, and Windows Update no longer shows a download percentage it could not compute honestly
- 🐛 **The controller works on the remote PIN pad** — waking a locked host brought the pad up and then ignored every button on it, because in that one case the streaming session was never told a controller was attached
- 🐛 **A launch that loses its host no longer strands you** — the app used to return to the host list and take the loading screen's foundations with it, leaving no error, no way back, and the app to be killed
- 🐛 The rounded corners of a host card no longer let a few light pixels through. Thanks to **@Soladus** for that one and for describing the overlay he wanted

## ✨ What's New in 5.0.0 — The Spotlight Update

The interface rebuilt around one idea — **one thing at a time, shown in full** — and everything the host and client can now do together. Most of the second half needs **StreamTweak 8.1.0**; with an older host nothing is asked for and nothing changes.

- **Home became host tabs and one full-screen card**, the host page a spotlight over a list. Before, every host cost three D-pad stops and two hosts put *Add a host* six stops away
- **One navigation zone on the host page, two on Home** — zones go where they earn their keep. The host page's buttons stay for the mouse but the pad never walks to them, because each already *has* a button: reaching *Play* from the twentieth game used to mean twenty presses of Up
- **Each prompt is stated once, where the action is** — A on whatever has focus, X on *Shutdown*, Select on the per-game settings; LT / RT at the ends of the host strip and LB / RB either side of the profile badge, each pair attached to the thing it moves
- **The stream settings read as badges** rather than a sentence, on the host page and beside *Ready to stream* on the Home card, so picking a host does not mean opening it to find out what it would stream at
- **Prompts follow the device in your hands**, and everything the pad reaches the keyboard reaches too — S, P, Q / E, PgUp / PgDn, G, D, alongside Esc and Enter, which already worked and were simply never shown
- **Per-host backgrounds, a configurable accent colour**, focus that lifts and glows, and a **Reduce animations** switch for handhelds. New application icon
- **The host is asked to match this device's link speed before connecting** — measured per host on the interface that actually reaches it, so a docked device with its cable up but its traffic on Wi-Fi is measured for what it really is. The host is only ever asked to come *down*, and it is never allowed to block a launch: if the change fails or times out, the launch says so in one line and carries on
- **You decide when the speed goes back** — the host holds it after a session ends, so starting something else costs nothing. Returning to the host list asks whether to restore it, once per session and never while something is still running over there
- **Optionally, the launch is covered until the game is really there** — *Wait for the game to appear*, off by default, holds the stream window until the host confirms the game is on screen, showing the game's own cover art meanwhile. **B** or **Esc** reveals the host at any moment, which is where the click has to happen when a game's own launcher is asking for a login
- **Wake a host and sign it in from the sofa** — the PIN pad, laid out the way that lock screen is, so there is no doubt whose sign-in you are looking at
- **The host card shows that host's last session** — grade, age, duration, RTT, host frame latency, drops and covers. It is the *host's* last session and not necessarily one of yours
- **A launch the host cannot capture is no longer shown to you** — the window waits for the first frame rather than the connection, so a host that accepts a launch and then fails to capture it plays out its timeout and retry off screen
- ⚠️ The per-host *Link-speed switch* option and its 10-second countdown are **gone**: the change is automatic and decided before every launch
- ⚠️ **A game that opens a launcher of its own cannot be waited out to the end** — no game window ever reaches the host's screen. Where the host can tell something is asking for a click, the launcher is shown to you; otherwise the launch screen says after twenty seconds that it is taking longer than usual, and the wait can be turned off for that title in its per-game settings

*Older releases are in [changelog.txt](changelog.txt).*

## 🏗️ Architecture

A Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline — FFmpeg, D3D11VA, DXVA2, libplacebo, `moonlight-common-c` — is identical to upstream; the UI layer is ours.

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
