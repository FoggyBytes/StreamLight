// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once
#include "reserve.h"
#include <QString>

namespace Vrr13 {
// Bounded, versioned model cache. No phase, native timestamps, or temporary
// boost survives a session. Empty profile key disables disk access (the lab/tests).
bool loadProfile(const QString& path, const QString& key, Reserve& reserve);
enum class PresentationValidation { Unavailable, Passed, Failed };
bool saveProfile(const QString& path, const QString& key, const Reserve& reserve,
                 PresentationValidation presentation = PresentationValidation::Unavailable);
// The playout delay a session with this key settled at, used to start the
// next session there instead of at a generic guess. Zero means none.
uint64_t loadStartDelay(const QString& path, const QString& key);
bool saveStartDelay(const QString& path, const QString& key, uint64_t delayUs);
}
