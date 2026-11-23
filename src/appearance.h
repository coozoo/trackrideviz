#pragma once

// Applies the appearance settings (widget style, light/dark colors) to the
// running application. Safe to call again after the settings change.
namespace Appearance
{
void apply();
// Long tooltips wrapped to a readable width: Qt shows plain text on one line, however long.
// Once, at start; for every widget of the program.
void wrapToolTips();
}
