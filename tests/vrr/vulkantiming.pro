TEMPLATE = app
TARGET = tst_vulkantiming
QT -= gui
CONFIG += console c++17 link_pkgconfig
PKGCONFIG += vulkan
INCLUDEPATH += $$PWD/../../moonlight-common-c/moonlight-common-c/src
SOURCES += $$PWD/tst_vulkantiming.cpp $$PWD/../../app/streaming/video/ffmpeg-renderers/vulkantiming.cpp
