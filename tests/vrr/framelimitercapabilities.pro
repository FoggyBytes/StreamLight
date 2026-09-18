TEMPLATE = app
TARGET = tst_framelimitercapabilities
QT += testlib
QT -= gui
CONFIG += console testcase c++17
CONFIG -= app_bundle
SOURCES += $$PWD/tst_framelimitercapabilities.cpp
HEADERS += $$PWD/../../app/backend/framelimitercapabilities.h
