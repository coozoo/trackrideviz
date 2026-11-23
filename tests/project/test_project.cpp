#include "gauge/builtingauges.h"
#include "overlay/overlay.h"
#include "project/project.h"
#include "telemetry/channelids.h"
#include "telemetry/telemetrysession.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

class TestProject : public QObject
{
    Q_OBJECT

private:
    static OverlayItem item(double x, double y, double height)
    {
        OverlayItem result;
        result.gaugeId = BuiltinGauges::DialId;
        result.x = x;
        result.y = y;
        result.height = height;
        return result;
    }

private slots:
    void itemRectFollowsTheFrame()
    {
        const Overlay overlay({item(0.25, 0.5, 0.25)}, nullptr, Units(), GaugeTheme());
        QCOMPARE(overlay.count(), 1);
        // the speedometer is square: the width equals the height
        QCOMPARE(overlay.itemRect(0, QRectF(0, 0, 1920, 1080)), QRectF(480, 540, 270, 270));
        QCOMPARE(overlay.itemRect(0, QRectF(0, 0, 1280, 720)), QRectF(320, 360, 180, 180));
        // a letterboxed preview: the frame does not start at the origin
        QCOMPARE(overlay.itemRect(0, QRectF(100, 50, 640, 360)), QRectF(260, 230, 90, 90));
    }

    // An item whose gauge is gone keeps its place in the list, so indexes
    // still match the project, but nothing is drawn or piped for it.
    void unknownGaugesDrawNothing()
    {
        OverlayItem unknown = item(0, 0, 0.5);
        unknown.gaugeId = "builtin:does-not-exist";
        const Overlay overlay({unknown, item(0.5, 0.5, 0.5)}, nullptr, Units(), GaugeTheme());
        QCOMPARE(overlay.count(), 2);
        QVERIFY(!overlay.hasGauge(0));
        QVERIFY(overlay.hasGauge(1));
        QVERIFY(!overlay.isEmpty());
        QCOMPARE(overlay.boundingRect(QSize(1000, 500)), QRect(500, 250, 250, 250));

        const Overlay nothing({unknown}, nullptr, Units(), GaugeTheme());
        QVERIFY(nothing.isEmpty());
        QVERIFY(nothing.boundingRect(QSize(1920, 1080)).isEmpty());
    }

    void withGeometryMovesOneItem()
    {
        const Overlay overlay({item(0.0, 0.0, 0.5), item(0.5, 0.5, 0.25)}, nullptr, Units(), GaugeTheme());
        const Overlay moved = overlay.withGeometry(1, 0.1, 0.2, 0.5, 0.0);
        const QRectF frame(0, 0, 1000, 1000);
        QCOMPARE(moved.itemRect(1, frame), QRectF(100, 200, 500, 500));
        QCOMPARE(moved.item(1).gaugeId, overlay.item(1).gaugeId);
        QCOMPARE(moved.itemRect(0, frame), overlay.itemRect(0, frame));
        // the original is untouched
        QCOMPARE(overlay.itemRect(1, frame), QRectF(500, 500, 250, 250));
        // set free of its shape: three times as wide as high
        QCOMPARE(overlay.withGeometry(1, 0.1, 0.2, 0.2, 3.0).itemRect(1, frame), QRectF(100, 200, 600, 200));
    }

    void itemAtFindsTheTopmost()
    {
        const Overlay overlay({item(0.0, 0.0, 0.5), item(0.1, 0.1, 0.5)}, nullptr, Units(), GaugeTheme());
        const QRectF frame(0, 0, 1000, 1000);
        QCOMPARE(overlay.itemAt(QPointF(50, 50), frame), 0);
        QCOMPARE(overlay.itemAt(QPointF(300, 300), frame), 1); // both are there; the later one is on top
        QCOMPARE(overlay.itemAt(QPointF(550, 550), frame), 1);
        QCOMPARE(overlay.itemAt(QPointF(900, 900), frame), -1);
    }

    void boundingRectIsEvenAndInsideTheFrame()
    {
        // 0.3337 * 1080 = 360.4 high, at an odd position
        const Overlay overlay({item(0.0105, 0.6267, 0.3337)}, nullptr, Units(), GaugeTheme());
        const QRect rect = overlay.boundingRect(QSize(1920, 1080));
        QVERIFY(!rect.isEmpty());
        QCOMPARE(rect.x() % 2, 0);
        QCOMPARE(rect.y() % 2, 0);
        QCOMPARE(rect.width() % 2, 0);
        QCOMPARE(rect.height() % 2, 0);
        QVERIFY(QRect(0, 0, 1920, 1080).contains(rect));
        QVERIFY(QRectF(rect).contains(overlay.itemRect(0, QRectF(0, 0, 1920, 1080))));
    }

    void regionsKeepDistantGaugesApart()
    {
        // two in opposite corners, two overlapping in the middle
        const Overlay overlay({item(0.0, 0.0, 0.2), item(0.9, 0.8, 0.2), item(0.4, 0.4, 0.2), item(0.45, 0.45, 0.2)},
                              nullptr, Units(), GaugeTheme());
        const QVector<QRect> regions = overlay.regions(QSize(1000, 1000));
        QCOMPARE(regions.size(), 3);
        // top to bottom
        QCOMPARE(regions[0], QRect(0, 0, 200, 200));
        QCOMPARE(regions[1], QRect(400, 400, 250, 250));   // the overlapping pair is one
        QCOMPARE(regions[2], QRect(900, 800, 100, 200));   // cut at the frame's edge
        for (const QRect &r : regions)
            {
                QCOMPARE(r.x() % 2, 0);
                QCOMPARE(r.width() % 2, 0);
                QCOMPARE(r.height() % 2, 0);
            }
    }

    void regionsAreLimitedInNumber()
    {
        QVector<OverlayItem> items;
        for (int i = 0; i < 6; ++i)
            {
                items.append(item(0.15 * i, 0.0, 0.1));    // a row of separate gauges
            }
        const Overlay overlay(items, nullptr, Units(), GaugeTheme());
        QCOMPARE(overlay.regions(QSize(1000, 1000)).size(), 6);
        const QVector<QRect> few = overlay.regions(QSize(1000, 1000), 2);
        QCOMPARE(few.size(), 2);
        // nothing is lost by joining
        QRect all;
        for (const QRect &r : few)
            {
                all = all.united(r);
            }
        QCOMPARE(all, overlay.boundingRect(QSize(1000, 1000)));
        QVERIFY(Overlay().regions(QSize(1000, 1000)).isEmpty());
    }

    void boundingRectCoversAllItemsAndIsClipped()
    {
        // one in the top left corner, one hanging over the bottom right corner
        const Overlay overlay({item(0.0, 0.0, 0.1), item(0.9, 0.8, 0.4)}, nullptr, Units(), GaugeTheme());
        const QRect rect = overlay.boundingRect(QSize(1000, 500));
        QCOMPARE(rect, QRect(0, 0, 1000, 500));
    }

    void defaultOverlayIsOnTheFrame()
    {
        const Overlay overlay(Overlay::defaultItems(), nullptr, Units(), GaugeTheme());
        QCOMPARE(overlay.count(), 1);
        QVERIFY(QRectF(0, 0, 1920, 1080).contains(overlay.itemRect(0, QRectF(0, 0, 1920, 1080))));
    }

    void renderDrawsOnlyInsideTheItems()
    {
        auto session = std::make_shared<TelemetrySession>();
        session->channelForWriting(ChannelId::Speed, "m/s").append(0.0, 5.0);
        session->channelForWriting(ChannelId::Speed, "m/s").append(10.0, 5.0);
        const Overlay overlay({item(0.5, 0.5, 0.5)}, session, Units(), GaugeTheme());
        QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        overlay.render(painter, QRectF(0, 0, 400, 200), 1.0);
        painter.end();
        QVERIFY(qAlpha(image.pixel(250, 150)) > 0);  // center of the gauge
        QCOMPARE(qAlpha(image.pixel(50, 50)), 0);
        QCOMPARE(qAlpha(image.pixel(350, 50)), 0);
    }

    void gaugeOfItsOwnLook()
    {
        auto session = std::make_shared<TelemetrySession>();
        session->channelForWriting(ChannelId::Speed, "m/s").append(0.0, 5.0);
        session->channelForWriting(ChannelId::Speed, "m/s").append(10.0, 5.0);
        const auto draw = [&](const OverlayItem &placed)
        {
            QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            Overlay({placed}, session, Units(), GaugeTheme()).render(painter, QRectF(0, 0, 400, 200), 1.0);
            return image;
        };
        const OverlayItem plain = item(0.25, 0.0, 1.0);
        OverlayItem faint = plain;
        faint.opacity = 0.5;
        OverlayItem own = plain;
        own.ownTheme = true;
        own.theme.background = QColor(0, 0, 255);
        const QImage asMade = draw(plain);
        // half as opaque everywhere it is drawn
        const QImage half = draw(faint);
        QVERIFY(qAbs(qAlpha(half.pixel(200, 100)) - qAlpha(asMade.pixel(200, 100)) / 2) <= 2);
        // its own colors, not the common ones
        const QImage blue = draw(own);
        QVERIFY(blue != asMade);
        int bluish = 0;
        for (int x = 120; x < 280; ++x)
            {
                const QRgb p = blue.pixel(x, 100);
                bluish += qBlue(p) > 150 && qRed(p) < 80 ? 1 : 0;
            }
        QVERIFY(bluish > 10);
    }

    void roundTrip()
    {
        Project project;
        project.videoPath = "/rides/2025/20250715_074932A.mp4";
        project.telemetryPath = "/rides/2025/data/ride.fit";
        project.syncOffset = 141.25;
        project.syncIsManual = true;
        project.syncRate = 15.0;
        project.overlay = {item(0.1, 0.2, 0.3), item(0.6, 0.7, 0.15)};
        project.overlay[1].gaugeId = "library:My dial";
        project.overlay[1].channel = "heartRate";
        // a street map switched on for one gauge, off for the other
        project.overlay[0].streets = OverlayItem::Shown;
        project.overlay[1].streets = OverlayItem::Hidden;
        project.overlay[1].closeUp = 600.0;
        // ... widening to 2 km between 3 and 12 m/s
        project.overlay[1].speedZoom = {2000.0, 3.0, 12.0};
        // the picture without a video: vertical, see-through
        project.background.size = QSize(1080, 1920);
        project.background.fps = 60;
        project.background.color = QColor(10, 20, 30, 0);
        project.overlay[1].headingUp = 1;
        project.overlay[0].flyover = 1;
        project.overlay[0].flyoverCamera = {50.0, 300.0, 2.5};
        project.overlay[1].units = OverlayItem::ImperialUnits;
        // a part made for the heading shows the wind's direction; the scale ends at 60
        project.overlay[1].channels.insert(ChannelId::Heading, ChannelId::WindDirection);
        project.overlay[1].scaleMax = 60.0;
        // made wider than the gauge was drawn
        project.overlay[1].aspect = 3.5;
        // placed on a vertical video
        project.layoutAspect = 9.0 / 16.0;
        // one gauge of its own look: green, larger letters, half see-through
        project.overlay[0].ownTheme = true;
        project.overlay[0].theme.accent = QColor(0, 200, 0);
        project.overlay[0].theme.font = "DejaVu Sans";
        project.overlay[0].textScale = 1.5;
        project.overlay[0].opacity = 0.5;
        project.overlay[1].map = "satellite";
        project.elevation = Terrain::Always;
        project.theme.accent = QColor(10, 200, 30, 128);
        project.theme.background = QColor(0, 0, 0, 0);

        Project loaded;
        QString error;
        QVERIFY2(ProjectFile::fromJson(ProjectFile::toJson(project, "/rides/2025"), "/rides/2025", &loaded, &error),
                 qPrintable(error));
        QCOMPARE(loaded.videoPath, project.videoPath);
        QCOMPARE(loaded.telemetryPath, project.telemetryPath);
        QCOMPARE(loaded.syncOffset, 141.25);
        QVERIFY(loaded.syncIsManual);
        QCOMPARE(loaded.syncRate, 15.0);
        QCOMPARE(loaded.overlay, project.overlay);
        QVERIFY(loaded.background == project.background);
        QCOMPARE(loaded.theme, project.theme);
        QCOMPARE(loaded.elevation, Terrain::Always);
        QCOMPARE(loaded.layoutAspect, 9.0 / 16.0);
        // a project from before the shape was kept: not known
        QJsonObject old = QJsonDocument::fromJson(ProjectFile::toJson(project, "/rides/2025")).object();
        old.remove("layoutAspect");
        QVERIFY(ProjectFile::fromJson(QJsonDocument(old).toJson(), "/rides/2025", &loaded, &error));
        QCOMPARE(loaded.layoutAspect, 0.0);
    }

    void pathsAreStoredRelative()
    {
        Project project;
        project.videoPath = "/rides/2025/20250715_074932A.mp4";
        project.telemetryPath = "/rides/2025/data/ride.fit";
        const QByteArray json = ProjectFile::toJson(project, "/rides/2025");
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value("video").toString(), QString("20250715_074932A.mp4"));
        QCOMPARE(root.value("data").toString(), QString("data/ride.fit"));

        // the whole folder moved: the references move with it
        Project moved;
        QVERIFY(ProjectFile::fromJson(json, "/backup/summer", &moved));
        QCOMPARE(moved.videoPath, QString("/backup/summer/20250715_074932A.mp4"));
        QCOMPARE(moved.telemetryPath, QString("/backup/summer/data/ride.fit"));
    }

    void pathsOutsideTheProjectFolderStayAbsolute()
    {
        Project project;
        project.videoPath = "/camera/card/20250715_074932A.mp4";
        project.telemetryPath = "/rides/ride.fit"; // the parent folder is outside too
        const QJsonObject root = QJsonDocument::fromJson(ProjectFile::toJson(project, "/rides/2025")).object();
        QCOMPARE(root.value("video").toString(), QString("/camera/card/20250715_074932A.mp4"));
        QCOMPARE(root.value("data").toString(), QString("/rides/ride.fit"));
    }

    void movedFileIsFoundNextToTheProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile video(dir.filePath("clip.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.close();
        // saved when the clip was somewhere else; that place no longer has it
        Project project;
        project.videoPath = "/no/longer/here/clip.mp4";
        project.telemetryPath = "/no/longer/here/ride.fit";
        Project loaded;
        QVERIFY(ProjectFile::fromJson(ProjectFile::toJson(project, "/elsewhere"), dir.path(), &loaded));
        QCOMPARE(loaded.videoPath, QDir::cleanPath(dir.filePath("clip.mp4")));
        // nothing to find: the stored path is kept so the user is told what is missing
        QCOMPARE(loaded.telemetryPath, QString("/no/longer/here/ride.fit"));
    }

    void musicIsKept()
    {
        Project project;
        project.soundtrack.tracks = {{"/rides/2025/music/one.mp3", 200.5, 10.0, 0.0}, {"/music/two.ogg", 90.0, 0.0, 60.0}};
        project.soundtrack.start = 12.5;
        project.soundtrack.fade = 3.0;
        project.soundtrack.videoVolume = 40;
        project.soundtrack.musicVolume = 180;
        project.soundtrack.musicMuted = true;
        project.soundtrack.ducking = true;
        const QByteArray json = ProjectFile::toJson(project, "/rides/2025");
        const QJsonObject music = QJsonDocument::fromJson(json).object().value("music").toObject();
        // inside the project's folder: relative; elsewhere: as it is
        QCOMPARE(music.value("tracks").toArray().at(0).toObject().value("file").toString(), QString("music/one.mp3"));
        QCOMPARE(music.value("tracks").toArray().at(1).toObject().value("file").toString(), QString("/music/two.ogg"));
        Project loaded;
        QVERIFY(ProjectFile::fromJson(json, "/rides/2025", &loaded));
        QVERIFY(loaded.soundtrack == project.soundtrack);
        // nothing written for no music
        QVERIFY(!QJsonDocument::fromJson(ProjectFile::toJson(Project(), "/tmp")).object().contains("music"));
    }

    void addedDataIsKept()
    {
        Project project;
        project.moreData = {{"/rides/2025/strap.fit", 3.5, true, {"heartRate"}},
                            {"/power/meter.fit", -12.0, false, {"power", "cadence"}}};
        const QByteArray json = ProjectFile::toJson(project, "/rides/2025");
        const QJsonArray stored = QJsonDocument::fromJson(json).object().value("moreData").toArray();
        QCOMPARE(stored.at(0).toObject().value("file").toString(), QString("strap.fit"));
        QCOMPARE(stored.at(1).toObject().value("file").toString(), QString("/power/meter.fit"));
        Project loaded;
        QVERIFY(ProjectFile::fromJson(json, "/rides/2025", &loaded));
        QVERIFY(loaded.moreData == project.moreData);
        QVERIFY(!QJsonDocument::fromJson(ProjectFile::toJson(Project(), "/tmp")).object().contains("moreData"));
    }

    void emptyProject()
    {
        Project loaded;
        loaded.videoPath = "stale";
        QVERIFY(ProjectFile::fromJson(ProjectFile::toJson(Project(), "/tmp"), "/tmp", &loaded));
        QVERIFY(loaded.videoPath.isEmpty());
        QVERIFY(loaded.telemetryPath.isEmpty());
        QVERIFY(!loaded.syncIsManual);
        QCOMPARE(loaded.overlay, Overlay::defaultItems());
    }

    void rejectsForeignAndNewerFiles()
    {
        Project project;
        project.videoPath = "untouched";
        QString error;
        QVERIFY(!ProjectFile::fromJson("not json at all", "/tmp", &project, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!ProjectFile::fromJson(R"({"format": "something-else", "version": 1})", "/tmp", &project, &error));
        error.clear();
        QVERIFY(!ProjectFile::fromJson(R"({"format": "rtvproj", "version": 99})", "/tmp", &project, &error));
        QVERIFY(error.contains("99"));
        QCOMPARE(project.videoPath, QString("untouched"));
    }

    // Files written before gauges could be bound to a channel.
    void readsVersion1()
    {
        Project project;
        project.theme.accent = Qt::green; // must be reset to the default, not kept
        QVERIFY(ProjectFile::fromJson(R"({"format": "rtvproj", "version": 1, "sync": {"offset": 7, "manual": true},
            "overlay": [{"gauge": "builtin:speedometer", "x": 0.1, "y": 0.2, "height": 0.3}]})", "/tmp", &project));
        QCOMPARE(project.overlay.size(), 1);
        QCOMPARE(project.overlay.first().channel, QString("speed"));
        QCOMPARE(project.overlay.first().gaugeId, QString(BuiltinGauges::DialId));
        QCOMPARE(project.theme, GaugeTheme());
        QCOMPARE(project.syncOffset, 7.0);
    }

    void damagedThemeColorsKeepTheDefaults()
    {
        Project project;
        QVERIFY(ProjectFile::fromJson(R"({"format": "rtvproj", "version": 2,
            "theme": {"accent": "not a color", "accent2": "#ff102030"}})", "/tmp", &project));
        QCOMPARE(project.theme.accent, GaugeTheme().accent);
        QCOMPARE(project.theme.accent2, QColor(0x10, 0x20, 0x30));
    }

    void damagedOverlayEntriesAreSkipped()
    {
        Project project;
        QVERIFY(ProjectFile::fromJson(R"({"format": "rtvproj", "version": 1, "overlay": [
            {"gauge": "builtin:speedometer", "x": 0.1, "y": 0.2, "height": 0.3},
            {"x": 0.1},
            {"gauge": "builtin:speedometer", "height": -1},
            42
        ]})", "/tmp", &project));
        QCOMPARE(project.overlay.size(), 1);
        QCOMPARE(project.overlay.first().height, 0.3);
    }

    void saveAndLoadFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("ride.rtvproj");
        Project project;
        project.videoPath = dir.filePath("video.mp4");
        project.syncOffset = -12.5;
        QString error;
        QVERIFY2(ProjectFile::save(path, project, &error), qPrintable(error));
        Project loaded;
        QVERIFY2(ProjectFile::load(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.videoPath, project.videoPath);
        QCOMPARE(loaded.syncOffset, -12.5);

        QVERIFY(!ProjectFile::load(dir.filePath("missing.rtvproj"), &loaded, &error));
        QVERIFY(!ProjectFile::save(dir.filePath("no/such/folder/x.rtvproj"), project, &error));
    }
};

QTEST_MAIN(TestProject)
#include "test_project.moc"
