#include "simpleui.h"

#include "gaugeoverlay.h"
#include "simplecontroller.h"

#include "gauge/gaugelibrary.h"
#include "overlay/gaugetemplate.h"
#include "overlay/templatelibrary.h"
#include "telemetry/demosession.h"
#include "units/units.h"

#include <QFileInfo>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QUrl>

#include <cstring>
#include <mutex>

namespace
{
// gauges are made for video: shown on a dark ground, as the library does
const QColor kPreviewGround(0x30, 0x34, 0x38);

// "image://previews/template/<id>" and "image://previews/gauge/<id>" (ids percent-encoded):
// a template on a 16:9 picture, a gauge alone, drawn with the demo ride at its showcase moment.
class PreviewProvider : public QQuickImageProvider
{
public:
    PreviewProvider()
        : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
    {
    }

    QImage requestImage(const QString &request, QSize *size, const QSize &requestedSize) override
    {
        static std::once_flag made;
        static TelemetrySessionPtr demo;
        std::call_once(made, []() { demo = DemoSession::create(); });
        const int slash = int(request.indexOf('/'));
        const QString kind = request.left(slash);
        const QString id = QUrl::fromPercentEncoding(request.mid(slash + 1).toUtf8());
        const bool isTemplate = kind == QLatin1String("template");
        QSize wanted = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize
                       : (isTemplate ? QSize(320, 180) : QSize(192, 192));
        QImage image(wanted, QImage::Format_ARGB32_Premultiplied);
        image.fill(kPreviewGround);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (isTemplate)
            {
                const GaugeTemplate chosen = TemplateLibrary::get(id);
                const QRectF frame(QPointF(0, 0), QSizeF(wanted));
                const Overlay overlay(chosen.overlayItems(frame.width() / frame.height()), demo, Units(), chosen.theme);
                painter.setClipRect(frame);
                overlay.render(painter, frame, DemoSession::ShowcaseTime);
            }
        else
            {
                const Gauge gauge = GaugeLibrary::gauge(id);
                if (!gauge.isEmpty())
                    {
                        // shown with what it was made for
                        const BoundGauge bound(gauge, GaugeLibrary::defaultChannel(gauge, demo->channelIds()), demo, Units(),
                                               GaugeTheme());
                        const QSizeF fit = bound.size().scaled(wanted.width() - 8, wanted.height() - 8, Qt::KeepAspectRatio);
                        bound.render(painter, QRectF((wanted.width() - fit.width()) / 2.0, (wanted.height() - fit.height()) / 2.0,
                                                     fit.width(), fit.height()), DemoSession::ShowcaseTime);
                    }
            }
        painter.end();
        if (size)
            {
                *size = image.size();
            }
        return image;
    }
};

void registerTypes()
{
    static bool done = false;
    if (done)
        {
            return;
        }
    done = true;
    // made for fingers: the touch style, unless another is asked for
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        {
            QQuickStyle::setStyle(QStringLiteral("Material"));
        }
    qmlRegisterType<GaugeOverlay>("TrackRideViz", 1, 0, "GaugeOverlay");
    qmlRegisterUncreatableType<SimpleController>("TrackRideViz", 1, 0, "SimpleController",
            QStringLiteral("the window's own, as `simple`"));
}
}

QQmlApplicationEngine *SimpleUi::open(const std::function<void(const Project &, const QString &, bool)> &back, const Project &project,
                                      const QString &projectPath)
{
    registerTypes();
    auto *engine = new QQmlApplicationEngine;
    auto *controller = new SimpleController(bool(back), engine);
    if (back)
        {
            QObject::connect(controller, &SimpleController::backRequested, engine, [back, controller]()
            {
                back(controller->project(), controller->projectPath(), controller->modified());
            });
        }
    controller->openProject(project, projectPath);
    engine->rootContext()->setContextProperty(QStringLiteral("simple"), controller);
    // the engine owns it
    engine->addImageProvider(QStringLiteral("previews"), new PreviewProvider);
    engine->load(QUrl(QStringLiteral("qrc:/qml/simple/Main.qml")));
    return engine;
}

QQmlApplicationEngine *SimpleUi::open(const QStringList &files)
{
    QQmlApplicationEngine *engine = open();
    auto *controller = engine->rootContext()->contextProperty(QStringLiteral("simple")).value<SimpleController *>();
    QList<QUrl> urls;
    for (const QString &file : files)
        {
            const QFileInfo info(file);
            if (info.isFile())
                {
                    urls << QUrl::fromLocalFile(info.absoluteFilePath());
                }
        }
    controller->openFiles(urls);
    return engine;
}

bool SimpleUi::wanted(int argc, char *argv[])
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return true;
#else
    for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--simple") == 0)
                {
                    return true;
                }
        }
    return false;
#endif
}
