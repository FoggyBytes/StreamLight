TEMPLATE = app
TARGET = compositionprobe
CONFIG += console c++17
CONFIG -= qt app_bundle
SOURCES += $$PWD/compositionprobe.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/d3d11composition.cpp
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/d3d11composition.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/presentationclock.h
LIBS += -ld3d11 -ldxgi -ldcomp -luser32 -ladvapi32
