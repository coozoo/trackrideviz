# rtvcore - everything that is not a widget.
# Included by trackrideviz.pro and by every test .pro.
# Rule: QtCore + QtGui only here, so widgets and QML front ends can both use it.

INCLUDEPATH += $$PWD

# A debug build run from its build folder finds the gauges and templates of
# the source tree (see BundledData). Release builds look only where packages
# put them.
CONFIG(debug, debug|release):DEFINES += RTV_SOURCE_DATA=\\\"$$clean_path($$PWD/../../data)\\\"

SOURCES += \
    $$PWD/gauge/builtingauges.cpp \
    $$PWD/gauge/channelicons.cpp \
    $$PWD/gauge/gaugefile.cpp \
    $$PWD/gauge/gaugelibrary.cpp \
    $$PWD/gauge/gaugerenderer.cpp \
    $$PWD/gauge/metricrange.cpp \
    $$PWD/gauge/veggaugeimporter.cpp \
    $$PWD/gauge/xmlgaugeimporter.cpp \
    $$PWD/overlay/gaugetemplate.cpp \
    $$PWD/overlay/overlay.cpp \
    $$PWD/overlay/templatelibrary.cpp \
    $$PWD/overlay/xmltemplateimporter.cpp \
    $$PWD/project/project.cpp \
    $$PWD/settings/appsettings.cpp \
    $$PWD/settings/bundleddata.cpp \
    $$PWD/sync/videosync.cpp \
    $$PWD/telemetry/channel.cpp \
    $$PWD/telemetry/demosession.cpp \
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
    $$PWD/gauge/channelicons.h \
    $$PWD/gauge/gauge.h \
    $$PWD/gauge/gaugefile.h \
    $$PWD/gauge/gaugelibrary.h \
    $$PWD/gauge/gaugerenderer.h \
    $$PWD/gauge/gaugetheme.h \
    $$PWD/gauge/metricrange.h \
    $$PWD/gauge/veggaugeimporter.h \
    $$PWD/gauge/xmlgaugeimporter.h \
    $$PWD/overlay/gaugetemplate.h \
    $$PWD/overlay/overlay.h \
    $$PWD/overlay/templatelibrary.h \
    $$PWD/overlay/xmltemplateimporter.h \
    $$PWD/project/project.h \
    $$PWD/settings/appsettings.h \
    $$PWD/settings/bundleddata.h \
    $$PWD/sync/videosync.h \
    $$PWD/telemetry/channel.h \
    $$PWD/telemetry/channelids.h \
    $$PWD/telemetry/demosession.h \
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
