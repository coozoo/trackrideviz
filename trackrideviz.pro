#-------------------------------------------------
#
# Race Track Visualizer
#
#-------------------------------------------------

QT       += core gui multimedia concurrent svg
# the simplified interface (src/simple, qml/simple)
QT       += qml quick quickcontrols2
# the full window; Android has the simplified interface alone
!android: QT += widgets openglwidgets

TARGET = trackrideviz
TRANSLATIONS = ./translations/$${TARGET}_en_US.ts ./translations/$${TARGET}_uk_UA.ts
TEMPLATE = app

CONFIG += c++17
CONFIG += lrelease
CONFIG += embed_translations
# do not show qDebug() messages in release builds
CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT

INCLUDEPATH += $$PWD/src

# UI-independent part (QtCore + QtGui only). Shared with tests and, later,
# with the QML front end - nothing in there may include QtWidgets.
include(src/core/core.pri)

SOURCES += main.cpp \
    src/simple/channelgraph.cpp \
    src/simple/gaugeoverlay.cpp \
    src/simple/routeview.cpp \
    src/simple/simplecontroller.cpp \
    src/simple/simpleui.cpp

HEADERS += \
    src/simple/channelgraph.h \
    src/simple/gaugeoverlay.h \
    src/simple/routeview.h \
    src/simple/simplecontroller.h \
    src/simple/simpleui.h

# the full window, the command line and the rest of the desktop
!android {
    SOURCES += \
        src/appearance.cpp \
        src/exportdialog.cpp \
        src/gaugeeditor.cpp \
        src/gaugelibrarypanel.cpp \
        src/datapanel.cpp \
        src/gpsproblemsdialog.cpp \
        src/joindialog.cpp \
        src/lapcomparepanel.cpp \
        src/lapspanel.cpp \
        src/backgrounddialog.cpp \
        src/musicpanel.cpp \
        src/playback.cpp \
        src/datasourcesdialog.cpp \
        src/dockhome.cpp \
        src/diagnostics.cpp \
        src/gstreamerworkaround.cpp \
        src/icons.cpp \
        src/timelinepanel.cpp \
        src/commandline.cpp \
        src/updater.cpp \
        src/mainwindow.cpp \
        src/settingsdialog.cpp \
        src/videocanvas.cpp

    HEADERS += \
        src/appearance.h \
        src/exportdialog.h \
        src/gaugeeditor.h \
        src/gaugelibrarypanel.h \
        src/chartsmoothing.h \
        src/datapanel.h \
        src/gpsproblemsdialog.h \
        src/joindialog.h \
        src/lapcomparepanel.h \
        src/lapspanel.h \
        src/backgrounddialog.h \
        src/musicpanel.h \
        src/playback.h \
        src/datasourcesdialog.h \
        src/dockhome.h \
        src/diagnostics.h \
        src/gstreamerworkaround.h \
        src/icons.h \
        src/trackview.h \
        src/timelinepanel.h \
        src/commandline.h \
        src/updater.h \
        src/mainwindow.h \
        src/settingsdialog.h \
        src/videocanvas.h
}

# Linux: to work around Qt 6.2's GStreamer playback (see gstreamerworkaround.h)
unix:!macx:!android:packagesExist(gstreamer-1.0) {
    CONFIG += link_pkgconfig
    PKGCONFIG += gstreamer-1.0
    DEFINES += HAVE_GSTREAMER
}

# A build for a release knows which it is (CI sets it), so it can tell when there is a newer one
RELEASE_TAG = $$(RTV_RELEASE_TAG)
!isEmpty(RELEASE_TAG): DEFINES += RTV_RELEASE_TAG=\\\"$$RELEASE_TAG\\\"

win32:RC_FILE = trackrideviz.rc
macx:ICON = trackrideviz.icns

# for lupdate: the texts of the QML
lupdate_only {
    SOURCES += qml/simple/*.qml
}

RESOURCES += \
    qml.qrc \
    resources.qrc

binary.files += $$TARGET
binary.path = /usr/bin
icon.files += images/trackrideviz.png
icon.path += /usr/share/icons/hicolor/256x256/apps
desktop.files += $${TARGET}.desktop
desktop.path += /usr/share/applications/
metainfo.files += io.github.coozoo.trackrideviz.metainfo.xml
metainfo.path = /usr/share/metainfo
# .rtvproj, .rtvgauge and .rtvtemplate (the desktop file says the program opens them)
mime.files += $${TARGET}-mime.xml
mime.path = /usr/share/mime/packages
# Gauges and templates that come with the program (see BundledData).
bundled.files += data/gauges data/templates
bundled.path = /usr/share/$${TARGET}
!android: INSTALLS += binary icon desktop metainfo mime bundled
# Android: an APK with the simplified interface, for 64-bit ARM, Android 9 and later. CI gives
# the version (RTV_ANDROID_VERSION, as in main.cpp) and a number that grows with each build
# (RTV_ANDROID_VERSION_CODE): Android installs an update only over a lower one.
android {
    ANDROID_ABIS = arm64-v8a
    ANDROID_MIN_SDK_VERSION = 28
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_VERSION_NAME = $$(RTV_ANDROID_VERSION)
    isEmpty(ANDROID_VERSION_NAME): ANDROID_VERSION_NAME = 0
    ANDROID_VERSION_CODE = $$(RTV_ANDROID_VERSION_CODE)
    isEmpty(ANDROID_VERSION_CODE): ANDROID_VERSION_CODE = 1
    # the gauges and templates that come with the program, in the APK's assets (see BundledData)
    androiddata.files = data/gauges data/templates
    androiddata.path = /assets/data
    INSTALLS += androiddata
}
macx {
    bundleddata.files = data/gauges data/templates
    bundleddata.path = Contents/Resources
    QMAKE_BUNDLE_DATA += bundleddata
}

# Tests are opt-in: pass `CONFIG+=tests` to qmake to add the `check`
# target. Release builds (build_all.yml) never set it.
#
# Tests build into a shadow subdir under the current Makefile dir
# ($$OUT_PWD). Source path comes from $$PWD.
tests {
    check.commands = \
        mkdir -p $$OUT_PWD/tests-shadow && \
        cd $$OUT_PWD/tests-shadow && \
        $(QMAKE) $$PWD/tests/tests.pro && \
        $(MAKE) && \
        QT_QPA_PLATFORM=offscreen ./channel/tst_channel && \
        QT_QPA_PLATFORM=offscreen ./gpx/tst_gpx && \
        QT_QPA_PLATFORM=offscreen ./fit/tst_fit && \
        QT_QPA_PLATFORM=offscreen ./sync/tst_sync && \
        QT_QPA_PLATFORM=offscreen ./gauge/tst_gauge && \
        QT_QPA_PLATFORM=offscreen ./settings/tst_settings && \
        QT_QPA_PLATFORM=offscreen ./project/tst_project && \
        QT_QPA_PLATFORM=offscreen ./export/tst_export && \
        QT_QPA_PLATFORM=offscreen ./template/tst_template && \
        QT_QPA_PLATFORM=offscreen ./csv/tst_csv && \
        QT_QPA_PLATFORM=offscreen ./derived/tst_derived && \
        QT_QPA_PLATFORM=offscreen ./laps/tst_laps && \
        QT_QPA_PLATFORM=offscreen ./simple/tst_simple

    QMAKE_EXTRA_TARGETS += check
}
