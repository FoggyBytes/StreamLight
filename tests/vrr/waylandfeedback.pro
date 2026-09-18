TEMPLATE = app
TARGET = tst_waylandfeedback
QT -= gui
CONFIG += console c++17 link_pkgconfig
PKGCONFIG += sdl2 wayland-client wayland-server
INCLUDEPATH += $$PWD/../../app $$PWD/../../moonlight-common-c/moonlight-common-c/src
SOURCES += $$PWD/tst_waylandfeedback.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/waylandfeedback/wayland.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/protocols/presentation-time-protocol.c
LIBS += -pthread
