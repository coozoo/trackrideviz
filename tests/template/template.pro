# Unit tests for templates: placement, file format, the old editor import, library.
# Core only: no widgets. Needs a QGuiApplication (fonts), run with
# QT_QPA_PLATFORM=offscreen.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_template

include(../../src/core/core.pri)

SOURCES += \
    test_template.cpp
