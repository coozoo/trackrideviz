# Pictures for comparing gauges side by side: a gauge from elsewhere and the
# drawn one made to replace it, fed the same sample data, on a local web
# page. Not part of the build. The pictures show other people's artwork:
# keep the output out of the repository.
#
#   qmake6 tools/gaugereview && make
#   QT_QPA_PLATFORM=offscreen ./gaugereview --sheet <gauge folders> <out folder>
#   QT_QPA_PLATFORM=offscreen ./gaugereview --pairs <mapping.json> <their gauges> <our gauges> <out folder>

QT += core gui
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = gaugereview

include(../../src/core/core.pri)

SOURCES += main.cpp
