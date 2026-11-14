#include "gaugefonts.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QThread>
#include <QStringList>

#include <mutex>

// By hand, for when the core is linked as a static library (the tests).
// The macro has to stand in a plain function.
static void initFontResources()
{
    Q_INIT_RESOURCE(fonts);
}

void GaugeFonts::load()
{
    // Qt adds fonts on the GUI thread only: from a worker (thumbnails, export)
    // it would crash. main() loads them before any of those start.
    if (!QCoreApplication::instance() || QThread::currentThread() != QCoreApplication::instance()->thread())
        {
            return;
        }
    static std::once_flag once;
    std::call_once(once, []()
    {
        initFontResources();
        for (const char *file : {"Light", "LightItalic", "Regular", "Italic", "Medium", "MediumItalic", "SemiBold",
                                 "SemiBoldItalic", "Bold", "BoldItalic"})
            {
                QFontDatabase::addApplicationFont(QString(":/gaugefonts/BarlowSemiCondensed-%1.ttf").arg(file));
            }
    });
}
