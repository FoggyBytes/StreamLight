#include "../../app/streaming/video/amddecodepolicy.h"
#include <cstdio>

int main()
{
    struct Case { const char* input; const char* expected; };
    const Case cases[] = {
        {"", "lowlatencydec"},
        {"info", "info,lowlatencydec"},
        {"info,", "info,lowlatencydec"},
        {"lowlatencyenc", "lowlatencyenc,lowlatencydec"},
        {"info, lowlatencydec ,checkir", "info, lowlatencydec ,checkir"},
    };
    for (const auto& c : cases) {
        const auto result = withAmdLowLatencyDecode(c.input);
        if (result != c.expected || withAmdLowLatencyDecode(result) != result) {
            std::fprintf(stderr, "FAIL: decoder flag merge for %s\n", c.input);
            return 1;
        }
    }
    return 0;
}
