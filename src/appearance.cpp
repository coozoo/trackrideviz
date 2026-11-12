#include "appearance.h"

#include "settings/appsettings.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace
{
// Both schemes are spelled out in full. Asking the style for its "standard"
// palette is not enough: current styles derive part of it from the desktop's
// own colour scheme, which gives light text on a light window when the two
// disagree.
QPalette makePalette(const QColor &window, const QColor &base, const QColor &text,
                     const QColor &disabled, const QColor &highlight, bool dark)
{
    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, dark ? window : window.lighter(103));
    palette.setColor(QPalette::ToolTipBase, dark ? window : QColor(255, 255, 220));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Link, dark ? highlight.lighter(130) : QColor(0, 0, 255));
    palette.setColor(QPalette::LinkVisited, dark ? highlight.lighter(110) : QColor(128, 0, 128));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Light, dark ? window.lighter(150) : QColor(255, 255, 255));
    palette.setColor(QPalette::Midlight, dark ? window.lighter(125) : window.lighter(105));
    palette.setColor(QPalette::Mid, dark ? window.darker(130) : window.darker(130));
    palette.setColor(QPalette::Dark, dark ? window.darker(160) : window.darker(150));
    palette.setColor(QPalette::Shadow, dark ? QColor(0, 0, 0) : QColor(118, 118, 118));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        {
            palette.setColor(QPalette::Disabled, role, disabled);
        }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, dark ? QColor(80, 80, 80) : QColor(145, 145, 145));
    return palette;
}

QPalette darkPalette()
{
    return makePalette(QColor(45, 45, 48), QColor(30, 30, 32), QColor(220, 220, 220),
                       QColor(128, 128, 128), QColor(42, 130, 218), true);
}

QPalette lightPalette()
{
    return makePalette(QColor(239, 239, 239), QColor(255, 255, 255), QColor(0, 0, 0),
                       QColor(150, 150, 150), QColor(48, 140, 198), false);
}
}

void Appearance::apply()
{
    // what the platform gave us, before anything was changed
    static const QString platformStyle = QApplication::style()->objectName();

    const QString wanted = AppSettings::style();
    const QStringList available = QStyleFactory::keys();
    const QString style = available.contains(wanted, Qt::CaseInsensitive) ? wanted : platformStyle;
    if (QApplication::style()->objectName().compare(style, Qt::CaseInsensitive) != 0)
        {
            QApplication::setStyle(style);
        }

    // A platform theme may have installed an application style sheet (qt5ct /
    // qt6ct do). Widgets it styles keep a private copy of the palette and
    // would stay in the old colours, so the sheet is taken off while the
    // palette changes and put back afterwards.
    const QString styleSheet = qApp->styleSheet();
    if (!styleSheet.isEmpty())
        {
            qApp->setStyleSheet(QString());
        }
    switch (AppSettings::colorScheme())
        {
        case AppSettings::Dark:
            QApplication::setPalette(darkPalette());
            break;
        case AppSettings::Light:
            QApplication::setPalette(lightPalette());
            break;
        case AppSettings::SystemColors:
            // a default-constructed palette hands the colours back to the platform
            QApplication::setPalette(QPalette());
            break;
        }
    if (!styleSheet.isEmpty())
        {
            qApp->setStyleSheet(styleSheet);
        }
}
