# Unit tests for Channel (time -> value sampling).
# Core only: no QApplication, no widgets.

QT += testlib
QT -= gui

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_channel

INCLUDEPATH += ../../src/core

SOURCES += \
    test_channel.cpp \
    ../../src/core/telemetry/channel.cpp

HEADERS += \
    ../../src/core/telemetry/channel.h
