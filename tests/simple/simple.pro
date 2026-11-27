# The simplified interface's controller without its QML: a video and data opened, a template
# chosen, an export made; its drawn items painted on their own. Needs ffmpeg; run with
# QT_QPA_PLATFORM=offscreen.

QT += testlib concurrent quick

CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = tst_simple

include(../rtvcore.pri)
INCLUDEPATH += $$PWD/../../src
# the gauges and templates of the source tree, not whatever the machine has installed
DEFINES += RTV_TEST_DATA=\\\"$$clean_path($$PWD/../../data)\\\"

SOURCES += \
    test_simple.cpp \
    ../../src/simple/channelgraph.cpp \
    ../../src/simple/flyovernode.cpp \
    ../../src/simple/gaugeoverlay.cpp \
    ../../src/simple/routeview.cpp \
    ../../src/simple/simplecontroller.cpp

HEADERS += \
    ../../src/simple/channelgraph.h \
    ../../src/simple/flyovernode.h \
    ../../src/simple/gaugeoverlay.h \
    ../../src/simple/routeview.h \
    ../../src/simple/simplecontroller.h

RESOURCES += ../../src/simple/shaders/shaders.qrc
