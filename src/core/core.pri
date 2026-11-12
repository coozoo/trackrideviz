# rtvcore - everything that is not a widget.
# Included by trackrideviz.pro and by every test .pro.
# Rule: QtCore + QtGui only here, so widgets and QML front ends can both use it.

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/gauge/builtingauges.cpp \
    $$PWD/gauge/gaugerenderer.cpp \
    $$PWD/overlay/overlay.cpp \
    $$PWD/project/project.cpp \
    $$PWD/settings/appsettings.cpp \
    $$PWD/sync/videosync.cpp \
    $$PWD/telemetry/channel.cpp \
    $$PWD/telemetry/fit/fitdecoder.cpp \
    $$PWD/telemetry/fit/fitimporter.cpp \
    $$PWD/telemetry/gpximporter.cpp \
    $$PWD/telemetry/telemetryimporter.cpp \
    $$PWD/telemetry/telemetrysession.cpp \
    $$PWD/units/units.cpp \
    $$PWD/video/exportjob.cpp \
    $$PWD/video/exportplan.cpp \
    $$PWD/video/ffmpegtools.cpp \
    $$PWD/video/mediainfo.cpp \
    $$PWD/video/videoencoders.cpp

HEADERS += \
    $$PWD/gauge/builtingauges.h \
    $$PWD/gauge/gauge.h \
    $$PWD/gauge/gaugerenderer.h \
    $$PWD/overlay/overlay.h \
    $$PWD/project/project.h \
    $$PWD/settings/appsettings.h \
    $$PWD/sync/videosync.h \
    $$PWD/telemetry/channel.h \
    $$PWD/telemetry/channelids.h \
    $$PWD/telemetry/fit/fitdecoder.h \
    $$PWD/telemetry/fit/fitimporter.h \
    $$PWD/telemetry/geo.h \
    $$PWD/telemetry/gpximporter.h \
    $$PWD/telemetry/telemetryimporter.h \
    $$PWD/telemetry/telemetrysession.h \
    $$PWD/units/units.h \
    $$PWD/video/exportjob.h \
    $$PWD/video/exportplan.h \
    $$PWD/video/exportsettings.h \
    $$PWD/video/ffmpegtools.h \
    $$PWD/video/mediainfo.h \
    $$PWD/video/videoencoders.h
