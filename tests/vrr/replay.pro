TEMPLATE = app
TARGET = vrrreplay

QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle

SOURCES += \
    $$PWD/vrrreplay.cpp \
    $$PWD/vrrreplayconfig.cpp \
    $$PWD/vrrreplaymodel.cpp \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtimingcontroller.cpp

HEADERS += \
    $$PWD/vrrreplayconfig.h \
    $$PWD/vrrreplaymodel.h

win32 {
    contains(QT_ARCH, x86_64) {
        INCLUDEPATH += $$PWD/../../libs/windows/include/x64
        LIBS += -L$$PWD/../../libs/windows/lib/x64 -lavutil
    }
    contains(QT_ARCH, arm64) {
        INCLUDEPATH += $$PWD/../../libs/windows/include/arm64
        LIBS += -L$$PWD/../../libs/windows/lib/arm64 -lavutil
    }
}

macx {
    !disable-prebuilts {
        INCLUDEPATH += $$PWD/../../libs/mac/include
        LIBS += -L$$PWD/../../libs/mac/lib -lavutil.60
    } else {
        CONFIG += link_pkgconfig
        PKGCONFIG += libavutil
    }
}

unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += libavutil
}

# Track the header-only feedback models in incremental builds.
HEADERS += \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/presentationtiming.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/prediction.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/recentreadiness.h \
    $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/smoothnessfeedback.h
