# Unit tests for the gauge renderer.
# Core only: no widgets. Needs a QGuiApplication (fonts), run with
# QT_QPA_PLATFORM=offscreen.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_gauge

include(../rtvcore.pri)

SOURCES += \
    test_gauge.cpp
