#include "gauge/builtingauges.h"
#include "gauge/gaugelibrary.h"
#include "overlay/gaugetemplate.h"
#include "overlay/librarypackage.h"
#include "overlay/templatelibrary.h"
#include "overlay/xmltemplateimporter.h"
#include "settings/bundleddata.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"
#include "util/gzip.h"

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

    static XmlTemplateImporter::FoundGauge fakeGauge(const QString &name)
    {
        XmlTemplateImporter::FoundGauge found;
        if (name == "Dial")
            {
                found.id = "library:Dial";
                found.size = QSizeF(200, 200);
                found.supportedTypes = QStringList{"speed"};
            }
        else if (name == "Bar")
            {
                found.id = "library:Bar";
                found.size = QSizeF(600, 40);
            }
        return found;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        GaugeLibrary::setUserFolder(m_dir.filePath("gauges"));
        TemplateLibrary::setUserFolder(m_dir.filePath("templates"));
        // the library tests count what the user has; nothing ships until bundledData()
        BundledData::setFolder(m_dir.filePath("nothing shipped"));
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
        const QString veg = m_dir.filePath("Overlay editor");
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

    // --- XML templates --------------------------------------------------------

    void xmlTemplateIsPlaced()
    {
        // dial in the bottom right corner of a 1280 x 720 frame, a bar along the top middle
        const QByteArray cdp = R"(<Project version="2"><Information/><GaugeSettings>
            <Gauge name="Dial" transparency="1" x="1070" y="510" />
            <Gauge name="Bar" transparency="0.5" x="340" y="0" />
            <Gauge name="Logo" transparency="1" x="0" y="0" />
            </GaugeSettings></Project>)";
        const XmlTemplateImporter::Result result = XmlTemplateImporter::parse(cdp, "Racing", fakeGauge);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.frameSize, QSize(1280, 720));     // the smallest that holds them
        QCOMPARE(result.missingGauges, QStringList{"Logo"});
        QCOMPARE(result.notes.size(), 1);                  // the bar's transparency
        const GaugeTemplate &t = result.gaugeTemplate;
        QCOMPARE(t.name, QString("Racing"));
        QCOMPARE(t.items.size(), 2);
        const TemplateItem &dial = t.items[0];
        QCOMPARE(dial.gaugeId, QString("library:Dial"));
        QCOMPARE(dial.channel, QString("speed"));
        QCOMPARE(dial.edgeX, TemplateItem::End);
        QCOMPARE(dial.edgeY, TemplateItem::End);
        QCOMPARE(dial.height, 300.0);                      // 200 of 720 -> of 1080
        QCOMPARE(dial.offsetX, -315.0);
        QCOMPARE(dial.offsetY, -315.0);
        const TemplateItem &bar = t.items[1];
        QCOMPARE(bar.edgeX, TemplateItem::Middle);
        QCOMPARE(bar.edgeY, TemplateItem::Start);
        QCOMPARE(bar.offsetX, -450.0);
        QCOMPARE(bar.offsetY, 0.0);
        // on the 16:9 video it was made for, exactly where it was
        const OverlayItem placed = t.overlayItems(16.0 / 9.0).first();
        QCOMPARE(placed.x, 1070.0 / 1280.0);
        QCOMPARE(placed.y, 510.0 / 720.0);

        // the name may say what it was made for
        const XmlTemplateImporter::Result named = XmlTemplateImporter::parse(cdp, "Racing 1080p", fakeGauge);
        QCOMPARE(named.frameSize, QSize(1920, 1080));
        QCOMPARE(named.gaugeTemplate.items[0].height, 200.0);
        // a small layout is standard definition
        const XmlTemplateImporter::Result small = XmlTemplateImporter::parse(
                    R"(<Project><GaugeSettings><Gauge name="Dial" x="500" y="270"/></GaugeSettings></Project>)", "Small", fakeGauge);
        QCOMPARE(small.frameSize, QSize(720, 480));
        QCOMPARE(XmlTemplateImporter::gaugeNames(cdp), QStringList({"Dial", "Bar", "Logo"}));
    }

    void xmlTemplateRejectsWhatIsNotOne()
    {
        QVERIFY(!XmlTemplateImporter::parse("garbage", "x", fakeGauge).ok);
        QVERIFY(!XmlTemplateImporter::parse("<Gauge/>", "x", fakeGauge).ok);
        QVERIFY(!XmlTemplateImporter::parse("<Project><GaugeSettings/></Project>", "x", fakeGauge).ok);
        QVERIFY(!XmlTemplateImporter::parse(R"(<Project><GaugeSettings><Gauge name="Logo" x="0" y="0"/></GaugeSettings></Project>)",
                                            "x", fakeGauge).ok);
    }

    void xmlTemplateImportBringsItsOwnGauges()
    {
        const QString root = m_dir.filePath("xml");
        auto gauge = [](int size)
        {
            return QString("<Gauge version='3' width='%1' height='%1'><Components>"
                           "<FilledEllipse x='10' y='10' horizontalRadius='5' verticalRadius='5' transparency='1' red='255' green='0' blue='0'/>"
                           "</Components><Inputs><Input category='Velocity' name='Speed (KPH)'>KPH</Input></Inputs></Gauge>")
                   .arg(size).toUtf8();
        };
        // the same gauge on its own, and a template for a smaller frame with a smaller copy
        write(root + "/Gauges/Round/gauge.xml", gauge(300));
        write(root + "/Templates/Small 720p/project.cdp",
              R"(<Project version="2"><GaugeSettings><Gauge name="Round" x="10" y="10"/></GaugeSettings></Project>)");
        write(root + "/Templates/Small 720p/Gauges/Round/gauge.xml", gauge(200));
        write(root + "/Templates/Same 720p/project.cdp",
              R"(<Project version="2"><GaugeSettings><Gauge name="Round" x="10" y="10"/></GaugeSettings></Project>)");
        write(root + "/Templates/Same 720p/Gauges/Round/gauge.xml", gauge(300));

        QCOMPARE(GaugeLibrary::importXml({root + "/Gauges/Round"}).imported, 1);
        GaugeLibrary::refresh();
        const TemplateLibrary::ImportSummary summary = TemplateLibrary::importXml(
                    {root + "/Templates/Small 720p", root + "/Templates/Same 720p", root + "/Templates/missing"});
        QCOMPARE(summary.imported, 2);
        QCOMPARE(summary.gaugesImported, 1);        // only the copy that differs
        QCOMPARE(summary.failed.size(), 1);
        GaugeLibrary::refresh();
        QCOMPARE(GaugeLibrary::gauge("library:Round").size, QSizeF(300, 300));
        QCOMPARE(GaugeLibrary::gauge("library:Round - Small 720p").size, QSizeF(200, 200));

        GaugeTemplate small;
        GaugeTemplate same;
        for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
            {
                if (entry.name == "Small 720p")
                    {
                        small = TemplateLibrary::get(entry.id);
                    }
                else if (entry.name == "Same 720p")
                    {
                        same = TemplateLibrary::get(entry.id);
                    }
            }
        QCOMPARE(small.items.size(), 1);
        QCOMPARE(small.items.first().gaugeId, QString("library:Round - Small 720p"));
        QCOMPARE(small.items.first().height, 300.0);
        QCOMPARE(small.items.first().channel, QString("speed"));
        QCOMPARE(same.items.first().gaugeId, QString("library:Round"));
        QCOMPARE(same.items.first().height, 450.0);
    }

    // --- what ships with the program ------------------------------------------

    void bundledData()
    {
        BundledData::setFolder(QStringLiteral(RTV_TEST_DATA));
        GaugeLibrary::refresh();

        QStringList gaugeIds;
        for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
            {
                if (entry.id.startsWith("bundled:"))
                    {
                        QVERIFY(entry.builtin);
                        gaugeIds << entry.id;
                    }
            }
        QVERIFY(gaugeIds.size() >= 8);
        const TelemetrySessionPtr demo = DemoSession::create();
        for (const QString &id : gaugeIds)
            {
                const BoundGauge bound(GaugeLibrary::gauge(id), ChannelId::Speed, demo, Units(), GaugeTheme());
                QVERIFY2(!bound.isEmpty(), qPrintable(id));
            }

        // the user's own (from the import test) come after the shipped ones
        QVector<TemplateLibrary::Entry> templates = TemplateLibrary::entries();
        templates.erase(std::remove_if(templates.begin(), templates.end(), [](const TemplateLibrary::Entry &entry)
        {
            return !entry.builtin;
        }), templates.end());
        QVERIFY(templates.size() >= 4);
        for (const TemplateLibrary::Entry &entry : templates)
            {
                QVERIFY(entry.id.startsWith("bundled-template:"));
                const GaugeTemplate t = TemplateLibrary::get(entry.id);
                QVERIFY2(!t.items.isEmpty(), qPrintable(entry.id));
                // shipped templates use shipped gauges only, and every one of them is there
                for (const TemplateItem &item : t.items)
                    {
                        QVERIFY2(item.gaugeId.startsWith("bundled:") || item.gaugeId.startsWith("builtin:"), qPrintable(item.gaugeId));
                        QVERIFY2(!GaugeLibrary::gauge(item.gaugeId).isEmpty(), qPrintable(item.gaugeId));
                    }
                // and they stay on a 16:9 picture
                for (const OverlayItem &placed : t.overlayItems(16.0 / 9.0))
                    {
                        QVERIFY2(placed.x >= 0.0 && placed.y >= 0.0 && placed.y + placed.height <= 1.0, qPrintable(entry.name));
                    }
            }

        // what came with the program cannot be removed, nor reached around
        QVERIFY(!GaugeLibrary::remove(gaugeIds.first()));
        QVERIFY(!GaugeLibrary::gauge(gaugeIds.first()).isEmpty());
        QVERIFY(!TemplateLibrary::remove(templates.first().id));
        QVERIFY(!TemplateLibrary::get(templates.first().id).items.isEmpty());
        QVERIFY(GaugeLibrary::gauge("bundled:../gauges/Arc").isEmpty());
        QVERIFY(TemplateLibrary::get("bundled-template:../templates/Ride").items.isEmpty());

        BundledData::setFolder(m_dir.filePath("nothing shipped"));
        GaugeLibrary::refresh();
        QVERIFY(GaugeLibrary::gauge(gaugeIds.first()).isEmpty());
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

    void packagesCarryGaugesAndTemplates()
    {
        // a library of its own: earlier tests count what is in theirs
        GaugeLibrary::setUserFolder(m_dir.filePath("package-gauges"));
        TemplateLibrary::setUserFolder(m_dir.filePath("package-templates"));

        // a drawing and a picture
        Gauge gauge;
        gauge.name = "Shared";
        gauge.size = QSizeF(100, 50);
        GaugeComponent drawing;
        drawing.type = GaugeComponent::Image;
        drawing.frame = QRectF(0, 0, 100, 50);
        drawing.imagePath = "images/plate.svg";
        drawing.svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 5'><rect width='10' height='5' fill='@accent'/></svg>";
        GaugeComponent picture = drawing;
        picture.imagePath = "images/dot.png";
        picture.svg.clear();
        picture.image = QImage(8, 8, QImage::Format_ARGB32);
        picture.image.fill(Qt::red);
        gauge.components << drawing << picture;

        const QByteArray packed = LibraryPackage::packGauge(gauge);
        QVERIFY(Gzip::isGzip(packed));
        bool ok = false;
        const QByteArray json = Gzip::decompress(packed, &ok);
        QVERIFY(ok);
        QVERIFY(json.contains("<svg"));          // drawings stay readable inside
        Gauge back;
        QVERIFY(LibraryPackage::unpackGauge(packed, &back));
        QCOMPARE(back.name, QString("Shared"));
        QCOMPARE(back.components.at(0).svg, drawing.svg);
        QCOMPARE(back.components.at(1).image.pixelColor(4, 4), QColor(Qt::red));
        // plain JSON is read too
        QVERIFY(LibraryPackage::unpackGauge(json, &back));
        QVERIFY(!LibraryPackage::unpackGauge("not a package", &back));

        // a template with a gauge of the library and one that comes with the program
        const QString sharedId = GaugeLibrary::add(gauge, gauge.name);
        GaugeTemplate t;
        t.name = "Shared layout";
        TemplateItem mine;
        mine.gaugeId = sharedId;
        mine.channel = ChannelId::Speed;
        mine.height = 200;
        TemplateItem builtin = mine;
        builtin.gaugeId = BuiltinGauges::DialId;
        t.items << mine << builtin;
        const QByteArray templatePackage = LibraryPackage::packTemplate(t);
        QVERIFY(!templatePackage.isEmpty());

        // somewhere else: an empty library
        GaugeLibrary::setUserFolder(m_dir.filePath("elsewhere-gauges"));
        TemplateLibrary::setUserFolder(m_dir.filePath("elsewhere-templates"));
        int added = 0;
        const QString templateId = LibraryPackage::importTemplate(templatePackage, &added);
        QVERIFY(!templateId.isEmpty());
        QCOMPARE(added, 1);
        const GaugeTemplate imported = TemplateLibrary::get(templateId);
        QCOMPARE(imported.items.size(), 2);
        QCOMPARE(imported.items.at(1).gaugeId, QString(BuiltinGauges::DialId));
        QCOMPARE(GaugeLibrary::gauge(imported.items.at(0).gaugeId).components.at(0).svg, drawing.svg);
        // again: the gauge is there as it is, nothing new; the template gets a name of its own
        const QString again = LibraryPackage::importTemplate(templatePackage, &added);
        QCOMPARE(added, 0);
        QVERIFY(again != templateId);
        QCOMPARE(TemplateLibrary::get(again).items.at(0).gaugeId, imported.items.at(0).gaugeId);
        // a different gauge of the same name does not replace the one there
        gauge.size = QSizeF(200, 50);
        const QString other = LibraryPackage::importGauge(LibraryPackage::packGauge(gauge));
        QVERIFY(!other.isEmpty() && other != imported.items.at(0).gaugeId);
        QCOMPARE(GaugeLibrary::gauge(other).name, QString("Shared (2)"));
        QCOMPARE(GaugeLibrary::gauge(imported.items.at(0).gaugeId).size, QSizeF(100, 50));
    }
};

QTEST_MAIN(TestTemplate)
#include "test_template.moc"
