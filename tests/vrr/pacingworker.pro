TEMPLATE = app
TARGET = tst_vrrpacingworker

QT += core qml
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
DEFINES += SDL_MAIN_HANDLED

SOURCES += \
    $$PWD/tst_vrrpacingworker.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrrpacingworker.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtimingcontroller.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtargetwaiter.cpp

INCLUDEPATH += \
    $$PWD/../../app \
    $$PWD/../../moonlight-common-c/moonlight-common-c/src

win32 {
    contains(QT_ARCH, x86_64) {
        INCLUDEPATH += $$PWD/../../libs/windows/include/x64 \
                       $$PWD/../../libs/windows/include/x64/SDL2
        LIBS += -L$$PWD/../../libs/windows/lib/x64 -lavutil -lSDL2
    }
    contains(QT_ARCH, arm64) {
        INCLUDEPATH += $$PWD/../../libs/windows/include/arm64 \
                       $$PWD/../../libs/windows/include/arm64/SDL2
        LIBS += -L$$PWD/../../libs/windows/lib/arm64 -lavutil -lSDL2
    }
}

macx {
    !disable-prebuilts {
        INCLUDEPATH += $$PWD/../../libs/mac/include
        LIBS += -L$$PWD/../../libs/mac/lib -lavutil.60 -lSDL2
    } else {
        CONFIG += link_pkgconfig
        PKGCONFIG += libavutil sdl2
    }
}

unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += libavutil sdl2
}

SOURCES += $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profile.cpp

# Track the header-only feedback models in incremental builds.
HEADERS += \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrframedroppolicy.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/presentationtiming.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/prediction.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/recentreadiness.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/smoothnessfeedback.h
