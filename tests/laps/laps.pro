# Unit tests for the lap engine.
# Core only: no widgets.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_laps

include(../rtvcore.pri)

SOURCES += \
    test_laps.cpp
