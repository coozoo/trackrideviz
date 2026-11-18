#include "settings/appsettings.h"
#include "telemetry/channelids.h"
#include "update/releasecheck.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
        QCOMPARE(AppSettings::units().speed, Units::KilometersPerHour);
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
        QVERIFY(AppSettings::exportFileType().isEmpty());
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
        settings.audio = ExportSettings::OpusAudio;
        settings.audioKbps = 128;
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
        QCOMPARE(loaded.audio, ExportSettings::OpusAudio);
        QCOMPARE(loaded.audioKbps, 128);
        // the file's type is kept, not its name; one not offered is not
        QCOMPARE(AppSettings::exportFileType(), QString("mp4"));
        settings.outputPath = "/somewhere/out.v2";
        AppSettings::setExportDefaults(settings);
        QCOMPARE(AppSettings::exportFileType(), QString("mp4"));
        QVERIFY(loaded.outputPath.isEmpty());
        QCOMPARE(loaded.start, 0.0);
        QCOMPARE(loaded.duration, 0.0);
        QCOMPARE(loaded.bitrateKbps, ExportSettings().bitrateKbps);
    }

    void exportProfiles()
    {
        QVERIFY(AppSettings::exportProfiles().isEmpty());
        ExportSettings settings;
        settings.outputPath = "/v/out.mkv";
        settings.audio = ExportSettings::OpusAudio;
        AppSettings::setExportProfiles({ExportProfile::of("Mine", settings)});
        AppSettings::setExportProfile("youtube");
        const QVector<ExportProfile> loaded = AppSettings::exportProfiles();
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().name, QString("Mine"));
        QCOMPARE(loaded.first().audio, ExportSettings::OpusAudio);
        QCOMPARE(loaded.first().suffix, QString("mkv"));
        QCOMPARE(AppSettings::exportProfile(), QString("youtube"));
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

    void mapKeysPerService()
    {
        // the one key of before was Thunderforest's: it stays theirs
        QSettings().setValue("maps/apiKey", "old");
        QCOMPARE(AppSettings::mapApiKey("thunderforest"), QString("old"));
        QCOMPARE(AppSettings::mapApiKey("maptiler"), QString());
        AppSettings::setMapApiKey("maptiler", "  mt  ");
        AppSettings::setMapApiKey("thunderforest", "new");
        QCOMPARE(AppSettings::mapApiKey("maptiler"), QString("mt"));
        QCOMPARE(AppSettings::mapApiKey("thunderforest"), QString("new"));
        QVERIFY(!QSettings().contains("maps/apiKey"));
        QStringList ids;
        for (const TileStore::Provider &provider : AppSettings::usableMapProviders())
            ids << provider.id;
        QVERIFY(ids.contains("maptiler-outdoor") && ids.contains("opencyclemap") && !ids.contains("mapbox-outdoors"));
        // a chosen map whose key is taken away: the first map is drawn
        AppSettings::setMapProvider("mapbox-outdoors");
        AppSettings::applyMapSources();
        QCOMPARE(TileStore::instance()->urlTemplate(), TileStore::providers().first().urlTemplate);
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

    void recentFiles()
    {
        QCOMPARE(AppSettings::recentFileCount(), 10);
        QVERIFY(AppSettings::recentFiles(AppSettings::RecentVideos).isEmpty());
        for (int i = 0; i < 12; ++i)
            {
                AppSettings::addRecentFile(AppSettings::RecentVideos, QString("/rides/%1.mp4").arg(i));
            }
        QStringList videos = AppSettings::recentFiles(AppSettings::RecentVideos);
        // the last first, ten of them
        QCOMPARE(videos.size(), 10);
        QCOMPARE(videos.first(), QString("/rides/11.mp4"));
        // opened again: to the top, once
        AppSettings::addRecentFile(AppSettings::RecentVideos, "/rides/5.mp4");
        videos = AppSettings::recentFiles(AppSettings::RecentVideos);
        QCOMPARE(videos.first(), QString("/rides/5.mp4"));
        QCOMPARE(videos.count("/rides/5.mp4"), 1);
        // each kind its own list
        AppSettings::addRecentFile(AppSettings::RecentProjects, "/rides/day.rtvproj");
        QCOMPARE(AppSettings::recentFiles(AppSettings::RecentProjects), QStringList{"/rides/day.rtvproj"});
        QVERIFY(AppSettings::recentFiles(AppSettings::RecentData).isEmpty());
        // fewer kept: the lists shorten at once; none: nothing is added
        AppSettings::setRecentFileCount(3);
        QCOMPARE(AppSettings::recentFiles(AppSettings::RecentVideos).size(), 3);
        AppSettings::setRecentFileCount(0);
        AppSettings::addRecentFile(AppSettings::RecentData, "/rides/a.fit");
        QVERIFY(AppSettings::recentFiles(AppSettings::RecentData).isEmpty());
        AppSettings::setRecentFileCount(10);
        AppSettings::clearRecentFiles(AppSettings::RecentVideos);
        QVERIFY(AppSettings::recentFiles(AppSettings::RecentVideos).isEmpty());
    }

    void paceReadsAsMinutesAndSeconds()
    {
        const Units metric;
        const ChannelDisplay pace = ChannelFormat::displayFor(ChannelId::Pace, "s/km", metric);
        QVERIFY(pace.minutesSeconds);
        QCOMPARE(pace.unit, QString("/km"));
        QCOMPARE(pace.text(pace.convert(318.4)), QString("5:18"));
        QCOMPARE(pace.text(pace.convert(59.6)), QString("1:00"));
        Units imperial;
        imperial.length = Units::Imperial;
        const ChannelDisplay perMile = ChannelFormat::displayFor(ChannelId::Pace, "s/km", imperial);
        QCOMPARE(perMile.unit, QString("/mi"));
        QCOMPARE(perMile.text(perMile.convert(300.0)), QString("8:03"));
        // anything else as a number
        QCOMPARE(ChannelFormat::displayFor(ChannelId::HeartRate, "bpm", metric).text(141.0), QLocale().toString(141.0, 'f', 0));
        // a run's cadence in steps
        QCOMPARE(ChannelFormat::displayFor(ChannelId::Cadence, "spm", metric).unit, QString("spm"));
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

    // speed and length are separate choices (UK: mph with meters)
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
        QCOMPARE(ChannelFormat::displayFor("power10secAverage", QString(), Units()).name, QString("Power10sec average"));
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

    void releaseTags()
    {
        QVersionNumber version;
        QString built;
        QVERIFY(ReleaseCheck::parseTag("0.01-202611171234_refs/heads/main", &version, &built));
        QCOMPARE(version, QVersionNumber(0, 1));
        QCOMPARE(built, QString("202611171234"));
        QVERIFY(!ReleaseCheck::parseTag("nightly", &version, &built));
        // built later, or a later version, is newer; an earlier one, a foreign tag, the same one are not
        QVERIFY(ReleaseCheck::isNewer("0.01-202611181000_refs/heads/main", "0.01-202611171234_refs/heads/main"));
        QVERIFY(ReleaseCheck::isNewer("0.2-202601010000", "0.01-202611171234"));
        QVERIFY(!ReleaseCheck::isNewer("0.01-202611101234", "0.01-202611171234"));
        QVERIFY(!ReleaseCheck::isNewer("0.01-202611171234_refs/heads/main", "0.01-202611171234_refs/heads/main"));
        QVERIFY(!ReleaseCheck::isNewer("latest", "0.01-202611171234"));
        // a build that is no release never takes one for newer
        QVERIFY(!ReleaseCheck::isNewer("0.01-202611181000", QString()));
    }

    void releaseFromGitHub()
    {
        const auto asset = [](const QString &name, int size, const QString &digest = QString())
        {
            QJsonObject file{{"name", name}, {"size", size}, {"browser_download_url", "https://example.org/" + name}};
            if (!digest.isEmpty())
                {
                    file.insert("digest", digest);
                }
            return file;
        };
        const QJsonObject answer
        {
            {"tag_name", "0.02-202612011200_refs/heads/main"},
            {"name", "Release CI 0.02-202612011200 refs/heads/main"},
            {"html_url", "https://github.com/coozoo/trackrideviz/releases/tag/x"},
            {"draft", false},
            {"body", "* Lap table (abc1234)"},
            {"assets", QJsonArray{asset("trackrideviz_0.02-202612011205_WIN64_setup.exe", 123, "sha256:ABCDEF"),
                                  asset("trackrideviz_0.02-202612011205_WIN64.zip", 456),
                                  asset("Race_Track_Visualizer-x86_64.AppImage", 789)}},
        };
        const QByteArray json = QJsonDocument(answer).toJson();
        const ReleaseCheck::Release release = ReleaseCheck::parse(json);
        QVERIFY(release.isValid());
        QCOMPARE(release.version, QString("0.2"));
        QCOMPARE(release.built, QString("202612011200"));
        QCOMPARE(release.notes, QString("* Lap table (abc1234)"));
        QCOMPARE(release.assets.size(), 3);
        // the files told apart by how their names end
        const ReleaseCheck::Asset *setup = release.assetEndingWith("_WIN64_setup.exe");
        QVERIFY(setup);
        QCOMPARE(setup->sha256, QByteArray("abcdef"));
        QCOMPARE(release.assetEndingWith("_WIN64.zip")->size, qint64(456));
        QVERIFY(release.assetEndingWith("-x86_64.AppImage"));
        QVERIFY(!release.assetEndingWith(".dmg"));
        // a draft, or an answer that is no release, is none
        QJsonObject draft = answer;
        draft.insert("draft", true);
        QVERIFY(!ReleaseCheck::parse(QJsonDocument(draft).toJson()).isValid());
        QVERIFY(!ReleaseCheck::parse("{\"message\": \"Not Found\"}").isValid());
    }
};

QTEST_APPLESS_MAIN(TestSettings)
#include "test_settings.moc"
