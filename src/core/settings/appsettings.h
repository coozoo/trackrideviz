#pragma once

#include "../units/units.h"

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

    // --- Units ------------------------------------------------------------
    static Units units();
    static void setUnits(const Units &units);
};
