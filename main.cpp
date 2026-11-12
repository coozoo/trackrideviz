#include "mainwindow.h"
#include "telemetry/telemetryimporter.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>
#include <QtDebug>

const QString APP_VERSION = "0.01";

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // Qt Multimedia tries the hardware decoders in its own fixed order and
    // gives up on hardware altogether when the first one that opens cannot
    // take the stream - which is what happens on Intel/AMD machines that
    // merely have the NVIDIA libraries installed. VA-API first covers Intel,
    // AMD and, through its VA-API driver, NVIDIA. The user's own setting wins.
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
        {
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "vaapi,vdpau,cuda");
        }
#endif
    QApplication a(argc, argv);
    a.setApplicationName("trackrideviz");
    a.setApplicationDisplayName("Race Track Visualizer");
    a.setApplicationVersion(APP_VERSION);
    a.setOrganizationName("trackrideviz");

    // .qm files are embedded by CONFIG += embed_translations under :/i18n
    QTranslator translator;
    if (translator.load(QLocale(), "trackrideviz", "_", ":/i18n"))
        {
            a.installTranslator(&translator);
            qInfo() << "Translation loaded:" << translator.filePath();
        }
    else
        {
            qInfo() << "No translation for" << QLocale().name() << "- using built-in English";
        }

    QString platform = "";
#if __GNUC__
#if __x86_64__
    platform = "-64bit";
#endif
#endif
    a.setProperty("appversion", APP_VERSION + platform + " (QTbuild:" + QString(QT_VERSION_STR) + ")");
    a.setProperty("appname", "Race Track Visualizer");

#ifdef Q_OS_LINUX
    a.setWindowIcon(QIcon(":/images/trackrideviz.png"));
#endif

    MainWindow w;
    w.show();

    // Files given on the command line: data files by their suffix, anything else is the video.
    const QStringList files = a.arguments().mid(1);
    for (const QString &file : files)
        {
            if (TelemetryImporter::canLoad(file))
                {
                    w.openTelemetry(file);
                }
            else
                {
                    w.openVideo(file);
                }
        }

    return a.exec();
}
