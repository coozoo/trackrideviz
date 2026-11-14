#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QVector>

class QNetworkAccessManager;
class QTimer;

// Map tiles under route gauges: read from a folder on disk, fetched from a
// tile server into it when missing.
//
// Positions here are "projected" the way gauges draw routes: x = longitude,
// y = minus the Mercator latitude, both in degrees (y grows downwards). At
// the equator a degree of y is a degree of latitude; the stretch further
// north is that of every web map, so tiles and route fit without warping.
//
// picture() may be called from any thread and only reads the disk. Missing
// tiles are asked for on the store's own thread, and arrived() says when
// pictures are worth asking for again. Nothing is fetched until
// setDownloads(true): tests and tools never touch the network.
class TileStore : public QObject
{
    Q_OBJECT

public:
    struct Picture
    {
        QImage image;      // null: no tile of the area is there yet
        QRectF bounds;     // what the image covers, projected
    };

    static TileStore *instance();

    // Mercator latitude in degrees (see above); latitudes past the poles of web maps are held there.
    static double mercatorLatitude(double latitude);
    // ... and back.
    static double latitudeOf(double mercatorLatitude);

    // Tiles covering at least `area` (projected), at the zoom level where it
    // is about a thousand pixels across - the same tiles whatever the size
    // drawn, so a ride previewed once exports without a connection.
    Picture picture(const QRectF &area);

    // A map that can be chosen by name.
    struct Provider
    {
        QString id;            // kept in the settings
        QString name;
        QString urlTemplate;   // with {apikey} when the server wants one
        QString attribution;
        int maxZoom{19};
    };
    // The maps the program knows; the first is the one it starts with.
    static QVector<Provider> providers();
    // By id; the first one when the id is not known.
    static Provider provider(const QString &id);

    // The server: an address with {z}, {x} and {y} ({s} for servers named a, b, c),
    // the credit line drawn on the map (empty: none is drawn), and the closest zoom it has.
    QString urlTemplate() const;
    QString attribution() const;
    void setSource(const QString &urlTemplate, const QString &attribution, int maxZoom = 19);
    void setDownloads(bool allowed);
    // Tiles of other tests or users are never mixed in: the folder of the cache.
    void setFolder(const QString &folder);

    // What the store starts with.
    static QString defaultUrlTemplate();
    static QString defaultAttribution();

    // The tile numbers picture() would use: zoom and the block's corners (inclusive).
    struct Block
    {
        int zoom{0};
        int left{0};
        int top{0};
        int right{0};
        int bottom{0};
    };
    static Block blockFor(const QRectF &area, int maxZoom = 19);
    // Where a tile is kept on disk (for the server set now).
    QString tilePath(int zoom, int x, int y) const;

signals:
    // Tiles asked for have come (or failed for good): pictures may be more complete now.
    void arrived();

private:
    struct Tile
    {
        int zoom;
        int x;
        int y;
        bool operator==(const Tile &other) const { return zoom == other.zoom && x == other.x && y == other.y; }
    };
    friend size_t qHash(const TileStore::Tile &tile, size_t seed);

    TileStore();
    QString pathOf(const Tile &tile) const;
    Q_INVOKABLE void fetchWanted();
    void startNext();

    mutable QMutex m_mutex;
    QString m_urlTemplate;
    QString m_attribution;
    QString m_folder;
    int m_maxZoom{19};
    bool m_downloads{false};
    QCache<QString, Picture> m_pictures;   // whole blocks, by source and tile numbers
    QSet<Tile> m_wanted;                   // missing, not asked for yet
    QSet<Tile> m_failed;                   // not asked for again in this run
    QSet<Tile> m_running;
    QNetworkAccessManager *m_network{nullptr};
    QTimer *m_announce{nullptr};
};
