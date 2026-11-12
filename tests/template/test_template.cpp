#include "gauge/builtingauges.h"
#include "gauge/gaugelibrary.h"
#include "overlay/gaugetemplate.h"
#include "overlay/templatelibrary.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"

#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class TestTemplate : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    static TemplateItem item(TemplateItem::Edge edgeX, double offsetX, TemplateItem::Edge edgeY, double offsetY, double height)
    {
        TemplateItem result;
        result.gaugeId = BuiltinGauges::DialId;
        result.channel = ChannelId::Speed;
        result.edgeX = edgeX;
        result.edgeY = edgeY;
        result.offsetX = offsetX;
        result.offsetY = offsetY;
        result.height = height;
        return result;
    }

    // gauges "in the library" for the converter, found by file name whatever its case
    static VetTemplateImporter::FoundGauge knownGauge(const QString &fileName)
    {
        VetTemplateImporter::FoundGauge found;
        if (fileName.compare("speed_dial.veg", Qt::CaseInsensitive) == 0)
            {
                found.id = "library:Speed Dial";
                found.height = 400.0;
            }
        else if (fileName.compare("small_box.veg", Qt::CaseInsensitive) == 0)
            {
                found.id = "library:small box";
                found.height = 70.0;
                found.supportedTypes = QStringList({"heartRate"});
            }
        return found;
    }

    QString write(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            {
                file.write(content);
            }
        return path;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        GaugeLibrary::setUserFolder(m_dir.filePath("gauges"));
        TemplateLibrary::setUserFolder(m_dir.filePath("templates"));
    }

    // --- placement ------------------------------------------------------------

    void placementOnA16By9Frame()
    {
        GaugeTemplate t;
        t.items = {
            item(TemplateItem::Start, 20, TemplateItem::Start, 10, 270),     // top left
            item(TemplateItem::End, -400, TemplateItem::End, -400, 400),     // flush with the bottom right corner
            item(TemplateItem::Middle, -200, TemplateItem::Start, 0, 108),   // top centre, 200 px wide half
        };
        const QVector<OverlayItem> placed = t.overlayItems(16.0 / 9.0);
        QCOMPARE(placed.size(), 3);
        QCOMPARE(placed[0].x, 20.0 / 1920.0);
        QCOMPARE(placed[0].y, 10.0 / 1080.0);
        QCOMPARE(placed[0].height, 0.25);
        QCOMPARE(placed[1].x, 1520.0 / 1920.0);
        QCOMPARE(placed[1].y, 680.0 / 1080.0);
        QCOMPARE(placed[2].x, 760.0 / 1920.0);
        QCOMPARE(placed[2].channel, QString("speed"));
        QCOMPARE(placed[2].gaugeId, QString(BuiltinGauges::DialId));
    }

    // a 4:3 camera picture: right-anchored gauges stay at the right edge, sizes follow the height
    void placementOnA4By3Frame()
    {
        GaugeTemplate t;
        t.items = {item(TemplateItem::End, -400, TemplateItem::End, -400, 400)};
        const OverlayItem placed = t.overlayItems(4.0 / 3.0).first();
        // the frame is 1440 reference pixels wide now
        QCOMPARE(placed.x, 1040.0 / 1440.0);
        QCOMPARE(placed.height, 400.0 / 1080.0);
        // a square gauge flush with the right edge: x + height * (height px / width px) = 1
        QVERIFY(qAbs(placed.x + placed.height * 1080.0 / 1440.0 - 1.0) < 1e-9);
        // no shape given: 16:9
        QCOMPARE(t.overlayItems(0.0).first().x, 1520.0 / 1920.0);
    }

    // --- file -----------------------------------------------------------------

    void fileRoundTrip()
    {
        GaugeTemplate t;
        t.name = "Track day";
        t.category = "Automotive";
        t.theme.accent = QColor(10, 200, 30, 128);
        t.theme.foreground = Qt::black;
        t.items = {item(TemplateItem::End, -10, TemplateItem::Middle, -50, 100)};
        t.items[0].gaugeId = "library:My dial";
        t.items[0].channel = ChannelId::HeartRate;
        GaugeTemplate loaded;
        QString error;
        QVERIFY2(TemplateFile::fromJson(TemplateFile::toJson(t), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name, t.name);
        QCOMPARE(loaded.category, t.category);
        QCOMPARE(loaded.theme, t.theme);
        QCOMPARE(loaded.items.size(), 1);
        QVERIFY(loaded.items[0] == t.items[0]);
    }

    void fileRejectsForeignAndNewer()
    {
        GaugeTemplate t;
        QString error;
        QVERIFY(!TemplateFile::fromJson("nonsense", &t, &error));
        QVERIFY(!TemplateFile::fromJson(R"({"format": "rtvgauge"})", &t, &error));
        QVERIFY(!TemplateFile::fromJson(R"({"format": "rtvtemplate", "version": 99})", &t, &error));
        QVERIFY(error.contains("99"));
        // entries without a gauge or a size are dropped, not fatal
        QVERIFY(TemplateFile::fromJson(R"({"format": "rtvtemplate", "version": 1, "items": [
            {"gauge": "builtin:speedometer", "height": 100}, {"height": 100}, {"gauge": "x", "height": 0}]})", &t, &error));
        QCOMPARE(t.items.size(), 1);
    }

    // --- .vet templates ----------------------------------------------------

    void vetTemplateIsConverted()
    {
        const QByteArray vet = QByteArray("\xEF\xBB\xBF") + R"({
            "category": "Cycling", "thumbnailPath": "Thumbnails/7.png",
            "backgroundColor": {"r": 0, "g": 100, "b": 255, "a": 50},
            "accentColor": {"r": 255, "g": 0, "b": 0, "a": 255},
            "accent2Color": {"r": 0, "g": 255, "b": 0, "a": 255},
            "widgets": [
                {"filename": "Speed_Dial.veg", "valueType": "speed2d",
                 "origin": {"x": {"offset": -400, "fromEdge": "max"}, "y": {"offset": -400, "fromEdge": "max"}}},
                {"filename": "Gone.veg", "valueType": "speed",
                 "origin": {"x": {"offset": 0, "fromEdge": "min"}, "y": {"offset": 0, "fromEdge": "min"}}},
                {"origin": {"x": {"offset": 10, "fromEdge": "min"}, "y": {"offset": -320, "fromEdge": "max"}},
                 "widgets": [
                    {"filename": "small_box.veg", "valueType": "heartRate", "origin": {"x": 0, "y": 0}},
                    {"filename": "small_box.veg", "valueType": "compass", "origin": {"x": 30, "y": 60}},
                    {"filename": "SMALL_BOX.veg", "origin": {"x": 0, "y": 120}}]},
                {"filename": "Speed_Dial.veg", "valueType": "cadence",
                 "origin": {"x": {"fromEdge": "mid", "offset": -200}, "y": {"fromEdge": "min", "offset": 0}}}
            ]})";
        const VetTemplateImporter::Result result = VetTemplateImporter::parse(vet, "Cycling 7", knownGauge);
        QVERIFY2(result.ok, qPrintable(result.error));
        const GaugeTemplate &t = result.gaugeTemplate;
        QCOMPARE(t.name, QString("Cycling 7"));
        QCOMPARE(t.category, QString("Cycling"));
        QCOMPARE(t.theme.background, QColor(0, 100, 255, 50));
        QCOMPARE(t.theme.accent, QColor(255, 0, 0));
        QCOMPARE(t.theme.accent2, QColor(0, 255, 0));
        QCOMPARE(t.theme.foreground, QColor(255, 255, 255));
        QCOMPARE(result.missingGauges, QStringList({"Gone.veg"}));

        QCOMPARE(t.items.size(), 5);
        const TemplateItem &dial = t.items[0];
        QCOMPARE(dial.gaugeId, QString("library:Speed Dial"));   // the name the gauge got on import
        QCOMPARE(dial.channel, QString("speed"));
        QCOMPARE(dial.edgeX, TemplateItem::End);
        QCOMPARE(dial.offsetX, -400.0);
        QCOMPARE(dial.height, 400.0);
        // the group: its anchor and offset, plus each member's own
        QCOMPARE(t.items[1].gaugeId, QString("library:small box"));
        QCOMPARE(t.items[1].channel, QString("heartRate"));
        QCOMPARE(t.items[1].edgeY, TemplateItem::End);
        QCOMPARE(t.items[1].offsetX, 10.0);
        QCOMPARE(t.items[1].offsetY, -320.0);
        QCOMPARE(t.items[2].channel, QString("heading"));
        QCOMPARE(t.items[2].offsetX, 40.0);
        QCOMPARE(t.items[2].offsetY, -260.0);
        QCOMPARE(t.items[2].height, 70.0);
        // no measurement given: what the gauge was made for
        QCOMPARE(t.items[3].gaugeId, QString("library:small box"));
        QCOMPARE(t.items[3].channel, QString("heartRate"));
        QCOMPARE(t.items[4].edgeX, TemplateItem::Middle);
        QCOMPARE(t.items[4].channel, QString("cadence"));

        QCOMPARE(VetTemplateImporter::gaugeFiles(vet), QStringList({"Speed_Dial.veg", "Gone.veg", "small_box.veg"}));
    }

    void vetTemplateWithoutUsableGauges()
    {
        const VetTemplateImporter::Result result = VetTemplateImporter::parse(
                    R"({"widgets": [{"filename": "Logo.veg", "origin": {"x": {"offset": 0, "fromEdge": "min"}, "y": {"offset": 0, "fromEdge": "min"}}}]})",
                    "Only a logo", knownGauge);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.missingGauges, QStringList({"Logo.veg"}));
        QVERIFY(!VetTemplateImporter::parse("not json", "x", knownGauge).ok);
        QVERIFY(!VetTemplateImporter::parse(R"({"frame": {"width": 1, "height": 1}, "components": []})", "a gauge", knownGauge).ok);
    }

    void themeTextStandsOut()
    {
        QCOMPARE(VetTemplateImporter::themeOf(QColor(0, 0, 0, 150), Qt::red, Qt::white).foreground, QColor(Qt::white));
        QCOMPARE(VetTemplateImporter::themeOf(QColor(240, 240, 240, 220), Qt::red, Qt::white).foreground, QColor(Qt::black));
        // light but nearly see-through: the video behind decides, white is the safer bet
        QCOMPARE(VetTemplateImporter::themeOf(QColor(240, 240, 240, 40), Qt::red, Qt::white).foreground, QColor(Qt::white));
    }

    // --- library ----------------------------------------------------------------

    void libraryStoresTemplates()
    {
        QVERIFY(TemplateLibrary::entries().isEmpty());
        GaugeTemplate t;
        t.name = "B side";
        t.items = {item(TemplateItem::Start, 0, TemplateItem::Start, 0, 100)};
        GaugeTemplate other = t;
        other.name = "a first";
        other.category = "General";
        QString error;
        const QString id = TemplateLibrary::add(t, &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        QVERIFY(!TemplateLibrary::add(other).isEmpty());
        const QVector<TemplateLibrary::Entry> entries = TemplateLibrary::entries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].name, QString("a first"));   // alphabetical, whatever the case
        QCOMPARE(entries[0].category, QString("General"));
        QCOMPARE(TemplateLibrary::get(id).items.size(), 1);

        // the same name again replaces
        t.items.append(t.items.first());
        QCOMPARE(TemplateLibrary::add(t), id);
        QCOMPARE(TemplateLibrary::entries().size(), 2);
        QCOMPARE(TemplateLibrary::get(id).items.size(), 2);

        QVERIFY(TemplateLibrary::remove(id));
        QVERIFY(TemplateLibrary::get(id).items.isEmpty());
        QVERIFY(TemplateLibrary::remove(TemplateLibrary::entries().first().id));
        GaugeTemplate unnamed;
        QVERIFY(TemplateLibrary::add(unnamed, &error).isEmpty());
    }

    void libraryIdsCannotPointOutside()
    {
        write(m_dir.filePath("secret.json"), TemplateFile::toJson(GaugeTemplate()));
        QVERIFY(TemplateLibrary::get("template:../secret").items.isEmpty());
        QVERIFY(!TemplateLibrary::remove("template:../secret"));
        QVERIFY(QFile::exists(m_dir.filePath("secret.json")));
        QVERIFY(!TemplateLibrary::remove("library:anything"));
    }

    void importBringsTheGaugesAlong()
    {
        const QString veg = m_dir.filePath("the old editor");
        write(veg + "/Widgets/Speed_Dial.veg", R"({"frame": {"width": 400, "height": 400}, "supportedTypes": ["speed"], "components": [
            {"type": "text", "autoTextType": "value", "frame": {"x": 200, "y": 200}}]})");
        write(veg + "/Widgets/unused.veg", R"({"frame": {"width": 10, "height": 10}, "components": [
            {"type": "text", "autoTextType": "value", "frame": {"x": 5, "y": 5}}]})");
        const QString good = write(veg + "/Templates/Cycling_7.vet", R"({"category": "Cycling", "widgets": [
            {"filename": "speed_dial.veg", "valueType": "speed",
             "origin": {"x": {"offset": 20, "fromEdge": "min"}, "y": {"offset": -420, "fromEdge": "max"}}},
            {"filename": "SML_Logo.veg", "origin": {"x": {"offset": 0, "fromEdge": "min"}, "y": {"offset": 0, "fromEdge": "min"}}}]})");
        const QString bad = write(veg + "/Templates/broken.vet", "garbage");

        const TemplateLibrary::ImportSummary summary = TemplateLibrary::importVet({good, bad}, veg + "/Widgets");
        QCOMPARE(summary.imported, 1);
        QCOMPARE(summary.gaugesImported, 1);       // only the one the template needs
        QCOMPARE(summary.failed.size(), 1);
        QVERIFY(summary.failed.first().startsWith("broken.vet"));
        QCOMPARE(summary.notes.size(), 1);
        QVERIFY(summary.notes.first().contains("SML_Logo.veg"));

        GaugeLibrary::refresh();
        QVERIFY(!GaugeLibrary::gauge("library:Speed Dial").isEmpty());
        QVERIFY(GaugeLibrary::gauge("library:unused").isEmpty());
        const QVector<TemplateLibrary::Entry> entries = TemplateLibrary::entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QString("Cycling 7"));
        const GaugeTemplate t = TemplateLibrary::get(entries.first().id);
        QCOMPARE(t.items.size(), 1);
        QCOMPARE(t.items.first().height, 400.0);
        // laid out on a 16:9 video: 20 px from the left, 20 px above the bottom
        const OverlayItem placed = t.overlayItems(16.0 / 9.0).first();
        QCOMPARE(placed.y + placed.height, 1.0 - 20.0 / 1080.0);

        // a second run finds the gauge already there
        QCOMPARE(TemplateLibrary::importVet({good}, veg + "/Widgets").gaugesImported, 0);
    }

    // --- the demo ride for pictures ------------------------------------------------

    void demoSessionHasWhatGaugesNeed()
    {
        const TelemetrySessionPtr demo = DemoSession::create();
        for (const char *id : {ChannelId::Speed, ChannelId::HeartRate, ChannelId::Cadence, ChannelId::Power,
                               ChannelId::Elevation, ChannelId::Heading, ChannelId::PositionLat, ChannelId::PositionLon,
                               ChannelId::GForceLateral, ChannelId::GForceLongitudinal})
            {
                QVERIFY2(demo->hasChannel(id), id);
            }
        QCOMPARE(demo->laps().size(), 2);
        QVERIFY(DemoSession::ShowcaseTime > 0 && DemoSession::ShowcaseTime < demo->duration());
    }
};

QTEST_MAIN(TestTemplate)
#include "test_template.moc"
