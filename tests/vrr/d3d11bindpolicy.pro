TEMPLATE = app
TARGET = tst_d3d11bindpolicy
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
SOURCES += $$PWD/tst_d3d11bindpolicy.cpp
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/d3d11bindpolicy.h
