TEMPLATE = app
TARGET = tst_vrrprofile
QT = core
CONFIG += console c++17
CONFIG -= app_bundle
SOURCES += $$PWD/tst_vrrprofile.cpp $$PWD/../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profile.cpp
