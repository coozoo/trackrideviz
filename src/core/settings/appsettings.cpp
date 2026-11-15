#include "appsettings.h"

#include "../maps/tilestore.h"

#include <QList>
#include <QSettings>

namespace
{
const char kStyle[] = "appearance/style";
const char kColorScheme[] = "appearance/colorScheme";
const char kLanguage[] = "appearance/language";
const char kSaveWindowLayout[] = "appearance/saveWindowLayout";
const char kHardwareDecoding[] = "preview/hardwareDecoding";
const char kPreferredDecoder[] = "preview/preferredDecoder";
const char kFfmpegPath[] = "export/ffmpegPath";
const char kExportFolder[] = "export/folder";
const char kMapDownloads[] = "maps/downloads";
const char kMapTileUrl[] = "maps/tileUrl";
const char kMapAttribution[] = "maps/attribution";
const char kMapProvider[] = "maps/provider";
const char kMapApiKey[] = "maps/apiKey";
const char kMapCreditShown[] = "maps/creditShown";
const char kLapsMapShown[] = "maps/lapsPanel";
const char kExportEncoder[] = "export/encoder";
const char kExportRateControl[] = "export/rateControl";
const char kExportQuality[] = "export/quality";
const char kExportSpeed[] = "export/speed";
const char kExportHeight[] = "export/height";
const char kExportAudio[] = "export/audio";
const char kPowerEstimated[] = "power/estimated";
const char kPowerRiderKg[] = "power/riderKg";
const char kPowerHeightCm[] = "power/heightCm";
const char kPowerBikeKg[] = "power/bikeKg";
const char kPowerTires[] = "power/tires";
const char kPowerCrr[] = "power/rollingResistance";
const char kPowerPosition[] = "power/position";
const char kPowerCda[] = "power/dragArea";
const char kPowerLoss[] = "power/drivetrainLoss";
const char kPowerWeather[] = "power/weather";
const char kPowerWeatherSpacing[] = "power/weatherSpacingKm";
const char kPowerWindShare[] = "power/windShare";
const char kSpeedUnit[] = "units/speed";
const char kLengthUnit[] = "units/length";
const char kTemperatureUnit[] = "units/temperature";

// Stored word for each enum value, in enum order. An unknown word (edited by
// hand, or written by a newer version) reads as the first, default value.
template <typename Enum, int N>
Enum readEnum(const char *key, const char *const (&words)[N])
{
    const QString stored = QSettings().value(key).toString();
    for (int i = 0; i < N; ++i)
        {
            if (stored == QLatin1String(words[i]))
                {
                    return Enum(i);
                }
        }
    return Enum(0);
}

const char *const kColorSchemeWords[] = {"system", "light", "dark"};
const char *const kDecoderWords[] = {"auto", "vaapi", "cuda", "qsv", "vdpau"};
const char *const kRateControlWords[] = {"quality", "bitrate"};
const char *const kExportSpeedWords[] = {"fast", "balanced", "small"};
const char *const kAudioWords[] = {"copy", "aac", "none"};
const char *const kSpeedWords[] = {"kmh", "mph"};
const char *const kLengthWords[] = {"metric", "imperial"};
const char *const kTemperatureWords[] = {"celsius", "fahrenheit"};
}

QString AppSettings::style()
{
    return QSettings().value(kStyle).toString();
}

void AppSettings::setStyle(const QString &style)
{
    QSettings().setValue(kStyle, style);
}

AppSettings::ColorScheme AppSettings::colorScheme()
{
    return readEnum<ColorScheme>(kColorScheme, kColorSchemeWords);
}

void AppSettings::setColorScheme(ColorScheme scheme)
{
    QSettings().setValue(kColorScheme, kColorSchemeWords[scheme]);
}

QString AppSettings::language()
{
    const QString language = QSettings().value(kLanguage).toString();
    return language.isEmpty() ? QStringLiteral("system") : language;
}

void AppSettings::setLanguage(const QString &language)
{
    QSettings().setValue(kLanguage, language);
}

bool AppSettings::saveWindowLayout()
{
    return QSettings().value(kSaveWindowLayout, true).toBool();
}

void AppSettings::setSaveWindowLayout(bool enabled)
{
    QSettings().setValue(kSaveWindowLayout, enabled);
}

bool AppSettings::hardwareDecoding()
{
    return QSettings().value(kHardwareDecoding, true).toBool();
}

void AppSettings::setHardwareDecoding(bool enabled)
{
    QSettings().setValue(kHardwareDecoding, enabled);
}

AppSettings::Decoder AppSettings::preferredDecoder()
{
    return readEnum<Decoder>(kPreferredDecoder, kDecoderWords);
}

void AppSettings::setPreferredDecoder(Decoder decoder)
{
    QSettings().setValue(kPreferredDecoder, kDecoderWords[decoder]);
}

QByteArray AppSettings::decodingDeviceTypes()
{
#ifdef Q_OS_LINUX
    const bool onLinux = true;
#else
    const bool onLinux = false;
#endif
    return decodingDeviceTypes(hardwareDecoding(), preferredDecoder(), onLinux);
}

QByteArray AppSettings::decodingDeviceTypes(bool hardwareDecoding, Decoder preferred, bool onLinux)
{
    if (!hardwareDecoding)
        {
            // a list without a usable name: Qt Multimedia then decodes in software
            return QByteArrayLiteral(",");
        }
    if (!onLinux)
        {
            // Windows and macOS each have one obvious decoder and Qt picks it
            return QByteArray();
        }
    // Qt tries these in its own fixed order and falls back to software as soon
    // as one that opens cannot take the stream. That order starts with CUDA,
    // which "opens" on any machine that merely has the NVIDIA libraries.
    // VA-API first works on Intel and AMD, and on NVIDIA through its VA-API driver.
    QList<QByteArray> order = {"vaapi", "vdpau", "cuda", "qsv"};
    if (preferred != AutoDecoder)
        {
            const QByteArray first = kDecoderWords[preferred];
            order.removeAll(first);
            order.prepend(first);
        }
    return order.join(',');
}

QString AppSettings::ffmpegPath()
{
    return QSettings().value(kFfmpegPath).toString();
}

void AppSettings::setFfmpegPath(const QString &path)
{
    QSettings().setValue(kFfmpegPath, path);
}

QString AppSettings::exportFolder()
{
    return QSettings().value(kExportFolder).toString();
}

void AppSettings::setExportFolder(const QString &folder)
{
    QSettings().setValue(kExportFolder, folder);
}

bool AppSettings::mapDownloads()
{
    return QSettings().value(kMapDownloads, true).toBool();
}

void AppSettings::setMapDownloads(bool allowed)
{
    QSettings().setValue(kMapDownloads, allowed);
}

QString AppSettings::mapProvider()
{
    // settings from before there was a choice: an address typed in was the user's own server
    const QString fallback = QSettings().value(kMapTileUrl).toString().trimmed().isEmpty() ? QString("osm") : QString(OwnMapServer);
    return QSettings().value(kMapProvider, fallback).toString();
}

void AppSettings::setMapProvider(const QString &id)
{
    QSettings().setValue(kMapProvider, id);
}

QString AppSettings::mapApiKey()
{
    return QSettings().value(kMapApiKey).toString().trimmed();
}

void AppSettings::setMapApiKey(const QString &key)
{
    QSettings().setValue(kMapApiKey, key.trimmed());
}

QString AppSettings::mapCredit()
{
    return mapProvider() == QLatin1String(OwnMapServer) ? mapAttribution() : TileStore::provider(mapProvider()).attribution;
}

bool AppSettings::lapsMapShown()
{
    return QSettings().value(kLapsMapShown, true).toBool();
}

void AppSettings::setLapsMapShown(bool shown)
{
    QSettings().setValue(kLapsMapShown, shown);
}

bool AppSettings::mapCreditShown()
{
    return QSettings().value(kMapCreditShown, true).toBool();
}

void AppSettings::setMapCreditShown(bool shown)
{
    QSettings().setValue(kMapCreditShown, shown);
}

QString AppSettings::mapTileUrl()
{
    return QSettings().value(kMapTileUrl).toString();
}

void AppSettings::setMapTileUrl(const QString &url)
{
    QSettings().setValue(kMapTileUrl, url);
}

QString AppSettings::mapAttribution()
{
    return QSettings().value(kMapAttribution).toString();
}

void AppSettings::setMapAttribution(const QString &text)
{
    QSettings().setValue(kMapAttribution, text);
}

bool AppSettings::hasExportDefaults()
{
    return QSettings().contains(kExportEncoder);
}

ExportSettings AppSettings::exportDefaults()
{
    const ExportSettings fallback;
    const QSettings stored;
    ExportSettings settings;
    settings.encoder = stored.value(kExportEncoder, fallback.encoder).toString();
    // these two defaults are not the first word of their list, so readEnum's
    // fallback would be wrong for a key that was never written
    settings.rateControl = stored.contains(kExportRateControl)
                           ? readEnum<ExportSettings::RateControl>(kExportRateControl, kRateControlWords) : fallback.rateControl;
    settings.speed = stored.contains(kExportSpeed)
                     ? readEnum<ExportSettings::Speed>(kExportSpeed, kExportSpeedWords) : fallback.speed;
    settings.quality = stored.value(kExportQuality, fallback.quality).toInt();
    settings.height = stored.value(kExportHeight, fallback.height).toInt();
    settings.audio = readEnum<ExportSettings::Audio>(kExportAudio, kAudioWords);
    return settings;
}

void AppSettings::setExportDefaults(const ExportSettings &settings)
{
    QSettings stored;
    stored.setValue(kExportEncoder, settings.encoder);
    stored.setValue(kExportRateControl, kRateControlWords[settings.rateControl]);
    stored.setValue(kExportQuality, settings.quality);
    stored.setValue(kExportSpeed, kExportSpeedWords[settings.speed]);
    stored.setValue(kExportHeight, settings.height);
    stored.setValue(kExportAudio, kAudioWords[settings.audio]);
}

PowerEstimate::Rider AppSettings::powerRider()
{
    const QSettings settings;
    const PowerEstimate::Rider usual;
    PowerEstimate::Rider rider;
    rider.enabled = settings.value(kPowerEstimated, usual.enabled).toBool();
    rider.riderKg = settings.value(kPowerRiderKg, usual.riderKg).toDouble();
    rider.heightCm = settings.value(kPowerHeightCm, usual.heightCm).toDouble();
    rider.bikeKg = settings.value(kPowerBikeKg, usual.bikeKg).toDouble();
    rider.tires = settings.value(kPowerTires, usual.tires).toString();
    rider.crr = settings.value(kPowerCrr, usual.crr).toDouble();
    rider.position = settings.value(kPowerPosition, usual.position).toString();
    rider.cda = settings.value(kPowerCda, usual.cda).toDouble();
    rider.drivetrainLoss = settings.value(kPowerLoss, usual.drivetrainLoss).toDouble();
    rider.weather = settings.value(kPowerWeather, usual.weather).toBool();
    rider.weatherSpacingKm = settings.value(kPowerWeatherSpacing, usual.weatherSpacingKm).toDouble();
    rider.windShare = settings.value(kPowerWindShare, usual.windShare).toDouble();
    // a word nobody knows is the usual kind, not a value of the user's own
    if (rider.tires != PowerEstimate::OwnValue && PowerEstimate::crrOf(rider.tires) <= 0.0)
        {
            rider.tires = usual.tires;
        }
    if (rider.position != PowerEstimate::OwnValue && !PowerEstimate::positions().contains(rider.position))
        {
            rider.position = usual.position;
        }
    return rider;
}

void AppSettings::setPowerRider(const PowerEstimate::Rider &rider)
{
    QSettings settings;
    settings.setValue(kPowerEstimated, rider.enabled);
    settings.setValue(kPowerRiderKg, rider.riderKg);
    settings.setValue(kPowerHeightCm, rider.heightCm);
    settings.setValue(kPowerBikeKg, rider.bikeKg);
    settings.setValue(kPowerTires, rider.tires);
    settings.setValue(kPowerCrr, rider.crr);
    settings.setValue(kPowerPosition, rider.position);
    settings.setValue(kPowerCda, rider.cda);
    settings.setValue(kPowerLoss, rider.drivetrainLoss);
    settings.setValue(kPowerWeather, rider.weather);
    settings.setValue(kPowerWeatherSpacing, rider.weatherSpacingKm);
    settings.setValue(kPowerWindShare, rider.windShare);
}

Units AppSettings::units()
{
    Units units;
    units.speed = readEnum<Units::Speed>(kSpeedUnit, kSpeedWords);
    units.length = readEnum<Units::Length>(kLengthUnit, kLengthWords);
    units.temperature = readEnum<Units::Temperature>(kTemperatureUnit, kTemperatureWords);
    return units;
}

void AppSettings::setUnits(const Units &units)
{
    QSettings settings;
    settings.setValue(kSpeedUnit, kSpeedWords[units.speed]);
    settings.setValue(kLengthUnit, kLengthWords[units.length]);
    settings.setValue(kTemperatureUnit, kTemperatureWords[units.temperature]);
}
