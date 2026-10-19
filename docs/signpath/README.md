# Code signing with SignPath

StreamLight's Windows release is signed through the [SignPath Foundation](https://signpath.org), which
signs only files that a GitHub Actions run produced. The build and the signing run in
[`.github/workflows/build-windows.yml`](../../.github/workflows/build-windows.yml).

The two XML files here are **review copies** of the artifact configurations in the SignPath portal.
The portal is the source of truth: changing these files does not change what gets signed.

## What is signed, and what is not

| File | Signed by |
| --- | --- |
| `StreamLight.exe`, `AntiHooking.dll` | SignPath, request 1 (`binaries`) — built from this repository |
| Inno Setup uninstaller (`unins000.exe`, and the Setup program the installer runs from `%TEMP%`) | SignPath, request 1 (`binaries`) |
| `StreamLight_X.Y.Z_Installer.exe` | SignPath, request 2 (`installer`) |
| Qt DLLs and plugins | The Qt Company, as shipped |
| FFmpeg, SDL, OpenSSL, opus, dav1d, libplacebo, discord-rpc | the Moonlight maintainer, as shipped in the Moonlight release they come from (see [`libs/README.md`](../../libs/README.md)) |
| `vc_redist.x64.exe` | Microsoft |

## How a signed run works

1. The build runs as usual and fills the deploy folder.
2. A first ISCC pass with `/DSignUninstaller` writes `uninst-<inno>-<hash>.e64` and stops, asking for it to
   be signed (Inno Setup's `SignedUninstaller` without a sign tool). That binary is the uninstaller and
   also the Setup program the installer unpacks and runs, so it cannot stay unsigned.
3. `StreamLight.exe`, `AntiHooking.dll` and that file (as `Uninstaller.exe`) go to SignPath in one zip.
4. The signed files go back in place, and a second ISCC pass builds the installer around the signed
   uninstaller.
5. The installer goes to SignPath on its own.

Each request waits for its approval in the SignPath portal when the signing policy asks for one
(`release-signing` does; `test-signing` signs with a test certificate Windows does not trust).

## Repository settings

- Secret `SIGNPATH_API_TOKEN` — without it the workflow builds unsigned, as before.
- Variable `SIGNPATH_ORGANIZATION_ID`.
- Variable `SIGNPATH_PROJECT_SLUG` — optional, `StreamLight` when absent.

## Portal setup

1. Project `StreamLight`, linked to this repository through the GitHub trusted build system.
2. Artifact configuration `binaries` = [`binaries.xml`](binaries.xml).
3. Artifact configuration `installer` = [`installer.xml`](installer.xml).
4. Signing policies `test-signing` and `release-signing`.

Every signed file must carry product name `StreamLight` and product version `X.Y.Z.0`: `app.pro`
and `AntiHooking/AntiHooking.pro` set them for the exe and the DLL, `StreamLight.iss` for the installer
and its uninstaller. Inno Setup pads both strings with spaces in the uninstaller and the installer
(see the comment in [`binaries.xml`](binaries.xml)), so their entries carry the same padding.
