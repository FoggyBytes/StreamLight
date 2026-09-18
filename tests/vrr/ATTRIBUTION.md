# Where this directory comes from

Imported verbatim from **Nonary/moonlight-qt**, branch `vrr17.1` at tag **`v6.1.0-vrr17.1`**
(`1ccefb6e`), by Chase Payne. GPLv3, the same licence as StreamLight.

**Nothing here carries a per-file note, on purpose**: these files are kept byte-identical to
Nonary's so that a later sync is a plain `diff` (ours are CRLF in the working tree because
`core.autocrlf` is true, so compare with the CRs stripped — `diff <(tr -d '\r' < ours) theirs`).

This is the harness the VRR timing parameters are tuned with. Adopting the VRR code without it
would mean having no way to answer a judder report — see §73.4 point 4 in
`docs/notes/31-vrr-6.0.0-beta.md`. Only the Windows-buildable `.pro` files are of use to us:
`ratepolicy`, `timingcontroller`, `pacingworker`, `replay`, `queuesim`, `dxgipresent`, `overlay`,
`presentationclock`, `incomingtiming`. The wayland, vulkantiming, gamescope and plvk ones need
Linux libraries we do not have, and the corresponding sources were not imported.

These tests are **not** part of the app build: `app/app.pro` does not reference them.
