# rtvcore - everything that is not a widget.
# Included by trackrideviz.pro and by every test .pro.
# Rule: QtCore + QtGui only here, so widgets and QML front ends can both use it.

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/telemetry/channel.cpp

HEADERS += \
    $$PWD/telemetry/channel.h
