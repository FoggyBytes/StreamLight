# StreamLight's updater (8.6.0): installs a release started from Settings → About without the
# Windows permission prompt, which a controller cannot answer. Its design, and every check it
# makes, is in SECURITY.md ("Updating without the Windows permission prompt").
#
# Started by the scheduled task "StreamLight Update", which the installer registers with the
# signed-in user's highest rights, or — when there is no usable task — by StreamLight through
# the ordinary "runas" prompt. Either way it runs elevated, and so it trusts nothing StreamLight
# wrote: it reads the release from GitHub itself and checks a copy of the installer against
# GitHub's digest.
#
# A separate executable on purpose, like the TV service: Setup replaces StreamLight.exe, and this
# process must outlive it. It is also why it first copies itself (and Qt6Core.dll) into a folder
# only administrators can write, and runs from there: a program running out of {app} would hold
# its own files open while Setup tries to replace them.
#
# QtCore only, without a QCoreApplication (no plugins are ever loaded): JSON, hashes, files.
# WinHTTP for the one request to GitHub, GDI+ for the screen it shows while Setup runs.
QT = core
TARGET = StreamLightUpdater
TEMPLATE = app
CONFIG += c++17 windows
CONFIG -= app_bundle console entrypoint

include(../globaldefs.pri)

win32 {
    # Same version, product and company as StreamLight.exe: SignPath requires them to match.
    VERSION = $$cat($$PWD/../app/version.txt)
    QMAKE_TARGET_COMPANY = FoggyBytes
    QMAKE_TARGET_PRODUCT = StreamLight
    QMAKE_TARGET_DESCRIPTION = StreamLight updater

    DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    LIBS += -lwinhttp -lcrypt32 -ladvapi32 -luser32 -lgdi32 -lgdiplus -lshell32 -lshlwapi \
            -lole32 -loleaut32 -ltaskschd -lversion -luuid
}

SOURCES += \
    main.cpp \
    common.cpp \
    release.cpp \
    screen.cpp \
    task.cpp \
    userlaunch.cpp \
    apply.cpp

HEADERS += \
    common.h \
    release.h \
    screen.h \
    task.h \
    userlaunch.h \
    apply.h

RESOURCES += updater.qrc
