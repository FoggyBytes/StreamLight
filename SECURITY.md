# Security Policy

## Supported versions

Only the **latest release** on the [Releases page](https://github.com/FoggyBytes/StreamLight/releases) receives fixes. StreamLight updates itself from Settings → About, so there is no reason to stay on an older version.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem.

Report it privately through GitHub: **[Report a vulnerability](https://github.com/FoggyBytes/StreamLight/security/advisories/new)** (Security tab → *Report a vulnerability*). Include the version, what an attacker can do, and the steps to reproduce it.

StreamLight is maintained by one person, so there is no fixed response time — but a security report is read before anything else, and you will get an answer in the advisory thread. Fixes ship as a normal release, with the advisory published once users have had the update available.

If the problem is in the streaming protocol or the decoder pipeline that StreamLight shares with Moonlight, it most likely affects [Moonlight](https://github.com/moonlight-stream/moonlight-qt) too: report it there as well.

## How StreamLight is put together

StreamLight is a fork of [Moonlight-Qt](https://github.com/moonlight-stream/moonlight-qt) (GPL v3). Knowing which parts are ours helps to judge where a problem lives:

- **Streaming protocol and decoders** — `moonlight-common-c`, FFmpeg, D3D11VA/DXVA2, libplacebo — are upstream Moonlight's, plus Nonary's VRR pacing and PyroWave codec, imported unchanged. Upstream's security fixes are carried over as they land.
- **Third-party DLLs** — the 13 libraries in the install folder (FFmpeg, SDL, OpenSSL, opus, dav1d, libplacebo, discord-rpc) are the **signed copies from the official Moonlight release** with the same library versions, byte for byte; the Qt DLLs and plugins are signed by The Qt Company. See [`libs/README.md`](libs/README.md).
- **Ours** — the interface, `StreamLight.exe`'s application code, the bridge to StreamTweak, the TV service (`StreamLightService.exe`), the self-updater and the installer.

## Security-relevant design

### `AntiHooking.dll`

The name sounds alarming; what it does is narrow and defensive. `AntiHooking.dll` is **Moonlight's own module, identical to upstream's** (same file, same Git blob as `moonlight-qt/AntiHooking/antihookingprotection.cpp`), and it ships with the official Moonlight client too.

It is loaded by `StreamLight.exe` only, inside its own process, and it intercepts that process's `LoadLibrary*` calls to **refuse a short, fixed list of DLLs** that third-party software injects into every program and that are known to crash or deadlock video playback:

- the on-screen-display DLLs of A-Volute *Nahimic* / ASUS *Sonic Studio* / SteelSeries audio suites (deadlock in D3D9 fullscreen — [moonlight-qt#102](https://github.com/moonlight-stream/moonlight-qt/issues/102));
- `EZFRD32/64.dll`, seen in many crash reports;
- the legacy Optimus hybrid-graphics shim DLLs (crash in `PresentEx` — [moonlight-qt#386](https://github.com/moonlight-stream/moonlight-qt/issues/386)).

It does not hide StreamLight from anything, does not touch other processes, and does not interfere with security software: it only stops StreamLight itself from loading those specific files.

### The bridge to StreamTweak (TCP 47998)

When the host runs [StreamTweak](https://github.com/FoggyBytes/StreamTweak), StreamLight talks to it on TCP port 47998 over the local network.

- **Every command is signed** (RSA-SHA256, PKCS#1 v1.5) with StreamLight's existing Moonlight client certificate — the same key pair that is generated on the PC at first launch and used to pair with Sunshine. The signature covers the client id, a timestamp and the command; the host checks it against the certificate it approved, rejects timestamps outside ±60 seconds and rejects replays.
- **A client is approved once, by a person at the host**: StreamLight sends its certificate and a 4-digit PIN that is shown on both screens, and StreamTweak asks its user to compare and approve. Nothing is trusted on first contact alone.
- Commands that act on the host — power, Windows Update, link speed, clipboard — are refused unless the signature verifies.
- **The StreamTweak section** (8.0.0, with StreamTweak 10.0.0) changes the host's settings only through three fixed switches — a managed app on or off, spatial audio, NVIDIA Sentinel's auto-restore — and StreamLight refuses to send anything else in their place. No path or command line leaves this device. The host accepts them only from a device it has given *Host management*, on top of the signature. Its Sessions tab only reads, with four fixed requests StreamLight checks before sending.
- **Streaming never depends on the bridge.** Without an approved bridge StreamLight is an ordinary Moonlight client.

### Shared clipboard (off by default)

When both sides turn it on, clipboard text travels over the bridge **encrypted with AES-256-GCM** under a key made fresh for each stream. The key is generated by the host and sent to the client wrapped with RSA-OAEP for the client's certificate; it is discarded when the stream ends. No hash or other derivative of the plaintext is ever sent. Text that password managers mark as sensitive is cleared on the other side when it is cleared at its source, and after 60 seconds at most.

### Self-update

The updater looks up the latest release through the GitHub API and downloads the installer asset from GitHub. **It refuses to run any installer without a SHA-256 digest published by GitHub for that asset**, checks the downloaded file against it, and checks it again immediately before launching it.

### Updating without the Windows permission prompt (8.6.0)

StreamLight is meant to be used with a controller only, and the Windows permission prompt (UAC) appears on a secure desktop that a controller cannot answer. So from 8.6.0 an update started from Settings installs without it, through a scheduled task that the installer — which already runs with administrator rights — registers. Skipping that prompt on purpose is the most sensitive thing StreamLight does, so it is fenced on four levels.

**What can be asked.** The task is called *StreamLight Update*. It runs one fixed program, `StreamLightUpdater.exe`, from the install folder, **with no arguments**. The task's action takes no `$(Arg…)`, so arguments passed when it is started are ignored. It is registered for the Windows account that ran the setup, with that account's highest rights, and only that account (plus administrators and SYSTEM) may start it. Starting it is the only thing StreamLight can do. It cannot choose *which* installer, version, folder or options; it can only ask for "the latest official release, if it is newer".

**What the updater checks, itself, before anything runs.** It trusts nothing that StreamLight computed:

- it runs elevated, or it stops at once. StreamLight starts the task only for an account that can elevate and that the task was registered for; otherwise (another account, a standard user, an install from before 8.6.0) it says on screen that Windows will ask, and starts the same updater through the ordinary prompt;
- its folders are checked before every use. `%ProgramData%\FoggyBytes` and `…\StreamLight` must belong to SYSTEM or the administrators (anyone may create folders in ProgramData, and an owner can give itself the right to rename what is inside); `…\update` and its `run` and `tmp` folders must give no write, delete or permission change to anyone else; none of them may be a junction or other reparse point. The installer sets those rights and removes a junction it finds there;
- it first copies itself (and `Qt6Core.dll`) into `…\update\run` and continues from there, so it holds nothing open in the install folder that Setup has to replace;
- it asks the GitHub API for the latest release itself, over HTTPS to `api.github.com` only, without redirects. The server's certificate must also chain to a root in the **machine's** store: a root added to the user's own store does not count. The reply may be at most 1 MB;
- it picks the installer asset with the same rule as the app (name `StreamLight_<version>_Installer.exe`, version equal to the release tag) and requires the SHA-256 digest GitHub publishes for it;
- that version must be **newer** than `StreamLight.exe` in the install folder, which it finds through the uninstall entry under `HKLM` — only administrators write either. No reinstall, no downgrade;
- it takes the file StreamLight downloaded, by that exact asset name, from the user's download folder and **copies** it into `…\update`, then hashes **the copy** against the digest through a handle that stays open, without write or delete sharing, until Setup has finished — so the file that was checked is the one that runs. A mismatch deletes the copy, and nothing runs;
- once release builds are signed (SignPath), checking the copy's signature is the next step; until then the digest is the check.

**How the installer is run.** From the protected folder, by full path, with its working folder and `TEMP`/`TMP` pointed at `…\update\tmp`, so nothing the user can write is in its way. The options are fixed: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /APPUPDATE`, and a log into the status folder. The install folder and the optional icons are the ones chosen at the last install. With `/APPUPDATE` Setup only posts its progress to the updater's window. One update runs at a time.

**The screen while StreamLight is closed.** The updater shows a full-screen window that redraws StreamLight's last frame, so the hand-over is not seen. It is drawn from `look.json`, which StreamLight leaves in the user's download folder: a file the user can write, read by an elevated process, so it is only **numbers and colours**, each one checked against a range, and at most 16 KB. The fonts and the icon are embedded in the updater; no picture is ever decoded and no text is ever taken from that file. A `cancel` file there can only stop an update before the screen is taken over.

**StreamLight never runs elevated.** The installer, started elevated, cannot reopen the app as the user. So the updater reopens `StreamLight.exe` from the install folder through Explorer's shell, with the user's ordinary rights. If that is not possible, it leaves StreamLight closed and does not start it elevated.

**What StreamLight sees.** The updater writes its progress (checking → ready → covering → installing → starting → done, or failed or cancelled, with the versions and a reason) to `%ProgramData%\FoggyBytes\StreamLight\update-status`, which users may read but not write. StreamLight closes only once the updater's window is over it. A failed check therefore leaves the app open, saying what went wrong. The update is still started only from Settings, and only when nothing is in progress that closing the app would leave half done (a stream, a wake, the link-speed question).

**What someone running as the user gains.** At most, the latest official release installed when it is newer than the one on the PC, exactly what pressing *Install now* does. They cannot choose the file, the version, the options or the folder; at worst they make an update fail, or draw its screen in odd colours. The trust anchor is the same as for a manual download, GitHub's release and its published digest, now checked by the elevated side rather than by the app.

The uninstaller deletes the task and both folders.

### The TV service (8.4.0)

`StreamLightService.exe` keeps a paired LG webOS TV in step with the PC, from boot on. It runs as **LOCAL SERVICE**, not LocalSystem, and the signed-in user may only start it, not stop or reconfigure it. With no TV paired it stops by itself.

- **Its pipe** (`\\.\pipe\StreamLight`) refuses remote clients, holds one instance, takes one JSON line of at most 16 KB per request from a fixed set of commands, and never returns the TV's key. StreamLight opens it at *identification* level, so the service cannot act as the user, and talks to it only when the process at the other end is the registered service (8.5.0).
- **Its data** (`%ProgramData%\FoggyBytes\StreamLight\service`) is readable and writable by the service and administrators only (8.5.0); StreamLight itself never reads it.
- **Its log** (`%ProgramData%\FoggyBytes\StreamLight\logs`) is a folder of its own, written by the service and readable by users (8.5.0), so StreamLight can copy it into its log folder as `StreamLightService.log`. It never contains the TV's key or certificate.
- **The TV** is identified by its MAC address before anything is sent. LG TVs present a certificate no system store knows, the same on every TV, so the one seen at pairing is kept: a TV that later presents another is sent nothing until it is paired again, and a TV paired over TLS is never reached over the unencrypted port (8.5.0). The TV's search over SSDP reads a device's description only from the device that answered.

### What the host sends

Names that come from the host — games, apps, devices, Windows Update titles, the Hue bridge's areas — are always shown as plain text, never as markup (8.5.0).

### Local data

Settings, paired hosts and the client certificate's private key are stored per Windows user in `HKCU\Software\FoggyBytes\StreamLight`. Logs, crash dumps and the cover cache stay on the PC (folders listed in Settings → About) and are never uploaded. See [PRIVACY.md](PRIVACY.md).

### Installer

The installer is built with Inno Setup on GitHub Actions from this repository ([`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml)); release assets carry a SHA-256 digest on the release page. It installs to Program Files — a folder chosen elsewhere is given Program Files' permissions, since the TV service runs from it (8.5.0) — installs the Microsoft Visual C++ runtime when the one on the PC is older than required, registers the TV service with a quoted path, registers the *StreamLight Update* task (8.6.0, see above), and includes an uninstaller (*Settings → Apps*).
