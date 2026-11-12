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
        result.gaugeId = BuiltinGauges::SpeedometerId;
        result.x = x;
        result.y = y;
        result.height = height;
        return result;
    }

private slots:
    void itemRectFollowsTheFrame()
    {
        const Overlay overlay({item(0.25, 0.5, 0.25)}, Units());
        QCOMPARE(overlay.count(), 1);
        // the speedometer is square: the width equals the height
        QCOMPARE(overlay.itemRect(0, QRectF(0, 0, 1920, 1080)), QRectF(480, 540, 270, 270));
        QCOMPARE(overlay.itemRect(0, QRectF(0, 0, 1280, 720)), QRectF(320, 360, 180, 180));
        // a letterboxed preview: the frame does not start at the origin
        QCOMPARE(overlay.itemRect(0, QRectF(100, 50, 640, 360)), QRectF(260, 230, 90, 90));
    }

    void unknownGaugesAreLeftOut()
    {
        OverlayItem unknown = item(0, 0, 0.5);
        unknown.gaugeId = "builtin:does-not-exist";
        const Overlay overlay({unknown, item(0, 0, 0.5)}, Units());
        QCOMPARE(overlay.count(), 1);
        QVERIFY(Overlay({unknown}, Units()).isEmpty());
        QVERIFY(Overlay({unknown}, Units()).boundingRect(QSize(1920, 1080)).isEmpty());
    }

    void boundingRectIsEvenAndInsideTheFrame()
    {
        // 0.3337 * 1080 = 360.4 high, at an odd position
        const Overlay overlay({item(0.0105, 0.6267, 0.3337)}, Units());
        const QRect rect = overlay.boundingRect(QSize(1920, 1080));
        QVERIFY(!rect.isEmpty());
        QCOMPARE(rect.x() % 2, 0);
        QCOMPARE(rect.y() % 2, 0);
        QCOMPARE(rect.width() % 2, 0);
        QCOMPARE(rect.height() % 2, 0);
        QVERIFY(QRect(0, 0, 1920, 1080).contains(rect));
        QVERIFY(QRectF(rect).contains(overlay.itemRect(0, QRectF(0, 0, 1920, 1080))));
    }

    void boundingRectCoversAllItemsAndIsClipped()
    {
        // one in the top left corner, one hanging over the bottom right corner
        const Overlay overlay({item(0.0, 0.0, 0.1), item(0.9, 0.8, 0.4)}, Units());
        const QRect rect = overlay.boundingRect(QSize(1000, 500));
        QCOMPARE(rect, QRect(0, 0, 1000, 500));
    }

    void defaultOverlayIsOnTheFrame()
    {
        const Overlay overlay(Overlay::defaultItems(), Units());
        QCOMPARE(overlay.count(), 1);
        QVERIFY(QRectF(0, 0, 1920, 1080).contains(overlay.itemRect(0, QRectF(0, 0, 1920, 1080))));
    }

    void renderDrawsOnlyInsideTheItems()
    {
        TelemetrySession session;
        session.channelForWriting(ChannelId::Speed, "m/s").append(0.0, 5.0);
        session.channelForWriting(ChannelId::Speed, "m/s").append(10.0, 5.0);
        const Overlay overlay({item(0.5, 0.5, 0.5)}, Units());
        QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        overlay.render(painter, QRectF(0, 0, 400, 200), &session, 1.0);
        painter.end();
        QVERIFY(qAlpha(image.pixel(250, 150)) > 0);  // centre of the gauge
        QCOMPARE(qAlpha(image.pixel(50, 50)), 0);
        QCOMPARE(qAlpha(image.pixel(350, 50)), 0);
    }

    void roundTrip()
    {
        Project project;
        project.videoPath = "/rides/2025/20250715_074932A.mp4";
        project.telemetryPath = "/rides/2025/data/ride.fit";
        project.syncOffset = 141.25;
        project.syncIsManual = true;
        project.overlay = {item(0.1, 0.2, 0.3), item(0.6, 0.7, 0.15)};

        Project loaded;
        QString error;
        QVERIFY2(ProjectFile::fromJson(ProjectFile::toJson(project, "/rides/2025"), "/rides/2025", &loaded, &error),
                 qPrintable(error));
        QCOMPARE(loaded.videoPath, project.videoPath);
        QCOMPARE(loaded.telemetryPath, project.telemetryPath);
        QCOMPARE(loaded.syncOffset, 141.25);
        QVERIFY(loaded.syncIsManual);
        QCOMPARE(loaded.overlay, project.overlay);
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
