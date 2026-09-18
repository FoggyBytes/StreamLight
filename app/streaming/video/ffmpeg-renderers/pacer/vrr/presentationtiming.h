// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <cstdint>

namespace Vrr13 {

// A successful native query does not establish the meaning of its timestamp.
// In particular, a DXGI refresh reference can precede the associated Present.
enum class PresentationTimeKind : uint8_t {
    Unavailable = 0,
    RefreshReference = 1,
    DisplayEvent = 2,
};

}
