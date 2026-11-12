# Unit tests: overlay placement and the project file.
# Core only: no widgets. Needs a QGuiApplication (fonts), run with
# QT_QPA_PLATFORM=offscreen.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_project

include(../../src/core/core.pri)

SOURCES += \
    test_project.cpp
