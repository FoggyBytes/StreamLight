# SAxense in Moonlight

Upstream: https://github.com/egormanga/SAxense

Pinned revision: `6fd1648e0b2168bfe270f26b766fc897562091ab`

Author/research credit: Sdore (Egor Vorontsov / egormanga), 2025.
Research: https://apps.sdore.me/SAxense/

`SAxense.c`, `README.md`, and `LICENSE` are unmodified upstream copies.
`packet.h` adapts the Bluetooth report layout and CRC from `SAxense.c`.
It replaces packed bitfields, global mutable state and the signal-driven CLI
with a bounded packet builder and initializes every transmitted byte. Playback,
device association, resampling, networking and lifecycle are Moonlight code.

License: MPL-2.0. The source does not carry an Exhibit B incompatibility notice;
the example Exhibit B in the license text is not an application of that notice.
These covered files retain their MPL notices and are additionally distributed
under GPL-3.0-or-later with the existing GPL Moonlight work, under MPL section 3.3.
Recipients can choose either license for the covered files. The GPL text is
the repository root `LICENSE`; the unchanged MPL text is here in `LICENSE`.
See https://www.mozilla.org/en-US/MPL/2.0/FAQ/#q14-may-i-combine-mpl-licensed-code-and-lgpl-licensed-code-in-the-same-executable-program

The binary embeds these notices, both license texts, the original source and
the adapted source. `moonlight --haptics-license` prints them without opening a
display or controller. Keep these resources in all packages and include this
directory in corresponding source distributions, including modified versions.
The embedded adapted source is the exact copy compiled into that binary.

This is a protocol component, not a complete controller framework. It adds no
Sony SDK, firmware, closed driver, system daemon or proprietary redistributable.
