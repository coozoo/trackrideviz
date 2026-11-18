#pragma once

#include <QIcon>
#include <QString>

// Icons of buttons and menus: the system theme's own where it has one, so
// the program looks like the rest of the desktop, otherwise the Tango one
// that comes with it (Windows and macOS have no theme).
namespace Icons
{
// `name` is a freedesktop icon name; `fallback` names the shipped picture
// when it is not the same.
QIcon named(const QString &name, const QString &fallback = QString());
// Only the shipped picture, for a set that has to look alike whatever the theme (the pages of
// the settings: some have no theme icon at all).
QIcon bundled(const QString &name);
}
