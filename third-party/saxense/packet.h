// SixAxis Sense - adapted for Moonlight, 2026.
// Original research and implementation by Sdore (Egor Vorontsov), 2025.
// https://github.com/egormanga/SAxense / https://apps.sdore.me/SAxense/
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Additionally distributed under GPL-3.0-or-later as part of Moonlight,
// pursuant to MPL 2.0 section 3.3. See PROVENANCE.md.
//
// Adaptation: bounded packet builder, explicit byte offsets and little-endian
// CRC, initialized padding. No upstream signal handler, timer or global state.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SAXENSE_REPORT_BYTES 142
#define SAXENSE_PCM_BYTES 64
#define SAXENSE_RATE 3000

static inline void saxense_packet(uint8_t report[SAXENSE_REPORT_BYTES],
                                   uint8_t sequence, const uint8_t pcm[SAXENSE_PCM_BYTES]) {
    memset(report, 0, SAXENSE_REPORT_BYTES);
    report[0] = 0x32;
    report[2] = 0x91; report[3] = 7;
    report[4] = 0xfe; report[9] = 0xff; report[10] = sequence;
    report[11] = 0x92; report[12] = SAXENSE_PCM_BYTES;
    memcpy(report + 13, pcm, SAXENSE_PCM_BYTES);
    uint32_t crc = ~UINT32_C(0xEADA2D49); // HID output prefix 0xA2
    for (size_t i = 0; i < SAXENSE_REPORT_BYTES - 4; ++i) {
        crc ^= report[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (crc & 1)));
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) report[SAXENSE_REPORT_BYTES - 4 + i] = (uint8_t)(crc >> (i * 8));
}
