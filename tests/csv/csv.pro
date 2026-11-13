# Unit tests for CSV files read through data profiles.
# Core only: no widgets.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_csv

include(../../src/core/core.pri)

SOURCES += \
    test_csv.cpp
