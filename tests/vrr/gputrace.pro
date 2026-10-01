TEMPLATE = app
TARGET = tst_gputrace
QT += core testlib
QT -= gui
CONFIG += console testcase c++17 link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += sdl2
INCLUDEPATH += $$PWD/../../app
SOURCES += $$PWD/tst_gputrace.cpp \
    $$PWD/../../app/diagnostics/gputrace.cpp \
    $$PWD/../../app/diagnostics/diagnosticzip.cpp
