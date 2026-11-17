#pragma once

#include "update/releasecheck.h"

#include <QString>
#include <QStringList>

class QWidget;

// How this copy of the program came, and its update where it can update itself.
//
// An AppImage is replaced by the new one in place (the program running keeps the old file
// open until it ends); a copy put there by the Windows installer gets the new installer, run
// in the same way it was installed. A zip, a macOS app and a distribution's package cannot
// replace themselves: for them there is the release page.
namespace Updater
{
enum Kind
{
    AppImage,
    WindowsInstaller,
    Other,
};
Kind kind();

// The file of `release` this copy updates from; null when it does not update itself.
const ReleaseCheck::Asset *assetFor(const ReleaseCheck::Release &release);

// What is to be started once the program has ended: the new AppImage, or the installer.
struct Next
{
    QString program;
    QStringList arguments;
    bool isValid() const { return !program.isEmpty(); }
};

// Downloads the file, with progress and a way to stop on `parent`, checks it against the
// checksum GitHub states, and for an AppImage puts it in place. Says what went wrong itself;
// an invalid Next when nothing is to be done.
Next prepare(QWidget *parent, const ReleaseCheck::Release &release);
}
