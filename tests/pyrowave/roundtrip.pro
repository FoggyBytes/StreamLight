TEMPLATE = app
TARGET = tst_pyrowaveroundtrip
QT -= gui core
CONFIG += console c++17 warn_off
CONFIG -= app_bundle

include($$PWD/../../pyrowave/pyrowave.pri)

SOURCES += \
    $$PWD/tst_pyrowaveroundtrip.cpp \
    $$PWD/tst_pyrowavedequant.cpp \
    $$PWD/../../app/streaming/video/pyrowave/pyrowaveframing.cpp
HEADERS += $$PWD/../../app/streaming/video/pyrowave/pyrowaveframing.h

win32: LIBS += -luser32
linux {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2 libavutil libavcodec libplacebo
    INCLUDEPATH += $$PWD/../../moonlight-common-c/moonlight-common-c/src
    SOURCES += $$PWD/../../app/streaming/video/pyrowave/pyrowavedecoder.cpp \
               $$PWD/../../app/streaming/video/pyrowave/pyrowaveplacebo.cpp \
               $$PWD/../../app/streaming/video/ffmpeg-renderers/plvk_c.c
    HEADERS += $$PWD/../../app/streaming/video/pyrowave/pyrowavedecoder.h \
               $$PWD/../../app/streaming/video/pyrowave/pyrowaveplacebo.h
}
