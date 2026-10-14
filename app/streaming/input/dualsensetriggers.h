// Imported from Nonary/moonlight-qt, branch master at f96b0524 (14/10/2026), by Chase Payne.
// GPL-3.0-or-later, compatible with StreamLight's GPLv3. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SDL_compat.h"
#include <Limelight.h>
#include <cstddef>

// SDL's DualSense effect payload (USB/Bluetooth framing and CRC belong to SDL).
struct DualSenseOutputReport {
    uint8_t validFlag0;
    uint8_t validFlag1;
    uint8_t motorRight;
    uint8_t motorLeft;
    uint8_t reserved[4];
    uint8_t muteButtonLed;
    uint8_t powerSaveControl;
    uint8_t rightTriggerEffectType;
    uint8_t rightTriggerEffect[DS_EFFECT_PAYLOAD_SIZE];
    uint8_t leftTriggerEffectType;
    uint8_t leftTriggerEffect[DS_EFFECT_PAYLOAD_SIZE];
    uint8_t reserved2[6];
    uint8_t validFlag2;
    uint8_t reserved3[2];
    uint8_t lightbarSetup;
    uint8_t ledBrightness;
    uint8_t playerLeds;
    uint8_t lightbarRed;
    uint8_t lightbarGreen;
    uint8_t lightbarBlue;
};
static_assert(sizeof(DualSenseOutputReport) == 47, "SDL DualSense effect size");
static_assert(offsetof(DualSenseOutputReport, rightTriggerEffectType) == 10, "Right trigger offset");
static_assert(offsetof(DualSenseOutputReport, leftTriggerEffectType) == 21, "Left trigger offset");

inline DualSenseOutputReport makeDualSenseTriggerReport(uint8_t flags, uint8_t typeLeft,
                                                       uint8_t typeRight, const uint8_t* left,
                                                       const uint8_t* right)
{
    DualSenseOutputReport report {};
    // Only trigger validity bits: don't switch waveform playback back to
    // emulated rumble or overwrite independently controlled LEDs/audio state.
    report.validFlag0 = flags & (DS_EFFECT_RIGHT_TRIGGER | DS_EFFECT_LEFT_TRIGGER);
    report.rightTriggerEffectType = typeRight;
    SDL_memcpy(report.rightTriggerEffect, right, sizeof(report.rightTriggerEffect));
    report.leftTriggerEffectType = typeLeft;
    SDL_memcpy(report.leftTriggerEffect, left, sizeof(report.leftTriggerEffect));
    return report;
}
