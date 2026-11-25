#pragma once

#include "project/project.h"

#include <QStringList>

#include <functional>

class QQmlApplicationEngine;

// The simplified interface: one QML window for phones, tablets and a quick job on the desktop.
namespace SimpleUi
{
// Opens it. `back`: how to return to the full window, which then shows a button for it (and
// closing the window returns too), with the project as it was left, its file and whether it is unsaved; empty when it
// is the program's only window. `project`: the full window's, its files opened, saved in
// `projectPath`. The engine belongs to the caller; deleting it closes the window.
QQmlApplicationEngine *open(const std::function<void(const Project &, const QString &, bool)> &back = {},
                            const Project &project = {}, const QString &projectPath = QString());
// The same, with files opened in it as Open does: a project, or a video, data and music.
QQmlApplicationEngine *open(const QStringList &files);

// Asked for on the command line (--simple), or the only interface there is (Android).
bool wanted(int argc, char *argv[]);
}
