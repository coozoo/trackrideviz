# Unit tests for Channel (time -> value sampling).
# Core only: no widgets.

QT += testlib
# the core contains QtGui code (gauge rendering), so gui stays linked

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_channel

include(../rtvcore.pri)

SOURCES += \
    test_channel.cpp
