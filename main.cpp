#include "appearance.h"
#include "mainwindow.h"
#include "project/project.h"
#include "settings/appsettings.h"
#include "telemetry/telemetryimporter.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QTranslator>
#include <QtDebug>

const QString APP_VERSION = "0.01";

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("trackrideviz");
    a.setApplicationDisplayName("Race Track Visualizer");
    a.setApplicationVersion(APP_VERSION);
    a.setOrganizationName("trackrideviz");

    // .qm files are embedded by CONFIG += embed_translations under :/i18n
    const QString language = AppSettings::language();
    if (language != "system")
        {
            // numbers and dates follow the chosen language too
            QLocale::setDefault(QLocale(language));
        }
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

    // The decoder order has to be in place before Qt Multimedia is first
    // used. A variable set by the user wins over the settings.
    const char decoderVariable[] = "QT_FFMPEG_DECODING_HW_DEVICE_TYPES";
    const bool decoderFromEnvironment = qEnvironmentVariableIsSet(decoderVariable);
    const QByteArray decoderTypes = AppSettings::decodingDeviceTypes();
    if (!decoderFromEnvironment && !decoderTypes.isNull())
        {
            qputenv(decoderVariable, decoderTypes);
        }
    a.setProperty("decoderFromEnvironment", decoderFromEnvironment);

    Appearance::apply();

    MainWindow w;
    w.show();

    // Files given on the command line: a project, or data files by their suffix; anything else is the video.
    const QStringList files = a.arguments().mid(1);
    for (const QString &file : files)
        {
            if (QFileInfo(file).suffix().compare(ProjectFile::Suffix, Qt::CaseInsensitive) == 0)
                {
                    w.openProject(file);
                }
            else if (TelemetryImporter::canLoad(file))
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
