#include "../../app/streaming/video/vrrrenderpolicy.h"

#include <cstdio>

int main()
{
    int failures = 0;

    auto checkPref = [&](bool enableVrr, bool preferVrrRenderer, bool expected, const char* description) {
        const bool actual = decoderPrefersVrrCapableRenderer(enableVrr, preferVrrRenderer);
        if (actual != expected) {
            std::fprintf(stderr, "FAIL: %s (got %d, expected %d)\n",
                         description, actual ? 1 : 0, expected ? 1 : 0);
            ++failures;
        }
    };

    checkPref(false, false, false, "fixed presentation does not prefer the VRR renderer");
    checkPref(true, false, true, "active VRR presentation prefers the VRR renderer");
    checkPref(false, true, true, "probe renderer policy prefers the VRR renderer without presentation");
    checkPref(true, true, true, "playback with both flags still prefers the VRR renderer");

    auto checkRange = [&](int negotiated, bool expected, const char* description) {
        const bool actual = vulkanShouldForceMappedFullRange(negotiated);
        if (actual != expected) {
            std::fprintf(stderr, "FAIL: %s (got %d, expected %d)\n",
                         description, actual ? 1 : 0, expected ? 1 : 0);
            ++failures;
        }
    };

    checkRange(kNegotiatedColorRangeLimited, false,
               "limited-range streams must not force a full-range mapping");
    checkRange(kNegotiatedColorRangeFull, true,
               "full-range streams retain the AMF metadata workaround");
    checkRange(2, false, "unknown range values must not force a full-range mapping");

    return failures ? 1 : 0;
}
