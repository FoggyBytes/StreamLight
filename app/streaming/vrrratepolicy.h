// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <vector>

// VRR rate selection and deterministic stream/display qualification live in
// one small policy object. It has no dependency on SDL, QSettings, or a
// renderer, so the arithmetic can be tested independently.
enum class VrrFpsChoiceKind {
    Fixed,
    Vrr,
    LowLatencyVrr,
    Custom,
};

struct VrrFpsChoice {
    int fps;
    VrrFpsChoiceKind kind;
};

class VrrRatePolicy
{
public:
    // Below-refresh recommendation: floor(refresh - refresh^2 / 3600).
    static int vrrRateForRefresh(int refreshHz);

    // Lower-rate option: floor((refresh * 5 / 6) / 5) * 5.
    static int lowLatencyRateForRefresh(int refreshHz);

    // Shared recommendation and native-presentation protection cutoff.
    // Keep floor(r - r*r/3600), including its rounding.
    static constexpr int protectedRateForRefresh(int refreshHz)
    {
        const long long numerator = static_cast<long long>(refreshHz) *
                                    (3600LL - refreshHz);
        return refreshHz > 1 && numerator > 0 ?
            static_cast<int>(numerator / 3600LL) : 0;
    }

    // Accept sources through native refresh. Per-frame presentation enforces
    // scanout safety; session admission no longer reserves a fixed FPS margin.
    static bool hasAdaptiveHeadroom(int streamRateHz, int displayRefreshHz);

    // Build baseline, recommended VRR, low-latency, and saved custom choices.
    static std::vector<VrrFpsChoice> buildChoices(const std::vector<int>& refreshRates,
                                                   int savedFps,
                                                   bool vrrEnabled);

};
