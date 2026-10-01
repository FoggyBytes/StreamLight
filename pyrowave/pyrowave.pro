# PyroWave codec (vendored upstream C API plus the Granite subset it links).
# The source list mirrors what upstream's CMake links into libpyrowave-shared:
# pyrowave, granite-vulkan, granite-util, granite-math and volk, plus the C API
# and the video scaler. Refresh the vendored tree with vendor-pyrowave.ps1.

QT -= core gui

TARGET = pyrowave
TEMPLATE = lib

# Build a static library
CONFIG += staticlib c++17

# Third-party code: keep its warnings out of our build logs
CONFIG += warn_off

# Include global qmake defs
include(../globaldefs.pri)

include(pyrowave.pri)
