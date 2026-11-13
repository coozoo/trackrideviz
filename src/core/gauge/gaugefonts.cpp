#include "gaugefonts.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QThread>
#include <QStringList>

#include <mutex>

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
        for (const char *file : {"Light", "LightItalic", "Regular", "Italic", "Medium", "MediumItalic", "SemiBold",
                                 "SemiBoldItalic", "Bold", "BoldItalic"})
            {
                QFontDatabase::addApplicationFont(QString(":/gaugefonts/BarlowSemiCondensed-%1.ttf").arg(file));
            }
    });
}
