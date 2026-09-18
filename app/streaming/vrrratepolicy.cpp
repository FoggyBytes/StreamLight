// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "vrrratepolicy.h"

#include <algorithm>
#include <map>

namespace {

bool isUsableRefreshRate(int refreshHz)
{
    // SDL reports zero for an unknown refresh rate.  Values at or below one
    // cannot produce a meaningful stream-rate recommendation either.
    return refreshHz > 1;
}

void addChoice(std::map<int, VrrFpsChoiceKind>& choices,
               int fps,
               VrrFpsChoiceKind kind)
{
    if (fps > 0) {
        // Keep the first semantic role for duplicate rates.  Baseline choices
        // intentionally win over a coincident calculated/native rate.
        choices.emplace(fps, kind);
    }
}

} // namespace

int VrrRatePolicy::vrrRateForRefresh(int refreshHz)
{
    if (!isUsableRefreshRate(refreshHz)) {
        return 0;
    }

    // Use the original below-refresh recommendation and integer rounding.
    return protectedRateForRefresh(refreshHz);
}

int VrrRatePolicy::lowLatencyRateForRefresh(int refreshHz)
{
    if (!isUsableRefreshRate(refreshHz)) {
        return 0;
    }

    return (refreshHz / 6) * 5;
}

bool VrrRatePolicy::hasAdaptiveHeadroom(int streamRateHz, int displayRefreshHz)
{
    if (streamRateHz <= 0 || displayRefreshHz <= 0) {
        return false;
    }

    return streamRateHz <= displayRefreshHz;
}

std::vector<VrrFpsChoice> VrrRatePolicy::buildChoices(const std::vector<int>& refreshRates,
                                                       int savedFps,
                                                       bool vrrEnabled)
{
    std::map<int, VrrFpsChoiceKind> choices;

    // These are always useful streaming rates, including on a 60 Hz display
    // where 60 is also the exact native rate.
    addChoice(choices, 30, VrrFpsChoiceKind::Fixed);
    addChoice(choices, 60, VrrFpsChoiceKind::Fixed);

    // Native refresh remains a normal choice with VRR enabled. Insert all
    // native rates first so another display's recommendation cannot relabel one.
    for (const int refreshHz : refreshRates) {
        if (isUsableRefreshRate(refreshHz)) {
            addChoice(choices, refreshHz, VrrFpsChoiceKind::Fixed);
        }
    }

    for (const int refreshHz : refreshRates) {
        if (!isUsableRefreshRate(refreshHz)) {
            continue;
        }

        if (vrrEnabled) {
            addChoice(choices, vrrRateForRefresh(refreshHz), VrrFpsChoiceKind::Vrr);
            addChoice(choices, lowLatencyRateForRefresh(refreshHz), VrrFpsChoiceKind::LowLatencyVrr);
        }
    }

    // Preserve manually saved values outside the built-in choices.
    if (savedFps > 0) {
        addChoice(choices, savedFps, VrrFpsChoiceKind::Custom);
    }

    std::vector<VrrFpsChoice> result;
    result.reserve(choices.size());
    for (const auto& choice : choices) {
        result.push_back({choice.first, choice.second});
    }

    return result;
}
