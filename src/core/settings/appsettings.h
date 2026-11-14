#pragma once

#include "../units/units.h"
#include "../video/exportsettings.h"

#include <QByteArray>
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

    // --- Maps -----------------------------------------------------------
    // Street maps under route gauges that ask for them may be fetched from the
    // tile server; without it only tiles already on disk are drawn.
    static bool mapDownloads();
    static void setMapDownloads(bool allowed);
    // Which map: an id of TileStore::providers(), or OwnMapServer for the address below.
    static constexpr const char *OwnMapServer = "own";
    static QString mapProvider();
    static void setMapProvider(const QString &id);
    // For the maps whose server wants a key.
    static QString mapApiKey();
    static void setMapApiKey(const QString &key);
    // The user's own server: its address with {z}, {x}, {y}, and the credit line it asks for.
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

    // --- Units ------------------------------------------------------------
    static Units units();
    static void setUnits(const Units &units);
};
