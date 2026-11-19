#pragma once

#include "../maps/tilestore.h"
#include "../telemetry/gears.h"
#include "../telemetry/powerestimate.h"
#include "../telemetry/surfaces.h"
#include "../units/units.h"
#include "../weather/weather.h"
#include "../video/exportprofile.h"
#include "../video/exportsettings.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

// Application-wide preferences, stored with QSettings.
//
// The one place that knows the key names and defaults; the settings dialog,
// main() and (later) the command line and the QML front end all go through
// it. Per-project choices (gauges, sync, export options of one video) do not
// belong here - they live in the project file.
//
// Values are stored as readable words ("dark", "vaapi", "mph"), so the file
// can be edited by hand and survives enums being reordered.
class AppSettings
{
public:
    // --- Appearance -------------------------------------------------------
    enum ColorScheme { SystemColors, Light, Dark };

    // Widget style name as QStyleFactory knows it; empty = the platform's own.
    static QString style();
    static void setStyle(const QString &style);
    static ColorScheme colorScheme();
    static void setColorScheme(ColorScheme scheme);
    // "system", or a locale name there is a translation for ("uk_UA"). Read at start-up.
    static QString language();
    static void setLanguage(const QString &language);
    static bool saveWindowLayout();
    static void setSaveWindowLayout(bool enabled);

    // Files opened lately, the last first, for the File menu; as many as recentFileCount().
    enum RecentKind { RecentVideos, RecentData, RecentProjects };
    static QStringList recentFiles(RecentKind kind);
    static void addRecentFile(RecentKind kind, const QString &path);
    static void clearRecentFiles(RecentKind kind);
    // how many each list keeps: 10 unless set; 0 keeps none
    static int recentFileCount();
    static void setRecentFileCount(int count);

    // Whether a newer release is looked for (once a day, quietly); when it was last; a release the
    // user chose to skip, not shown again.
    static bool checkForUpdates();
    static void setCheckForUpdates(bool check);
    static QDateTime lastUpdateCheck();
    static void setLastUpdateCheck(const QDateTime &when);
    static QString skippedRelease();
    static void setSkippedRelease(const QString &tag);
    static constexpr int kMaxRecentFiles = 30;

    // --- Preview ----------------------------------------------------------
    enum Decoder { AutoDecoder, VaApi, Cuda, Qsv, Vdpau };

    // Both are read at start-up: the decoder is chosen when Qt Multimedia loads.
    static bool hardwareDecoding();
    static void setHardwareDecoding(bool enabled);
    static Decoder preferredDecoder();
    static void setPreferredDecoder(Decoder decoder);

    // Value for the QT_FFMPEG_DECODING_HW_DEVICE_TYPES environment variable,
    // or a null array when Qt's own choice should stand.
    static QByteArray decodingDeviceTypes();
    // The rule behind it, separated from storage and platform so it can be tested.
    static QByteArray decodingDeviceTypes(bool hardwareDecoding, Decoder preferred, bool onLinux);

    // --- Export ---------------------------------------------------------
    // Full path of the ffmpeg program; empty = find "ffmpeg" on the PATH.
    // Exports on the program's own video engine (FFmpeg's libraries linked in) rather than the
    // ffmpeg program; true unless chosen otherwise. A build without the libraries uses ffmpeg anyway.
    static bool ownVideoEngine();
    static void setOwnVideoEngine(bool own);
    static QString ffmpegPath();
    static void setFfmpegPath(const QString &path);
    // Where exported videos go; empty = next to the source video.
    static QString exportFolder();
    static void setExportFolder(const QString &folder);
    // What the export dialog starts with: the choices of the last export.
    // Output file and range belong to one video and are not kept.
    // The bitrate is not kept either: it starts from that of each video.
    static ExportSettings exportDefaults();
    // False until the first export: the dialog then picks an encoder itself.
    static bool hasExportDefaults();
    // The user's own export profiles, and the profile of the last export (empty: none).
    static QVector<ExportProfile> exportProfiles();
    static void setExportProfiles(const QVector<ExportProfile> &profiles);
    static QString exportProfile();
    static void setExportProfile(const QString &id);
    // The extension of the last exported file ("mp4", "mkv"...); empty before the first export.
    static QString exportFileType();

    // --- Maps -----------------------------------------------------------
    // Street maps under route gauges that ask for them may be fetched from the
    // tile server; without it only tiles already on disk are drawn.
    static bool mapDownloads();
    static void setMapDownloads(bool allowed);
    // Which map: an id of TileStore::providers(), or OwnMapServer for the address below.
    static constexpr const char *OwnMapServer = "own";
    static QString mapProvider();
    static void setMapProvider(const QString &id);
    // The keys of map services that want one (TileStore::keyServices()), by service id.
    static QString mapApiKey(const QString &service);
    static void setMapApiKey(const QString &service, const QString &key);
    static QHash<QString, QString> mapApiKeys();
    // The maps that can be chosen with the keys there are.
    static QVector<TileStore::Provider> usableMapProviders();
    // The user's own server: its address with {z}, {x}, {y}, and the credit line it asks for.
    // The tile stores (maps, the terrain) set as these settings say.
    static void applyMapSources();
    static QString mapTileUrl();
    static void setMapTileUrl(const QString &url);
    static QString mapAttribution();
    static void setMapAttribution(const QString &text);
    // The credit line of the map chosen above.
    static QString mapCredit();
    // A street map under the route in the Laps panel.
    static bool lapsMapShown();
    static void setLapsMapShown(bool shown);
    // The credit drawn on the map itself; off when the video credits the map elsewhere.
    static bool mapCreditShown();
    static void setMapCreditShown(bool shown);
    static void setExportDefaults(const ExportSettings &settings);

    // --- Bike -----------------------------------------------------------
    // Its gears and wheel; its weight is the power estimate's (powerRider()).
    static Drivetrain drivetrain();
    static void setDrivetrain(const Drivetrain &drivetrain);

    // --- Power ----------------------------------------------------------
    // The rider and the bike, for the power of rides recorded without a power meter.
    static PowerEstimate::Rider powerRider();
    static void setPowerRider(const PowerEstimate::Rider &rider);

    // --- Surfaces -------------------------------------------------------
    // The kinds of surface a stretch of a ride can be marked as; Surfaces::usual() until changed.
    static QVector<SurfaceType> surfaceTypes();
    static void setSurfaceTypes(const QVector<SurfaceType> &types);

    // --- Weather --------------------------------------------------------
    // The wind and the air of the day of a ride, from a weather history service.
    static WeatherChoice weather();
    static void setWeather(const WeatherChoice &choice);
    // Each service's key is kept, so that going back to one needs no typing.
    static QString weatherApiKey(const QString &provider);
    static void setWeatherApiKey(const QString &provider, const QString &key);

    // --- Units ------------------------------------------------------------
    static Units units();
    static void setUnits(const Units &units);
};
