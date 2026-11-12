#pragma once

#include <QString>

// Where the gauges and templates that come with the program are: a folder
// holding `gauges/` and `templates/`, read-only. In the sources it is `data/`.
//
// Looked for, first found wins:
//   $TRACKRIDEVIZ_DATA                        anything, for trying out new artwork
//   <program folder>/data                     Windows zip
//   <program folder>/../share/trackrideviz    Linux packages, AppImage, `make install` to any prefix
//   <program folder>/../Resources             macOS app bundle
//   <system data folders>/trackrideviz        /usr/share, /usr/local/share, XDG_DATA_DIRS
//   the source tree's data/                   debug builds run from the build folder
namespace BundledData
{
// Empty when none was found.
QString folder();
QString gaugesFolder();
QString templatesFolder();
// Replaces the search (tests); empty restores it.
void setFolder(const QString &folder);
}
