# Unit tests for the GPX importer.
# Core only: no widgets.

QT += testlib
# the core contains QtGui code (gauge rendering), so gui stays linked

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_gpx

include(../../src/core/core.pri)

SOURCES += \
    test_gpx.cpp
