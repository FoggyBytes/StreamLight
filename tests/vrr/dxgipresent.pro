TEMPLATE = app
TARGET = tst_dxgipresent
CONFIG += console c++17
CONFIG -= qt app_bundle
SOURCES += $$PWD/tst_dxgipresent.cpp
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/dxgipresent.h
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/d3d11fencewait.h
