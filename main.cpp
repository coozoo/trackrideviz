#include "applog.h"
#include "gauge/gaugefonts.h"
#include "settings/appsettings.h"
#include "simple/simpleui.h"
#include "video/libav.h"

// Android has the simplified interface alone: none of the widgets, the command line or GStreamer
#ifndef Q_OS_ANDROID
#include "appearance.h"
#include "commandline.h"
#include "diagnostics.h"
#include "gstreamerworkaround.h"
#include "mainwindow.h"

#include <QApplication>
#endif

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QtDebug>

#include <memory>

#ifdef Q_OS_ANDROID
#include <android/log.h>
#endif

const QString APP_VERSION = "0.05";

#ifdef Q_OS_ANDROID
namespace
{
// What Qt and the program say, to logcat under the program's name with the category at the end
// ("trackrideviz: gauges painted: ... (rtv.perf)"), and kept for Copy log in the menu.
void toLogcat(QtMsgType type, const QMessageLogContext &context, const QString &text)
{
    AppLog::add(QString("%1 (%2)").arg(text, QLatin1String(context.category ? context.category : "default")));
    const int priority = type == QtDebugMsg     ? ANDROID_LOG_DEBUG
                         : type == QtInfoMsg    ? ANDROID_LOG_INFO
                         : type == QtWarningMsg ? ANDROID_LOG_WARN
                         : type == QtFatalMsg   ? ANDROID_LOG_FATAL
                                                : ANDROID_LOG_ERROR;
    __android_log_print(priority, "trackrideviz", "%s (%s)", qUtf8Printable(text),
                        context.category ? context.category : "default");
}
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_ANDROID
    // the OpenSSL packed in the APK (libssl_3.so), not one Android may have of its own
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
    qInstallMessageHandler(toLogcat);
    const std::unique_ptr<QGuiApplication> application(new QGuiApplication(argc, argv));
#else
    // everything Qt says from the very start, for View > Diagnostics
    DiagnosticLog::install();
    // exporting from a terminal or a script: no window, nothing of the desktop needed
    const bool commandLine = CommandLine::wanted(argc, argv);
    if (commandLine)
        {
            CommandLine::attachConsole();
            if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
                {
                    qputenv("QT_QPA_PLATFORM", "offscreen");
                }
        }
    const std::unique_ptr<QGuiApplication> application(commandLine ? new QGuiApplication(argc, argv) : new QApplication(argc, argv));
#endif
    QGuiApplication &a = *application;
    a.setApplicationName("trackrideviz");
    a.setApplicationDisplayName("Race Track Visualizer");
    a.setApplicationVersion(APP_VERSION);
    a.setOrganizationName("trackrideviz");
    // the video engine's messages into the program's log, before anything opens the graphics card
    Libav::quiet();
#ifdef Q_OS_WIN
    // ffmpeg.exe and ffprobe.exe are in ffmpeg\ beside the program, with the Vulkan loader only
    // they need (their filters import it; Windows without a graphics driver has none). The rest
    // of their DLLs are the program's own, found through the PATH they inherit. The loader is
    // kept out of the program's folder: there Wine's graphics would load it too, and recurse.
    qputenv("PATH", QDir::toNativeSeparators(a.applicationDirPath()).toLocal8Bit() + ";" + qgetenv("PATH"));
#endif
    // before anything draws gauges in another thread (library pictures, export)
    GaugeFonts::load();

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
#ifndef Q_OS_ANDROID
    if (commandLine)
        {
            return CommandLine::run();
        }
#endif

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

#ifndef Q_OS_ANDROID
    // before the players are made
    GStreamerWorkaround::install();

    Appearance::apply();
    Appearance::wrapToolTips();
#endif

    // the simplified interface alone: asked for, or the only one there is (Android)
    if (SimpleUi::wanted(argc, argv))
        {
            QStringList files = a.arguments().mid(1);
            files.removeAll(QStringLiteral("--simple"));
            const std::unique_ptr<QQmlApplicationEngine> simple(SimpleUi::open(files));
            return a.exec();
        }

#ifdef Q_OS_ANDROID
    return 0;
#else
    MainWindow w;
    w.show();

    // Files given on the command line (a double click on one, too) are taken as if dropped on the
    // window: a project, a video, data, gauges and templates for the library.
    const QStringList files = a.arguments().mid(1);
    if (!files.isEmpty())
        {
            w.openDropped(files);
        }

    return a.exec();
#endif
}
