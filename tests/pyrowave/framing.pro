TEMPLATE = app
TARGET = tst_pyrowaveframing
QT -= gui core
CONFIG += console c++17
CONFIG -= app_bundle
SOURCES += \
    $$PWD/tst_pyrowaveframing.cpp \
    $$PWD/../../app/streaming/video/pyrowave/pyrowaveframing.cpp
HEADERS += $$PWD/../../app/streaming/video/pyrowave/pyrowaveframing.h
