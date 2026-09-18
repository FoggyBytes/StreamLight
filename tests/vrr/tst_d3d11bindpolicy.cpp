#include "../../app/streaming/video/ffmpeg-renderers/d3d11bindpolicy.h"

#include <cstdio>

int main()
{
    int failures = 0;

    auto check = [&](bool intel, bool separate, bool bindSafe, int width, int height,
                     bool expected, const char* description) {
        const bool actual = d3d11ShouldBindDecoderOutputTextures(
            intel, separate, bindSafe, width, height);
        if (actual != expected) {
            std::fprintf(stderr, "FAIL: %s (got %d, expected %d)\n",
                         description, actual ? 1 : 0, expected ? 1 : 0);
            ++failures;
        }
    };

    check(true, false, false, 1920, 1080, true,
          "Intel 1080p keeps the stock bind path");
    check(true, false, true, 3840, 2160, true,
          "Intel 4K keeps the stock bind path");
    check(false, true, false, 1920, 1080, true,
          "separate devices keep the stock bind path at 1080p");
    check(false, true, true, 3840, 2160, true,
          "fenced separate devices retain zero-copy binding for 4K high refresh");
    check(false, true, false, 3840, 2160, true,
          "separate-device binding does not depend on single-device eligibility");
    check(false, false, true, 1920, 1080, false,
          "safe AMD/NVIDIA 1080p keeps the compatibility copy path");
    check(false, false, true, 2560, 1440, false,
          "safe AMD/NVIDIA 1440p keeps the compatibility copy path");
    check(false, false, true, 3840, 1600, false,
          "ultrawide 1600p is not 4K-class and keeps the copy path");
    check(false, false, true, 3839, 2160, false,
          "width below 4K keeps the copy path");
    check(false, false, true, 3840, 2159, false,
          "height below 4K keeps the copy path");
    check(false, false, true, 3840, 2160, true,
          "safe AMD/NVIDIA 4K binds to avoid the uncompressed copy");
    check(false, false, true, 4096, 2160, true,
          "DCI 4K binds on safe discrete GPUs");
    check(false, false, true, 7680, 4320, true,
          "8K retains the eligible direct bind path");
    check(false, false, false, 3840, 2160, false,
          "4K still copies when the discrete GPU cannot bind safely");

    return failures ? 1 : 0;
}
