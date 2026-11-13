# rtvcore - everything that is not a widget.
# Included by trackrideviz.pro and by every test .pro.
# Rule: QtCore + QtGui (and QtSvg, which needs no more) only here, so widgets
# and QML front ends can both use it.

QT += svg
INCLUDEPATH += $$PWD

# A debug build run from its build folder finds the gauges and templates of
# the source tree (see BundledData). Release builds look only where packages
# put them.
CONFIG(debug, debug|release):DEFINES += RTV_SOURCE_DATA=\\\"$$clean_path($$PWD/../../data)\\\"

SOURCES += \
    $$PWD/gauge/builtingauges.cpp \
    $$PWD/gauge/channelicons.cpp \
    $$PWD/gauge/gaugefile.cpp \
    $$PWD/gauge/gaugefonts.cpp \
    $$PWD/gauge/gaugetheme.cpp \
    $$PWD/gauge/gaugelibrary.cpp \
    $$PWD/gauge/gaugerenderer.cpp \
    $$PWD/gauge/metricrange.cpp \
    $$PWD/gauge/veggaugeimporter.cpp \
    $$PWD/gauge/xmlgaugeimporter.cpp \
    $$PWD/laps/lapengine.cpp \
    $$PWD/overlay/gaugetemplate.cpp \
    $$PWD/overlay/librarypackage.cpp \
    $$PWD/overlay/overlay.cpp \
    $$PWD/overlay/templatelibrary.cpp \
    $$PWD/overlay/xmltemplateimporter.cpp \
    $$PWD/project/project.cpp \
    $$PWD/settings/appsettings.cpp \
    $$PWD/settings/bundleddata.cpp \
    $$PWD/sync/syncmap.cpp \
    $$PWD/sync/videosync.cpp \
    $$PWD/telemetry/channel.cpp \
    $$PWD/telemetry/csv/csvimporter.cpp \
    $$PWD/telemetry/csv/dataprofile.cpp \
    $$PWD/telemetry/csv/dataprofilelibrary.cpp \
    $$PWD/telemetry/csv/xmldataprofileimporter.cpp \
    $$PWD/telemetry/datatypes.cpp \
    $$PWD/telemetry/dataedits.cpp \
    $$PWD/telemetry/derivedchannels.cpp \
    $$PWD/telemetry/smoothing.cpp \
    $$PWD/telemetry/demosession.cpp \
    $$PWD/telemetry/fit/fitdecoder.cpp \
    $$PWD/telemetry/fit/fitimporter.cpp \
    $$PWD/telemetry/gpximporter.cpp \
    $$PWD/telemetry/telemetryimporter.cpp \
    $$PWD/telemetry/telemetrysession.cpp \
    $$PWD/units/units.cpp \
    $$PWD/util/gzip.cpp \
    $$PWD/video/cutlist.cpp \
    $$PWD/video/exportjob.cpp \
    $$PWD/video/exportplan.cpp \
    $$PWD/video/ffmpegtools.cpp \
    $$PWD/video/mediainfo.cpp \
    $$PWD/video/videoencoders.cpp \
    $$PWD/video/videojoin.cpp

HEADERS += \
    $$PWD/gauge/builtingauges.h \
    $$PWD/gauge/channelicons.h \
    $$PWD/gauge/gauge.h \
    $$PWD/gauge/gaugefile.h \
    $$PWD/gauge/gaugefonts.h \
    $$PWD/gauge/gaugelibrary.h \
    $$PWD/gauge/gaugerenderer.h \
    $$PWD/gauge/gaugetheme.h \
    $$PWD/gauge/metricrange.h \
    $$PWD/gauge/veggaugeimporter.h \
    $$PWD/gauge/xmlgaugeimporter.h \
    $$PWD/laps/lapengine.h \
    $$PWD/overlay/gaugetemplate.h \
    $$PWD/overlay/librarypackage.h \
    $$PWD/overlay/overlay.h \
    $$PWD/overlay/templatelibrary.h \
    $$PWD/overlay/xmltemplateimporter.h \
    $$PWD/project/project.h \
    $$PWD/settings/appsettings.h \
    $$PWD/settings/bundleddata.h \
    $$PWD/sync/syncmap.h \
    $$PWD/sync/videosync.h \
    $$PWD/telemetry/channel.h \
    $$PWD/telemetry/channelids.h \
    $$PWD/telemetry/csv/csvimporter.h \
    $$PWD/telemetry/csv/dataprofile.h \
    $$PWD/telemetry/csv/dataprofilelibrary.h \
    $$PWD/telemetry/csv/xmldataprofileimporter.h \
    $$PWD/telemetry/datatypes.h \
    $$PWD/telemetry/dataedits.h \
    $$PWD/telemetry/derivedchannels.h \
    $$PWD/telemetry/smoothing.h \
    $$PWD/telemetry/demosession.h \
    $$PWD/telemetry/fit/fitdecoder.h \
    $$PWD/telemetry/fit/fitimporter.h \
    $$PWD/telemetry/geo.h \
    $$PWD/telemetry/gpximporter.h \
    $$PWD/telemetry/telemetryimporter.h \
    $$PWD/telemetry/telemetrysession.h \
    $$PWD/units/units.h \
    $$PWD/util/gzip.h \
    $$PWD/video/cutlist.h \
    $$PWD/video/exportjob.h \
    $$PWD/video/exportplan.h \
    $$PWD/video/exportsettings.h \
    $$PWD/video/ffmpegtools.h \
    $$PWD/video/mediainfo.h \
    $$PWD/video/videoencoders.h \
    $$PWD/video/videojoin.h

RESOURCES += $$PWD/gauge/fonts/fonts.qrc

# gzip for the library's own files (Gzip); zlib is there wherever Qt is
LIBS += -lz
