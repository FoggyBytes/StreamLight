QT       -= core gui

TARGET = AntiHooking
TEMPLATE = lib

include(../globaldefs.pri)

INCLUDEPATH += $$PWD/../libs/windows/include
contains(QT_ARCH, i386) {
    LIBS += -L$$PWD/../libs/windows/lib/x86
}
contains(QT_ARCH, x86_64) {
    LIBS += -L$$PWD/../libs/windows/lib/x64
}
contains(QT_ARCH, arm64) {
    LIBS += -L$$PWD/../libs/windows/lib/arm64
}

LIBS += -ldetours
DEFINES += ANTIHOOKING_LIBRARY

# StreamLight: a version resource, so the DLL carries the same product name and version as
# StreamLight.exe. SignPath requires them to match on every file it signs. Read from the same
# app/version.txt as app.pro. skip_target_version_ext keeps the name AntiHooking.dll: with a
# VERSION, qmake would otherwise append the major version to a Windows DLL (AntiHooking7.dll).
win32 {
    VERSION = $$cat($$PWD/../app/version.txt)
    CONFIG += skip_target_version_ext
    QMAKE_TARGET_COMPANY = FoggyBytes
    QMAKE_TARGET_PRODUCT = StreamLight
    QMAKE_TARGET_DESCRIPTION = StreamLight anti-hooking protection
}
SOURCES += antihookingprotection.cpp
HEADERS += antihookingprotection.h
