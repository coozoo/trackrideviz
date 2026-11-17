#include "releasecheck.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace
{
const char kLatest[] = "https://api.github.com/repos/coozoo/trackrideviz/releases/latest";
const char kReleases[] = "https://github.com/coozoo/trackrideviz/releases";
const int kTimeout = 15000;   // ms
}

const ReleaseCheck::Asset *ReleaseCheck::Release::assetEndingWith(const QString &ending) const
{
    for (const Asset &asset : assets)
        {
            if (asset.name.endsWith(ending, Qt::CaseInsensitive))
                {
                    return &asset;
                }
        }
    return nullptr;
}

ReleaseCheck::ReleaseCheck(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

QString ReleaseCheck::currentTag()
{
#ifdef RTV_RELEASE_TAG
    return QStringLiteral(RTV_RELEASE_TAG);
#else
    return QString();
#endif
}

QUrl ReleaseCheck::releasesPage()
{
    return QUrl(QString::fromLatin1(kReleases));
}

bool ReleaseCheck::parseTag(const QString &tag, QVersionNumber *version, QString *built)
{
    // the version, a dash and the minute of the build; whatever follows does not count
    static const QRegularExpression form(QStringLiteral("^v?(\\d+(?:\\.\\d+)*)-(\\d{12})"));
    const QRegularExpressionMatch match = form.match(tag.trimmed());
    if (!match.hasMatch())
        {
            return false;
        }
    if (version)
        {
            *version = QVersionNumber::fromString(match.captured(1));
        }
    if (built)
        {
            *built = match.captured(2);
        }
    return true;
}

bool ReleaseCheck::isNewer(const QString &tag, const QString &than)
{
    QVersionNumber version;
    QVersionNumber ours;
    QString built;
    QString ourBuilt;
    if (!parseTag(tag, &version, &built) || !parseTag(than, &ours, &ourBuilt))
        {
            return false;
        }
    const int byVersion = QVersionNumber::compare(version, ours);
    return byVersion != 0 ? byVersion > 0 : built > ourBuilt;
}

ReleaseCheck::Release ReleaseCheck::parse(const QByteArray &json)
{
    Release release;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QString tag = root.value("tag_name").toString();
    QVersionNumber version;
    if (!parseTag(tag, &version, &release.built) || root.value("draft").toBool())
        {
            return Release();
        }
    release.tag = tag;
    release.version = version.toString();
    release.title = root.value("name").toString();
    release.notes = root.value("body").toString();
    release.page = QUrl(root.value("html_url").toString());
    for (const QJsonValue &item : root.value("assets").toArray())
        {
            const QJsonObject file = item.toObject();
            Asset asset;
            asset.name = file.value("name").toString();
            asset.url = QUrl(file.value("browser_download_url").toString());
            asset.size = qint64(file.value("size").toDouble());
            // "sha256:<hex>" for files uploaded since GitHub keeps them
            const QString digest = file.value("digest").toString();
            if (digest.startsWith(QLatin1String("sha256:")))
                {
                    asset.sha256 = digest.mid(7).toLatin1().toLower();
                }
            if (!asset.name.isEmpty() && asset.url.isValid())
                {
                    release.assets.append(asset);
                }
        }
    return release;
}

void ReleaseCheck::check()
{
    QNetworkRequest request{QUrl(QString::fromLatin1(kLatest))};
    // GitHub answers only those who say who they are
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("trackrideviz"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(kTimeout);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]()
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            {
                emit failed(reply->errorString());
                return;
            }
        const Release latest = parse(reply->readAll());
        if (!latest.isValid())
            {
                emit failed(tr("The answer about the latest release could not be read."));
                return;
            }
        if (isNewer(latest.tag, currentTag()))
            {
                emit newer(latest);
            }
        else
            {
                emit upToDate();
            }
    });
}
