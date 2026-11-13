# Unit tests for channels worked out from others.
# Core only: no widgets.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_derived

include(../../src/core/core.pri)

SOURCES += \
    test_derived.cpp
