TEMPLATE = app
TARGET = tst_plvkpresentation
CONFIG += console c++17 link_pkgconfig
CONFIG -= qt app_bundle
PKGCONFIG += libplacebo
SOURCES += $$PWD/tst_plvkpresentation.cpp
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/plvkpresentation.h
