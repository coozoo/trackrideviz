#include "gaugefonts.h"

#include <QCoreApplication>
#include <QFont>
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
        // for imported gauges
        for (const char *file : {"Roboto-Light", "Roboto-Regular", "Roboto-Italic", "Roboto-Bold", "Roboto-BoldItalic",
                                 "RobotoCondensed-Regular", "RobotoCondensed-Bold", "Orbitron-Medium", "Orbitron-Bold",
                                 "DSEG7Classic-Regular", "DSEG7Classic-Italic", "DSEG7Classic-Bold",
                                 "DSEG7Classic-BoldItalic"})
            {
                QFontDatabase::addApplicationFont(QString(":/gaugefonts/%1.ttf").arg(file));
            }
        // What imported gauges ask for, when the computer does not have it: a font of the same
        // kind. Qt tries these only after the font itself, so an installed one still wins.
        QFont::insertSubstitutions("DS-Digital", {"DSEG7 Classic"});
        QFont::insertSubstitutions("Furore", {"Orbitron"});
        QFont::insertSubstitutions("Impact", {"Roboto Condensed"});
        for (const char *mono : {"Consolas", "Courier New", "Lucida Console"})
            {
                QFont::insertSubstitutions(mono, {"Cascadia Mono", "Menlo", "DejaVu Sans Mono", "Liberation Mono",
                                                  "Noto Sans Mono", "Courier New", "Courier"});
            }
        for (const char *sans : {"Arial", "Helvetica", "Microsoft Sans Serif", "sans-serif"})
            {
                QFont::insertSubstitutions(sans, {"Arial", "Helvetica", "Liberation Sans", "Arimo", "DejaVu Sans",
                                                  "Roboto"});
            }
        for (const char *sans : {"Verdana", "Tahoma"})
            {
                QFont::insertSubstitutions(sans, {"Verdana", "Tahoma", "DejaVu Sans", "Roboto"});
            }
    });
}
