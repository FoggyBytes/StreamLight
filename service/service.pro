# StreamLight's Windows service (8.4.0): keeps an LG webOS TV in step with this PC — on when it
# starts, off when it shuts down — from boot on, before anyone has signed in.
#
# A separate executable on purpose: it stays up all the time, so it must not load Qt Gui, SDL or
# FFmpeg, and it must not hold StreamLight.exe open while an update replaces it. QtCore only;
# WinHTTP for the websocket (Qt WebSockets is not in the kit), Winsock and IP Helper for
# discovery and Wake-on-LAN.
QT = core
TARGET = StreamLightService
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle

include(../globaldefs.pri)

win32 {
    # Same version, product and company as StreamLight.exe: SignPath requires them to match.
    VERSION = $$cat($$PWD/../app/version.txt)
    QMAKE_TARGET_COMPANY = FoggyBytes
    QMAKE_TARGET_PRODUCT = StreamLight
    QMAKE_TARGET_DESCRIPTION = StreamLight service

    DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    LIBS += -lwinhttp -lws2_32 -liphlpapi -ladvapi32 -luser32 -lwevtapi -lcrypt32
}

SOURCES += \
    controlpipe.cpp \
    eventwatch.cpp \
    main.cpp \
    network.cpp \
    powerpolicy.cpp \
    servicecore.cpp \
    servicehost.cpp \
    servicelog.cpp \
    tvcontroller.cpp \
    tvstore.cpp \
    webostv.cpp \
    websocket.cpp

HEADERS += \
    controlpipe.h \
    eventwatch.h \
    network.h \
    powerpolicy.h \
    servicecore.h \
    servicehost.h \
    servicelog.h \
    tvcontroller.h \
    tvstore.h \
    webostv.h \
    websocket.h
