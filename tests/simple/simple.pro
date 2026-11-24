# The simplified interface's controller without its QML: a video and data opened, a template
# chosen, an export made. Needs ffmpeg; run with QT_QPA_PLATFORM=offscreen.

QT += testlib concurrent

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_simple

include(../rtvcore.pri)
INCLUDEPATH += $$PWD/../../src

SOURCES += \
    test_simple.cpp \
    ../../src/simple/simplecontroller.cpp

HEADERS += \
    ../../src/simple/simplecontroller.h
