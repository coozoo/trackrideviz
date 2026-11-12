# Unit tests for VideoSync (video position on the telemetry timeline).
# Core only: no widgets.

QT += testlib
# the core contains QtGui code (gauge rendering), so gui stays linked

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_sync

include(../../src/core/core.pri)

SOURCES += \
    test_sync.cpp
