#include "updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QSettings>
#include <QWidget>

#include <cstdio>

namespace
{
const char kAppImageEnding[] = "-x86_64.AppImage";
const char kInstallerEnding[] = "_WIN64_setup.exe";
// where the installer notes itself, by whom it was installed: the user alone or everyone
const char kUninstallKey[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\trackrideviz";

#ifdef Q_OS_WIN
// The installer's note about this copy, when it is the one installed here: "CurrentUser" or
// "AllUsers"; empty when the program came some other way (the zip).
QString installMode()
{
    const QString here = QDir::cleanPath(QCoreApplication::applicationDirPath());
    for (const char *root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"})
        {
            const QSettings entry(QString("%1\\%2").arg(QLatin1String(root), QLatin1String(kUninstallKey)),
                                  QSettings::Registry64Format);
            const QString location = QDir::cleanPath(QDir::fromNativeSeparators(entry.value("InstallLocation").toString()));
            if (!location.isEmpty() && location.compare(here, Qt::CaseInsensitive) == 0)
                {
                    return entry.value("InstallMode").toString();
                }
        }
    return QString();
}
#endif
}

Updater::Kind Updater::kind()
{
#ifdef Q_OS_WIN
    return installMode().isEmpty() ? Other : WindowsInstaller;
#elif defined(Q_OS_LINUX)
    // the AppImage runtime tells where the file it runs from is
    return qEnvironmentVariableIsEmpty("APPIMAGE") ? Other : AppImage;
#else
    return Other;
#endif
}

const ReleaseCheck::Asset *Updater::assetFor(const ReleaseCheck::Release &release)
{
    switch (kind())
        {
        case AppImage:
            return release.assetEndingWith(kAppImageEnding);
        case WindowsInstaller:
            return release.assetEndingWith(kInstallerEnding);
        case Other:
            break;
        }
    return nullptr;
}

Updater::Next Updater::prepare(QWidget *parent, const ReleaseCheck::Release &release)
{
    const ReleaseCheck::Asset *asset = assetFor(release);
    if (!asset)
        {
            return Next();
        }
    const QString title = QCoreApplication::translate("Updater", "Update");
    const auto fail = [parent, title](const QString &why)
    {
        QMessageBox::warning(parent, title, why);
        return Next();
    };

    // the AppImage beside the one running (renamed over it at the end: the same disk),
    // the installer among the temporary files
    QString target;
    QString download;
    if (kind() == AppImage)
        {
            target = qEnvironmentVariable("APPIMAGE");
            const QFileInfo current(target);
            if (!QFileInfo(current.absolutePath()).isWritable())
                {
                    return fail(QCoreApplication::translate("Updater", "%1 cannot be changed here. Download the new version "
                                                            "from the release page instead.").arg(current.absolutePath()));
                }
            download = current.absolutePath() + "/." + current.fileName() + ".update";
        }
    else
        {
            download = QDir::temp().filePath(asset->name);
            target = download;
        }

    QFile file(download);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return fail(QCoreApplication::translate("Updater", "Could not write %1: %2").arg(download, file.errorString()));
        }
    QNetworkAccessManager network;
    QNetworkRequest request(asset->url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("trackrideviz"));
    // GitHub hands the file over from elsewhere
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    QNetworkReply *reply = network.get(request);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    QProgressDialog progress(QCoreApplication::translate("Updater", "Downloading %1...").arg(asset->name),
                             QCoreApplication::translate("Updater", "Stop"), 0, 1000, parent);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [&]()
    {
        const QByteArray chunk = reply->readAll();
        sha256.addData(chunk);
        file.write(chunk);
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress, [&](qint64 received, qint64 total)
    {
        const qint64 size = total > 0 ? total : asset->size;
        progress.setValue(size > 0 ? int(1000 * received / size) : 0);
    });
    // Qt 6.2 ends a stalled download with the same error as Stop: told apart by who stopped it
    bool stoppedByHand = false;
    QObject::connect(&progress, &QProgressDialog::canceled, reply, [&]()
    {
        stoppedByHand = true;
        reply->abort();
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray rest = reply->readAll();
    sha256.addData(rest);
    file.write(rest);
    file.close();
    progress.reset();
    const bool stopped = stoppedByHand && reply->error() == QNetworkReply::OperationCanceledError;
    const QString error = reply->error() == QNetworkReply::NoError ? QString()
                          : reply->error() == QNetworkReply::OperationCanceledError
                          ? QCoreApplication::translate("Updater", "nothing came for a minute")
                          : reply->errorString();
    reply->deleteLater();
    if (stopped || !error.isEmpty())
        {
            QFile::remove(download);
            return stopped ? Next() : fail(QCoreApplication::translate("Updater", "The download failed: %1").arg(error));
        }
    // the same file as the one GitHub knows, whole
    const QByteArray sum = sha256.result().toHex();
    if ((!asset->sha256.isEmpty() && sum != asset->sha256) || (asset->size > 0 && QFileInfo(download).size() != asset->size))
        {
            QFile::remove(download);
            return fail(QCoreApplication::translate("Updater", "The downloaded file is not the one released (its checksum "
                                                    "differs), so it was thrown away. Try again later, or take it from "
                                                    "the release page."));
        }

    Next next;
    next.program = target;
    if (kind() == AppImage)
        {
            QFile::setPermissions(download, QFile::permissions(target) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
            // in one step: the old file stays whole for the program still running from it
            if (std::rename(QFile::encodeName(download).constData(), QFile::encodeName(target).constData()) != 0)
                {
                    QFile::remove(download);
                    return fail(QCoreApplication::translate("Updater", "Could not put the new version in place of %1.")
                                .arg(target));
                }
            return next;
        }
#ifdef Q_OS_WIN
    // installed for the user alone: quietly, and the new version started at the end; for everyone
    // Windows asks for an administrator anyway, so the installer shows its pages
    if (installMode() == QLatin1String("CurrentUser"))
        {
            next.arguments = QStringList{"/S", "/CurrentUser", "/RESTART"};
        }
    else
        {
            next.arguments = QStringList{"/AllUsers"};
        }
#endif
    return next;
}
