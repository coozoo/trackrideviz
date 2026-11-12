# Draws the gauges and writes the templates that ship with the program into
# data/ of the source tree. Not part of the build: run it after changing the
# artwork here, look at the result in the program, commit data/.
#
#   qmake6 tools/makebundled && make && QT_QPA_PLATFORM=offscreen ./makebundled [data folder]

QT += core gui
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = makebundled

include(../../src/core/core.pri)

DEFINES += RTV_DATA_FOLDER=\\\"$$clean_path($$PWD/../../data)\\\"

SOURCES += main.cpp
