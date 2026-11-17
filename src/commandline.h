#pragma once

// Exports without the window, from a terminal or a script:
//   trackrideviz --render ride.rtvproj -o ride.mp4
//   trackrideviz --render --video GX010042.MP4 --data ride.fit --template "Track day"
// and lists of what can be chosen (encoders that work here, export profiles, templates).
// trackrideviz --help tells it all.
namespace CommandLine
{
// The program was started to work here, not to open its window: decided before any Qt
// application exists, from the raw arguments.
bool wanted(int argc, char *argv[]);
// On Windows the program has no console of its own: the one it was started from is taken.
void attachConsole();
// Does what was asked; the program's exit code. A QGuiApplication exists already.
int run();
}
