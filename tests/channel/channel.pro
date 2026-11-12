# Unit tests for Channel (time -> value sampling).
# Core only: no QApplication, no widgets.

QT += testlib
QT -= gui

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_channel

include(../../src/core/core.pri)

SOURCES += \
    test_channel.cpp
