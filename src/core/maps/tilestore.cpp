#include "tilestore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <cmath>

namespace
{
const double kPi = 3.14159265358979323846;
const int kTileSize = 256;
const int kMaxZoom = 19;
// servers ask for no more than two requests at a time
const int kParallel = 2;
// Mercator maps end here
const double kMaxLatitude = 85.0511;
}

size_t qHash(const TileStore::Tile &tile, size_t seed)
{
    return qHashMulti(seed, tile.zoom, tile.x, tile.y);
}

TileStore *TileStore::instance()
{
    static TileStore *store = new TileStore;
    return store;
}

TileStore *TileStore::terrain()
{
    static TileStore *store = []()
    {
        TileStore *made = new TileStore;
        made->m_urlTemplate = QStringLiteral("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png");
        made->m_attribution.clear();
        made->m_maxZoom = 15;
        made->m_folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/terrain";
        return made;
    }();
    return store;
}

namespace
{
struct MapOptions
{
    bool downloads{false};
    bool credit{true};
    QHash<QString, QString> keys;   // by TileStore::KeyService id
};

// The provider's address with its key put in.
QString addressOf(const TileStore::Provider &provider, const QHash<QString, QString> &keys)
{
    QString address = provider.urlTemplate;
    address.replace("{apikey}", keys.value(provider.service).trimmed());
    return address;
}

QMutex mapsMutex;
QHash<QString, TileStore *> maps;   // forMap()'s, by provider id
MapOptions mapOptions;
}

TileStore *TileStore::forMap(const QString &providerId)
{
    if (providerId.isEmpty())
        {
            return instance();
        }
    const QMutexLocker lock(&mapsMutex);
    const Provider provider = TileStore::provider(providerId);
    if (!provider.service.isEmpty() && mapOptions.keys.value(provider.service).trimmed().isEmpty())
        {
            // its key was taken away: the map of the settings rather than none
            return instance();
        }
    if (TileStore *store = maps.value(providerId))
        {
            return store;
        }
    TileStore *made = new TileStore;
    made->setSource(addressOf(provider, mapOptions.keys), mapOptions.credit ? provider.attribution : QString(),
                    provider.maxZoom);
    made->setDownloads(mapOptions.downloads);
    connect(made, &TileStore::arrived, instance(), &TileStore::arrived);
    maps.insert(providerId, made);
    return made;
}

void TileStore::setMapOptions(bool downloads, bool credit, const QHash<QString, QString> &keys)
{
    const QMutexLocker lock(&mapsMutex);
    mapOptions = {downloads, credit, keys};
    for (auto it = maps.cbegin(); it != maps.cend(); ++it)
        {
            const Provider provider = TileStore::provider(it.key());
            it.value()->setSource(addressOf(provider, keys), credit ? provider.attribution : QString(), provider.maxZoom);
            it.value()->setDownloads(downloads);
        }
}

int TileStore::pendingOfAllMaps()
{
    int pending = instance()->pending();
    const QMutexLocker lock(&mapsMutex);
    for (const TileStore *store : std::as_const(maps))
        {
            pending += store->pending();
        }
    return pending;
}

QImage TileStore::tile(int zoom, int x, int y)
{
    const QMutexLocker lock(&m_mutex);
    const quint64 key = (quint64(zoom) << 56) | (quint64(quint32(x)) << 28) | quint64(quint32(y));
    if (const QImage *known = m_tiles.object(key))
        {
            return *known;
        }
    const Tile tile{zoom, x, y};
    const QImage read(pathOf(tile));
    if (!read.isNull())
        {
            m_tiles.insert(key, new QImage(read));
            return read;
        }
    if (!m_wanted.contains(tile) && !m_failed.contains(tile) && !m_running.contains(tile))
        {
            m_wanted.insert(tile);
            if (m_downloads)
                {
                    QMetaObject::invokeMethod(this, "fetchWanted", Qt::QueuedConnection);
                }
        }
    return QImage();
}

TileStore::TileStore()
    : m_urlTemplate(defaultUrlTemplate())
    , m_attribution(defaultAttribution())
    , m_folder(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tiles")
    , m_tiles(64)
    , m_pictures(256)   // megabytes
{
    if (QCoreApplication::instance())
        {
            moveToThread(QCoreApplication::instance()->thread());
        }
}

QString TileStore::defaultUrlTemplate()
{
    return QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
}

QString TileStore::defaultAttribution()
{
    return QStringLiteral("© OpenStreetMap contributors");
}

QVector<TileStore::Provider> TileStore::providers()
{
    const QString osm = QStringLiteral("© OpenStreetMap contributors");
    const QString esri = QStringLiteral("Esri, HERE, Garmin, USGS, ") + osm;
    const QString thunderforest = QStringLiteral("Maps © Thunderforest · Data ") + osm;
    const QString stadia = QStringLiteral("© Stadia Maps · © OpenMapTiles · ") + osm;
    const QString stamen = QStringLiteral("© Stadia Maps · © Stamen Design · © OpenMapTiles · ") + osm;
    const QString maptiler = QStringLiteral("© MapTiler · ") + osm;
    const QString mapbox = QStringLiteral("© Mapbox · ") + osm;
    const QString ign = QStringLiteral("IGN-F / Géoplateforme");
    // by concatenation: the %2F of the address would be taken for an argument of arg()
    const auto ignLayer = [](const char *layer, const char *format)
    {
        return QStringLiteral("https://data.geopf.fr/wmts?layer=") + QLatin1String(layer)
               + QStringLiteral("&style=normal&tilematrixset=PM&Service=WMTS&Request=GetTile&Version=1.0.0&Format=image%2F")
               + QLatin1String(format) + QStringLiteral("&TileMatrix={z}&TileCol={x}&TileRow={y}");
    };
    return
    {
        // free to use, each asking for its credit
        {"osm", "OpenStreetMap", defaultUrlTemplate(), defaultAttribution(), 19, QString()},
        {"cyclosm", "CyclOSM", "https://{s}.tile-cyclosm.openstreetmap.fr/cyclosm/{z}/{x}/{y}.png",
         osm + " · CyclOSM", 20, QString()},
        {"opentopomap", "OpenTopoMap", "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",
         "© OpenStreetMap, SRTM · OpenTopoMap (CC-BY-SA)", 17, QString()},
        {"osm-hot", "OpenStreetMap Humanitarian", "https://{s}.tile.openstreetmap.fr/hot/{z}/{x}/{y}.png",
         osm + " · Humanitarian OSM Team, OSM France", 19, QString()},
        {"osm-france", "OpenStreetMap France", "https://{s}.tile.openstreetmap.fr/osmfr/{z}/{x}/{y}.png",
         osm + " · OSM France", 19, QString()},
        {"opnvkarte", "ÖPNVKarte (public transport)", "https://tileserver.memomaps.de/tilegen/{z}/{x}/{y}.png",
         osm + " · ÖPNVKarte", 18, QString()},
        {"satellite", "Satellite (Esri)",
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
         "Esri, Maxar, Earthstar Geographics", 19, QString()},
        {"esri-topo", "Esri Topographic",
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}", esri, 19, QString()},
        {"esri-street", "Esri Streets",
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}", esri, 19, QString()},
        {"relief", "Relief (maps-for-free)", "https://maps-for-free.com/layer/relief/z{z}/row{y}/{z}_{x}-{y}.jpg",
         "maps-for-free.com", 11, QString()},
        {"ign-plan", "IGN France map", ignLayer("GEOGRAPHICALGRIDSYSTEMS.PLANIGNV2", "png"), ign, 19, QString()},
        {"ign-photo", "IGN France aerial photos", ignLayer("ORTHOIMAGERY.ORTHOPHOTOS", "jpeg"), ign, 19, QString()},
        {"usgs-topo", "USGS Topo (USA)",
         "https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/{z}/{y}/{x}",
         "USGS The National Map", 16, QString()},
        {"usgs-imagery", "USGS Imagery (USA)",
         "https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{y}/{x}",
         "USGS The National Map", 16, QString()},
        // with a key of the user's own
        {"opencyclemap", "Thunderforest OpenCycleMap",
         "https://{s}.tile.thunderforest.com/cycle/{z}/{x}/{y}.png?apikey={apikey}", thunderforest, 22, "thunderforest"},
        {"tf-outdoors", "Thunderforest Outdoors",
         "https://{s}.tile.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey={apikey}", thunderforest, 22, "thunderforest"},
        {"tf-landscape", "Thunderforest Landscape",
         "https://{s}.tile.thunderforest.com/landscape/{z}/{x}/{y}.png?apikey={apikey}", thunderforest, 22, "thunderforest"},
        {"tf-transport", "Thunderforest Transport",
         "https://{s}.tile.thunderforest.com/transport/{z}/{x}/{y}.png?apikey={apikey}", thunderforest, 22, "thunderforest"},
        {"stadia-smooth", "Stadia Alidade Smooth",
         "https://tiles.stadiamaps.com/tiles/alidade_smooth/{z}/{x}/{y}.png?api_key={apikey}", stadia, 20, "stadia"},
        {"stadia-dark", "Stadia Alidade Smooth Dark",
         "https://tiles.stadiamaps.com/tiles/alidade_smooth_dark/{z}/{x}/{y}.png?api_key={apikey}", stadia, 20, "stadia"},
        {"stadia-outdoors", "Stadia Outdoors",
         "https://tiles.stadiamaps.com/tiles/outdoors/{z}/{x}/{y}.png?api_key={apikey}", stadia, 20, "stadia"},
        {"stamen-terrain", "Stamen Terrain",
         "https://tiles.stadiamaps.com/tiles/stamen_terrain/{z}/{x}/{y}.png?api_key={apikey}", stamen, 18, "stadia"},
        {"stamen-toner", "Stamen Toner",
         "https://tiles.stadiamaps.com/tiles/stamen_toner/{z}/{x}/{y}.png?api_key={apikey}", stamen, 20, "stadia"},
        {"stamen-watercolor", "Stamen Watercolor",
         "https://tiles.stadiamaps.com/tiles/stamen_watercolor/{z}/{x}/{y}.jpg?api_key={apikey}", stamen, 16, "stadia"},
        {"maptiler-streets", "MapTiler Streets",
         "https://api.maptiler.com/maps/streets-v2/256/{z}/{x}/{y}.png?key={apikey}", maptiler, 20, "maptiler"},
        {"maptiler-outdoor", "MapTiler Outdoor",
         "https://api.maptiler.com/maps/outdoor-v2/256/{z}/{x}/{y}.png?key={apikey}", maptiler, 20, "maptiler"},
        {"maptiler-topo", "MapTiler Topo",
         "https://api.maptiler.com/maps/topo-v2/256/{z}/{x}/{y}.png?key={apikey}", maptiler, 20, "maptiler"},
        {"maptiler-winter", "MapTiler Winter",
         "https://api.maptiler.com/maps/winter-v2/256/{z}/{x}/{y}.png?key={apikey}", maptiler, 20, "maptiler"},
        {"maptiler-satellite", "MapTiler Satellite",
         "https://api.maptiler.com/maps/satellite/256/{z}/{x}/{y}.jpg?key={apikey}", maptiler, 20, "maptiler"},
        {"mapbox-satellite", "Mapbox Satellite Streets",
         "https://api.mapbox.com/styles/v1/mapbox/satellite-streets-v12/tiles/256/{z}/{x}/{y}?access_token={apikey}",
         mapbox, 22, "mapbox"},
        {"mapbox-outdoors", "Mapbox Outdoors",
         "https://api.mapbox.com/styles/v1/mapbox/outdoors-v12/tiles/256/{z}/{x}/{y}?access_token={apikey}",
         mapbox, 22, "mapbox"},
        {"mapbox-streets", "Mapbox Streets",
         "https://api.mapbox.com/styles/v1/mapbox/streets-v12/tiles/256/{z}/{x}/{y}?access_token={apikey}",
         mapbox, 22, "mapbox"},
    };
}

QVector<TileStore::KeyService> TileStore::keyServices()
{
    return
    {
        {"thunderforest", "Thunderforest", "https://manage.thunderforest.com/"},
        {"stadia", "Stadia Maps", "https://client.stadiamaps.com/signup/"},
        {"maptiler", "MapTiler", "https://cloud.maptiler.com/account/keys/"},
        {"mapbox", "Mapbox", "https://account.mapbox.com/access-tokens/"},
    };
}

QVector<TileStore::Provider> TileStore::usableProviders(const QHash<QString, QString> &keys)
{
    QVector<Provider> usable;
    for (const Provider &one : providers())
        {
            if (one.service.isEmpty() || !keys.value(one.service).trimmed().isEmpty())
                {
                    usable.append(one);
                }
        }
    return usable;
}

TileStore::Provider TileStore::provider(const QString &id)
{
    const QVector<Provider> known = providers();
    for (const Provider &one : known)
        {
            if (one.id == id)
                {
                    return one;
                }
        }
    return known.first();
}

double TileStore::mercatorLatitude(double latitude)
{
    const double held = std::clamp(latitude, -kMaxLatitude, kMaxLatitude);
    return std::log(std::tan(kPi / 4.0 + held * kPi / 360.0)) * 180.0 / kPi;
}

QString TileStore::urlTemplate() const
{
    const QMutexLocker lock(&m_mutex);
    return m_urlTemplate;
}

QString TileStore::attribution() const
{
    const QMutexLocker lock(&m_mutex);
    return m_attribution;
}

void TileStore::setSource(const QString &urlTemplate, const QString &attribution, int maxZoom)
{
    const QMutexLocker lock(&m_mutex);
    const QString address = urlTemplate.trimmed().isEmpty() ? defaultUrlTemplate() : urlTemplate.trimmed();
    maxZoom = std::clamp(maxZoom, 0, kMaxZoom);
    if (address != m_urlTemplate || maxZoom != m_maxZoom)
        {
            m_urlTemplate = address;
            m_maxZoom = maxZoom;
            m_pictures.clear();
            ++m_picturesCleared;
            m_wanted.clear();
            m_failed.clear();
        }
    m_attribution = attribution.trimmed();
}

void TileStore::setDownloads(bool allowed)
{
    {
        const QMutexLocker lock(&m_mutex);
        m_downloads = allowed;
        if (allowed)
            {
                m_failed.clear();
                m_pictures.clear();
                ++m_picturesCleared;
            }
    }
    if (allowed)
        {
            QMetaObject::invokeMethod(this, "fetchWanted", Qt::QueuedConnection);
        }
}

int TileStore::pending() const
{
    const QMutexLocker lock(&m_mutex);
    return m_downloads ? int(m_wanted.size() + m_running.size()) : 0;
}

qint64 TileStore::storedBytes() const
{
    QString folder;
    {
        const QMutexLocker lock(&m_mutex);
        folder = m_folder;
    }
    qint64 bytes = 0;
    QDirIterator files(folder, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext())
        {
            files.next();
            bytes += files.fileInfo().size();
        }
    return bytes;
}

void TileStore::clearStored()
{
    {
        const QMutexLocker lock(&m_mutex);
        // only what is ours: a folder of tiles named by this store
        if (!m_folder.isEmpty() && (m_folder.endsWith("/tiles") || m_folder.endsWith("/terrain")))
            {
                QDir(m_folder).removeRecursively();
            }
        m_pictures.clear();
        ++m_picturesCleared;
        m_tiles.clear();
        m_failed.clear();
    }
    emit arrived();
}

void TileStore::setFolder(const QString &folder)
{
    const QMutexLocker lock(&m_mutex);
    m_folder = folder;
    m_pictures.clear();
    ++m_picturesCleared;
    m_tiles.clear();
}

double TileStore::latitudeOf(double mercatorLatitude)
{
    return (2.0 * std::atan(std::exp(mercatorLatitude * kPi / 180.0)) - kPi / 2.0) * 180.0 / kPi;
}

TileStore::Block TileStore::blockFor(const QRectF &area, int maxZoom, double pixels)
{
    // the world is 360 projected degrees each way; a tile at zoom z is 360 / 2^z of it
    const double extent = std::max({area.width(), area.height(), 1e-9});
    Block block;
    block.zoom = std::clamp(int(std::floor(std::log2(pixels / kTileSize * 360.0 / extent))), 0, std::min(maxZoom, kMaxZoom));
    const int count = 1 << block.zoom;
    auto number = [count](double projected)
    {
        return std::clamp(int(std::floor((projected + 180.0) / 360.0 * count)), 0, count - 1);
    };
    block.left = number(area.left());
    block.right = number(area.right());
    block.top = number(area.top());
    block.bottom = number(area.bottom());
    return block;
}

QString TileStore::pathOf(const Tile &tile) const
{
    // one folder per server, so changing it never shows the old one's tiles; its name is
    // worked out once per server, not for every tile
    if (m_serverOf != m_urlTemplate)
        {
            m_serverOf = m_urlTemplate;
            m_server = QString::fromLatin1(QCryptographicHash::hash(m_urlTemplate.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
        }
    return QString("%1/%2/%3/%4/%5.png").arg(m_folder, m_server, QString::number(tile.zoom), QString::number(tile.x),
                                                QString::number(tile.y));
}

QString TileStore::tilePath(int zoom, int x, int y) const
{
    const QMutexLocker lock(&m_mutex);
    return pathOf({zoom, x, y});
}

void TileStore::want(const QVector<QRectF> &areas, double pixels)
{
    // which tiles, under the lock; whether they are on disk, without it: the preview and an
    // export draw maps meanwhile
    QHash<Tile, QString> candidates;
    {
        const QMutexLocker lock(&m_mutex);
        for (const QRectF &area : areas)
            {
                const Block block = blockFor(area, m_maxZoom, pixels);
                for (int y = block.top; y <= block.bottom; ++y)
                    {
                        for (int x = block.left; x <= block.right; ++x)
                            {
                                const Tile tile{block.zoom, x, y};
                                if (!candidates.contains(tile) && !m_wanted.contains(tile) && !m_failed.contains(tile)
                                        && !m_running.contains(tile))
                                    {
                                        candidates.insert(tile, pathOf(tile));
                                    }
                            }
                    }
            }
    }
    QVector<Tile> missing;
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it)
        {
            if (!QFile::exists(it.value()))
                {
                    missing.append(it.key());
                }
        }
    if (missing.isEmpty())
        {
            return;
        }
    bool asked = false;
    {
        const QMutexLocker lock(&m_mutex);
        for (const Tile &tile : std::as_const(missing))
            {
                if (!m_wanted.contains(tile) && !m_failed.contains(tile) && !m_running.contains(tile))
                    {
                        m_wanted.insert(tile);
                        asked = true;
                    }
            }
        asked = asked && m_downloads;
    }
    if (asked)
        {
            QMetaObject::invokeMethod(this, "fetchWanted", Qt::QueuedConnection);
        }
}

TileStore::Picture TileStore::picture(const QRectF &area, double pixels)
{
    if (!area.isValid())
        {
            return {};
        }
    // Which block and where its tiles are, under the lock; reading and drawing them, without it:
    // the preview and an export ask for maps at the same time.
    Block block;
    QString key;
    QVector<QString> paths;
    quint64 cleared = 0;
    {
        const QMutexLocker lock(&m_mutex);
        block = blockFor(area, m_maxZoom, pixels);
        cleared = m_picturesCleared;
        key = QString("%1/%2/%3/%4/%5").arg(block.zoom).arg(block.left).arg(block.top).arg(block.right).arg(block.bottom);
        if (const Picture *known = m_pictures.object(key))
            {
                return *known;
            }
        for (int y = block.top; y <= block.bottom; ++y)
            {
                for (int x = block.left; x <= block.right; ++x)
                    {
                        paths.append(pathOf({block.zoom, x, y}));
                    }
            }
    }
    const int columns = block.right - block.left + 1;
    const int rows = block.bottom - block.top + 1;
    Picture result;
    QImage image(columns * kTileSize, rows * kTileSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    int found = 0;
    QVector<Tile> missing;
    {
        QPainter painter(&image);
        int index = 0;
        for (int y = block.top; y <= block.bottom; ++y)
            {
                for (int x = block.left; x <= block.right; ++x)
                    {
                        const QImage one(paths.at(index++));
                        if (!one.isNull())
                            {
                                painter.drawImage(QRect((x - block.left) * kTileSize, (y - block.top) * kTileSize, kTileSize, kTileSize), one);
                                ++found;
                            }
                        else
                            {
                                missing.append({block.zoom, x, y});
                            }
                    }
            }
    }
    if (found > 0)
        {
            const double size = 360.0 / (1 << block.zoom);
            result.image = image;
            result.bounds = QRectF(block.left * size - 180.0, block.top * size - 180.0, columns * size, rows * size);
        }
    bool asked = false;
    {
        const QMutexLocker lock(&m_mutex);
        // a tile that came while this was drawn is not in it: then it is not remembered
        if (found > 0 && cleared == m_picturesCleared)
            {
                // remembered until a tile comes; a block with nothing yet costs nothing to look at again.
                // The cache counts megabytes: a block of a whole route can be a few dozen
                m_pictures.insert(key, new Picture(result), std::max<qint64>(1, image.sizeInBytes() >> 20));
            }
        for (const Tile &tile : std::as_const(missing))
            {
                if (!m_failed.contains(tile) && !m_running.contains(tile))
                    {
                        m_wanted.insert(tile);
                        asked = true;
                    }
            }
        asked = asked && m_downloads;
    }
    if (asked)
        {
            QMetaObject::invokeMethod(this, "fetchWanted", Qt::QueuedConnection);
        }
    return result;
}

void TileStore::fetchWanted()
{
    if (!m_network)
        {
            m_network = new QNetworkAccessManager(this);
            m_announce = new QTimer(this);
            m_announce->setSingleShot(true);
            m_announce->setInterval(200);
            connect(m_announce, &QTimer::timeout, this, &TileStore::arrived);
        }
    startNext();
}

void TileStore::startNext()
{
    for (;;)
        {
            Tile tile{};
            QString address;
            QString path;
            {
                const QMutexLocker lock(&m_mutex);
                if (!m_downloads || m_wanted.isEmpty() || m_running.size() >= kParallel)
                    {
                        return;
                    }
                tile = *m_wanted.constBegin();
                m_wanted.remove(tile);
                m_running.insert(tile);
                address = m_urlTemplate;
                path = pathOf(tile);
            }
            address.replace("{z}", QString::number(tile.zoom)).replace("{x}", QString::number(tile.x))
            .replace("{y}", QString::number(tile.y))
            // servers a, b and c share the work
            .replace("{s}", QString(QChar('a' + (tile.x + tile.y) % 3)));
            QNetworkRequest request{QUrl(address)};
            // tile servers ask every program to say who it is
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QString("%1/%2 (+https://github.com/coozoo/trackrideviz)")
                              .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()));
            request.setTransferTimeout(20000);
            QNetworkReply *reply = m_network->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, tile, path]()
            {
                reply->deleteLater();
                const QByteArray data = reply->readAll();
                bool kept = false;
                if (reply->error() == QNetworkReply::NoError && !QImage::fromData(data).isNull())
                    {
                        QDir().mkpath(QFileInfo(path).path());
                        QSaveFile file(path);
                        kept = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
                    }
                {
                    const QMutexLocker lock(&m_mutex);
                    m_running.remove(tile);
                    if (kept)
                        {
                            m_pictures.clear();
                            ++m_picturesCleared;
                        }
                    if (!kept)
                        {
                            m_failed.insert(tile);
                        }
                }
                if (kept)
                    {
                        m_announce->start();
                    }
                startNext();
            });
        }
}
