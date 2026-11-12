# Unit tests for templates: placement, file format, import of .vet files, library,
# the gauges and templates that ship with the program.
# Core only: no widgets. Needs a QGuiApplication (fonts), run with
# QT_QPA_PLATFORM=offscreen.

QT += testlib

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_template

include(../../src/core/core.pri)

# the gauges and templates that ship with the program
DEFINES += RTV_TEST_DATA=\\\"$$clean_path($$PWD/../../data)\\\"

SOURCES += \
    test_template.cpp
