The precompiled dependencies for Windows and macOS versions of [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) are built using AppVeyor CI running on [this repository](https://github.com/cgutman/moonlight-deps).

The precompiled dependencies for Steam Link versions of [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) are built using AppVeyor CI running on [this repository](https://github.com/cgutman/moonlight-steamlink-deps).

Usage of the headers and binaries in this repository are governed by their respective upstream license(s).

## StreamLight: Windows x64 DLLs

The 12 runtime DLLs in `windows/lib/x64` (`avcodec-63`, `avformat-63`, `avutil-61`, `swscale-10`, `dav1d`, `discord-rpc`, `libcrypto-3-x64`, `libssl-3-x64`, `libplacebo-374`, `opus`, `SDL2`, `SDL3`) are the signed copies from the official **Moonlight 6.2.0** release (`MoonlightPortable-x64-6.2.0.zip`, SHA-256 `6f91d268b41ed5ca8066d9d7578dc403b8db190991ff48f00b1700465f8f7ebf`). Their code is identical to `moonlight-qt-deps` v19; the only difference is the Authenticode signature by Cameron Gutman, which Windows Smart App Control needs.

Update them only when a Moonlight release ships the new versions, and take them from that release: a DLL taken from `moonlight-qt-deps` alone is unsigned. Never take `AntiHooking.dll` or `Moonlight.exe` from it - those are built by Moonlight, not by StreamLight. Nor `SDL2_ttf.dll`, which Moonlight still ships: StreamLight stopped using it in 8.3.0 (its overlays are painted with QPainter), and its files were removed from every platform here (Windows x64 and arm64, macOS). The `.lib` and `.pdb` files stay those of the matching `moonlight-qt-deps` bundle; a signature does not change the code.
