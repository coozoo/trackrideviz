#include "gaugefonts.h"

#include <QFontDatabase>
#include <QStringList>

#include <mutex>

void GaugeFonts::load()
{
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
