#-------------------------------------------------
#
# Race Track Visualizer
#
#-------------------------------------------------

QT       += core gui multimedia concurrent openglwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

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
    src/appearance.cpp \
    src/exportdialog.cpp \
    src/gaugelibrarypanel.cpp \
    src/lapspanel.cpp \
    src/mainwindow.cpp \
    src/settingsdialog.cpp \
    src/videocanvas.cpp

HEADERS += \
    src/appearance.h \
    src/exportdialog.h \
    src/gaugelibrarypanel.h \
    src/lapspanel.h \
    src/mainwindow.h \
    src/settingsdialog.h \
    src/videocanvas.h

win32:RC_FILE = trackrideviz.rc
macx:ICON = trackrideviz.icns

RESOURCES += \
    resources.qrc

binary.files += $$TARGET
binary.path = /usr/bin
icon.files += images/trackrideviz.png
icon.path += /usr/share/icons/hicolor/256x256/apps
desktop.files += $${TARGET}.desktop
desktop.path += /usr/share/applications/
# Gauges and templates that come with the program (see BundledData).
bundled.files += data/gauges data/templates
bundled.path = /usr/share/$${TARGET}
INSTALLS += binary icon desktop bundled
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
        QT_QPA_PLATFORM=offscreen ./template/tst_template

    QMAKE_EXTRA_TARGETS += check
}
