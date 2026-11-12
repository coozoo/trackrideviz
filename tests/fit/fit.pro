# Unit tests for the FIT importer.
# Core only: no QApplication, no widgets.

QT += testlib
QT -= gui

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_fit

include(../../src/core/core.pri)

SOURCES += \
    test_fit.cpp
