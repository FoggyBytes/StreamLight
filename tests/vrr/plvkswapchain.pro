TEMPLATE = app
TARGET = tst_plvkswapchain
CONFIG += console c++17 link_pkgconfig
CONFIG -= qt app_bundle
PKGCONFIG += libplacebo
SOURCES += $$PWD/tst_plvkswapchain.cpp
HEADERS += $$PWD/../../app/streaming/video/ffmpeg-renderers/plvkswapchain.h
