TEMPLATE = app
TARGET = tst_overlay
QT = core
CONFIG += console c++17
CONFIG -= app_bundle
DEFINES += SDL_MAIN_HANDLED
INCLUDEPATH += $$PWD/../../app $$PWD/../../app/streaming/video
SOURCES += $$PWD/tst_overlay.cpp $$PWD/../../app/streaming/video/overlaymanager.cpp $$PWD/../../app/path.cpp
win32 {
    VRR_ARCH = $$QT_ARCH
    contains(QT_ARCH, x86_64): VRR_ARCH = x64
    INCLUDEPATH += $$PWD/../../libs/windows/include/$$VRR_ARCH $$PWD/../../libs/windows/include/$$VRR_ARCH/SDL2
    LIBS += -L$$PWD/../../libs/windows/lib/$$VRR_ARCH -lSDL2 -lSDL2_ttf shell32.lib
}
macx:!disable-prebuilts {
    INCLUDEPATH += $$PWD/../../libs/mac/include
    LIBS += -L$$PWD/../../libs/mac/lib -lSDL2 -lSDL2_ttf
} else:unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2 SDL2_ttf
}
