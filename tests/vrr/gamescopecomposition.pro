TEMPLATE = app
TARGET = tst_gamescopecomposition
QT -= gui
CONFIG += console c++17
SOURCES += $$PWD/tst_gamescopecomposition.cpp \
           $$PWD/../../app/streaming/gamescopecomposition.cpp
HEADERS += $$PWD/../../app/streaming/gamescopecomposition.h
