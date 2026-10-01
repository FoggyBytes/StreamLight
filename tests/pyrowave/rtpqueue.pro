TEMPLATE = app
TARGET = tst_rtpvideoqueue
CONFIG += console c11
CONFIG -= app_bundle
QT -= gui core

COMMON = $$clean_path($$PWD/../../moonlight-common-c/moonlight-common-c)
INCLUDEPATH += $$COMMON/src $$COMMON/nanors $$COMMON/nanors/deps $$COMMON/nanors/deps/obl $$COMMON/enet/include

SOURCES += tst_rtpvideoqueue.c \
           $$COMMON/src/RtpVideoQueue.c
