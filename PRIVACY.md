# Privacy

StreamLight has **no analytics, no telemetry, no accounts and no ads**. Nothing is ever sent to FoggyBytes: there is no FoggyBytes server.

This page lists every connection StreamLight makes on its own, and every piece of data that leaves the PC.

## Your own computers

These connections are the point of the program and go only to hosts you added or that answered on your local network.

- **Streaming** — the Moonlight protocol to the host you pair with (Sunshine, Apollo, Vibeshine, Vibepollo): video, audio, your input, and the game list and cover art the host serves.
- **Discovery** — mDNS on the local network, to find hosts.
- **StreamTweak** — when the host runs [StreamTweak](https://github.com/FoggyBytes/StreamTweak), the bridge on TCP 47998. On first contact StreamLight sends its client certificate and this PC's name, which the host shows when asking whether to approve it. During a stream it sends **stream statistics** (frame rate, latency, jitter, bitrate, decode time, dropped frames) so StreamTweak can keep a quality history on the host. With **Shared clipboard** turned on (off by default), clipboard text, encrypted — see [SECURITY.md](SECURITY.md).

## Third-party services

| When | Where | What is sent | Can it be turned off |
|---|---|---|---|
| At every launch, and when Settings opens | `api.github.com` (GitHub) | A request for the latest StreamLight release; the request carries your IP address and `StreamLight/<version>` as its user agent | No — it is how the update prompt works. Nothing else is sent |
| When you choose *Update now* | `github.com` / GitHub's download servers | The download of the installer | It only happens on request |
| At launch | `moonlight-stream.org` (the Moonlight project) | Requests for the gamepad mapping database and the host compatibility list — inherited unchanged from Moonlight | No |
| During a stream, if the Discord app is running | Discord, through the Discord app on this PC | The name of the game being streamed, for your Discord status | **Yes** — Settings → *Discord Rich Presence* (on by default) |

GitHub's and Discord's own privacy statements apply to what they receive: [GitHub](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement), [Discord](https://discord.com/privacy).

Links in Settings (releases, licence, donations, help pages) open your browser only when you click them.

## Location

With the theme set to **Auto**, StreamLight asks Windows for this PC's position, to compute sunrise and sunset (and to know whether *Sunrise & sunset* can be offered). Windows decides whether to answer, under Settings → Privacy & security → Location. The coordinates are **kept in memory only**: never written to disk, never sent anywhere. With Dark, Light or System the location is never read.

## Data kept on this PC

- **Settings, paired hosts and the client certificate** — in the Windows registry, `HKCU\Software\FoggyBytes\StreamLight`, for the current Windows user.
- **Play time and last played** — per host and per game, in the same place. They stay on this PC.
- **Logs and crash dumps** — in the folder chosen in Settings → About (temporary files by default); **never uploaded**. If you attach a log to an issue, read it first: it contains host names and IP addresses.
- **Cover cache** — `%LOCALAPPDATA%\FoggyBytes\StreamLight\cache`, clearable from Settings → About.
- **Steam artwork** — only when you apply it from Settings, pictures are written into Steam's own folder on this PC (`userdata\<account>\config\grid`); nothing is sent to Steam.

Uninstalling StreamLight removes the program; Settings → About can clear logs and covers beforehand.
