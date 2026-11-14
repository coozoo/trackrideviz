# Unit tests for the FIT importer.
# Core only: no widgets.

QT += testlib
# the core contains QtGui code (gauge rendering), so gui stays linked

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_fit

include(../rtvcore.pri)

SOURCES += \
    test_fit.cpp
