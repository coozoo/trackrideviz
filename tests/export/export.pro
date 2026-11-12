# Unit tests: export plan, encoder options, media probe, and a real export when ffmpeg is installed.
# Core only: no widgets. Needs a QGuiApplication (fonts), run with
# QT_QPA_PLATFORM=offscreen.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_export

include(../../src/core/core.pri)

SOURCES += \
    test_export.cpp
