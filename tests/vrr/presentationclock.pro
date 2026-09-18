TEMPLATE = app
TARGET = tst_presentationclock
CONFIG += console c++17
CONFIG -= qt app_bundle
SOURCES += $$PWD/tst_presentationclock.cpp
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/presentationclock.h
