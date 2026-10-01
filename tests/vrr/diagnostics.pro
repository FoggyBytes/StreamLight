TEMPLATE = app
TARGET = tst_vrrdiagnostics
QT += core testlib
QT -= gui
CONFIG += console testcase c++17
CONFIG -= app_bundle

SOURCES += \
    $$PWD/tst_vrrdiagnostics.cpp \
    $$PWD/../../app/diagnostics/diagnosticcapture.cpp \
    $$PWD/../../app/diagnostics/diagnosticzip.cpp
HEADERS += \
    $$PWD/../../app/diagnostics/diagnosticcapture.h \
    $$PWD/../../app/diagnostics/diagnosticzip.h
