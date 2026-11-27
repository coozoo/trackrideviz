#include "appsettings.h"

#include "../maps/tilestore.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>

#include <algorithm>
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
const char kVideoEngine[] = "export/engine";   // "own" or "program"
const char kExportAllCores[] = "export/allCores";
const char kExportFolder[] = "export/folder";
const char kMapDownloads[] = "maps/downloads";
const char kMapTileUrl[] = "maps/tileUrl";
const char kMapAttribution[] = "maps/attribution";
const char kMapProvider[] = "maps/provider";
// before keys were kept per service: the Thunderforest one, the only map that wanted a key
const char kMapApiKey[] = "maps/apiKey";
const char kMapKeys[] = "maps/keys/";
const char kMapCreditShown[] = "maps/creditShown";
const char kLapsMapShown[] = "maps/lapsPanel";
const char kExportEncoder[] = "export/encoder";
const char kCheckForUpdates[] = "updates/check";
const char kLastUpdateCheck[] = "updates/lastCheck";
const char kSkippedRelease[] = "updates/skipped";
const char kExportRateControl[] = "export/rateControl";
const char kExportQuality[] = "export/quality";
const char kExportSpeed[] = "export/speed";
const char kExportHeight[] = "export/height";
const char kExportAudio[] = "export/audio";
const char kExportAudioKbps[] = "export/audioKbps";
const char kExportProfiles[] = "export/profiles";
const char kExportProfile[] = "export/profile";
const char kExportFileType[] = "export/fileType";
const char kPowerEstimated[] = "power/estimated";
const char kPowerRiderKg[] = "power/riderKg";
const char kPowerHeightCm[] = "power/heightCm";
const char kPowerBikeKg[] = "power/bikeKg";
const char kPowerTires[] = "power/tires";
const char kPowerCrr[] = "power/rollingResistance";
const char kPowerPosition[] = "power/position";
const char kPowerCda[] = "power/dragArea";
const char kPowerLoss[] = "power/drivetrainLoss";
const char kSurfaces[] = "surfaces";
const char kChainrings[] = "bike/chainrings";
const char kCassette[] = "bike/cassette";
const char kTireSize[] = "bike/tireSize";
const char kWheelMm[] = "bike/wheelMm";
const char kWeatherFetched[] = "weather/fetched";
const char kWeatherSpacing[] = "weather/spacingKm";
const char kWeatherProvider[] = "weather/provider";
const char kWeatherKeys[] = "weather/apiKey/";
const char kWeatherWindShare[] = "weather/windShare";
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

namespace
{
const char *recentKey(AppSettings::RecentKind kind)
{
    switch (kind)
        {
        case AppSettings::RecentVideos:
            return "recent/videos";
        case AppSettings::RecentData:
            return "recent/data";
        case AppSettings::RecentProjects:
            return "recent/projects";
        }
    return "recent/videos";
}
}

QStringList AppSettings::recentFiles(RecentKind kind)
{
    return QSettings().value(recentKey(kind)).toStringList().mid(0, recentFileCount());
}

void AppSettings::addRecentFile(RecentKind kind, const QString &path)
{
    if (path.isEmpty() || recentFileCount() == 0)
        {
            return;
        }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QStringList files = QSettings().value(recentKey(kind)).toStringList();
    // once, at the top; the same file under another spelling of its path too
    files.erase(std::remove_if(files.begin(), files.end(), [&](const QString &file)
    {
        // QFileInfo counts any two files that are not there as the same
        const QFileInfo info(file);
        return info.exists() ? info == QFileInfo(absolute) : info.absoluteFilePath() == absolute;
    }), files.end());
    files.prepend(absolute);
    QSettings().setValue(recentKey(kind), files.mid(0, recentFileCount()));
}

void AppSettings::clearRecentFiles(RecentKind kind)
{
    QSettings().remove(recentKey(kind));
}

bool AppSettings::checkForUpdates()
{
    return QSettings().value(kCheckForUpdates, true).toBool();
}

void AppSettings::setCheckForUpdates(bool check)
{
    QSettings().setValue(kCheckForUpdates, check);
}

QDateTime AppSettings::lastUpdateCheck()
{
    return QSettings().value(kLastUpdateCheck).toDateTime();
}

void AppSettings::setLastUpdateCheck(const QDateTime &when)
{
    QSettings().setValue(kLastUpdateCheck, when);
}

QString AppSettings::skippedRelease()
{
    return QSettings().value(kSkippedRelease).toString();
}

void AppSettings::setSkippedRelease(const QString &tag)
{
    QSettings().setValue(kSkippedRelease, tag);
}

int AppSettings::recentFileCount()
{
    return std::clamp(QSettings().value("recent/count", 10).toInt(), 0, kMaxRecentFiles);
}

void AppSettings::setRecentFileCount(int count)
{
    count = std::clamp(count, 0, kMaxRecentFiles);
    QSettings settings;
    settings.setValue("recent/count", count);
    // the lists no longer than that at once
    for (const RecentKind kind : {RecentVideos, RecentData, RecentProjects})
        {
            const QStringList files = settings.value(recentKey(kind)).toStringList();
            if (files.size() > count)
                {
                    settings.setValue(recentKey(kind), files.mid(0, count));
                }
        }
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

bool AppSettings::ownVideoEngine()
{
    return QSettings().value(kVideoEngine, "own").toString() != QLatin1String("program");
}

void AppSettings::setOwnVideoEngine(bool own)
{
    QSettings().setValue(kVideoEngine, own ? "own" : "program");
}

bool AppSettings::exportAllCores()
{
    return QSettings().value(kExportAllCores, false).toBool();
}

void AppSettings::setExportAllCores(bool all)
{
    QSettings().setValue(kExportAllCores, all);
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

QString AppSettings::mapApiKey(const QString &service)
{
    QSettings settings;
    const QString fallback = service == QLatin1String("thunderforest") ? settings.value(kMapApiKey).toString() : QString();
    return settings.value(kMapKeys + service, fallback).toString().trimmed();
}

void AppSettings::setMapApiKey(const QString &service, const QString &key)
{
    QSettings settings;
    settings.setValue(kMapKeys + service, key.trimmed());
    if (service == QLatin1String("thunderforest"))
        {
            settings.remove(kMapApiKey);
        }
}

QHash<QString, QString> AppSettings::mapApiKeys()
{
    QHash<QString, QString> keys;
    for (const TileStore::KeyService &service : TileStore::keyServices())
        {
            keys.insert(service.id, mapApiKey(service.id));
        }
    return keys;
}

QVector<TileStore::Provider> AppSettings::usableMapProviders()
{
    return TileStore::usableProviders(mapApiKeys());
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
                           ? readEnum<ExportSettings::RateControl>(kExportRateControl, ExportSettings::RateControlWords) : fallback.rateControl;
    settings.speed = stored.contains(kExportSpeed)
                     ? readEnum<ExportSettings::Speed>(kExportSpeed, ExportSettings::SpeedWords) : fallback.speed;
    settings.quality = stored.value(kExportQuality, fallback.quality).toInt();
    settings.height = stored.value(kExportHeight, fallback.height).toInt();
    settings.audio = readEnum<ExportSettings::Audio>(kExportAudio, ExportSettings::AudioWords);
    settings.audioKbps = stored.value(kExportAudioKbps, fallback.audioKbps).toInt();
    return settings;
}

void AppSettings::setExportDefaults(const ExportSettings &settings)
{
    QSettings stored;
    stored.setValue(kExportEncoder, settings.encoder);
    stored.setValue(kExportRateControl, ExportSettings::RateControlWords[settings.rateControl]);
    stored.setValue(kExportQuality, settings.quality);
    stored.setValue(kExportSpeed, ExportSettings::SpeedWords[settings.speed]);
    stored.setValue(kExportHeight, settings.height);
    stored.setValue(kExportAudio, ExportSettings::AudioWords[settings.audio]);
    stored.setValue(kExportAudioKbps, settings.audioKbps);
    // only the types offered: one typed by hand may be one ffmpeg does not know, and would
    // then be what every later export starts with
    const QString fileType = QFileInfo(settings.outputPath).suffix().toLower();
    if (fileType == QLatin1String("mp4") || fileType == QLatin1String("mkv") || fileType == QLatin1String("mov"))
        {
            stored.setValue(kExportFileType, fileType);
        }
}

QString AppSettings::exportFileType()
{
    return QSettings().value(kExportFileType).toString();
}

QVector<ExportProfile> AppSettings::exportProfiles()
{
    QVector<ExportProfile> profiles;
    const QJsonArray stored = QJsonDocument::fromJson(QSettings().value(kExportProfiles).toByteArray()).array();
    for (const QJsonValue &value : stored)
        {
            const ExportProfile profile = ExportProfile::fromJson(value.toObject());
            if (!profile.name.isEmpty())
                {
                    profiles.append(profile);
                }
        }
    return profiles;
}

void AppSettings::setExportProfiles(const QVector<ExportProfile> &profiles)
{
    QJsonArray stored;
    for (const ExportProfile &profile : profiles)
        {
            stored.append(profile.toJson());
        }
    QSettings().setValue(kExportProfiles, QJsonDocument(stored).toJson(QJsonDocument::Compact));
}

QString AppSettings::exportProfile()
{
    return QSettings().value(kExportProfile).toString();
}

void AppSettings::setExportProfile(const QString &id)
{
    QSettings().setValue(kExportProfile, id);
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
}

Drivetrain AppSettings::drivetrain()
{
    const QSettings settings;
    Drivetrain drivetrain;
    drivetrain.chainrings = Gears::parseTeeth(settings.value(kChainrings).toString());
    drivetrain.cassette = Gears::parseTeeth(settings.value(kCassette).toString());
    drivetrain.tireSize = settings.value(kTireSize).toString();
    drivetrain.wheelMm = std::max(0.0, settings.value(kWheelMm, 0.0).toDouble());
    return drivetrain;
}

void AppSettings::setDrivetrain(const Drivetrain &drivetrain)
{
    QSettings settings;
    settings.setValue(kChainrings, Gears::teethText(drivetrain.chainrings));
    settings.setValue(kCassette, Gears::teethText(drivetrain.cassette));
    settings.setValue(kTireSize, drivetrain.tireSize.trimmed());
    settings.setValue(kWheelMm, drivetrain.wheelMm);
}

QVector<SurfaceType> AppSettings::surfaceTypes()
{
    QSettings settings;
    QVector<SurfaceType> types;
    const int count = settings.beginReadArray(kSurfaces);
    for (int i = 0; i < count; ++i)
        {
            settings.setArrayIndex(i);
            const SurfaceType type{settings.value("name").toString().trimmed(), settings.value("factor").toDouble()};
            if (!type.name.isEmpty() && type.factor > 0.0)
                {
                    types.append(type);
                }
        }
    settings.endArray();
    return types.isEmpty() ? Surfaces::usual() : types;
}

void AppSettings::setSurfaceTypes(const QVector<SurfaceType> &types)
{
    QSettings settings;
    settings.remove(kSurfaces);
    // the usual ones are not written: they follow the language and later versions
    if (types == Surfaces::usual())
        {
            return;
        }
    settings.beginWriteArray(kSurfaces, int(types.size()));
    for (int i = 0; i < types.size(); ++i)
        {
            settings.setArrayIndex(i);
            settings.setValue("name", types.at(i).name);
            settings.setValue("factor", types.at(i).factor);
        }
    settings.endArray();
}

WeatherChoice AppSettings::weather()
{
    const QSettings settings;
    const WeatherChoice usual;
    WeatherChoice choice;
    choice.enabled = settings.value(kWeatherFetched, usual.enabled).toBool();
    choice.spacingKm = settings.value(kWeatherSpacing, usual.spacingKm).toDouble();
    choice.windShare = settings.value(kWeatherWindShare, usual.windShare).toDouble();
    choice.provider = WeatherStore::provider(settings.value(kWeatherProvider, usual.provider).toString()).id;
    choice.apiKey = weatherApiKey(choice.provider);
    return choice;
}

void AppSettings::setWeather(const WeatherChoice &choice)
{
    QSettings settings;
    settings.setValue(kWeatherFetched, choice.enabled);
    settings.setValue(kWeatherSpacing, choice.spacingKm);
    settings.setValue(kWeatherWindShare, choice.windShare);
    settings.setValue(kWeatherProvider, choice.provider);
    setWeatherApiKey(choice.provider, choice.apiKey);
}

QString AppSettings::weatherApiKey(const QString &provider)
{
    return QSettings().value(kWeatherKeys + provider).toString();
}

void AppSettings::setWeatherApiKey(const QString &provider, const QString &key)
{
    QSettings().setValue(kWeatherKeys + provider, key.trimmed());
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

void AppSettings::applyMapSources()
{
    QString address = mapTileUrl();
    QString credit = mapAttribution();
    int maxZoom = 19;
    const QHash<QString, QString> keys = mapApiKeys();
    if (mapProvider() != QLatin1String(OwnMapServer))
        {
            TileStore::Provider provider = TileStore::provider(mapProvider());
            if (!provider.service.isEmpty() && keys.value(provider.service).isEmpty())
                {
                    // its key was taken away
                    provider = TileStore::providers().first();
                }
            address = provider.urlTemplate;
            address.replace("{apikey}", keys.value(provider.service));
            credit = provider.attribution;
            maxZoom = provider.maxZoom;
        }
    TileStore::instance()->setSource(address, mapCreditShown() ? credit : QString(), maxZoom);
    TileStore::instance()->setDownloads(mapDownloads());
    // gauges with a map of their own
    TileStore::setMapOptions(mapDownloads(), mapCreditShown(), keys);
    TileStore::terrain()->setDownloads(mapDownloads());
}
