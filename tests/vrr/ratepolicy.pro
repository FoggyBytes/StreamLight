TEMPLATE = app
TARGET = tst_vrrratepolicy

QT += testlib
QT -= gui
CONFIG += console testcase c++17
CONFIG -= app_bundle

SOURCES += \
    $$PWD/tst_vrrratepolicy.cpp \
    $$PWD/../../app/streaming/vrrratepolicy.cpp
