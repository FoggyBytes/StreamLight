TEMPLATE = app
TARGET = tst_gamescoperepaint
QT -= gui
CONFIG += console c++17 link_pkgconfig
PKGCONFIG += wayland-client wayland-server
INCLUDEPATH += $$PWD/../../app
SOURCES += $$PWD/tst_gamescoperepaint.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/gamescoperepaint.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/protocols/gamescope-private-protocol.c
LIBS += -pthread
# Assertions exercise protocol/lifecycle invariants even in release builds.
QMAKE_CXXFLAGS_RELEASE -= -DNDEBUG
