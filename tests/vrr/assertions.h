#pragma once

// qmake's release feature can add NDEBUG after evaluating the project file.
// Tests perform setup inside assert(), so it must be active in every build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
