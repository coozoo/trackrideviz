# rtvcore - everything that is not a widget.
# Included by trackrideviz.pro and by every test .pro.
# Rule: QtCore + QtGui only here, so widgets and QML front ends can both use it.

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/gauge/builtingauges.cpp \
    $$PWD/gauge/gaugerenderer.cpp \
    $$PWD/sync/videosync.cpp \
    $$PWD/telemetry/channel.cpp \
    $$PWD/telemetry/fit/fitdecoder.cpp \
    $$PWD/telemetry/fit/fitimporter.cpp \
    $$PWD/telemetry/gpximporter.cpp \
    $$PWD/telemetry/telemetryimporter.cpp \
    $$PWD/telemetry/telemetrysession.cpp

HEADERS += \
    $$PWD/gauge/builtingauges.h \
    $$PWD/gauge/gauge.h \
    $$PWD/gauge/gaugerenderer.h \
    $$PWD/sync/videosync.h \
    $$PWD/telemetry/channel.h \
    $$PWD/telemetry/channelids.h \
    $$PWD/telemetry/fit/fitdecoder.h \
    $$PWD/telemetry/fit/fitimporter.h \
    $$PWD/telemetry/geo.h \
    $$PWD/telemetry/gpximporter.h \
    $$PWD/telemetry/telemetryimporter.h \
    $$PWD/telemetry/telemetrysession.h
