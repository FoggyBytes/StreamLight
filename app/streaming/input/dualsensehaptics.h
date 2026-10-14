// Imported from Nonary/moonlight-qt, branch master at f96b0524 (14/10/2026), by Chase Payne.
// GPL-3.0-or-later, compatible with StreamLight's GPLv3. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SDL_compat.h"
#include <cstdint>

// Owns only waveform playback. SDL remains the input owner. The registry is
// independent of Session lifetime: receive callbacks can race with teardown.
namespace DualSenseHaptics {
bool attach(unsigned slot, SDL_GameController* controller);
void detach(unsigned slot);
bool playing(unsigned slot);
void receive(uint16_t slot, uint32_t sequence, const uint8_t* pcm, uint16_t frames);
}
