#include "gaugefonts.h"

#include <QFontDatabase>
#include <QStringList>

#include <mutex>

void GaugeFonts::load()
{
    static std::once_flag once;
    std::call_once(once, []()
    {
        for (const char *file : {"Medium", "MediumItalic", "SemiBold", "SemiBoldItalic"})
            {
                QFontDatabase::addApplicationFont(QString(":/gaugefonts/BarlowSemiCondensed-%1.ttf").arg(file));
            }
    });
}
