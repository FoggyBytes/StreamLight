#include "../../app/streaming/video/ffmpeg-renderers/plvkswapchain.h"

#include <cstdio>
namespace {
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

int main()
{
    expect(plVkPersistentPresentModeProvidesLatchProtection(
               VK_PRESENT_MODE_MAILBOX_KHR),
           "persistent Mailbox must provide latch protection without a software floor");
    expect(!plVkPersistentPresentModeProvidesLatchProtection(
               VK_PRESENT_MODE_IMMEDIATE_KHR),
           "persistent Immediate must retain software spacing");
    expect(!plVkPersistentPresentModeProvidesLatchProtection(
               VK_PRESENT_MODE_FIFO_KHR),
           "persistent FIFO must not claim adaptive latch protection");

    return failures ? 1 : 0;
}
