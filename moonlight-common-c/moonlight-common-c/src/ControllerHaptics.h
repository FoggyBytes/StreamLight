// SPDX-License-Identifier: GPL-3.0-or-later
// Versioned Vibeshine extension. All multibyte fields and samples are LE.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define ML_HAPTICS_PACKET_TYPE 0x5601
#define ML_HAPTICS_CHANNEL 0x08
#define ML_HAPTICS_HEADER_SIZE 12
#define ML_HAPTICS_MAX_FRAMES 240
#define ML_HAPTICS_FRAME_BYTES 4
#define ML_HAPTICS_MAX_PAYLOAD (ML_HAPTICS_HEADER_SIZE + ML_HAPTICS_MAX_FRAMES * ML_HAPTICS_FRAME_BYTES)
#define LI_CCAP_HAPTICS_PCM 0x8000
#define ML_FF_HAPTICS_PCM 0x04

static inline uint16_t MlHapticsRead16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t MlHapticsRead32(const uint8_t* p) {
    return (uint32_t)MlHapticsRead16(p) | ((uint32_t)MlHapticsRead16(p + 2) << 16);
}
static inline void MlHapticsWrite16(uint8_t* p, uint16_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
}
static inline void MlHapticsWrite32(uint8_t* p, uint32_t n) {
    MlHapticsWrite16(p, (uint16_t)n); MlHapticsWrite16(p + 2, (uint16_t)(n >> 16));
}
// Payload: version=1, format=1 (48 kHz S16LE stereo), controller, sequence,
// frame count, reserved=0, then interleaved left/right actuator samples.
static inline int MlHapticsValidate(const uint8_t* p, size_t size) {
    if (!p || size < ML_HAPTICS_HEADER_SIZE || p[0] != 1 || p[1] != 1 ||
        MlHapticsRead16(p + 2) >= 16 || MlHapticsRead16(p + 10) != 0) return 0;
    uint16_t frames = MlHapticsRead16(p + 8);
    return frames > 0 && frames <= ML_HAPTICS_MAX_FRAMES &&
        size == ML_HAPTICS_HEADER_SIZE + (size_t)frames * ML_HAPTICS_FRAME_BYTES;
}
