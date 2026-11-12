# Unit tests for the GPX importer.
# Core only: no QApplication, no widgets.

QT += testlib
QT -= gui

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_gpx

include(../../src/core/core.pri)

SOURCES += \
    test_gpx.cpp
