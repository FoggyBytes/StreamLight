## 🎮 StreamLight

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://github.com/FoggyBytes/StreamLight) [![Framework](https://img.shields.io/badge/Framework-Qt%206-brightgreen.svg)](https://www.qt.io/) [![Downloads](.badges/downloads.svg)](https://github.com/FoggyBytes/StreamLight/releases) [![Built on Moonlight](https://img.shields.io/badge/built%20on-Moonlight-blue?&logo=github)](https://github.com/moonlight-stream/moonlight-qt) [![Built with Claude Code](https://img.shields.io/badge/Built%20with-Claude%20Code-brightgreen.svg)](https://claude.ai/code)

<div align="center">
  <img width="960" height="540" alt="streamlighthome" src="https://github.com/user-attachments/assets/885c182c-6d94-4c08-9819-5db7c7fc276c" />
</div>

**StreamLight** is the client-side half of the FoggyBytes streaming duo: the official FoggyBytes fork of [Moonlight](https://github.com/moonlight-stream/moonlight-qt) with native integration for its host-side companion, [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak). It adds host NIC control, live host metrics in the overlay, store badges on game covers, session-quality reporting, remote Windows Update, and Tailscale presence — all driven from the client over a local TCP bridge to StreamTweak. From **3.0.0** the entire UI has been redesigned from the ground up with a flat, gamepad-first look inspired by the Xbox and Steam Big Picture interfaces, while the underlying streaming engine (FFmpeg / D3D11VA / DXVA2 / libplacebo / `moonlight-common-c`) is unchanged from 2.3.1.

<div align="center">
  <img width="960" height="540" alt="streamlighthost" src="https://github.com/user-attachments/assets/624e2b4e-2765-4145-8200-1589a3b336bb" />
</div>

<div align="center">
  
</div>

## ✅ Compatibility

Windows 10 and 11. Works as a standalone Moonlight-compatible client against any Sunshine / Apollo / Vibeshine / Vibepollo host, **and** unlocks its full feature set when paired with [**StreamTweak**](https://github.com/FoggyBytes/StreamTweak) on the host PC (Tailscale, live NIC speed transitions, host metrics overlay, store badges, session-quality reporting, remote pause, remote host power-off, remote Windows Update).

> 🔐 **Authenticated bridge (3.1.0+).** Every command StreamLight sends to StreamTweak is signed with its existing Moonlight identity certificate; the host approves each client once via a 4-digit PIN confirmation. **Authorization never affects streaming** — without it you can still stream normally, you only lose the StreamTweak integration: host metrics overlay, NIC speed & host link matching, store badges on covers, session quality reports & live charts, Tailscale, remote pause, remote host power-off, and remote Windows Update. Each host card shows its access state (AUTHORIZED / PENDING / DENIED) as a badge. As of StreamTweak 7.2.0 this authentication is mandatory (the host no longer has an option to disable it). Requires **StreamTweak 7.3.0 or later** on the host for remote Windows Update and update-and-shut-down, **8.0.0** for the delivered-vs-target bitrate, and **8.1.0** for host link matching and the seamless launch; update both apps together.

> ⚠️ **Not affiliated with or endorsed by the Moonlight project.** StreamLight is an independent fork. For upstream Moonlight support, use the [official client](https://github.com/moonlight-stream/moonlight-qt).

## 🔥 Features

**🕹️ Gamepad-First UI** *(new in 3.0.0)*
- Every action is reachable from the pad: D-pad navigation across host cards, app grid, settings tabs, and dialogs
- Contextual gamepad prompts in a clickable bottom status bar (A / B / X / Y / LB / RB), every prompt also clickable with the mouse
- Glyphs swap automatically between **Xbox** and **PlayStation** icons based on the connected controller (DualSense / DualShock / Xbox / generic)
- App list sort order: Desktop first, Steam Big Picture second, all others alphabetically

**🏠 Home & App Grid**
- Portrait host cards with per-host coloured gradient headers and an inline **+ Add Hosts** tile
- Per-card **Profiles** and **Options** buttons under each tile *(Profiles new in 4.0.0)*. **Options** *(a wide tile grid as of 3.3.0)*: All Apps, Tailscale, Test Network, Rename, Delete, Details, Power…, Windows Update, Wake
- One-row header in the app grid: `profile · ONLINE · IP · NIC speed · resolution · FPS · bitrate · HDR · codec · audio` *(effective config, reflecting the active host profile; new fields in 4.0.0)*
- 200×267 covers with store badges and a tight bright-green focus border; the running app's cover gets a thicker, pulsing border
- App-name tooltip appears **instantly** below the cover, anchored outside the focus frame

**⚙️ Settings**
- 6 tabs (Video / Audio / Input / Decoder / Network / Session) with bold uppercase labels and LB/RB tab cycling
- Pill-style **segmented selectors** replace every dropdown — D-pad ◀ / ▶ pick the value, no popup, no focus leak
- Inline subtitles replace hover tooltips; legacy copy reviewed and shortened
- **Bitrate slider** with hold-to-accelerate, `Mbps` unit, and an **X / □ "Default"** prompt that restores the recommended value
- **Wait for the game to appear** *(new in 5.0.0)* — a Session-tab toggle, **off by default**: with it on, the launch screen is held until the host reports the game is on screen instead of showing the stream as soon as it starts. Overridable per host profile and per game — turn it off for titles that open a launcher of their own, which never produce a game window for it to wait on
- **Auto-start Tailscale on launch** *(new in 3.0.0)* — a Network-tab toggle that launches `tailscale-ipn.exe` in the background at every startup so remote hosts can be reached via their `100.x.y.z` Tailscale IP without keeping the Tailscale tray manually open. Pairs naturally with the unified Tailscale tile on Home (see Paired Features below). Requires the official Tailscale Windows installer from [tailscale.com/download](https://tailscale.com/download); the Microsoft Store package is not supported

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

- **Host link matching** *(StreamTweak 8.1.0+)* — before every launch StreamLight reads the wired link that reaches that host, asks the host to match it over `NETINFO`/`SETSPEED`, and waits for confirmation before starting the stream. The speed is decided by the client, because only the client knows what its own connection can do; the host keeps the permission and the restore. Current host NIC speed is shown on every host card and per-host header, **polled live every 2 seconds**, and both say when a change is coming *(before 5.0.0 this was a manual Options entry sending `PREPARE` against a speed configured on the host — both are gone)*
- **Host session report** *(StreamTweak 8.1.0+)* — the host's last finished session is fetched over `LASTSESSION` and shown on its card: grade, age, duration, RTT and peak, host frame latency, drop rate, and the cover art of what was played (a thumbnail, resized and cached host-side). Up to three covers travel, with `games_total` carrying the real count so the card can show a "+2". Asked for once when the host is approved and again on returning to the host list, since a session having just ended is the commonest reason to be there
- **Seamless launch** *(StreamTweak 8.1.0+, opt-in)* — with **Wait for the game to appear** on, the stream window is created hidden and shown only when the host reports over `GAMESTATE` that the game is on screen, so the launch is covered by StreamLight's own screen instead of the host's desktop mid-reconfiguration. **B** (or **Esc**) reveals the host at any moment, which is also the answer when the host reports that something over there wants a click — a store login, a game's own launcher. With the switch off, or against a host that doesn't answer, the window appears as soon as the first frame arrives
- **Host Metrics in Overlay** *(StreamTweak 4.4.0+)* — live GPU %, encoder %, GPU temperature, VRAM used/total, CPU %, and network TX in the performance overlay; section hidden entirely when StreamTweak is unreachable
- **Store Badges on Game Covers** *(StreamTweak 5.0.0+)* — per-game badge overlaid on each cover (Steam, Epic, GOG, Ubisoft, Xbox, Battle.net, EA App), fetched via the `APPSTORES` command
- **Session Quality Reporting** *(StreamTweak 5.2.0+)* — FPS, drops, RTT, jitter, decode latency, bitrate streamed every second; StreamTweak generates the quality grade and sparkline charts in its Logs tab
- **Remote Session Pause** *(StreamTweak 6.0.0+)* — Pause button on StreamTweak's Home page terminates the stream on the client side; signal piggybacked on the existing per-second `STATS` channel
- **Remote Host Power-Off** *(StreamTweak 7.2.0+)* — a **Power…** chooser (Options menu) shuts down the host PC, this client PC, or both; a status-bar **X · Shutdown** shortcut opens it for the highlighted host. Host power-off rides the authenticated bridge and only works on an AUTHORIZED host
- **Remote Windows Update** *(StreamTweak 7.3.0+)* — Options → **Check Windows Update on host…** scans, classifies and installs Windows updates on the host (Security + Defender / All), rebooting only if required, with a backgroundable progress view. The Power… chooser can also **install pending updates before shutting down**, on the host and/or this client, showing where updates are pending
- **Delivered vs Target Bitrate** *(StreamTweak 8.0.0+)* — StreamLight reports the bitrate it was configured to aim for, so the host's Dashboard shows the delivered rate *against your target* instead of a bare number. Neither side can work that out alone: the client sets the target, the host measures what actually goes out
- **Remote PIN Unlock** *(StreamTweak 8.1.0+)* — after a **Wake**, StreamLight follows the boot and, if the host comes up at its lock screen, shows a **controller-navigable number pad** to type the host's Windows PIN. The session that carries the PIN is never shown and is kept out of the host's session history entirely — no spatial-audio switch, no managed apps closed, no game credited. Wrong attempts are counted and stop at three, since Windows suspends the PIN after a few failures. A host that is already unlocked skips the pad; one that predates the feature never offers it. The link speed is matched **after** the PIN, and the host card only calls itself ready once that finishes
- **Tailscale, unified into one tile** *(StreamTweak 6.3.0+; single tile in StreamLight 3.3.0)* — after pairing via LAN IP, StreamLight queries the `TAILSCALE` bridge command. If StreamTweak reports a `100.x.y.z` Tailscale address, StreamLight records it on the host's **single** tile, which then tracks both the LAN and Tailscale IPs and shows a `TAILSCALE · AVAILABLE` badge (just `TAILSCALE` when only the Tailscale path is up). Opening the host or *All Apps* uses whichever path is available (LAN locally, Tailscale remotely); a dedicated **Tailscale** option forces the `100.x` endpoint. Combined with the **Auto-start Tailscale on launch** Settings toggle, the round-trip is automatic: open StreamLight → Tailscale comes up → one click streams from anywhere

## ✨ What's New in 5.0.0 — The Spotlight Update

The interface has been rebuilt around one idea: **one thing at a time, shown in full**. Around it sits everything the host and client can now do together — most of which needs **StreamTweak 8.1.0**; with an older host nothing is asked for and nothing changes.

- **The interface has been rebuilt** — around one idea: *one thing at a time, shown in full*. **Home** is your hosts as tabs under the wordmark, with the selected one taking the whole screen — name, state, addresses and actions at once — and **LT / RT** move between them. The **host page** follows the same shape: the game you're on fills the top with its cover, its name, the store it came from and the right verb (*Resume* when it's already running, *Play* when it isn't), and the library is a list of titles underneath with every name in full. Before, every host cost three D-pad stops and two hosts put *Add a host* six stops away
- **One zone on the host page, two on Home** — zones go where they earn their keep. The host page's buttons stay on screen for the mouse with their controller button drawn on them, but the pad never walks to them, because each already *has* a button: reaching *Play* from the twentieth game used to mean twenty presses of **Up**. Home keeps its second zone, because there the host has four actions and only *Shutdown* has a button of its own
- **Each button prompt is stated once, where the action is** — **A** on whatever has the focus, **X** on *Shutdown*, **Select** on the per-game settings; the status bar keeps only what belongs to no single control. **LT / RT** are drawn at the two ends of the host strip and **LB / RB** either side of the profile badge, each pair attached to the thing it moves rather than captioned elsewhere, so neither needs a label. LB / RB now cycle through **Global** as well as the configured profiles, so there is a way back to your unmodified settings without opening a dialog — and a host with a single profile can be switched at all, which before it could not. The badge itself now reads *Global* rather than disappearing, since shoulders either side of an empty space would point at nothing
- **The stream settings read as badges** — separate outlined values under a *Host* and a *Stream* heading, so the eye finds *120 FPS* by its shape instead of reading a sentence to the end. The same values sit beside *Ready to stream* on the Home card too — resolution, frame rate, bitrate, HDR, codec, audio — so picking a host doesn't mean opening it first to find out what it would stream at, and they follow the active profile as you cycle it
- **Pick your accent colour** — five presets or any hex code, and everything the interface highlights follows it. Status colours deliberately **never** do: online stays green, a pending link change stays amber, *Shut down* stays red, because a colour that follows the accent stops meaning anything. The default is now the cyan — the old green was doing two jobs at once, saying both *"the focus is here"* and *"this host is online"*
- **Each host can carry its own background** — a colour you pick or a picture of your own, with the gradient derived from whichever you chose so the two never clash
- **The host card shows that host's last session** *(StreamTweak 8.1.0+)* — the quality grade, how long ago, how long it ran, round-trip time and its peak, the host's own frame latency, the frame drops, and the cover art of what was played, up to three covers with a **"+2"** for the rest. It is the *host's* last session and not necessarily one of yours, since StreamTweak logs whatever streamed — and this is the screen you're on when deciding whether to go again
- **The prompts follow the device in your hands** — touch the keyboard or the mouse and every button prompt in the app becomes the key to press; touch the controller and they all turn back into its glyphs, in that controller's own icons. Before, they were always controller glyphs: starting StreamLight with a keyboard and never picking up a pad still told you to press **B**. Everything the pad reaches, the keyboard reaches too — **S** Settings, **P** power off the host, **PgUp** / **PgDn** move between hosts, **Q** / **E** between that host's profiles, **G** a game's own settings, **D** the recommended bitrate, alongside **Esc** and **Enter**, which already worked and were simply never shown
- **Focus moves rather than teleports** — the cover you're on lifts and glows, and its artwork sits blurred behind the library. All of it switches off with the new **Reduce animations** setting, which is a fair thing to want on a handheld. New application icon
- **Optionally, the launch is covered until the game is really there** — turn on **Wait for the game to appear** (Settings → Session, **off by default**) and the stream window stays out of sight until the host confirms the game is on screen, instead of handing you the host's desktop while it rearranges itself: windows moving, a launcher opening, the resolution changing. What you watch meanwhile is the game's own cover art, its name, and one line saying what is happening — the connection being prepared, the host opening the game, the game loading. The colours behind it are drawn from the artwork, and the cover regains its colour as the launch advances; a percentage would have to be invented, since nothing can know how long a game takes to load. Overridable per host profile and per game
- **B gets you there anyway** — press **B** (**Esc** without a controller) and the host appears immediately, whatever it says it is doing. Some launches stall on something only visible over there — a game's own launcher asking for a login, an update prompt behind the splash — and that is where the click has to happen. While the window is hidden nothing you press reaches the host, so a stray press can't land in a game that is still loading or answer a dialog you never saw
- **Wake a host and sign it in from the sofa** *(StreamTweak 8.1.0+)* — StreamLight follows the wake step by step and, if the host comes up at its lock screen, shows a **controller-navigable number pad** for its Windows PIN. The pad is laid out the way that screen is — the time, the date, the host's name — so there's no doubt whose sign-in you're looking at. The session carrying the PIN is never shown and never recorded on the host, wrong attempts are counted and stop at three (Windows suspends the PIN after a few failures), and a host that's already unlocked skips the pad entirely. Once you're in, that session is **closed** rather than dropped, so the host reports itself online instead of still streaming
- **The link speed is reported on the host, and only once it has settled** — the card on Home and the header on the host page say *Link* while the adapter is renegotiating and hold the last known figure, instead of narrating each intermediate value. The status bar no longer carries any of it: everything about a host is now said on that host
- **A launch the host can't capture is no longer shown to you** — the stream window waits for the first frame rather than for the connection, so a host that accepts a launch and then fails to capture it (a virtual display losing DXGI access, say) plays out its ten-second timeout and automatic retry off screen. A healthy launch is unaffected — its first frame arrives in about a third of a second
- **The host is asked to match this device's link speed, before connecting** — and the stream starts only once the host confirms. A host running faster than the client (2.5 Gbps feeding a 1 Gbps handheld) sends each video frame as a burst the slower link can't drain in time; the packets that die first are the few carrying audio, so the symptom is **sound cutting out for a couple of seconds while the picture stays perfect**
- **Measured per host, on the interface that actually reaches it** — a docked device with its cable up but its traffic on Wi-Fi is measured for what it really is, and a host reached over Wi-Fi or Tailscale is left alone. The host is only ever asked to come *down*; it puts itself back afterwards
- **New switch in Settings → Network**, on by default, with a line showing this device's connection — or the reason the feature can't act, so it's never quietly doing nothing. **Per-host profiles can override it**, because a profile describes a situation: *docked* and *handheld* want different answers here as much as they want different resolutions
- **Visible before it happens** — the host card and the app list show the pending change; the launch screen shows it while it happens (`2.5 Gbps → 1 Gbps`) and moves on by itself once the host is ready, watching its real state rather than counting down. When the speeds already match, nothing appears at all — and when a host *would* benefit but isn't offering the change, the card says so, since the feature has to be enabled on the host too
- **Never blocks a launch** — if the change fails or times out, the launch says so in one line and carries on. And if the *host* is the one stalling, the launch screen now says so after half a minute instead of showing "Starting…" for the full two-minute timeout
- **You decide when the speed goes back** — the host holds it after a session ends, so browsing the library and starting something else costs nothing. On the way back to the host list StreamLight asks whether to restore it: once per session, and never while something is still running over there. The restore is then shown as it happens and confirms the speed it returned to, so you know when the host is safe to switch off. A host's **Options** also carries **Restore NIC Speed**, which reads **Match Link Speed** when the host is already on its own speed — handy for getting a host ready *before* launching, so the stream doesn't open with a renegotiation
- **The installer no longer opens maximised** — with its contents still laid out for a small window and the artwork stranded in a corner. Windows was maximising a wizard that is not meant to be resized in the first place, which is what showed up on handhelds
- ⚠️ The per-host *Link-speed switch* option and its 10-second countdown are **gone**: the change is automatic and decided before every launch, so a manual trigger could only disagree with what was about to happen
- ⚠️ **A game that opens a launcher of its own can't be waited out to the end** — no game window ever reaches the host's screen, so there is nothing for the wait to end on. Where the host can tell that something is asking for a click, the launcher is shown to you, which is where the click has to happen. Otherwise there are two answers: after **twenty seconds** the launch screen says it is taking longer than usual, pointing at the **B** prompt below it, and **Wait for the game to appear** can simply be turned off for that title in its per-game settings — the wait is off by default anyway, so this only matters if you turned it on globally or on the host's profile

## ✨ What's New in 4.5.1 — Frame Pacing Lock

No StreamTweak update required — everything in 4.5.1 is client-side and works with any host.

- **Frame Pacing is locked when V-Sync is off** — in *Settings*, in the **per-game overrides** and in the in-stream **Stream Settings** overlay. Without V-Sync the stream renders as fast as it can and nothing is paced, so the control no longer offers a mode that has no effect: in Settings and in the overlay it now reads **"Off"**, showing what is actually happening, while the per-game override editor keeps showing the override you saved, marked as needing V-Sync. Your saved mode is **never discarded** and comes back as soon as V-Sync is re-enabled. Thanks to [@Soladus](https://github.com/FoggyBytes/StreamLight/issues/8) for the report and for helping settle how it should read.

## ✨ What's New in 4.5.0 — Bitrate Target Reporting

Works with any host. **StreamTweak 8.0.0+** is needed to see the new figure on the host.

- **Delivered vs target bitrate** — StreamLight now reports the bitrate it was configured to aim for, so StreamTweak's Dashboard can show what's actually going out *against your target* rather than a bare number. Neither side could tell that on its own: the client sets the target, the host measures the delivered rate. Older hosts simply ignore the new value

## ✨ What's New in 4.4.1 — Frame Pacing Overlay Fix

No StreamTweak update required — everything in 4.4.1 is client-side and works with any host.

- **Clearer frame pacing status in the overlay** — setting *Frame Pacing → Off* while streaming in **exclusive fullscreen with V-Sync on** still showed *"Frame pacing: Software"*, because the D3D11 renderer force-enables software pacing to stay in sync with VBlank in that mode. The overlay now shows **"Software (forced by V-Sync)"** so it's no longer misleading — to disable pacing entirely, turn **V-Sync off** or use **borderless / windowed fullscreen**. Thanks to [@Soladus](https://github.com/FoggyBytes/StreamLight/issues/6) for the report.

## ✨ What's New in 4.4.0 — Live Stream Settings

No StreamTweak update required — everything in 4.4.0 is client-side and works with any host.

- **Live Stream Settings overlay** — change **resolution, frame rate, bitrate, HDR and frame pacing while streaming**, without returning to the host list. A panel opens **top-right**, fully navigable with the **controller or keyboard**; the new settings are applied with a brief reconnect. Open it with **Ctrl+Alt+Shift+O** (keyboard) or **Select+L1+R1+B** (controller) — both rebindable in *Settings → Shortcuts*.
- **Custom resolution on the fly** — pick *Custom* on the Resolution row and dial in Width and Height right in the overlay (snapped to even pixels, with a live aspect-ratio hint).
- **Save to the active profile** — besides applying for the current session (**A**), save the chosen values straight to the profile in use — the per-game override, the active host profile, or Global, shown by name (**Y** on the controller, **S** on the keyboard).
- **Redesigned in-stream overlays** — the Stream Settings panel is a clean rounded card with the active profile in the header, a green-highlighted selected row, colour-coded values with ‹ › arrows, a live changes status, and real Xbox / PlayStation / Nintendo button glyphs for Apply / Cancel / Save. The performance/stats overlay gets the same rounded-card look, hugs its content (no empty space), and both overlays share one font size and sit a few pixels off the screen edges.
- The overlay's button prompts follow your **controller glyph set** (Xbox / PlayStation / Nintendo), including the Nintendo A/B swap.
- **Tailscale routing fix** — hosts no longer get **stuck on their Tailscale (100.x) address at home**. A Tailscale address could be saved as the LAN address and, since it answers from everywhere, the app kept using it (slower) even on your local network — across restarts, until you re-entered the IP by hand. StreamLight now keeps Tailscale addresses out of the LAN slot, cleans up any already saved that way, and switches back to the LAN automatically as soon as it's reachable. The host tile also no longer flickers between its LAN and Tailscale address while you're away, and connecting right after returning to the host list no longer stalls briefly.

## ✨ What's New in 4.3.0 — Shortcuts Refinements

No StreamTweak update required — everything in 4.3.0 is client-side.

- **Simpler, layout-proof keyboard rebinding** — the Rebind dialog now uses three modifier toggles (**Ctrl / Alt / Shift**, at least two required) plus a single key you press, instead of holding the whole combo live. No more random failures, and you can finally bind **numbers and punctuation** with Shift on any keyboard layout (the key is bound by its physical position).
- **Fullscreen lockout fixed** — starting a stream directly in exclusive fullscreen with *capture system keys = In Game* could leave the shortcuts (and Alt+F4) stuck, forcing a Task Manager kill. The keyboard grab now follows window focus, so you're never locked out.
- **Safer controller combos** — a controller shortcut now needs at least **3 buttons**, one of them Start / Select / LB / RB, so it can't trigger by accident during play.
- **"Gamepad" → "Controller"** across the interface, the overlay shortcut de-duplicated to a single rebindable combo, and the Shortcuts tab tidied up (Controller above Keyboard, per-section rules).

## ✨ What's New in 4.2.0 — Configurable Shortcuts

No StreamTweak update required — everything in 4.2.0 is client-side.

- **Configurable shortcuts** — a new **Settings → Shortcuts** tab lets you rebind every in-stream **keyboard hotkey** (quit, fullscreen, overlay, mouse mode, paste, minimize and more) and both **gamepad combos** (quit, cycle overlay). Record a new key combination, or build a gamepad combo by picking the buttons to hold together; conflicts are flagged and each shortcut can be reset individually or all at once. Handy to avoid clashes with other software, or for **nested streaming** where a shortcut would otherwise be captured by the outer client instead of the machine in the middle.
- **Controller glyph selector** — choose which button icons appear across the app: **Auto** (follows the connected pad) or force **Xbox / PlayStation / Nintendo** — useful for generic controllers that aren't detected correctly, or simply a personal preference.

## ✨ What's New in 4.1.0 — Custom Resolutions

No StreamTweak update required — everything in 4.1.0 is client-side.

- **Custom resolution** — a new **Custom** button beside the 720p / 1080p / 1440p / 4K presets (*Settings → Video*) opens a popup to type any **width × height** in pixels, for displays that don't match a preset (e.g. the 16:10 **1920×1200** panel on an MSI Claw). The button shows the active custom value and stays highlighted while in use; pick a preset to switch back. A subtle separator now divides the resolution block from the frame-rate block.
- **Custom resolution in profiles** — the same **Custom** button is available in **per-host profiles** and **per-game settings**, so a docked profile or a specific game can stream at its own exact resolution while everything else inherits the global / profile value.

## ✨ What's New in 4.0.1

- **Reports host frame latency to StreamTweak** — StreamLight now sends the host's per-frame processing latency (capture + encode, already shown in the full performance overlay) to StreamTweak. **StreamTweak 7.4.0+** uses it to grade streaming quality more accurately and plots it as a *Host frame latency* chart in its Logs. Backward compatible — works with any StreamTweak and has no effect on streaming.

## ✨ What's New in 4.0.0 — The Profiles Update

No StreamTweak update required — everything in 4.0.0 is client-side.

- **Per-game settings** — each game can override the global streaming settings (resolution, frame rate, bitrate, HDR, video codec, frame pacing, audio); anything left on **"Global"** inherits your main Settings. Open it from a game's tile (the **tune** icon, top-right) or press **Select** on the highlighted game with a gamepad. A green badge marks customized games; the global settings are never touched. *(e.g. a AAA title at 4K/120/AV1/HDR, a 2D indie at 1080p/60.)*
- **Per-host profiles** — save up to **three named profiles per host** (e.g. a *Docked* 4K/120/HDR profile and a *Portable* 1080p/60 SDR one) and switch between them in a click. A new **Profiles** button under each host tile opens the manager: create, rename (≤14 chars) and delete profiles, or pick **Off** to fall back to the global settings. The active profile is applied automatically every time you stream that host; **LB / RB** cycle it straight from the host list. Profiles activate only on confirm (**A** / click) — moving the cursor never changes the active one. Per-game settings stack **on top** of the active profile (cascade: global ← host profile ← per-game).
- **Settings reflect the active profile** — with a host profile active, opening Settings shows the rows that profile overrides as **greyed and locked**, with a *🔒 "Greyed settings are controlled by the active host profile"* notice at the top of each affected section.
- **Richer host header** — the app-list header now shows the full **effective** config: `profile · IP · NIC · resolution · FPS · bitrate · HDR (when on) · codec · audio`.
- **Hide host IP addresses** — a privacy toggle (*Settings → Session*) masks every host IP across the app as `•••.•••.•••.•••` — handy for screenshots. Off by default.
- **Nintendo controller glyphs** — the status bar now shows Switch-style A/B/X/Y/L/R icons (mapped by physical button position) when a Switch Pro Controller or Joy-Cons are connected, next to the existing Xbox and PlayStation glyphs.
- **Frame Pacing modes renamed** — the two manual modes are now **Software** / **Hardware** (was Matched / Multiple), naming the pacer that actually runs. Same behaviour.

## ✨ What's New in 3.4.1 — Frame Pacing Modes

A small follow-up to The Smooth Motion Update, from beta-tester feedback. No StreamTweak update required.

- **Frame Pacing is now a four-way choice** (*Settings → Video*) instead of a single on/off switch: **Off**, **Automatic**, **Matched** and **Multiple**.
  - **Automatic** — keeps 3.4.0's behaviour: hardware pacing when your display's refresh is a whole multiple of the stream's FPS, software otherwise
  - **Matched** — forces software pacing; best when your screen runs at the stream's frame rate (e.g. 60 Hz for 60 FPS)
  - **Multiple** — hardware pacing only, **no software fallback**, for a screen at a whole multiple of the stream (e.g. 120 Hz = 2× or 240 Hz = 4× for 60 FPS). Choosing this guarantees the software pacer never engages — for anyone who wants only the lowest-latency hardware cadence
- Thanks to [**@Soladus**](https://github.com/FoggyBytes/StreamLight/issues/2), whose feedback after testing the 3.4.0 fix prompted the explicit Matched/Multiple modes

## ✨ What's New in 3.4.0 — The Smooth Motion Update

Smoother motion on high-refresh displays and a configurable performance overlay, plus the fixes from the unreleased 3.3.1. No StreamTweak update required.

- **Smoother motion on high-refresh displays** — the **Frame Pacing** option (*Settings → Video*) now automatically uses **hardware frame pacing** when your display's refresh rate is an exact multiple of the stream's frame rate (e.g. 60 FPS on a 120 Hz screen). Each frame is held on screen for the right number of refresh cycles directly in hardware, giving a perfectly even cadence and removing the panning judder that used to require NVIDIA Inspector or Special-K — with no extra setting to manage. Hardware pacing applies to the Direct3D 11 renderer (the Windows default); everything else falls back to software pacing as before. The **performance overlay** now shows the active pacing mode (*Hardware / Software / Off*)
- **Overlay profiles** — a new **Overlay** tab (*Settings → Overlay*) lets you choose how much the in-stream performance overlay shows: **Off**, **Minimal** (resolution, FPS, bitrate, latency and network drops at a glance), **Default** (a balanced set plus a compact host-metrics summary) or **Full** (every stat StreamLight collects, including the host's GPU / encoder / temperature / VRAM / CPU). A live preview shows exactly how the overlay will look for the chosen profile. The overlay control moved here from the Network tab, and bitrate is now shown in the overlay. The overlay hotkey now **cycles** through the profiles — *Off → Minimal → Default → Full → Off* — each press of **Ctrl+Alt+O** (keyboard) or **Select+L1+R1+X** (gamepad) jumps to the next, and the Settings selector follows along
- **Automatic reconnect on a slow host start** — when a stream fails because no video ever arrives (the host's virtual display or HDR/AV1 encoder is still warming up on a cold start — the case that used to need a manual second attempt), StreamLight now quietly retries once instead of showing an error. A brief *"Host is starting up — reconnecting…"* message appears and the resume usually connects right away. Toggle in *Settings → Network* (on by default)
- **UI polish** — Settings rows are slightly more compact across every tab, and the highlight ring around toggle switches (on hover/focus/press) is now half the size
- **Status-bar glitch fixed** — while a host Windows-update job was running, the bottom-right *Update* progress chip could overlap the version number and render as garbled text. The chip and version are now laid out together so they never collide
- **`X · Shutdown` always available** — pressing **X** (or clicking the shortcut) on *My Hosts* now opens the Power chooser to shut down **this PC** even when the highlighted host is offline, or when no hosts are configured at all. Host shutdown still requires an online, approved host
- **Fullscreen-exit freeze fixed** — closing StreamLight while its window was fullscreen could lock up the whole PC, forcing a hard power-off; it now leaves fullscreen before shutting down so the graphics driver tears the window down cleanly

> 🙏 Huge thanks to [**@Soladus**](https://github.com/Soladus) for reporting the high-refresh judder ([#2](https://github.com/FoggyBytes/StreamLight/issues/2)) and beta-testing the frame-pacing fix on both NVIDIA and AMD clients — the logs and measurements made this release possible.

## ✨ What's New in 3.3.0 — "The Patch Tuesday Update"

Windows Update, driven entirely from the couch. **Requires StreamTweak 7.3.0 or later**; update both apps together.

- **Update the host's Windows Update** — Options → *Check Windows Update on host…* scans the host, shows the updates classified (*Security & critical / Defender / Optional*; feature/version upgrades shown but not installed remotely), lets you pick *Security + Defender* or *All updates*, then installs and restarts the host only if required. The job runs in the background (status-bar chip + **RB** to reopen), so the app stays usable while it works
- **Update and shut down** — the Power… chooser can install pending Windows updates before powering off the host and/or this client; it checks both sides, shows where updates are pending (🟠 pending / ✓ up to date), and enables the option only when there's something to install. Plus an **X · Shutdown** status-bar shortcut for the highlighted host
- **Options menu redesigned** — a wide tile grid (emoji + short label) instead of a text list, much comfier to navigate with a controller
- **Tailscale unified into one tile** — a host reachable on the LAN *and* over Tailscale is now a **single** tile that tracks both IPs (with a `TAILSCALE · AVAILABLE` badge) instead of a duplicate. A *Tailscale* option opens the host's apps over the `100.x` address; existing duplicate tiles are merged automatically on first launch

> 🙏 Thanks again to [**@SolemnDucc**](https://github.com/FoggyBytes/StreamLight/issues/1) for the headless-host Windows Update suggestions.

## ✨ What's New in 3.2.0 — "The Power Update"

A new **Power…** option on each paired, online host lets you shut down the host PC, this (client) PC, or both — host shutdown rides the authenticated bridge and only works once the host has approved this device. **Requires StreamTweak 7.2.0 or later**; update both apps together.

- **Power… menu** — Host / Client / Both chooser; "Both" shuts the host down and then powers off the client a moment later. Fully gamepad- and keyboard-navigable, with Cancel focused by default
- **Host shutdown gated on approval** — Host and Both are available only on an AUTHORIZED host; Client always works

> 🙏 Thanks to [**@SolemnDucc**](https://github.com/FoggyBytes/StreamLight/issues/1) for suggesting this feature ([#1](https://github.com/FoggyBytes/StreamLight/issues/1)).

## ✨ What's New in 3.1.0 — "The Secure Bridge Update"

The bridge to StreamTweak is now **authenticated**. StreamLight signs every command with its existing Moonlight identity certificate, so only clients the host user has approved can control the host — closing the previously open bridge. **Requires StreamTweak 7.1.0 or later**; update both apps together.

- **Authenticated bridge** — each command (NIC control, host metrics, store map, telemetry) is signed with StreamLight's Moonlight certificate. On first contact StreamLight enrolls automatically and StreamTweak shows a one-time *"Allow this client?"* prompt on the host
- **Per-host access badge** — each host card shows its access state at a glance: **AUTHORIZED** (green), **PENDING** (amber) or **DENIED** (red); hidden for hosts without StreamTweak. A new **"Request StreamTweak access"** menu option re-sends the request

This release also folds in the **3.0.1 "Bridge Fix"** hardening (multi-packet `APPSTORES` buffering, per-request sockets to stop host cross-talk, UUID-keyed store cache, a 3-second `STATS` watchdog, and JSON-field remote-pause matching). The headline **3.0.0 "Big UI Update"** and the complete version history are in [changelog.txt](changelog.txt).

## 🏗️ Architecture

StreamLight is a Qt 6 / QML fork of Moonlight-Qt. The decoder pipeline (FFmpeg / D3D11VA / DXVA2 / libplacebo / `moonlight-common-c`) is identical to upstream Moonlight; the UI layer was rewritten for 3.0.

Integration with StreamTweak happens over a TCP bridge on **port 47998** (LAN, line-delimited ASCII). Commands sent by StreamLight: `NETINFO`, `SETSPEED`, `STATUS`, `STATS`, `APPSTORES`, `TAILSCALE`, `SESSIONDATA`, plus the power/update commands `SHUTDOWN`, `SHUTDOWN_UPDATE`, `UPDATESTATE`, `UPDATECHECK`, `UPDATE_NOW`, `UPDATEPROGRESS`. From 3.1.0 each command is preceded by an `AUTH1` line signing it with the client's Moonlight certificate (RSA-SHA256), and a one-time `ENROLL` registers the client with the host for approval. The same bridge carries NIC commands, host metrics, store data, Tailscale presence, remote-pause signals, remote power-off, and remote Windows Update.

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

1. Go to the **Releases** page of this repository.
2. Download the latest installer (`StreamLight_5.0.0_Installer.exe`) and run it.

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
