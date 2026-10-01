TEMPLATE = app
TARGET = tst_pyrowaved3d11
QT -= gui core
CONFIG += console c++17 warn_off
CONFIG -= app_bundle

include($$PWD/../../pyrowave/pyrowave.pri)

DEFINES += SDL_MAIN_HANDLED
INCLUDEPATH += \
    $$PWD/../../app \
    $$PWD/../../libs/windows/include/x64 \
    $$PWD/../../libs/windows/include/x64/SDL2 \
    $$PWD/../../libs/windows/include \
    $$PWD/../../moonlight-common-c/moonlight-common-c/src

SOURCES += \
    $$PWD/tst_pyrowaved3d11.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/d3d11pyrowave.cpp \
    $$PWD/../../app/streaming/video/pyrowave/pyrowavedecoder.cpp \
    $$PWD/../../app/streaming/video/pyrowave/pyrowaveframing.cpp

LIBS += -L$$PWD/../../libs/windows/lib/x64 -lSDL2 -lavutil -ld3d11 -ldxgi -luser32
