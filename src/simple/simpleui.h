#pragma once

#include <QStringList>

#include <functional>

class QQmlApplicationEngine;

// The simplified interface: one QML window for phones, tablets and a quick job on the desktop.
namespace SimpleUi
{
// Opens it. `back`: how to return to the full window, which then shows a button for it (and
// closing the window returns too); empty when it is the program's only window. The engine
// belongs to the caller; deleting it closes the window.
QQmlApplicationEngine *open(const std::function<void()> &back = {});
// The same, with files opened in it: the first video, and data (a video with data of its own
// needs none).
QQmlApplicationEngine *open(const QStringList &files);

// Asked for on the command line (--simple), or the only interface there is (Android).
bool wanted(int argc, char *argv[]);
}
