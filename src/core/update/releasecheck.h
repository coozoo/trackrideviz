#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QVersionNumber>

class QNetworkAccessManager;

// Whether a newer release than this program is out.
//
// The project's releases on GitHub are asked for over HTTPS, as a browser opens the releases
// page: no account, no key. A release is named by its tag, the version and the minute it was
// built ("0.01-202611171234_refs/heads/main"); a later version, or the same version built
// later, is newer. Only a program built for a release knows its tag (CI builds it in as
// RTV_RELEASE_TAG); others - development builds, distributions' packages - never ask.
class ReleaseCheck : public QObject
{
    Q_OBJECT

public:
    // One file of a release.
    struct Asset
    {
        QString name;
        QUrl url;
        qint64 size{0};
        QByteArray sha256;   // hex, as GitHub states it; empty when it does not
    };
    struct Release
    {
        QString tag;
        QString version;     // "0.01"
        QString built;       // "202611171234"
        QString title;
        QString notes;       // Markdown
        QUrl page;
        QVector<Asset> assets;

        bool isValid() const { return !tag.isEmpty(); }
        // The first file whose name ends so; null when there is none.
        const Asset *assetEndingWith(const QString &ending) const;
    };

    explicit ReleaseCheck(QObject *parent = nullptr);

    // The release this program was built as; empty for any other build.
    static QString currentTag();
    // The page of all releases.
    static QUrl releasesPage();

    // A release's tag read: false when it is not one.
    static bool parseTag(const QString &tag, QVersionNumber *version, QString *built);
    // `tag` names a later release than `than`.
    static bool isNewer(const QString &tag, const QString &than);
    // GitHub's answer for one release; invalid when it is not one.
    static Release parse(const QByteArray &json);

    // Asks for the latest release; one of the signals follows.
    void check();

signals:
    void newer(const ReleaseCheck::Release &release);
    void upToDate();
    void failed(const QString &why);

private:
    QNetworkAccessManager *m_network{nullptr};
};
