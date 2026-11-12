#include "settings/appsettings.h"
#include "telemetry/channelids.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestSettings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private slots:
    // Keep the developer's real configuration out of it.
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName("trackrideviz-test");
        QCoreApplication::setApplicationName("tst_settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void init()
    {
        QSettings().clear();
    }

    void defaults()
    {
        QCOMPARE(AppSettings::style(), QString());
        QCOMPARE(AppSettings::colorScheme(), AppSettings::SystemColors);
        QCOMPARE(AppSettings::language(), QString("system"));
        QVERIFY(AppSettings::saveWindowLayout());
        QVERIFY(AppSettings::hardwareDecoding());
        QCOMPARE(AppSettings::preferredDecoder(), AppSettings::AutoDecoder);
        QCOMPARE(AppSettings::units(), Units());
    }

    void roundTrip()
    {
        AppSettings::setStyle("Fusion");
        AppSettings::setColorScheme(AppSettings::Dark);
        AppSettings::setLanguage("uk_UA");
        AppSettings::setSaveWindowLayout(false);
        AppSettings::setHardwareDecoding(false);
        AppSettings::setPreferredDecoder(AppSettings::Cuda);
        Units units;
        units.speed = Units::MilesPerHour;
        units.length = Units::Imperial;
        units.temperature = Units::Fahrenheit;
        AppSettings::setUnits(units);

        QCOMPARE(AppSettings::style(), QString("Fusion"));
        QCOMPARE(AppSettings::colorScheme(), AppSettings::Dark);
        QCOMPARE(AppSettings::language(), QString("uk_UA"));
        QVERIFY(!AppSettings::saveWindowLayout());
        QVERIFY(!AppSettings::hardwareDecoding());
        QCOMPARE(AppSettings::preferredDecoder(), AppSettings::Cuda);
        QCOMPARE(AppSettings::units(), units);
    }

    void storedAsWords()
    {
        AppSettings::setColorScheme(AppSettings::Dark);
        AppSettings::setPreferredDecoder(AppSettings::VaApi);
        QCOMPARE(QSettings().value("appearance/colorScheme").toString(), QString("dark"));
        QCOMPARE(QSettings().value("preview/preferredDecoder").toString(), QString("vaapi"));
    }

    void unknownWordsFallBackToDefaults()
    {
        QSettings().setValue("appearance/colorScheme", "sepia");
        QSettings().setValue("preview/preferredDecoder", 7);
        QSettings().setValue("units/speed", "knots");
        QCOMPARE(AppSettings::colorScheme(), AppSettings::SystemColors);
        QCOMPARE(AppSettings::preferredDecoder(), AppSettings::AutoDecoder);
        QCOMPARE(AppSettings::units().speed, Units::KilometresPerHour);
    }

    void decodingDeviceTypes()
    {
        // off: software everywhere
        QCOMPARE(AppSettings::decodingDeviceTypes(false, AppSettings::AutoDecoder, true), QByteArray(","));
        QCOMPARE(AppSettings::decodingDeviceTypes(false, AppSettings::Cuda, false), QByteArray(","));
        // outside Linux Qt's own choice stands
        QVERIFY(AppSettings::decodingDeviceTypes(true, AppSettings::AutoDecoder, false).isNull());
        QVERIFY(AppSettings::decodingDeviceTypes(true, AppSettings::VaApi, false).isNull());
        // Linux: VA-API before CUDA unless asked otherwise
        QCOMPARE(AppSettings::decodingDeviceTypes(true, AppSettings::AutoDecoder, true), QByteArray("vaapi,vdpau,cuda,qsv"));
        QCOMPARE(AppSettings::decodingDeviceTypes(true, AppSettings::Cuda, true), QByteArray("cuda,vaapi,vdpau,qsv"));
        QCOMPARE(AppSettings::decodingDeviceTypes(true, AppSettings::Qsv, true), QByteArray("qsv,vaapi,vdpau,cuda"));
        QCOMPARE(AppSettings::decodingDeviceTypes(true, AppSettings::VaApi, true), QByteArray("vaapi,vdpau,cuda,qsv"));
    }

    void exportDefaults()
    {
        QVERIFY(!AppSettings::hasExportDefaults());
        ExportSettings settings = AppSettings::exportDefaults();
        QCOMPARE(settings.encoder, QString("libx264"));
        QCOMPARE(settings.rateControl, ExportSettings::Bitrate);
        QCOMPARE(settings.speed, ExportSettings::Balanced);
        QCOMPARE(settings.audio, ExportSettings::CopyAudio);
        QCOMPARE(settings.height, 0);

        settings.encoder = "hevc_vaapi";
        settings.rateControl = ExportSettings::Quality;
        settings.quality = 28;
        settings.speed = ExportSettings::Fast;
        settings.height = 720;
        settings.audio = ExportSettings::NoAudio;
        // these belong to one video and one export
        settings.outputPath = "/somewhere/out.mp4";
        settings.start = 12.0;
        settings.duration = 30.0;
        settings.bitrateKbps = 1234;
        AppSettings::setExportDefaults(settings);

        QVERIFY(AppSettings::hasExportDefaults());
        const ExportSettings loaded = AppSettings::exportDefaults();
        QCOMPARE(loaded.encoder, QString("hevc_vaapi"));
        QCOMPARE(loaded.rateControl, ExportSettings::Quality);
        QCOMPARE(loaded.quality, 28);
        QCOMPARE(loaded.speed, ExportSettings::Fast);
        QCOMPARE(loaded.height, 720);
        QCOMPARE(loaded.audio, ExportSettings::NoAudio);
        QVERIFY(loaded.outputPath.isEmpty());
        QCOMPARE(loaded.start, 0.0);
        QCOMPARE(loaded.duration, 0.0);
        QCOMPARE(loaded.bitrateKbps, ExportSettings().bitrateKbps);
    }

    void exportToolSettings()
    {
        QCOMPARE(AppSettings::ffmpegPath(), QString());
        QCOMPARE(AppSettings::exportFolder(), QString());
        AppSettings::setFfmpegPath("/opt/ffmpeg/bin/ffmpeg");
        AppSettings::setExportFolder("/videos/out");
        QCOMPARE(AppSettings::ffmpegPath(), QString("/opt/ffmpeg/bin/ffmpeg"));
        QCOMPARE(AppSettings::exportFolder(), QString("/videos/out"));
    }

    void metricDisplay()
    {
        const Units units;
        const ChannelDisplay speed = ChannelFormat::displayFor(ChannelId::Speed, "m/s", units);
        QCOMPARE(speed.convert(10.0), 36.0);
        QCOMPARE(speed.unit, QString("km/h"));
        QCOMPARE(ChannelFormat::displayFor(ChannelId::Distance, "m", units).convert(1500.0), 1.5);
        QCOMPARE(ChannelFormat::displayFor(ChannelId::Elevation, "m", units).convert(250.0), 250.0);
        QCOMPARE(ChannelFormat::displayFor(ChannelId::Temperature, "C", units).convert(20.0), 20.0);
    }

    void imperialDisplay()
    {
        Units units;
        units.speed = Units::MilesPerHour;
        units.length = Units::Imperial;
        units.temperature = Units::Fahrenheit;
        const ChannelDisplay speed = ChannelFormat::displayFor(ChannelId::Speed, "m/s", units);
        QCOMPARE(speed.unit, QString("mph"));
        QVERIFY(qAbs(speed.convert(44.704) - 100.0) < 1e-6);
        QVERIFY(qAbs(ChannelFormat::displayFor(ChannelId::Distance, "m", units).convert(1609.344) - 1.0) < 1e-9);
        QVERIFY(qAbs(ChannelFormat::displayFor(ChannelId::Elevation, "m", units).convert(304.8) - 1000.0) < 1e-6);
        const ChannelDisplay temperature = ChannelFormat::displayFor(ChannelId::Temperature, "C", units);
        QCOMPARE(temperature.convert(0.0), 32.0);
        QCOMPARE(temperature.convert(100.0), 212.0);
    }

    // speed and length are separate choices (UK: mph with metres)
    void unitsAreIndependent()
    {
        Units units;
        units.speed = Units::MilesPerHour;
        QCOMPARE(ChannelFormat::displayFor(ChannelId::Speed, "m/s", units).unit, QString("mph"));
        QCOMPARE(ChannelFormat::displayFor(ChannelId::Elevation, "m", units).unit, QString("m"));
    }

    void unknownNamesAreMadeReadable()
    {
        QCOMPARE(ChannelFormat::displayFor("relativeElevation", QString(), Units()).name, QString("Relative elevation"));
        QCOMPARE(ChannelFormat::displayFor("power3secAverage", QString(), Units()).name, QString("Power3sec average"));
        QCOMPARE(ChannelFormat::displayFor("engineRpm", QString(), Units()).name, QString("Engine rpm"));
        QCOMPARE(ChannelFormat::displayFor("gForceLatLon", QString(), Units()).name, QString("G-force"));
        QCOMPARE(ChannelFormat::displayFor("dev.Form Power", "W", Units()).name, QString("dev.Form Power"));
    }

    void unknownChannelKeepsItsIdAndValue()
    {
        const ChannelDisplay display = ChannelFormat::displayFor("fit.record.87", "raw", Units());
        QCOMPARE(display.name, QString("fit.record.87"));
        QCOMPARE(display.unit, QString("raw"));
        QCOMPARE(display.convert(12.5), 12.5);
    }
};

QTEST_APPLESS_MAIN(TestSettings)
#include "test_settings.moc"
