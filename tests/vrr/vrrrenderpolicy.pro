TEMPLATE = app
TARGET = tst_vrrrenderpolicy
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
SOURCES += $$PWD/tst_vrrrenderpolicy.cpp
HEADERS += $$PWD/../../app/streaming/video/vrrrenderpolicy.h
