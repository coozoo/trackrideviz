#include "simpleui.h"

#include "gaugeoverlay.h"
#include "simplecontroller.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include <cstring>

namespace
{
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

QQmlApplicationEngine *SimpleUi::open(const std::function<void()> &back)
{
    registerTypes();
    auto *engine = new QQmlApplicationEngine;
    auto *controller = new SimpleController(bool(back), engine);
    if (back)
        {
            QObject::connect(controller, &SimpleController::backRequested, engine, [back]() { back(); });
        }
    engine->rootContext()->setContextProperty(QStringLiteral("simple"), controller);
    engine->load(QUrl(QStringLiteral("qrc:/qml/simple/Main.qml")));
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
