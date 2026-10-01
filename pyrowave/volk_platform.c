// Granite builds volk with the platform surface entry points enabled, but only
// for this one translation unit (the rest of Granite must not see <windows.h>
// through vulkan.h). qmake has no per-file defines, hence this wrapper.
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "pyrowave/Granite/third_party/volk/volk.c"
