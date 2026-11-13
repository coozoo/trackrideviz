#pragma once

// Fonts that ship with the program, so gauges look the same on every
// computer: Barlow Semi Condensed (SIL Open Font License, fonts/OFL-*.txt).
namespace GaugeFonts
{
inline constexpr char Family[] = "Barlow Semi Condensed";

// Makes them known to Qt; once is enough, later calls do nothing.
// Needs a QGuiApplication.
void load();
}
