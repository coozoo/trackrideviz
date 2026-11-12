#include "appsettings.h"

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
const char kExportEncoder[] = "export/encoder";
const char kExportRateControl[] = "export/rateControl";
const char kExportQuality[] = "export/quality";
const char kExportSpeed[] = "export/speed";
const char kExportHeight[] = "export/height";
const char kExportAudio[] = "export/audio";
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
