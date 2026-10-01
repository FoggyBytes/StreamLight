# Where this directory comes from

Imported verbatim from **Nonary/moonlight-qt**, branch `release/6.1.0-vrr18` at tag **`v6.1.0-vrr18`**
(`1ad5848b`), by Chase Payne. GPLv3, the same licence as StreamLight. Kept byte-identical (CRLF aside)
so a later sync is a plain `diff`, like `tests/vrr`.

Not part of the app build. Checked on Windows for 6.4.0 (§81 in `docs/notes/39-fast-lane-6.4.0.md`):
`framing`, `udpreceive`, `d3d11`, `roundtrip` pass as they are. ⚠️ `rtpqueue` needs one stub to link:
our `moonlight-common-c` is master plus Nonary's PyroWave commits, and master's `RtpVideoQueue.c` calls
`isReferenceFrameInvalidationEnabled()`, which Nonary's fork (and so this test) predates. Add
`bool isReferenceFrameInvalidationEnabled(void) { return true; }` in a wrapper .pro — `true` is the stricter
case, since the PyroWave mask alone must then keep lost-frame reporting off — and it passes.
