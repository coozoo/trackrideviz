#include "mainwindow.h"

#include <QApplication>
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

    return a.exec();
}
