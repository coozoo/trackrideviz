#include "gauge/builtingauges.h"
#include "gauge/gaugefile.h"
#include "gauge/gaugelibrary.h"
#include "gauge/gaugerenderer.h"
#include "gauge/metricrange.h"
#include "gauge/veggaugeimporter.h"
#include "gauge/xmlgaugeimporter.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"

#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

// A moment in UTC, built the same way on every Qt version.
static QDateTime utc(const QDate &date, const QTime &time)
{
    return QDateTime::fromMSecsSinceEpoch(QDate(1970, 1, 1).daysTo(date) * 86400000 + time.msecsSinceStartOfDay()).toUTC();
}

class TestGauge : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    // speed 0 -> 10 m/s (36 km/h) over 10 s, heading 0 -> 90 degrees
    static TelemetrySessionPtr makeSession()
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
        speed.append(0.0, 0.0);
        speed.append(10.0, 10.0);
        Channel &heading = session->channelForWriting(ChannelId::Heading, "deg");
        heading.append(0.0, 0.0);
        heading.append(10.0, 90.0);
        return session;
    }

    static QImage canvas(int size = 100)
    {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        return image;
    }

    static QImage filled(const QColor &color, int size = 100)
    {
        QImage image = canvas(size);
        image.fill(color);
        return image;
    }

    // 100x100 gauge with one image component covering `frame`.
    static Gauge imageGauge(const QImage &image, const QRectF &frame, GaugeColor::Role role = GaugeColor::Original)
    {
        GaugeComponent component;
        component.type = GaugeComponent::Image;
        component.image = image;
        component.frame = frame;
        component.color.role = role;
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(component);
        return gauge;
    }

    // A bar from the centre up to 12 o'clock, turned around the centre over the scale 0..40 km/h.
    static Gauge needleGauge()
    {
        QImage bar = canvas();
        QPainter painter(&bar);
        painter.fillRect(QRect(46, 10, 8, 40), Qt::white);
        painter.end();
        Gauge gauge = imageGauge(bar, QRectF(0, 0, 100, 100), GaugeColor::Accent);
        GaugeManipulation turn;
        turn.type = GaugeManipulation::Rotate;
        turn.pivot = QPointF(50, 50);
        turn.minAngle = 0;
        turn.maxAngle = 180;
        turn.hasValueRange = true;
        turn.minValue = 0;
        turn.maxValue = 36;
        gauge.components[0].manipulations.append(turn);
        return gauge;
    }

    static QImage draw(const Gauge &gauge, double time, const QString &channel = ChannelId::Speed,
                       const TelemetrySessionPtr &session = makeSession(), int size = 100)
    {
        const BoundGauge bound(gauge, channel, session, Units(), GaugeTheme());
        QImage image = canvas(size);
        QPainter painter(&image);
        bound.render(painter, QRectF(0, 0, size, size), time);
        return image;
    }

    static bool painted(const QImage &image, int x, int y)
    {
        return qAlpha(image.pixel(x, y)) > 128;
    }

    static int paintedPixels(const QImage &image)
    {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            {
                for (int x = 0; x < image.width(); ++x)
                    {
                        count += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
                    }
            }
        return count;
    }

    // A text drawn as `component` looks the same as the fixed text `expected`.
    static void compareText(const GaugeComponent &component, const QString &expected, const TelemetrySessionPtr &session,
                     double time)
    {
        Gauge shown;
        shown.size = QSizeF(200, 100);
        shown.components.append(component);
        GaugeComponent fixed = component;
        fixed.autoText = GaugeComponent::FixedText;
        fixed.text = expected;
        fixed.valueFormat = GaugeComponent::Number;
        Gauge wanted = shown;
        wanted.components[0] = fixed;
        const BoundGauge a(shown, ChannelId::Speed, session, Units(), GaugeTheme());
        const BoundGauge b(wanted, ChannelId::Speed, session, Units(), GaugeTheme());
        QImage left(200, 100, QImage::Format_ARGB32_Premultiplied);
        left.fill(Qt::transparent);
        QImage right = left;
        QPainter p1(&left);
        a.render(p1, QRectF(0, 0, 200, 100), time);
        p1.end();
        QPainter p2(&right);
        b.render(p2, QRectF(0, 0, 200, 100), time);
        p2.end();
        QVERIFY2(left == right, qPrintable(expected));
        QVERIFY(paintedPixels(left) > 0);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        GaugeLibrary::setUserFolder(m_dir.filePath("library"));
    }

    // --- scale ranges -------------------------------------------------------

    void range_data()
    {
        QTest::addColumn<double>("dataMin");
        QTest::addColumn<double>("dataMax");
        QTest::addColumn<int>("markings");
        QTest::addColumn<double>("min");
        QTest::addColumn<double>("max");
        QTest::addColumn<double>("step");

        QTest::newRow("bike speed, 9 marks") << 0.0 << 52.3 << 9 << 0.0 << 80.0 << 10.0;
        QTest::newRow("car speed, 9 marks") << 0.0 << 187.0 << 9 << 0.0 << 200.0 << 25.0;
        QTest::newRow("13 marks") << 0.0 << 52.3 << 13 << 0.0 << 60.0 << 5.0;
        QTest::newRow("exact fit") << 0.0 << 80.0 << 9 << 0.0 << 80.0 << 10.0;
        QTest::newRow("no markings: 4 intervals") << 0.0 << 171.0 << 0 << 0.0 << 200.0 << 50.0;
        QTest::newRow("small values") << 0.0 << 0.83 << 5 << 0.0 << 1.0 << 0.25;
        QTest::newRow("far from zero") << 1850.0 << 2130.0 << 5 << 1800.0 << 2200.0 << 100.0;
        QTest::newRow("near zero start") << 12.0 << 95.0 << 5 << 0.0 << 100.0 << 25.0;
        QTest::newRow("negative") << -6.2 << 9.8 << 5 << -10.0 << 10.0 << 5.0;
        QTest::newRow("all negative") << -40.0 << -5.0 << 5 << -40.0 << 0.0 << 10.0;
        QTest::newRow("flat data") << 0.0 << 0.0 << 5 << 0.0 << 4.0 << 1.0;
    }

    void range()
    {
        QFETCH(double, dataMin);
        QFETCH(double, dataMax);
        QFETCH(int, markings);
        const MetricRange range = MetricRange::covering(dataMin, dataMax, markings);
        QTEST(range.min, "min");
        QTEST(range.max, "max");
        QTEST(range.step, "step");
        // whatever the numbers, the data has to fit
        QVERIFY(range.min <= dataMin && range.max >= dataMax);
    }

    void rangeSurvivesNonsense()
    {
        const double nan = std::nan("");
        const MetricRange range = MetricRange::covering(nan, nan, 5);
        QVERIFY(range.max > range.min);
        QVERIFY(MetricRange::covering(10.0, -10.0, 5).max > MetricRange::covering(10.0, -10.0, 5).min);
    }

    void markingTexts()
    {
        QCOMPARE(MetricRange::covering(0.0, 52.3, 5).markingTexts(5), QStringList({"0", "20", "40", "60", "80"}));
        QCOMPARE(MetricRange::covering(0.0, 9.0, 5).markingTexts(5), QStringList({"0.0", "2.5", "5.0", "7.5", "10.0"}));
        QCOMPARE(MetricRange::covering(0.0, 0.83, 5).markingTexts(5), QStringList({"0.00", "0.25", "0.50", "0.75", "1.00"}));
        QCOMPARE(MetricRange::covering(-6.2, 9.8, 5).markingTexts(5), QStringList({"-10", "-5", "0", "5", "10"}));
    }

    // --- where a value falls on a scale ------------------------------------

    void fraction()
    {
        MetricRange range;
        range.min = 0;
        range.max = 80;
        GaugeManipulation m;
        QCOMPARE(BoundGauge::fraction(m, range, 0), 0.0);
        QCOMPARE(BoundGauge::fraction(m, range, 20), 0.25);
        QCOMPARE(BoundGauge::fraction(m, range, 500), 1.0);   // stops at the end
        QCOMPARE(BoundGauge::fraction(m, range, -5), 0.0);
        QCOMPARE(BoundGauge::fraction(m, range, std::nan("")), 0.0);

        m.reversed = true;
        QCOMPARE(BoundGauge::fraction(m, range, 20), 0.75);
    }

    void fractionWithOwnRangeAndWrap()
    {
        MetricRange range; // 0..100, must be ignored
        GaugeManipulation compass;
        compass.hasValueRange = true;
        compass.minValue = 0;
        compass.maxValue = 360;
        compass.wrapValue = true;
        QCOMPARE(BoundGauge::fraction(compass, range, 90), 0.25);
        QCOMPARE(BoundGauge::fraction(compass, range, 450), 0.25);  // once around
        QCOMPARE(BoundGauge::fraction(compass, range, -90), 0.75);

        GaugeManipulation degenerate;
        degenerate.hasValueRange = true;
        degenerate.minValue = 100;
        degenerate.maxValue = 100;
        QCOMPARE(BoundGauge::fraction(degenerate, range, 100), 0.0); // no division by zero
    }

    // --- drawing ------------------------------------------------------------

    void needleFollowsTheChannel()
    {
        const Gauge gauge = needleGauge();
        QImage image = draw(gauge, 0.0);
        QVERIFY(painted(image, 50, 25));    // up
        QVERIFY(!painted(image, 75, 50));
        image = draw(gauge, 5.0);           // 18 km/h -> 90 degrees
        QVERIFY(painted(image, 75, 50));    // right
        QVERIFY(!painted(image, 50, 25));
        image = draw(gauge, 10.0);          // 36 km/h -> 180 degrees
        QVERIFY(painted(image, 50, 75));    // down
    }

    void needleRestsAtTheStartWithoutData()
    {
        const Gauge gauge = needleGauge();
        // after the end of the recording, with a channel the recording lacks, and with no recording at all
        const QImage after = draw(gauge, 60.0);
        const QImage missing = draw(gauge, 5.0, "power");
        const QImage none = draw(gauge, 5.0, ChannelId::Speed, nullptr);
        for (const QImage *image : {&after, &missing, &none})
            {
                QVERIFY(painted(*image, 50, 25));
                QVERIFY(!painted(*image, 75, 50));
            }
    }

    void needleRestsAtZeroWithoutData()
    {
        // a scale around 0 (grade, g-force): level, not at its end
        Gauge gauge = needleGauge();
        GaugeManipulation &turn = gauge.components[0].manipulations[0];
        turn.minValue = -36;
        turn.maxValue = 36;
        turn.minAngle = -90;
        turn.maxAngle = 90;
        const QImage missing = draw(gauge, 5.0, "grade");
        QVERIFY(painted(missing, 50, 25));     // straight up
        QVERIFY(!painted(missing, 25, 50));
        // a scale that does not reach 0 rests at the end nearest to it
        turn.minValue = 10;
        const QImage start = draw(gauge, 5.0, "grade");
        QVERIFY(painted(start, 25, 50));
    }

    void gaugeIsScaledIntoTarget()
    {
        const BoundGauge bound(needleGauge(), ChannelId::Speed, makeSession(), Units(), GaugeTheme());
        QImage image = canvas(200);
        QPainter painter(&image);
        bound.render(painter, QRectF(100, 100, 100, 100), 0.0);  // same size, moved
        painter.end();
        QVERIFY(painted(image, 150, 125));
        QVERIFY(!painted(image, 50, 25));

        image = canvas(200);
        painter.begin(&image);
        bound.render(painter, QRectF(0, 0, 200, 200), 0.0);      // twice the size
        painter.end();
        QVERIFY(painted(image, 100, 50));
        QVERIFY(painted(image, 93, 30));   // the 8 px bar is 16 px wide now
        QVERIFY(painted(image, 106, 30));
    }

    void painterStateIsRestored()
    {
        const BoundGauge bound(BuiltinGauges::dial(), ChannelId::Speed, makeSession(), Units(), GaugeTheme());
        QImage image = canvas();
        QPainter painter(&image);
        const QTransform before = painter.transform();
        const QFont font = painter.font();
        bound.render(painter, QRectF(10, 10, 50, 50), 3.0);
        QCOMPARE(painter.transform(), before);
        QCOMPARE(painter.font(), font);
        QVERIFY(!painter.hasClipping());
    }

    void themeColoursTheShape()
    {
        // the artwork is magenta; only its shape must survive
        const Gauge gauge = imageGauge(filled(Qt::magenta), QRectF(20, 20, 60, 60), GaugeColor::Accent);
        GaugeTheme theme;
        theme.accent = QColor(0, 200, 0);
        const BoundGauge bound(gauge, ChannelId::Speed, nullptr, Units(), theme);
        QImage image = canvas();
        QPainter painter(&image);
        bound.render(painter, QRectF(0, 0, 100, 100), 0.0);
        painter.end();
        QCOMPARE(image.pixelColor(50, 50), QColor(0, 200, 0));
        QCOMPARE(qAlpha(image.pixel(10, 10)), 0);

        // "original" keeps the picture as it is
        const QImage untouched = draw(imageGauge(filled(Qt::magenta), QRectF(20, 20, 60, 60)), 0.0);
        QCOMPARE(untouched.pixelColor(50, 50), QColor(Qt::magenta));
    }

    void gaugeStartsOnWhatItWasMadeFor()
    {
        Gauge gauge;
        gauge.supportedTypes = {"coolantTemp", "temperature"};
        const QStringList ride{ChannelId::Speed, ChannelId::Temperature};
        // the first it was made for that the data has
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, ride), QString(ChannelId::Temperature));
        // the data lacks it, or there is none yet: still its own, not speed
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, {ChannelId::Speed}), QString("coolantTemp"));
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, {}), QString("coolantTemp"));
        // the other program's names, as gauges imported earlier keep them
        gauge.supportedTypes = {"currentLapTime"};
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, {}), QString(ChannelId::LapTime));
        gauge.supportedTypes = {"gForceLatLon"};
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, {}), QString(ChannelId::GForceLateral));
        // made for nothing in particular
        gauge.supportedTypes = {"defaultDynamic"};
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, ride), QString(ChannelId::Speed));
        QCOMPARE(GaugeLibrary::defaultChannel(gauge, {ChannelId::HeartRate}), QString(ChannelId::HeartRate));
    }

    void svgDrawingInThemeColours()
    {
        // a plate in the theme's background (half transparent) with an accent dot, as a drawing
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        GaugeComponent drawing;
        drawing.type = GaugeComponent::Image;
        drawing.color.role = GaugeColor::Original;
        drawing.frame = QRectF(0, 0, 100, 100);
        drawing.imagePath = "images/plate.svg";
        drawing.svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
                      "<rect width='10' height='5' fill='@background' fill-opacity='@background-opacity'/>"
                      "<circle cx='5' cy='7.5' r='2' fill='@accent'/></svg>";
        gauge.components << drawing;
        GaugeTheme theme;
        theme.background = QColor(0, 0, 255, 128);
        theme.accent = QColor(0, 200, 0);
        // drawn big: a vector drawing stays sharp, its edge a single pixel wide
        const int size = 1000;
        const BoundGauge bound(gauge, ChannelId::Speed, nullptr, Units(), theme);
        QImage image = canvas(size);
        QPainter painter(&image);
        bound.render(painter, QRectF(0, 0, size, size), 0.0);
        painter.end();
        const QColor plate = image.pixelColor(500, 200);
        QVERIFY2(plate.blue() > 240 && std::abs(plate.alpha() - 128) <= 2, qPrintable(plate.name(QColor::HexArgb)));
        QCOMPARE(image.pixelColor(500, 750), QColor(0, 200, 0));
        QCOMPARE(qAlpha(image.pixel(500, 503)), 0);
        // with a colour of its own it is one shape in that colour, as transparent as drawn
        gauge.components[0].color.role = GaugeColor::Accent;
        const QColor tinted = draw(gauge, 0.0).pixelColor(50, 20);
        QVERIFY2(tinted.red() == 255 && std::abs(tinted.green() - 80) <= 2 && std::abs(tinted.blue() - 40) <= 2,
                 qPrintable(tinted.name()));
        QVERIFY2(std::abs(tinted.alpha() - GaugeTheme().background.alpha()) <= 2, qPrintable(tinted.name(QColor::HexArgb)));

        // saved as an .svg next to the gauge, and read back
        const QString folder = m_dir.filePath("svg-gauge");
        QVERIFY(GaugeFile::save(folder, gauge));
        QVERIFY(QFile::exists(folder + "/images/plate.svg"));
        Gauge loaded;
        QVERIFY(GaugeFile::load(folder, &loaded));
        QCOMPARE(loaded.components.first().svg, drawing.svg);
        QVERIFY(loaded.components.first().image.isNull());
    }

    void themeColourKeepsTransparency()
    {
        const Gauge gauge = imageGauge(filled(QColor(255, 255, 255, 128)), QRectF(0, 0, 100, 100), GaugeColor::Background);
        GaugeTheme theme;
        theme.background = QColor(0, 0, 0, 255);
        const BoundGauge bound(gauge, ChannelId::Speed, nullptr, Units(), theme);
        QImage image = canvas();
        QPainter painter(&image);
        bound.render(painter, QRectF(0, 0, 100, 100), 0.0);
        painter.end();
        const int alpha = qAlpha(image.pixel(50, 50));
        QVERIFY2(alpha > 120 && alpha < 136, qPrintable(QString::number(alpha)));
    }

    void radialClipRevealsASlice()
    {
        Gauge gauge = imageGauge(filled(Qt::white), QRectF(0, 0, 100, 100));
        GaugeManipulation clip;
        clip.type = GaugeManipulation::RadialClip;
        clip.pivot = QPointF(50, 50);
        clip.minAngle = 0;      // from 12 o'clock...
        clip.maxAngle = 360;    // ...all the way round, clockwise
        clip.hasValueRange = true;
        clip.minValue = 0;
        clip.maxValue = 36;
        gauge.components[0].manipulations.append(clip);

        QCOMPARE(paintedPixels(draw(gauge, 0.0)), 0);
        const QImage quarter = draw(gauge, 2.5);     // 9 km/h: a quarter, 12 to 3 o'clock
        QVERIFY(painted(quarter, 75, 25));
        QVERIFY(!painted(quarter, 75, 75));
        QVERIFY(!painted(quarter, 25, 25));
        const QImage threeQuarters = draw(gauge, 7.5);
        QVERIFY(painted(threeQuarters, 75, 75));
        QVERIFY(painted(threeQuarters, 25, 75));
        QVERIFY(!painted(threeQuarters, 25, 25));
        QCOMPARE(paintedPixels(draw(gauge, 10.0)), 100 * 100);
    }

    void widthAndHeightClipFillBars()
    {
        Gauge bar = imageGauge(filled(Qt::white), QRectF(0, 0, 100, 100));
        GaugeManipulation width;
        width.type = GaugeManipulation::WidthClip;
        width.minX = 0;
        width.maxX = 100;
        width.hasValueRange = true;
        width.maxValue = 36;
        bar.components[0].manipulations.append(width);
        const QImage half = draw(bar, 5.0);
        QVERIFY(painted(half, 25, 50));
        QVERIFY(!painted(half, 75, 50));

        // a column that fills from the bottom up
        Gauge column = imageGauge(filled(Qt::white), QRectF(0, 0, 100, 100));
        GaugeManipulation height;
        height.type = GaugeManipulation::HeightClip;
        height.minY = 100;
        height.maxY = 0;
        height.hasValueRange = true;
        height.maxValue = 36;
        column.components[0].manipulations.append(height);
        const QImage quarter = draw(column, 2.5);
        QVERIFY(painted(quarter, 50, 90));
        QVERIFY(!painted(quarter, 50, 50));
        const QImage most = draw(column, 7.5);
        QVERIFY(painted(most, 50, 50));
        QVERIFY(!painted(most, 50, 10));
    }

    void translateMovesAndMayRunBackwards()
    {
        Gauge gauge = imageGauge(filled(Qt::white, 10), QRectF(0, 45, 10, 10));
        GaugeManipulation slide;
        slide.type = GaugeManipulation::Translate;
        slide.minX = 0;
        slide.maxX = 90;
        slide.hasValueRange = true;
        slide.maxValue = 36;
        gauge.components[0].manipulations.append(slide);
        QVERIFY(painted(draw(gauge, 0.0), 5, 50));
        QVERIFY(painted(draw(gauge, 10.0), 95, 50));
        QVERIFY(!painted(draw(gauge, 10.0), 5, 50));

        gauge.components[0].manipulations[0].reversed = true;
        QVERIFY(painted(draw(gauge, 0.0), 95, 50));
        QVERIFY(painted(draw(gauge, 10.0), 5, 50));
    }

    // Tapes and compass rings are much larger than the gauge they scroll through.
    void nothingIsDrawnOutsideTheGauge()
    {
        const Gauge gauge = imageGauge(filled(Qt::white), QRectF(-500, -500, 1100, 1100));
        const BoundGauge bound(gauge, ChannelId::Speed, nullptr, Units(), GaugeTheme());
        QImage image = canvas(300);
        QPainter painter(&image);
        bound.render(painter, QRectF(100, 100, 100, 100), 0.0);
        painter.end();
        QCOMPARE(paintedPixels(image), 100 * 100);
        QVERIFY(painted(image, 150, 150));
        QVERIFY(!painted(image, 50, 150));
        QVERIFY(!painted(image, 250, 250));
    }

    void clipRectStaysPut()
    {
        // a window on the left half; the needle swings out of it
        Gauge gauge = needleGauge();
        gauge.components[0].manipulations[0].clipRect = QRectF(0, 0, 60, 100);
        QVERIFY(painted(draw(gauge, 0.0), 50, 25));
        QVERIFY(!painted(draw(gauge, 5.0), 75, 50));
    }

    void componentFollowsItsOwnChannel()
    {
        // the gauge is bound to speed, the needle to heading: 0..360 degrees over a full turn
        Gauge gauge = needleGauge();
        gauge.components[0].channel = ChannelId::Heading;
        GaugeManipulation &turn = gauge.components[0].manipulations[0];
        turn.maxAngle = 360;
        turn.maxValue = 360;
        const QImage image = draw(gauge, 10.0);   // heading 90: right. Speed (36 of 36) would be straight up again
        QVERIFY(painted(image, 75, 50));
        QVERIFY(!painted(image, 50, 25));
    }

    void groupMovesItsChildrenTogether()
    {
        const Gauge needle = needleGauge();
        GaugeComponent group;
        group.type = GaugeComponent::Group;
        group.manipulations = needle.components.first().manipulations;
        GaugeComponent child = needle.components.first();
        child.manipulations.clear();
        group.children.append(child);
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(group);
        QVERIFY(painted(draw(gauge, 5.0), 75, 50));
        QVERIFY(!painted(draw(gauge, 5.0), 50, 25));
    }

    void hideWhenUnknown()
    {
        Gauge gauge = imageGauge(filled(Qt::white), QRectF(0, 0, 100, 100));
        QVERIFY(paintedPixels(draw(gauge, 60.0)) > 0);
        gauge.components[0].hideWhenUnknown = true;
        QVERIFY(paintedPixels(draw(gauge, 5.0)) > 0);
        QCOMPARE(paintedPixels(draw(gauge, 60.0)), 0);
    }

    void textShowsValueOrPlaceholder()
    {
        GaugeComponent text;
        text.type = GaugeComponent::Text;
        text.autoText = GaugeComponent::Value;
        text.frame = QRectF(50, 50, 0, 0);
        text.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        text.fontSize = 40;
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(text);
        const QImage atStart = draw(gauge, 0.0);    // "0.0"
        const QImage atEnd = draw(gauge, 10.0);     // "36.0"
        const QImage outside = draw(gauge, 60.0);   // "--"
        QVERIFY(paintedPixels(atStart) > 0);
        QVERIFY(paintedPixels(outside) > 0);
        QVERIFY(atStart != atEnd);
        QVERIFY(atEnd != outside);
    }

    void textIsAnchoredByItsAlignment()
    {
        GaugeComponent text;
        text.type = GaugeComponent::Text;
        text.text = "MMMM";
        text.frame = QRectF(50, 50, 0, 0);
        text.fontSize = 16;
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(text);
        auto columns = [this](const QImage &image)
        {
            int left = image.width();
            int right = -1;
            for (int y = 0; y < image.height(); ++y)
                {
                    for (int x = 0; x < image.width(); ++x)
                        {
                            if (painted(image, x, y))
                                {
                                    left = qMin(left, x);
                                    right = qMax(right, x);
                                }
                        }
                }
            return qMakePair(left, right);
        };
        gauge.components[0].alignment = Qt::AlignLeft | Qt::AlignVCenter;
        QVERIFY(columns(draw(gauge, 0.0)).first >= 49);
        gauge.components[0].alignment = Qt::AlignRight | Qt::AlignVCenter;
        QVERIFY(columns(draw(gauge, 0.0)).second <= 51);
        gauge.components[0].alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        const auto centred = columns(draw(gauge, 0.0));
        QVERIFY(centred.first < 50 && centred.second > 50);
        QVERIFY(qAbs((centred.first + centred.second) / 2 - 50) <= 2);
    }

    void scaleComesFromTheDataAndTheMarkings()
    {
        // the dial has 9 markings; 36 km/h fits 0..40 in steps of 5
        const BoundGauge bound(BuiltinGauges::dial(), ChannelId::Speed, makeSession(), Units(), GaugeTheme());
        QCOMPARE(bound.rangeOf().min, 0.0);
        QCOMPARE(bound.rangeOf().max, 40.0);
        QCOMPARE(bound.rangeOf().step, 5.0);

        // in mph the same ride needs a different scale: 22.4 mph -> 0..40 in steps of 5 as well
        Units imperial;
        imperial.speed = Units::MilesPerHour;
        const BoundGauge mph(BuiltinGauges::dial(), ChannelId::Speed, makeSession(), imperial, GaugeTheme());
        QCOMPARE(mph.rangeOf().max, 40.0);
        // and bound to heading (0..90): 0..100 in steps of 12.5 does not exist, so 0..160 by 20
        const BoundGauge heading(BuiltinGauges::dial(), ChannelId::Heading, makeSession(), Units(), GaugeTheme());
        QVERIFY(heading.rangeOf().max >= 90.0);
        // no data: a plain 0..100-ish scale rather than nothing
        const BoundGauge empty(BuiltinGauges::dial(), ChannelId::Speed, nullptr, Units(), GaugeTheme());
        QVERIFY(empty.rangeOf().max >= 100.0);
    }

    static GaugeManipulation fullScale(GaugeManipulation m)
    {
        m.hasValueRange = true;
        m.minValue = 0;
        m.maxValue = 36;
        return m;
    }

    void staticRadialClipIgnoresTheValue()
    {
        Gauge gauge = imageGauge(filled(Qt::white), QRectF(0, 0, 100, 100));
        GaugeManipulation mask;
        mask.type = GaugeManipulation::StaticRadialClip;
        mask.pivot = QPointF(50, 50);
        mask.minAngle = 0;
        mask.maxAngle = 90;      // the quarter from 12 to 3 o'clock
        gauge.components[0].manipulations.append(mask);
        for (double time : {0.0, 5.0, 10.0, 60.0})
            {
                const QImage image = draw(gauge, time);
                QVERIFY(painted(image, 75, 25));
                QVERIFY(!painted(image, 25, 75));
                QVERIFY(!painted(image, 75, 75));
            }
    }

    void slantedBarEdge()
    {
        // a bar filling to the middle; with a 45 degree slant the edge passes
        // the centre line at x = 50 and leans right at the top
        Gauge gauge = imageGauge(filled(Qt::white), QRectF(0, 0, 100, 100));
        GaugeManipulation fill;
        fill.type = GaugeManipulation::WidthClip;
        fill.minX = -100;
        fill.maxX = 200;
        fill.clipAngle = 45;
        gauge.components[0].manipulations.append(fullScale(fill));
        const QImage image = draw(gauge, 5.0);   // 18 of 36 -> x = 50
        QVERIFY(painted(image, 45, 50));
        QVERIFY(!painted(image, 55, 50));
        QVERIFY(painted(image, 60, 10));         // top: edge at x = 90
        QVERIFY(!painted(image, 40, 90));        // bottom: edge at x = 10
        QVERIFY(painted(image, 5, 90));
    }

    void ballMovesWithTwoChannels()
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &lateral = session->channelForWriting(ChannelId::GForceLateral, "g");
        Channel &longitudinal = session->channelForWriting(ChannelId::GForceLongitudinal, "g");
        // right turn at 0..10 s, then braking
        lateral.append(0.0, 1.0);
        lateral.append(10.0, 1.0);
        lateral.append(20.0, 0.0);
        lateral.append(30.0, -1.0);
        longitudinal.append(0.0, 0.0);
        longitudinal.append(10.0, 0.0);
        longitudinal.append(20.0, -1.0);
        longitudinal.append(30.0, 0.0);
        Gauge gauge = imageGauge(filled(Qt::white, 10), QRectF(45, 45, 10, 10));
        GaugeManipulation ball;
        ball.type = GaugeManipulation::Translate2D;
        ball.maxRadius = 40;
        gauge.components[0].manipulations.append(ball);

        // bound to the two-value type of .veg files: the two axes are found by themselves
        const QImage right = draw(gauge, 5.0, "gForceLatLon", session);
        QVERIFY(painted(right, 90, 50));
        QVERIFY(!painted(right, 50, 50));
        const QImage braking = draw(gauge, 20.0, "gForceLatLon", session);
        QVERIFY(painted(braking, 50, 90));       // negative forward: down
        // no such data: the ball rests in the middle
        QVERIFY(painted(draw(gauge, 5.0, "gForceLatLon"), 50, 50));

        gauge.components[0].manipulations[0].invertY = true;
        QVERIFY(painted(draw(gauge, 20.0, "gForceLatLon", session), 50, 10));
    }

    void animationPicksPicturesFasterForHigherValues()
    {
        // four pictures side by side, each a solid block at its own height
        QImage strip(400, 100, QImage::Format_ARGB32_Premultiplied);
        strip.fill(Qt::transparent);
        QPainter p(&strip);
        for (int i = 0; i < 4; ++i)
            {
                p.fillRect(QRect(i * 100, i * 25, 100, 25), Qt::white);
            }
        p.end();
        Gauge gauge = imageGauge(strip, QRectF(0, 0, 400, 100));
        GaugeManipulation animation;
        animation.type = GaugeManipulation::AnimationStrip;
        animation.frames = 4;
        animation.minFps = 1;
        animation.maxFps = 1;
        animation.frameRect = QRectF(0, 0, 100, 100);
        gauge.components[0].manipulations.append(animation);
        auto pictureAt = [&](double time)
        {
            const QImage image = draw(gauge, time);
            for (int i = 0; i < 4; ++i)
                {
                    if (painted(image, 50, i * 25 + 12))
                        {
                            return i;
                        }
                }
            return -1;
        };
        // one picture a second, round and round
        QCOMPARE(pictureAt(0.5), 0);
        QCOMPARE(pictureAt(1.5), 1);
        QCOMPARE(pictureAt(3.5), 3);
        QCOMPARE(pictureAt(4.5), 0);
        // only one picture at a time shows
        QCOMPARE(paintedPixels(draw(gauge, 1.5)), 100 * 25);

        // 1..9 pictures a second over 0..36 km/h: the speed doubles the pace
        gauge.components[0].manipulations[0].maxFps = 9;
        gauge.components[0].manipulations[0].minFps = 1;
        auto session = std::make_shared<TelemetrySession>();
        Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
        speed.append(0.0, 0.0);
        speed.append(100.0, 0.0);
        const BoundGauge slow(gauge, ChannelId::Speed, session, Units(), GaugeTheme());
        auto fast = std::make_shared<TelemetrySession>();
        Channel &fastSpeed = fast->channelForWriting(ChannelId::Speed, "m/s");
        // from a standstill, so the scale starts at zero
        fastSpeed.append(0.0, 0.0);
        fastSpeed.append(0.001, 10.0);
        fastSpeed.append(100.0, 10.0);
        const BoundGauge quick(gauge, ChannelId::Speed, fast, Units(), GaugeTheme());
        auto index = [&](const BoundGauge &bound, double time)
        {
            QImage image = canvas();
            QPainter painter(&image);
            bound.render(painter, QRectF(0, 0, 100, 100), time);
            painter.end();
            for (int i = 0; i < 4; ++i)
                {
                    if (painted(image, 50, i * 25 + 12))
                        {
                            return i;
                        }
                }
            return -1;
        };
        // the scale of a channel without markings is 0..40 km/h: 36 km/h is 0.9 of it -> 8.2 pictures a second
        QCOMPARE(index(slow, 10.5), 2);           // 10.5 pictures -> picture 10 % 4
        QCOMPARE(index(quick, 1.0), 0);           // 8.2 -> 8 % 4
        QCOMPARE(index(quick, 1.25), 2);          // 10.25 -> 10 % 4
    }

    void graphLineAndMarkers()
    {
        GaugeComponent graph;
        graph.type = GaugeComponent::Graph;
        graph.frame = QRectF(0, 0, 100, 100);
        graph.lineWidth = 2;
        graph.fillAlpha = 255;
        graph.color.role = GaugeColor::Foreground;
        // a cross-hair marker 4 x 4 at the current point
        GaugeComponent dot;
        dot.type = GaugeComponent::Image;
        dot.image = filled(Qt::red, 4);
        dot.frame = QRectF(-2, -2, 4, 4);
        graph.knob.append(dot);
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(graph);

        // speed rises evenly from 0 to 36 km/h over 10 s: a diagonal, filled below
        const QImage image = draw(gauge, 5.0);
        QVERIFY(painted(image, 75, 90));
        QVERIFY(!painted(image, 25, 10));
        // the marker sits in the middle of the line
        QCOMPARE(image.pixelColor(50, 50), QColor(Qt::red));
        QVERIFY(image.pixelColor(80, 20) != QColor(Qt::red));
        // past the end of the recording there is no marker, only the line
        const QImage after = draw(gauge, 60.0);
        QVERIFY(after.pixelColor(50, 50) != QColor(Qt::red));
        QVERIFY(painted(after, 75, 90));
    }

    void trackMapKeepsTheShapeAndShowsWhereWeAre()
    {
        // north 0..10 s, then east: an L, twice as wide as high in metres
        auto session = std::make_shared<TelemetrySession>();
        Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
        const double north = 0.001;
        const double east = 2.0 * north / std::cos(60.0 * 3.14159265358979323846 / 180.0);
        for (int t = 0; t <= 20; ++t)
            {
                lat.append(t, 60.0 + north * std::min(t, 10) / 10.0);
                lon.append(t, 10.0 + east * std::max(0, t - 10) / 10.0);
            }
        GaugeComponent map;
        map.type = GaugeComponent::TrackMap;
        map.frame = QRectF(0, 0, 100, 100);
        map.padding = 10;
        map.lineWidth = 2;
        GaugeComponent dot;
        dot.type = GaugeComponent::Image;
        dot.image = filled(Qt::red, 6);
        dot.frame = QRectF(-3, -3, 6, 6);
        map.knob.append(dot);
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(map);

        // 80 wide, 40 high, centred: from (10, 70) up to (10, 30), then right to (90, 30)
        const QImage start = draw(gauge, 0.0, ChannelId::Speed, session);
        QVERIFY(painted(start, 10, 50));
        QVERIFY(painted(start, 50, 30));
        QVERIFY(!painted(start, 50, 70));          // no line along the bottom
        QVERIFY(!painted(start, 10, 15));          // the shape is not stretched to the full height
        QCOMPARE(start.pixelColor(10, 70), QColor(Qt::red));
        const QImage corner = draw(gauge, 10.0, ChannelId::Speed, session);
        QCOMPARE(corner.pixelColor(10, 30), QColor(Qt::red));
        QVERIFY(corner.pixelColor(10, 70) != QColor(Qt::red));
    }

    void lapMapShowsTheCurrentLapWithItsStartLine()
    {
        // lap 1 a square in the west, lap 2 a square in the east
        auto session = std::make_shared<TelemetrySession>();
        Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
        const QVector<QPointF> square = {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int lap = 0; lap < 2; ++lap)
            {
                for (int i = 0; i < square.size(); ++i)
                    {
                        const double t = lap * 10 + i * 2 + (lap ? 0.5 : 0.0);
                        lat.append(t, 0.001 * square[i].y());
                        lon.append(t, 0.001 * square[i].x() + lap * 0.01);
                    }
            }
        session->addLap({0.0, 8.0});
        session->addLap({10.5, 18.5});

        GaugeComponent map;
        map.type = GaugeComponent::LapMap;
        map.frame = QRectF(0, 0, 100, 100);
        map.padding = 20;
        map.lineWidth = 2;
        GaugeComponent line;  // a bar across the track: 20 wide, 2 high
        line.type = GaugeComponent::Image;
        line.image = filled(Qt::red, 4);
        line.frame = QRectF(-10, -1, 20, 2);
        map.startLine.append(line);
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(map);

        // each lap fills the frame on its own; both start at the lower left going north
        for (double time : {4.0, 14.0})
            {
                const QImage image = draw(gauge, time, ChannelId::Speed, session);
                QVERIFY(painted(image, 20, 50));
                QVERIFY(painted(image, 80, 50));
                QVERIFY(painted(image, 50, 20));
                // the start line crosses the track at its start, at right angles to it
                QCOMPARE(image.pixelColor(14, 80), QColor(Qt::red));
                QCOMPARE(image.pixelColor(26, 80), QColor(Qt::red));
                QVERIFY(image.pixelColor(20, 86) != QColor(Qt::red));
            }
    }

    void channelIconTakesTheThemeColour()
    {
        GaugeComponent icon;
        icon.type = GaugeComponent::Image;
        icon.channelIcon = true;
        icon.frame = QRectF(0, 0, 100, 100);
        icon.color.role = GaugeColor::Accent;
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(icon);
        GaugeTheme theme;
        theme.accent = QColor(0, 0, 255);
        const BoundGauge heart(gauge, ChannelId::HeartRate, nullptr, Units(), theme);
        const BoundGauge mountain(gauge, ChannelId::Elevation, nullptr, Units(), theme);
        QImage a = canvas();
        QImage b = canvas();
        {
            QPainter painter(&a);
            heart.render(painter, QRectF(0, 0, 100, 100), 0.0);
        }
        {
            QPainter painter(&b);
            mountain.render(painter, QRectF(0, 0, 100, 100), 0.0);
        }
        QVERIFY(paintedPixels(a) > 1000);
        QVERIFY(a != b);                                  // a picture of what is measured
        QCOMPARE(a.pixelColor(50, 50), QColor(0, 0, 255));
    }

    // Static parts come from cached pictures when the painter only moves; a
    // scaled painter draws everything directly. Both must give the same gauge.
    void cachedAndDirectDrawingAgree()
    {
        Gauge gauge = BuiltinGauges::dial();
        GaugeComponent map;          // a route line, cached separately
        map.type = GaugeComponent::TrackMap;
        map.frame = QRectF(100, 100, 200, 200);
        map.lineWidth = 3;
        map.color.role = GaugeColor::Accent;
        gauge.components.insert(1, map);
        const TelemetrySessionPtr demo = DemoSession::create();
        const BoundGauge bound(gauge, ChannelId::Speed, demo, Units(), GaugeTheme());
        for (double time : {50.0, 205.0, 450.0})
            {
                QImage cached = canvas(200);
                {
                    QPainter painter(&cached);
                    bound.render(painter, QRectF(0, 0, 200, 200), time);
                }
                QImage direct = canvas(200);
                {
                    QPainter painter(&direct);
                    painter.scale(2.0, 2.0);
                    bound.render(painter, QRectF(0, 0, 100, 100), time);
                }
                int different = 0;
                for (int y = 0; y < 200; ++y)
                    {
                        for (int x = 0; x < 200; ++x)
                            {
                                const QColor a = cached.pixelColor(x, y);
                                const QColor b = direct.pixelColor(x, y);
                                if (qAbs(a.red() - b.red()) > 48 || qAbs(a.green() - b.green()) > 48
                                        || qAbs(a.blue() - b.blue()) > 48 || qAbs(a.alpha() - b.alpha()) > 48)
                                    {
                                        ++different;
                                    }
                            }
                    }
                // anti-aliased edges may land a little differently, nothing more
                QVERIFY2(different < 200 * 200 / 100, qPrintable(QString("%1 pixels differ at %2 s").arg(different).arg(time)));
            }
    }

    void builtinGauges()
    {
        for (const QString &id : BuiltinGauges::ids())
            {
                const Gauge gauge = BuiltinGauges::byId(id);
                QVERIFY2(!gauge.isEmpty(), qPrintable(id));
                QVERIFY(!gauge.name.isEmpty());
                const QImage slow = draw(gauge, 1.0, ChannelId::Speed, makeSession(), 400);
                const QImage fast = draw(gauge, 9.0, ChannelId::Speed, makeSession(), 400);
                QVERIFY2(paintedPixels(slow) > 400 * 400 / 4, qPrintable(id));
                QVERIFY2(slow != fast, qPrintable(id));
            }
        QVERIFY(BuiltinGauges::byId("builtin:no-such-gauge").isEmpty());
        // the corners of the round dial stay transparent
        QCOMPARE(qAlpha(draw(BuiltinGauges::dial(), 1.0, ChannelId::Speed, makeSession(), 400).pixel(2, 2)), 0);
    }

    // --- native files -------------------------------------------------------

    void jsonRoundTrip()
    {
        Gauge gauge = needleGauge();
        gauge.name = "Round trip";
        gauge.supportedTypes = QStringList({"speed", "power"});
        gauge.unsupportedTypes = QStringList({"elevation"});
        GaugeComponent &image = gauge.components[0];
        image.imagePath = "images/needle.png";
        image.channel = "heading";
        image.hideWhenUnknown = true;
        image.manipulations[0].wrapValue = true;
        image.manipulations[0].reversed = true;
        image.manipulations[0].clipRect = QRectF(1, 2, 3, 4);
        GaugeManipulation slide;
        slide.type = GaugeManipulation::Translate;
        slide.minX = -5;
        slide.maxX = 5;
        slide.minY = 1;
        slide.maxY = 2;
        image.manipulations.append(slide);

        GaugeComponent text;
        text.type = GaugeComponent::Text;
        text.autoText = GaugeComponent::LabelValueUnit;
        text.frame = QRectF(12.5, 30, 0, 0);
        text.fontFamily = "Arial";
        text.fontSize = 18.5;
        text.bold = true;
        text.italic = true;
        text.precision = 1;
        text.alignment = Qt::AlignRight | Qt::AlignBottom;
        text.upperCase = true;
        text.color.role = GaugeColor::Custom;
        text.color.custom = QColor(10, 20, 30, 40);
        GaugeComponent group;
        group.type = GaugeComponent::Group;
        group.children.append(text);
        gauge.components.append(group);

        Gauge loaded;
        QString error;
        QVERIFY2(GaugeFile::fromJson(GaugeFile::toJson(gauge), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name, gauge.name);
        QCOMPARE(loaded.size, gauge.size);
        QCOMPARE(loaded.supportedTypes, gauge.supportedTypes);
        QCOMPARE(loaded.unsupportedTypes, gauge.unsupportedTypes);
        QCOMPARE(loaded.components.size(), 2);

        const GaugeComponent &i = loaded.components.at(0);
        QCOMPARE(i.type, GaugeComponent::Image);
        QCOMPARE(i.frame, QRectF(0, 0, 100, 100));
        QCOMPARE(i.imagePath, QString("images/needle.png"));
        QCOMPARE(i.color.role, GaugeColor::Accent);
        QCOMPARE(i.channel, QString("heading"));
        QVERIFY(i.hideWhenUnknown);
        QCOMPARE(i.manipulations.size(), 2);
        const GaugeManipulation &m = i.manipulations.at(0);
        QCOMPARE(m.type, GaugeManipulation::Rotate);
        QCOMPARE(m.pivot, QPointF(50, 50));
        QCOMPARE(m.maxAngle, 180.0);
        QVERIFY(m.hasValueRange && m.wrapValue && m.reversed);
        QCOMPARE(m.maxValue, 36.0);
        QCOMPARE(m.clipRect, QRectF(1, 2, 3, 4));
        QCOMPARE(i.manipulations.at(1).type, GaugeManipulation::Translate);
        QCOMPARE(i.manipulations.at(1).minX, -5.0);
        QCOMPARE(i.manipulations.at(1).maxY, 2.0);
        QVERIFY(!i.manipulations.at(1).hasValueRange);
        QVERIFY(i.manipulations.at(1).clipRect.isNull());

        const GaugeComponent &g = loaded.components.at(1);
        QCOMPARE(g.type, GaugeComponent::Group);
        QCOMPARE(g.children.size(), 1);
        const GaugeComponent &t = g.children.at(0);
        QCOMPARE(t.autoText, GaugeComponent::LabelValueUnit);
        QCOMPARE(t.frame.topLeft(), QPointF(12.5, 30));
        QCOMPARE(t.fontFamily, QString("Arial"));
        QCOMPARE(t.fontSize, 18.5);
        QVERIFY(t.bold && t.italic && t.upperCase);
        QCOMPARE(t.precision, 1);
        QCOMPARE(t.alignment, Qt::AlignRight | Qt::AlignBottom);
        QCOMPARE(t.color.role, GaugeColor::Custom);
        QCOMPARE(t.color.custom, QColor(10, 20, 30, 40));
    }

    void jsonRoundTripOfGraphsMapsAndAnimations()
    {
        GaugeComponent graph;
        graph.type = GaugeComponent::Graph;
        graph.frame = QRectF(1, 2, 300, 100);
        graph.lineWidth = 3.5;
        graph.fillAlpha = 80;
        graph.fillColor = QColor(1, 2, 3);
        GaugeComponent dot;
        dot.type = GaugeComponent::Image;
        dot.imagePath = "images/dot.png";
        dot.frame = QRectF(-5, -5, 10, 10);
        graph.knob.append(dot);
        graph.xMarker.append(dot);
        graph.yMarker.append(dot);
        GaugeComponent lap;
        lap.type = GaugeComponent::LapMap;
        lap.padding = 15;
        lap.startLine.append(dot);
        GaugeComponent icon;
        icon.type = GaugeComponent::Image;
        icon.channelIcon = true;
        icon.frame = QRectF(0, 0, 32, 32);
        GaugeComponent strip;
        strip.type = GaugeComponent::Image;
        strip.imagePath = "images/strip.png";
        GaugeManipulation animation;
        animation.type = GaugeManipulation::AnimationStrip;
        animation.frames = 9;
        animation.minFps = 5;
        animation.maxFps = 18;
        animation.frameRect = QRectF(0, 0, 92, 70);
        GaugeManipulation ball;
        ball.type = GaugeManipulation::Translate2D;
        ball.maxRadius = 40;
        ball.invertY = true;
        ball.xChannel = "a";
        ball.yChannel = "b";
        GaugeManipulation mask;
        mask.type = GaugeManipulation::StaticRadialClip;
        mask.pivot = QPointF(200, 200);
        mask.minAngle = -149;
        mask.maxAngle = 150;
        GaugeManipulation slant;
        slant.type = GaugeManipulation::HeightClip;
        slant.clipAngle = -23;
        strip.manipulations = {animation, ball, mask, slant};
        Gauge gauge;
        gauge.size = QSizeF(400, 400);
        gauge.components = {graph, lap, icon, strip};

        Gauge loaded;
        QString error;
        QVERIFY2(GaugeFile::fromJson(GaugeFile::toJson(gauge), &loaded, &error), qPrintable(error));
        const GaugeComponent &g = loaded.components[0];
        QCOMPARE(g.type, GaugeComponent::Graph);
        QCOMPARE(g.lineWidth, 3.5);
        QCOMPARE(g.fillAlpha, 80);
        QCOMPARE(g.fillColor, QColor(1, 2, 3));
        QCOMPARE(g.knob.size(), 1);
        QCOMPARE(g.knob[0].imagePath, QString("images/dot.png"));
        QCOMPARE(g.xMarker.size(), 1);
        QCOMPARE(g.yMarker.size(), 1);
        QCOMPARE(loaded.components[1].type, GaugeComponent::LapMap);
        QCOMPARE(loaded.components[1].padding, 15.0);
        QCOMPARE(loaded.components[1].startLine.size(), 1);
        QVERIFY(loaded.components[2].channelIcon);
        const QVector<GaugeManipulation> &m = loaded.components[3].manipulations;
        QCOMPARE(m.size(), 4);
        QCOMPARE(m[0].type, GaugeManipulation::AnimationStrip);
        QCOMPARE(m[0].frames, 9);
        QCOMPARE(m[0].maxFps, 18.0);
        QCOMPARE(m[0].frameRect, QRectF(0, 0, 92, 70));
        QCOMPARE(m[1].type, GaugeManipulation::Translate2D);
        QCOMPARE(m[1].maxRadius, 40.0);
        QVERIFY(m[1].invertY && !m[1].invertX);
        QCOMPARE(m[1].xChannel, QString("a"));
        QCOMPARE(m[1].yChannel, QString("b"));
        QCOMPARE(m[2].type, GaugeManipulation::StaticRadialClip);
        QCOMPARE(m[2].pivot, QPointF(200, 200));
        QCOMPARE(m[2].maxAngle, 150.0);
        QCOMPARE(m[3].clipAngle, -23.0);
    }

    void picturesInsideMarkersAreSaved()
    {
        GaugeComponent dot;
        dot.type = GaugeComponent::Image;
        dot.image = filled(Qt::red, 8);
        dot.frame = QRectF(-4, -4, 8, 8);
        GaugeComponent map;
        map.type = GaugeComponent::TrackMap;
        map.frame = QRectF(0, 0, 100, 100);
        map.knob.append(dot);
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(map);
        const QString folder = m_dir.filePath("knob gauge");
        QString error;
        QVERIFY2(GaugeFile::save(folder, gauge, &error), qPrintable(error));
        Gauge loaded;
        QVERIFY(GaugeFile::load(folder, &loaded, &error));
        QVERIFY(!loaded.components[0].knob[0].image.isNull());
    }

    void rejectsForeignFiles()
    {
        Gauge gauge;
        QString error;
        QVERIFY(!GaugeFile::fromJson("nonsense", &gauge, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!GaugeFile::fromJson(R"({"format": "other"})", &gauge, &error));
        QVERIFY(!GaugeFile::fromJson(R"({"format": "rtvgauge", "version": 99, "size": [1, 1]})", &gauge, &error));
        QVERIFY(error.contains("99"));
        QVERIFY(!GaugeFile::fromJson(R"({"format": "rtvgauge", "version": 1, "size": [0, 0]})", &gauge, &error));
    }

    void folderRoundTripKeepsThePictures()
    {
        const QString folder = m_dir.filePath("saved gauge");
        Gauge gauge = BuiltinGauges::dial();     // its images have no file names yet
        QString error;
        QVERIFY2(GaugeFile::save(folder, gauge, &error), qPrintable(error));
        QVERIFY(QFile::exists(folder + "/gauge.json"));

        Gauge loaded;
        QVERIFY2(GaugeFile::load(folder, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.components.size(), gauge.components.size());
        // drawn from disk it looks the same as drawn from memory
        QCOMPARE(draw(loaded, 4.0, ChannelId::Speed, makeSession(), 400), draw(gauge, 4.0, ChannelId::Speed, makeSession(), 400));

        QVERIFY(!GaugeFile::load(m_dir.filePath("no such folder"), &loaded, &error));
    }

    void imagePathsCannotLeaveTheFolder()
    {
        const QString folder = m_dir.filePath("jail/gauge");
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(filled(Qt::red).save(m_dir.filePath("jail/secret.png")));
        QFile json(folder + "/gauge.json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(R"({"format": "rtvgauge", "version": 1, "size": [100, 100], "components": [
            {"type": "image", "frame": [0, 0, 100, 100], "image": "../secret.png"}]})");
        json.close();
        Gauge loaded;
        QVERIFY(GaugeFile::load(folder, &loaded));
        QCOMPARE(loaded.components.size(), 1);
        QVERIFY(loaded.components.first().image.isNull());
    }

    // --- .veg gauges ---------------------------------------------------

    void vegNamesAndChannels()
    {
        QCOMPARE(VegGaugeImporter::nameFromFileName("____SML_VX_speed"), QString("SML VX speed"));
        QCOMPARE(VegGaugeImporter::channelFor("speed2d"), QString("speed"));
        QCOMPARE(VegGaugeImporter::channelFor("vehicleSpeed"), QString("speed"));
        QCOMPARE(VegGaugeImporter::channelFor("compass"), QString("heading"));
        QCOMPARE(VegGaugeImporter::channelFor("heartRate"), QString("heartRate"));
        QCOMPARE(VegGaugeImporter::channelFor("engineRpm"), QString("engineRpm")); // kept for when such data exists
        QCOMPARE(VegGaugeImporter::channelFor(QString()), QString());
    }

    void vegGaugeIsConverted()
    {
        // written in the shape of a .veg file, with the byte order mark those have
        const QByteArray veg = QByteArray("\xEF\xBB\xBF") + R"({
            "frame": {"width": 250, "height": 70},
            "supportedTypes": ["speed", "speed2d", "speed3d", "vehicleSpeed"],
            "unsupportedTypes": ["elevation"],
            "components": [
                {"type": "image", "imagePath": "images\\plate.png", "colorStyle": "background",
                 "frame": {"x": 0, "y": 0, "width": 250, "height": 70}},
                {"type": "image", "imagePath": "images/needle.png", "colorStyle": {"r": 255, "g": 0, "b": 0, "a": 200},
                 "auxiliaryType": "compass", "hideWhenUnknown": true,
                 "frame": {"x": 0, "y": 0, "width": 70, "height": 70},
                 "manipulation": {"type": "dynamicRotate", "pivotPointX": 35, "pivotPointY": 35, "minAngle": 360, "maxAngle": 0,
                                  "minValue": 0, "maxValue": 360, "wrapValue": true,
                                  "clipRect": {"x": 1, "y": 2, "width": 3, "height": 4}}},
                {"type": "image", "imagePath": "images/bar.png", "frame": {"x": 0, "y": 0, "width": 10, "height": 10},
                 "manipulations": [{"type": "dynamicTranslate", "minX": -20, "maxX": 20, "moveDirection": "maxToMin"},
                                   {"type": "dynamicWidthClip", "minX": 5, "maxX": 95, "clipAngle": 20}]},
                {"type": "text", "autoTextType": "label", "casingTransform": "toUpper", "fontFamily": "Arial Italic",
                 "fontSize": 18, "colorStyle": "accent2", "verticalAlign": "center", "horizontalAlign": "left",
                 "frame": {"x": 67, "y": 17}},
                {"type": "text", "autoTextType": "valueAndUnit", "valuePrecision": 0, "fontFamily": "Roboto Bold",
                 "fontSize": 30, "colorStyle": "autoForeground", "verticalAlign": "bottom", "horizontalAlign": "right",
                 "fontColor": {"r": 1, "g": 2, "b": 3},
                 "textPosition": {"x": 121, "y": 34}, "frame": {"x": 60, "y": 17, "width": 66, "height": 40}},
                {"type": "text", "textValue": "RPM x1000", "fontSize": 12, "frame": {"x": 5, "y": 5}},
                {"type": "group", "auxiliaryType": "grade",
                 "manipulation": {"type": "dynamicRotate", "pivotPointX": 180, "pivotPointY": 180, "minAngle": 45, "maxAngle": -45,
                                  "minValue": -45, "maxValue": 45},
                 "components": [{"type": "text", "autoTextType": "intervalMarking", "textValue": "1000", "fontSize": 14,
                                 "frame": {"x": 1, "y": 2}}]}
            ]})";
        const VegGaugeImporter::Result result = VegGaugeImporter::parse(veg, "Test gauge", QString());
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
        const Gauge &gauge = result.gauge;
        QCOMPARE(gauge.name, QString("Test gauge"));
        QCOMPARE(gauge.size, QSizeF(250, 70));
        QCOMPARE(gauge.supportedTypes, QStringList({"speed"}));   // four names for one channel
        QCOMPARE(gauge.unsupportedTypes, QStringList({"elevation"}));
        QCOMPARE(gauge.components.size(), 7);

        QCOMPARE(gauge.components.at(0).imagePath, QString("images/plate.png"));
        QCOMPARE(gauge.components.at(0).color.role, GaugeColor::Background);
        QCOMPARE(gauge.components.at(0).frame, QRectF(0, 0, 250, 70));

        const GaugeComponent &needle = gauge.components.at(1);
        QCOMPARE(needle.color.role, GaugeColor::Custom);
        QCOMPARE(needle.color.custom, QColor(255, 0, 0, 200));
        QCOMPARE(needle.channel, QString("heading"));
        QVERIFY(needle.hideWhenUnknown);
        QCOMPARE(needle.manipulations.size(), 1);
        QCOMPARE(needle.manipulations.at(0).type, GaugeManipulation::Rotate);
        QCOMPARE(needle.manipulations.at(0).pivot, QPointF(35, 35));
        QCOMPARE(needle.manipulations.at(0).minAngle, 360.0);
        QVERIFY(needle.manipulations.at(0).hasValueRange && needle.manipulations.at(0).wrapValue);
        QCOMPARE(needle.manipulations.at(0).clipRect, QRectF(1, 2, 3, 4));

        const GaugeComponent &bar = gauge.components.at(2);
        QCOMPARE(bar.color.role, GaugeColor::Original);
        QCOMPARE(bar.manipulations.size(), 2);
        QCOMPARE(bar.manipulations.at(0).type, GaugeManipulation::Translate);
        QVERIFY(bar.manipulations.at(0).reversed);
        QVERIFY(!bar.manipulations.at(0).hasValueRange);
        QCOMPARE(bar.manipulations.at(1).type, GaugeManipulation::WidthClip);
        QCOMPARE(bar.manipulations.at(1).maxX, 95.0);

        const GaugeComponent &label = gauge.components.at(3);
        QCOMPARE(label.type, GaugeComponent::Text);
        QCOMPARE(label.autoText, GaugeComponent::Label);
        QVERIFY(label.upperCase && label.italic && !label.bold);
        QCOMPARE(label.fontFamily, QString("Arial"));
        QCOMPARE(label.fontSize, 18.0);
        QCOMPARE(label.color.role, GaugeColor::Accent2);
        QCOMPARE(label.alignment, Qt::AlignLeft | Qt::AlignVCenter);
        QCOMPARE(label.frame.topLeft(), QPointF(67, 17));
        QCOMPARE(label.precision, -1);

        const GaugeComponent &value = gauge.components.at(4);
        QCOMPARE(value.autoText, GaugeComponent::ValueAndUnit);
        QCOMPARE(value.precision, 0);
        QVERIFY(value.bold);
        QCOMPARE(value.fontFamily, QString("Roboto"));
        QCOMPARE(value.alignment, Qt::AlignRight | Qt::AlignBottom);
        QCOMPARE(value.frame.topLeft(), QPointF(121, 34));       // textPosition wins over the frame
        QCOMPARE(value.color.custom, QColor(1, 2, 3));           // and fontColor over colorStyle

        QCOMPARE(gauge.components.at(5).autoText, GaugeComponent::FixedText);
        QCOMPARE(gauge.components.at(5).text, QString("RPM x1000"));

        const GaugeComponent &group = gauge.components.at(6);
        QCOMPARE(group.type, GaugeComponent::Group);
        QCOMPARE(group.channel, QString("grade"));
        QCOMPARE(group.manipulations.size(), 1);
        QCOMPARE(group.manipulations.at(0).minValue, -45.0);
        QCOMPARE(group.children.size(), 1);
        QCOMPARE(group.children.at(0).autoText, GaugeComponent::IntervalMarking);
    }

    void vegPartsLeftOutAreReported()
    {
        const QByteArray veg = R"({"frame": {"width": 100, "height": 100}, "components": [
            {"type": "image", "imagePath": "images/a.png", "frame": {"x": 0, "y": 0, "width": 100, "height": 100}},
            {"type": "image", "imagePath": "images/logo.png", "isLogo": 1, "frame": {"x": 0, "y": 0, "width": 10, "height": 10}},
            {"type": "hologram", "frame": {"x": 0, "y": 0, "width": 10, "height": 10}},
            {"type": "image", "imagePath": "images/b.png", "frame": {"x": 0, "y": 0, "width": 10, "height": 10},
             "manipulation": {"type": "dynamicWobble"}},
            {"type": "image", "imagePath": "images/hit.png", "hitTestingOnly": true, "frame": {"x": 0, "y": 0, "width": 10, "height": 10}}
        ]})";
        const VegGaugeImporter::Result result = VegGaugeImporter::parse(veg, "Partly", QString());
        QVERIFY(result.ok);
        QCOMPARE(result.gauge.components.size(), 1);
        const QString warnings = result.warnings.join('\n');
        QVERIFY2(warnings.contains("logo") && warnings.contains("hologram") && warnings.contains("dynamicWobble"),
                 qPrintable(warnings));
        QVERIFY(!warnings.contains("hit"));
    }

    void vegGraphsMapsAndAnimationsAreConverted()
    {
        const QByteArray veg = R"({"frame": {"width": 600, "height": 350}, "components": [
            {"type": "graphLine", "colorStyle": "accent", "lineWidth": 3, "bottomFillColor": {"a": 80},
             "frame": {"x": 50, "y": 100, "width": 400, "height": 200},
             "xAxisMarkerComponent": {"type": "image", "imagePath": "images/x.png", "frame": {"x": -1, "y": 0, "width": 2, "height": 200}},
             "yAxisMarkerComponent": {"type": "image", "imagePath": "images/y.png", "frame": {"x": 0, "y": -1, "width": 400, "height": 2}},
             "lineMarkerComponent": {"type": "group", "components": [
                 {"type": "image", "imagePath": "images/dot.png", "frame": {"x": -5, "y": -5, "width": 10, "height": 10}}]}},
            {"type": "graphLine", "lineWidth": 2, "bottomFillColor": {"r": 10, "g": 20, "b": 30, "a": 40},
             "frame": {"x": 0, "y": 0, "width": 10, "height": 10}},
            {"type": "trackLine", "padding": 20.0, "thickness": 3, "colorStyle": "autoForeground",
             "frame": {"x": 0, "y": 0, "width": 400, "height": 400},
             "trackKnobComponents": [{"type": "image", "imagePath": "images/dot.png", "frame": {"x": -10, "y": -10, "width": 20, "height": 20}}]},
            {"type": "lapCircuit", "padding": 15.0, "thickness": 5, "frame": {"x": 0, "y": 0, "width": 200, "height": 200},
             "startLineComponents": [{"type": "image", "imagePath": "images/start.png", "frame": {"x": -18, "y": -6, "width": 36, "height": 12}}]},
            {"type": "image", "imagePath": "images/strip.png", "frame": {"x": 0, "y": 0, "width": 828, "height": 70},
             "manipulation": {"type": "animationStrip", "numberOfFrames": 9, "minFramesPerSecond": 5, "maxFramesPerSecond": 18,
                              "frameRect": {"x": 0, "y": 0, "width": 92, "height": 70}}},
            {"type": "image", "imagePath": "images/ball.png", "frame": {"x": 10, "y": 10, "width": 230, "height": 230},
             "manipulation": {"type": "dynamicTranslate", "maxRadius": 40, "invertXAxis": false, "invertYAxis": true}},
            {"type": "image", "imagePath": "images/arc.png", "frame": {"x": 0, "y": 0, "width": 400, "height": 400},
             "manipulation": {"type": "staticRadialClip", "minAngle": -149, "maxAngle": 150, "pivotPointX": 200, "pivotPointY": 200}},
            {"type": "image", "imagePath": "images/bar.png", "frame": {"x": 24, "y": 67, "width": 190, "height": 18},
             "manipulation": {"type": "dynamicWidthClip", "clipAngle": 20, "minX": 13, "maxX": 230}},
            {"autoImageType": "icon", "colorStyle": "accent", "frame": {"height": 32.0, "width": 32.0, "x": 40, "y": 160}, "type": "image"}
        ]})";
        const VegGaugeImporter::Result result = VegGaugeImporter::parse(veg, "Everything", QString());
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
        const QVector<GaugeComponent> &c = result.gauge.components;
        QCOMPARE(c.size(), 9);

        QCOMPARE(c[0].type, GaugeComponent::Graph);
        QCOMPARE(c[0].lineWidth, 3.0);
        QCOMPARE(c[0].fillAlpha, 80);
        QVERIFY(!c[0].fillColor.isValid());           // the line's colour
        QCOMPARE(c[0].xMarker.size(), 1);
        QCOMPARE(c[0].yMarker.size(), 1);
        QCOMPARE(c[0].knob.size(), 1);
        QCOMPARE(c[0].knob[0].type, GaugeComponent::Group);
        QCOMPARE(c[1].fillColor, QColor(10, 20, 30));
        QCOMPARE(c[1].fillAlpha, 40);

        QCOMPARE(c[2].type, GaugeComponent::TrackMap);
        QCOMPARE(c[2].padding, 20.0);
        QCOMPARE(c[2].lineWidth, 3.0);
        QCOMPARE(c[2].knob.size(), 1);
        QCOMPARE(c[3].type, GaugeComponent::LapMap);
        QCOMPARE(c[3].startLine.size(), 1);
        QCOMPARE(c[3].startLine[0].frame, QRectF(-18, -6, 36, 12));

        const GaugeManipulation &strip = c[4].manipulations[0];
        QCOMPARE(strip.type, GaugeManipulation::AnimationStrip);
        QCOMPARE(strip.frames, 9);
        QCOMPARE(strip.minFps, 5.0);
        QCOMPARE(strip.maxFps, 18.0);
        QCOMPARE(strip.frameRect, QRectF(0, 0, 92, 70));

        const GaugeManipulation &ball = c[5].manipulations[0];
        QCOMPARE(ball.type, GaugeManipulation::Translate2D);
        QCOMPARE(ball.maxRadius, 40.0);
        QVERIFY(!ball.invertX && ball.invertY);

        QCOMPARE(c[6].manipulations[0].type, GaugeManipulation::StaticRadialClip);
        QCOMPARE(c[6].manipulations[0].minAngle, -149.0);
        QCOMPARE(c[7].manipulations[0].clipAngle, 20.0);
        QVERIFY(c[8].channelIcon);
        QCOMPARE(c[8].color.role, GaugeColor::Accent);
    }

    void vegRejectsWhatIsNotAGauge()
    {
        QVERIFY(!VegGaugeImporter::parse("not json", "x", QString()).ok);
        QVERIFY(!VegGaugeImporter::parse(R"({"frame": {"width": 0, "height": 0}, "components": []})", "x", QString()).ok);
        QVERIFY(!VegGaugeImporter::parse(R"({"category": "General", "widgets": []})", "x", QString()).ok); // a template
        // nothing left after leaving out the logo
        const VegGaugeImporter::Result onlyLogo = VegGaugeImporter::parse(
                    R"({"frame": {"width": 100, "height": 100}, "components": [
                        {"type": "image", "imagePath": "images/target.png", "isLogo": 1, "frame": {"x": 0, "y": 0, "width": 100, "height": 100}}]})",
                    "x", QString());
        QVERIFY(!onlyLogo.ok);
        QVERIFY(!onlyLogo.error.isEmpty());
        QVERIFY(!VegGaugeImporter::importFile(m_dir.filePath("missing.veg")).ok);
    }

    void vegFileWithImagesFoundIgnoringCase()
    {
        const QString folder = m_dir.filePath("veg");
        QVERIFY(QDir().mkpath(folder + "/Images/VX"));
        QVERIFY(filled(Qt::magenta, 20).save(folder + "/Images/VX/Speed.PNG"));
        QFile veg(folder + "/____SML_test_speed.veg");
        QVERIFY(veg.open(QIODevice::WriteOnly));
        veg.write(R"({"frame": {"width": 100, "height": 100}, "supportedTypes": ["speed"], "components": [
            {"type": "image", "imagePath": "images/vx/speed.png", "colorStyle": "accent", "frame": {"x": 0, "y": 0, "width": 100, "height": 100}},
            {"type": "image", "imagePath": "images/vx/gone.png", "frame": {"x": 0, "y": 0, "width": 100, "height": 100}},
            {"type": "image", "imagePath": "../../outside.png", "frame": {"x": 0, "y": 0, "width": 100, "height": 100}}
        ]})");
        veg.close();

        const VegGaugeImporter::Result result = VegGaugeImporter::importFile(veg.fileName());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.gauge.name, QString("SML test speed"));
        QCOMPARE(result.gauge.components.size(), 1);
        QVERIFY(!result.gauge.components.first().image.isNull());
        QCOMPARE(result.warnings.size(), 2);
        QVERIFY(result.warnings.join(' ').contains("gone.png"));
    }

    // --- the library ----------------------------------------------------------

    void libraryStoresAndFindsGauges()
    {
        const int before = int(GaugeLibrary::entries().size());
        QCOMPARE(before, int(BuiltinGauges::ids().size()));
        QVERIFY(GaugeLibrary::entries().first().builtin);

        Gauge gauge = needleGauge();
        gauge.name = "My needle";
        QString error;
        const QString id = GaugeLibrary::add(gauge, "My needle", &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        QCOMPARE(id, QString("library:My needle"));
        QCOMPARE(int(GaugeLibrary::entries().size()), before + 1);
        QCOMPARE(GaugeLibrary::entries().last().name, QString("My needle"));
        QVERIFY(!GaugeLibrary::entries().last().builtin);

        const Gauge loaded = GaugeLibrary::gauge(id);
        QVERIFY(!loaded.isEmpty());
        QCOMPARE(draw(loaded, 5.0), draw(gauge, 5.0));

        QVERIFY(GaugeLibrary::remove(id));
        QVERIFY(GaugeLibrary::gauge(id).isEmpty());
        QCOMPARE(int(GaugeLibrary::entries().size()), before);
        QVERIFY(!GaugeLibrary::remove(BuiltinGauges::DialId));
    }

    void libraryIdsCannotPointOutside()
    {
        QVERIFY(GaugeLibrary::gauge("library:../library").isEmpty());
        QVERIFY(GaugeLibrary::gauge("library:").isEmpty());
        QVERIFY(GaugeLibrary::gauge("something:else").isEmpty());
        QVERIFY(!GaugeLibrary::remove("library:.."));
        QVERIFY(QDir(GaugeLibrary::userFolder()).exists() || !QDir(m_dir.path()).isEmpty());
        // a name with path characters is flattened into one folder
        const QString id = GaugeLibrary::add(needleGauge(), "../../evil/name");
        QVERIFY(id.startsWith("library:"));
        QVERIFY(!id.contains('/'));
        QVERIFY(QFileInfo(GaugeLibrary::userFolder() + "/" + id.mid(8) + "/gauge.json").exists());
        QVERIFY(GaugeLibrary::remove(id));
    }

    void libraryImportsVegFiles()
    {
        const QString folder = m_dir.filePath("veg-import");
        QVERIFY(QDir().mkpath(folder + "/images"));
        QVERIFY(filled(Qt::white, 20).save(folder + "/images/plate.png"));
        auto write = [&folder](const QString &name, const QByteArray &content)
        {
            QFile file(folder + "/" + name);
            if (file.open(QIODevice::WriteOnly))
                {
                    file.write(content);
                }
            return file.fileName();
        };
        const QString good = write("good_one.veg", R"({"frame": {"width": 100, "height": 100}, "components": [
            {"type": "image", "imagePath": "images/plate.png", "colorStyle": "accent", "frame": {"x": 0, "y": 0, "width": 100, "height": 100}}]})");
        const QString partial = write("partial.veg", R"({"frame": {"width": 100, "height": 100}, "components": [
            {"type": "image", "imagePath": "images/plate.png", "frame": {"x": 0, "y": 0, "width": 100, "height": 100}},
            {"type": "hologram"}]})");
        const QString bad = write("bad.veg", "garbage");

        const GaugeLibrary::ImportSummary summary = GaugeLibrary::importVeg({good, partial, bad});
        QCOMPARE(summary.imported, 2);
        QCOMPARE(summary.incomplete, 1);
        QCOMPARE(summary.failed.size(), 1);
        QVERIFY(summary.failed.first().startsWith("bad.veg"));
        QVERIFY(summary.notes.first().contains("hologram"));

        GaugeLibrary::refresh();
        const Gauge imported = GaugeLibrary::gauge("library:good one");
        QVERIFY(!imported.isEmpty());
        // the picture travelled with it: the library folder is self-contained
        QVERIFY(QFile::exists(GaugeLibrary::userFolder() + "/good one/images/plate.png"));
        QVERIFY(paintedPixels(draw(imported, 1.0)) > 0);

        // importing again replaces, it does not pile up
        QCOMPARE(GaugeLibrary::importVeg({good}).imported, 1);
        int count = 0;
        for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
            {
                count += entry.id == "library:good one" ? 1 : 0;
            }
        QCOMPARE(count, 1);
    }

    // --- shapes, scales, formats -------------------------------------------

    void shapesAreDrawn()
    {
        auto one = [](GaugeComponent::ShapeKind kind, const QRectF &frame, bool filled, double lineWidth = 4.0)
        {
            GaugeComponent c;
            c.type = GaugeComponent::Shape;
            c.shape = kind;
            c.frame = frame;
            c.filled = filled;
            c.lineWidth = lineWidth;
            c.color.role = GaugeColor::Accent;
            Gauge gauge;
            gauge.size = QSizeF(100, 100);
            gauge.components.append(c);
            return gauge;
        };
        const QImage rect = draw(one(GaugeComponent::Rectangle, QRectF(10, 10, 30, 30), true), 0.0);
        QVERIFY(painted(rect, 25, 25));
        QVERIFY(!painted(rect, 50, 50));
        QCOMPARE(QColor(rect.pixel(25, 25)), GaugeTheme().accent);

        const QImage ring = draw(one(GaugeComponent::Ellipse, QRectF(10, 10, 80, 80), false), 0.0);
        QVERIFY(painted(ring, 50, 10));
        QVERIFY(!painted(ring, 50, 50));

        // 12 to 3 o'clock
        Gauge arc = one(GaugeComponent::Arc, QRectF(10, 10, 80, 80), false);
        arc.components[0].startAngle = 0;
        arc.components[0].sweepAngle = 90;
        const QImage quarter = draw(arc, 0.0);
        QVERIFY(painted(quarter, 78, 22));      // on the circle, up right
        QVERIFY(!painted(quarter, 22, 78));
        QVERIFY(!painted(quarter, 22, 22));

        Gauge pie = one(GaugeComponent::Pie, QRectF(10, 10, 80, 80), true);
        pie.components[0].startAngle = 90;      // 3 to 6 o'clock
        pie.components[0].sweepAngle = 90;
        const QImage slice = draw(pie, 0.0);
        QVERIFY(painted(slice, 65, 65));
        QVERIFY(!painted(slice, 35, 65));
        QVERIFY(!painted(slice, 65, 35));

        Gauge line = one(GaugeComponent::Line, QRectF(), false, 6.0);
        line.components[0].points = {QPointF(50, 10), QPointF(50, 90)};
        const QImage stroke = draw(line, 0.0);
        QVERIFY(painted(stroke, 50, 50));
        QVERIFY(!painted(stroke, 60, 50));

        // a fill running from the colour to the gradient's end colour
        Gauge gradient = one(GaugeComponent::Rectangle, QRectF(0, 0, 100, 100), true);
        gradient.components[0].color = GaugeColor{GaugeColor::Custom, QColor(255, 0, 0)};
        gradient.components[0].gradientColor = GaugeColor{GaugeColor::Custom, QColor(0, 0, 255)};
        gradient.components[0].gradientLine = QLineF(0, 0, 100, 0);
        const QImage shaded = draw(gradient, 0.0);
        QVERIFY(qRed(shaded.pixel(2, 50)) > 200 && qBlue(shaded.pixel(2, 50)) < 50);
        QVERIFY(qBlue(shaded.pixel(97, 50)) > 200 && qRed(shaded.pixel(97, 50)) < 50);
    }

    void unevenScaleFollowsItsCurve()
    {
        GaugeManipulation m;
        m.curve = {QPointF(0, 0.0), QPointF(10, 0.5), QPointF(100, 1.0)};
        const MetricRange range;
        QCOMPARE(BoundGauge::fraction(m, range, 0.0), 0.0);
        QCOMPARE(BoundGauge::fraction(m, range, 5.0), 0.25);
        QCOMPARE(BoundGauge::fraction(m, range, 10.0), 0.5);
        QCOMPARE(BoundGauge::fraction(m, range, 55.0), 0.75);
        QCOMPARE(BoundGauge::fraction(m, range, 500.0), 1.0);
        QCOMPARE(BoundGauge::fraction(m, range, -5.0), 0.0);
        m.reversed = true;
        QCOMPARE(BoundGauge::fraction(m, range, 5.0), 0.75);
    }

    void dialDrawsArcTicksAndNumbers()
    {
        GaugeComponent dial;
        dial.type = GaugeComponent::Dial;
        dial.frame = QRectF(10, 10, 80, 80);
        dial.color.role = GaugeColor::Foreground;
        dial.fontSize = 10;
        dial.alignment = Qt::AlignCenter;
        GaugeScale &s = dial.scale;
        s.radius = 40;
        s.startAngle = -90;     // 9 o'clock...
        s.endAngle = 90;        // ...over the top to 3 o'clock
        s.minValue = 0;
        s.maxValue = 100;
        s.major = GaugeTicks{50, 10, 3, GaugeColor{GaugeColor::Custom, Qt::red}};
        s.minor = GaugeTicks{10, 5, 1, GaugeColor{GaugeColor::Custom, Qt::green}};
        s.labelOffset = 22;
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(dial);
        const QImage image = draw(gauge, 0.0);
        // major ticks at 9, 12 and 3 o'clock, inside the circle
        QVERIFY(qRed(image.pixel(50, 14)) > 200);
        QVERIFY(qRed(image.pixel(14, 50)) > 200);
        QVERIFY(qRed(image.pixel(86, 50)) > 200);
        // no scale below
        QVERIFY(!painted(image, 50, 86));
        // the number for the middle under the top tick
        bool numberThere = false;
        for (int y = 22; y < 40; ++y)
            {
                for (int x = 40; x < 60; ++x)
                    {
                        numberThere = numberThere || painted(image, x, y);
                    }
            }
        QVERIFY(numberThere);
    }

    void tapeSlidesWithTheValue()
    {
        GaugeComponent tape;
        tape.type = GaugeComponent::Tape;
        tape.frame = QRectF(0, 0, 100, 40);
        tape.color.role = GaugeColor::Foreground;
        tape.fontSize = 8;
        tape.alignment = Qt::AlignCenter;
        GaugeScale &s = tape.scale;
        s.orientation = GaugeScale::NumbersAbove;
        s.minValue = 0;
        s.maxValue = 100;
        s.length = 80;
        s.spacing = 20;     // 10 km/h every 20 units
        s.major = GaugeTicks{10, 10, 2, GaugeColor{GaugeColor::Custom, Qt::red}};
        s.labelOffset = 4;
        s.markerLength = 30;
        s.markerWidth = 2;
        s.markerColor = GaugeColor{GaugeColor::Custom, Qt::blue};
        tape.unit = "km/h";
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(tape);
        const QImage at18 = draw(gauge, 5.0);    // 18 km/h
        const QImage at36 = draw(gauge, 10.0);   // 36 km/h
        QVERIFY(at18 != at36);
        // the marker stays in the middle
        const QFontMetricsF metrics([] { QFont f; f.setPixelSize(8); return f; }());
        const int middle = int(std::lround(metrics.horizontalAdvance("100") / 2.0 + 40.0));
        bool blue = false;
        for (int x = middle - 1; x <= middle + 1; ++x)
            {
                for (int y = 0; y < 40; ++y)
                    {
                        blue = blue || (qBlue(at18.pixel(x, y)) > 200 && qRed(at18.pixel(x, y)) < 50);
                    }
            }
        QVERIFY(blue);
    }

    void valueFormats()
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
        speed.append(0.0, -3.75);
        speed.append(1.0, 125.5);
        speed.append(2.0, 49.5);
        session->setStartUtc(utc(QDate(2025, 7, 15), QTime(4, 47, 11)));
        session->setUtcOffsetSeconds(3 * 3600);

        GaugeComponent c;
        c.type = GaugeComponent::Text;
        c.autoText = GaugeComponent::Value;
        c.frame = QRectF(5, 5, 190, 90);
        c.boxed = true;
        c.alignment = Qt::AlignLeft | Qt::AlignTop;
        c.unit = "m/s";
        c.precision = 1;

        compareText(c, "-3.8", session, 0.0);
        c.valueFormat = GaugeComponent::WholeNumber;
        compareText(c, "-3", session, 0.0);              // cut off, not rounded
        c.showPlus = true;
        compareText(c, "+125", session, 1.0);
        c.showPlus = false;
        c.clampValue = true;
        c.clampMin = 0;
        c.clampMax = 100;
        compareText(c, "100", session, 1.0);
        compareText(c, "0", session, 0.0);
        c.clampValue = false;
        c.valueFormat = GaugeComponent::Duration;
        compareText(c, "2:05.5", session, 1.0);
        c.valueFormat = GaugeComponent::DegreesMinutesSeconds;
        c.precision = 0;
        compareText(c, "49° 30' 0\"", session, 2.0);
        c.valueFormat = GaugeComponent::Clock;
        c.clockFormat = "HH:mm:ss";
        compareText(c, "07:47:13", session, 2.0);        // the recording's local time
        c.localTime = false;
        compareText(c, "04:47:13", session, 2.0);
    }

    void fixedUnitIsKeptWhateverTheDisplayUnits()
    {
        GaugeComponent c;
        c.type = GaugeComponent::Text;
        c.autoText = GaugeComponent::Value;
        c.valueFormat = GaugeComponent::WholeNumber;
        c.frame = QRectF(5, 5, 190, 90);
        c.boxed = true;
        c.unit = "mph";
        // 10 m/s
        compareText(c, "22", makeSession(), 10.0);
        c.unit = "km/h";
        compareText(c, "36", makeSession(), 10.0);
        c.unit = "kn";
        compareText(c, "19", makeSession(), 10.0);
        // a unit that does not fit the channel is ignored: m/s as stored
        c.unit = "ft";
        compareText(c, "36", makeSession(), 10.0);
        QCOMPARE(ChannelFormat::displayIn(ChannelId::Elevation, "m", "ft", Units()).unit, QString("ft"));
        QCOMPARE(ChannelFormat::displayIn(ChannelId::Temperature, "C", "°F", Units()).convert(100.0), 212.0);
        QCOMPARE(ChannelFormat::displayIn(ChannelId::HeartRate, "bpm", "mph", Units()).scale, 1.0);
    }

    void graphWindowMovesAlong()
    {
        GaugeComponent graph;
        graph.type = GaugeComponent::Graph;
        graph.frame = QRectF(0, 0, 100, 100);
        graph.lineWidth = 2;
        graph.windowBefore = 3.0;
        graph.windowAfter = 1.0;
        GaugeComponent knob;
        knob.type = GaugeComponent::Shape;
        knob.shape = GaugeComponent::Ellipse;
        knob.frame = QRectF(-4, -4, 8, 8);
        knob.color = GaugeColor{GaugeColor::Custom, Qt::red};
        graph.knob.append(knob);
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(graph);
        // the current point sits three quarters along, at any time
        for (const double time : {4.0, 6.0})
            {
                const QImage image = draw(gauge, time);
                bool red = false;
                for (int y = 0; y < 100; ++y)
                    {
                        red = red || (qRed(image.pixel(75, y)) > 200 && qGreen(image.pixel(75, y)) < 50);
                    }
                QVERIFY2(red, qPrintable(QString::number(time)));
            }
        QVERIFY(draw(gauge, 4.0) != draw(gauge, 6.0));
    }

    void jsonRoundTripOfShapesAndScales()
    {
        Gauge gauge;
        gauge.name = "Vector";
        gauge.size = QSizeF(300, 200);
        GaugeComponent rect;
        rect.type = GaugeComponent::Shape;
        rect.shape = GaugeComponent::Rectangle;
        rect.frame = QRectF(1, 2, 3, 4);
        rect.cornerRadius = 5;
        rect.color = GaugeColor{GaugeColor::Custom, QColor(1, 2, 3, 4)};
        rect.gradientColor = GaugeColor{GaugeColor::Accent, QColor()};
        rect.gradientLine = QLineF(1, 2, 3, 4);
        rect.unit = "mph";
        GaugeManipulation clip;
        clip.type = GaugeManipulation::WidthClip;
        clip.curve = {QPointF(0, 0), QPointF(5, 0.8), QPointF(10, 1)};
        rect.manipulations.append(clip);
        GaugeComponent arc;
        arc.type = GaugeComponent::Shape;
        arc.shape = GaugeComponent::Arc;
        arc.filled = false;
        arc.frame = QRectF(0, 0, 50, 50);
        arc.startAngle = -120;
        arc.sweepAngle = 240;
        arc.lineWidth = 7;
        GaugeComponent outline = rect;
        outline.filled = false;
        outline.lineWidth = 2;
        outline.gradientColor = GaugeColor();
        outline.manipulations.clear();
        GaugeComponent line;
        line.type = GaugeComponent::Shape;
        line.shape = GaugeComponent::Line;
        line.filled = false;
        line.points = {QPointF(1, 1), QPointF(2, 3), QPointF(4, 5)};
        line.lineWidth = 3;
        GaugeComponent dial;
        dial.type = GaugeComponent::Dial;
        dial.frame = QRectF(0, 0, 100, 100);
        dial.scale.startAngle = -135;
        dial.scale.endAngle = 135;
        dial.scale.radius = 48;
        dial.scale.arcWidth = 2;
        dial.scale.arcColor = GaugeColor{GaugeColor::Foreground, QColor()};
        dial.scale.major = GaugeTicks{20, 8, 2, GaugeColor{GaugeColor::Accent, QColor()}};
        dial.scale.minor = GaugeTicks{5, 4, 1, GaugeColor{GaugeColor::Custom, QColor(9, 8, 7)}};
        dial.scale.labelOffset = 15;
        dial.fontFamily = "Arial";
        dial.fontSize = 11;
        dial.bold = true;
        dial.alignment = Qt::AlignRight | Qt::AlignBottom;
        GaugeComponent tape;
        tape.type = GaugeComponent::Tape;
        tape.frame = QRectF(0, 0, 40, 200);
        tape.scale.orientation = GaugeScale::NumbersLeft;
        tape.scale.reverse = true;
        tape.scale.loop = true;
        tape.scale.minValue = 0;
        tape.scale.maxValue = 360;
        tape.scale.length = 180;
        tape.scale.spacing = 30;
        tape.scale.markerLength = 20;
        tape.scale.markerWidth = 2;
        tape.scale.markerColor = GaugeColor{GaugeColor::Accent2, QColor()};
        GaugeComponent text;
        text.type = GaugeComponent::Text;
        text.autoText = GaugeComponent::Value;
        text.boxed = true;
        text.frame = QRectF(10, 20, 30, 40);
        text.valueFormat = GaugeComponent::Clock;
        text.clockFormat = "hh:mm AP";
        text.localTime = false;
        text.showPlus = true;
        text.clampValue = true;
        text.clampMin = -1;
        text.clampMax = 2;
        GaugeComponent graph;
        graph.type = GaugeComponent::Graph;
        graph.frame = QRectF(0, 0, 100, 50);
        graph.windowBefore = 60;
        graph.windowAfter = 10;
        gauge.components = {rect, arc, outline, line, dial, tape, text, graph};

        const QByteArray json = GaugeFile::toJson(gauge);
        Gauge loaded;
        QString error;
        QVERIFY2(GaugeFile::fromJson(json, &loaded, &error), qPrintable(error));
        QCOMPARE(GaugeFile::toJson(loaded), json);
        const QVector<GaugeComponent> &c = loaded.components;
        QCOMPARE(c[0].shape, GaugeComponent::Rectangle);
        QVERIFY(c[0].filled);
        QCOMPARE(c[0].cornerRadius, 5.0);
        QCOMPARE(c[0].color.custom, QColor(1, 2, 3, 4));
        QCOMPARE(c[0].gradientColor.role, GaugeColor::Accent);
        QCOMPARE(c[0].gradientLine, QLineF(1, 2, 3, 4));
        QCOMPARE(c[0].unit, QString("mph"));
        QCOMPARE(c[0].manipulations[0].curve.size(), 3);
        QCOMPARE(c[0].manipulations[0].curve[1], QPointF(5, 0.8));
        QCOMPARE(c[1].shape, GaugeComponent::Arc);
        QVERIFY(!c[1].filled);
        QCOMPARE(c[1].startAngle, -120.0);
        QCOMPARE(c[1].sweepAngle, 240.0);
        QCOMPARE(c[1].lineWidth, 7.0);
        QVERIFY(!c[2].filled);
        QCOMPARE(c[2].lineWidth, 2.0);
        QCOMPARE(c[3].points.size(), 3);
        QCOMPARE(c[3].points[2], QPointF(4, 5));
        QCOMPARE(c[4].type, GaugeComponent::Dial);
        QCOMPARE(c[4].scale.endAngle, 135.0);
        QCOMPARE(c[4].scale.radius, 48.0);
        QCOMPARE(c[4].scale.arcWidth, 2.0);
        QVERIFY(c[4].scale.major == dial.scale.major);
        QVERIFY(c[4].scale.minor == dial.scale.minor);
        QCOMPARE(c[4].scale.labelOffset, 15.0);
        QVERIFY(c[4].bold);
        QCOMPARE(c[4].alignment, Qt::AlignRight | Qt::AlignBottom);
        QCOMPARE(c[5].scale.orientation, GaugeScale::NumbersLeft);
        QVERIFY(c[5].scale.reverse && c[5].scale.loop);
        QCOMPARE(c[5].scale.spacing, 30.0);
        QCOMPARE(c[5].scale.markerColor.role, GaugeColor::Accent2);
        QVERIFY(c[6].boxed);
        QCOMPARE(c[6].frame, QRectF(10, 20, 30, 40));
        QCOMPARE(c[6].valueFormat, GaugeComponent::Clock);
        QCOMPARE(c[6].clockFormat, QString("hh:mm AP"));
        QVERIFY(!c[6].localTime && c[6].showPlus && c[6].clampValue);
        QCOMPARE(c[6].clampMax, 2.0);
        QCOMPARE(c[7].windowBefore, 60.0);
        QCOMPARE(c[7].windowAfter, 10.0);
    }

    // --- XML gauges -----------------------------------------------------------

    void xmlChannels()
    {
        QString unit;
        QCOMPARE(XmlGaugeImporter::channelFor("Speed (MPH)", &unit), QString("speed"));
        QCOMPARE(unit, QString("mph"));
        QCOMPARE(XmlGaugeImporter::channelFor("Speed (KPH)", &unit), QString("speed"));
        QCOMPARE(unit, QString("km/h"));
        QCOMPARE(XmlGaugeImporter::channelFor("Altitude / Z Position (Feet)", &unit), QString("elevation"));
        QCOMPARE(unit, QString("ft"));
        QCOMPARE(XmlGaugeImporter::channelFor("Latitude / Y Position (Degrees)", &unit), QString("positionLat"));
        QCOMPARE(unit, QString());
        QCOMPARE(XmlGaugeImporter::channelFor("Air Temperature (F)", &unit), QString("temperature"));
        QCOMPARE(unit, QString("°F"));
        QCOMPARE(XmlGaugeImporter::channelFor("Lateral Gs"), QString("gForceLateral"));
        QCOMPARE(XmlGaugeImporter::channelFor("Heart Rate"), QString("heartRate"));
        // kept for when such data exists
        QCOMPARE(XmlGaugeImporter::channelFor("Engine Oil Pressure (psi)", &unit), QString("engineOilPressurePsi"));
        QCOMPARE(XmlGaugeImporter::channelFor("Power Balance Right (%)"), QString("powerBalanceRight"));
    }

    void xmlGaugeIsConverted()
    {
        // made up here in the shape of such a file: one of everything
        const QByteArray xml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Gauge version="3" x="0" y="0" width="300" height="200" transparency="0.5">
  <Components>
    <FilledRectangle x="0" y="0" width="300" height="200" roundCorners="True" cornerRadius="8" transparency="1" red="10" green="20" blue="30" />
    <AnalogFace x="100" y="100" minValue="0" maxValue="200" minAngle="-225" maxAngle="45" arcRadius="90" labelOffset="20"
        arcPenWidth="2" arcTransparency="1" arcRed="255" arcGreen="255" arcBlue="255"
        minorTickUnit="10" minorTickLength="4" minorTickPenWidth="1" minorTickTransparency="1" minorTickRed="200" minorTickGreen="200" minorTickBlue="200"
        majorTickUnit="20" majorTickLength="8" majorTickPenWidth="2" majorTickTransparency="1" majorTickRed="255" majorTickGreen="0" majorTickBlue="0"
        fontFace="Arial" fontSize="9" fontBold="True" fontItalic="False" fontUnderline="False" fontDirection="horizontal" fontAlign="left" fontVAlign="top"
        fontTransparency="1" fontRed="1" fontGreen="2" fontBlue="3">
      <Components>
        <Needle x="0" y="0" length="80" offset="-10" syncValueRangeWithParent="True" syncAngleRangeWithParent="True" minValue="0" maxValue="1" minAngle="0" maxAngle="1"
            penWidth="3" transparency="1" red="255" green="0" blue="0">
          <Inputs><Input name="Value">MPH</Input></Inputs>
        </Needle>
        <AnimatedArc x="0" y="0" radius="70" syncValueRangeWithParent="False" syncAngleRangeWithParent="True" minValue="0" maxValue="100" loop="True"
            penWidth="5" transparency="1" red="0" green="255" blue="0">
          <Inputs><Input name="Value">MPH</Input></Inputs>
        </AnimatedArc>
        <NonLinearNeedle x="0" y="0" length="50" syncValueRangeWithParent="False" syncAngleRangeWithParent="False" minValue="0" maxValue="100" minAngle="-180" maxAngle="0"
            stepCount="1" stepValue0="10" stepAngle0="-90" penWidth="1" transparency="1" red="0" green="0" blue="255">
          <Inputs><Input name="Value">BPM</Input></Inputs>
        </NonLinearNeedle>
      </Components>
    </AnalogFace>
    <Bar x="200" y="10" width="80" height="10" minValue="0" maxValue="50" direction="BottomToTop"
        startTransparency="1" startRed="0" startGreen="255" startBlue="0" endTransparency="1" endRed="255" endGreen="0" endBlue="0">
      <Inputs><Input name="Value">BPM</Input></Inputs>
    </Bar>
    <Text x="200" y="30" text="MPH" fontRectWidth="60" fontRectHeight="20" fontFace="Arial" fontSize="12" fontBold="False" fontItalic="True" fontUnderline="False"
        fontDirection="horizontal" fontAlign="right" fontVAlign="bottom" fontTransparency="1" fontRed="255" fontGreen="255" fontBlue="255" />
    <DynamicText x="200" y="50" displayType="Running Time" decimalPrecision="1" showPlus="True" restrictRange="True" restrictedMinValue="0" restrictedMaxValue="99"
        fontRectWidth="60" fontRectHeight="20" fontFace="Courier New" fontSize="12" fontBold="False" fontItalic="False" fontUnderline="False"
        fontDirection="horizontal" fontAlign="center" fontVAlign="center" fontTransparency="1" fontRed="255" fontGreen="255" fontBlue="255">
      <Inputs><Input name="Value">MPH</Input></Inputs>
    </DynamicText>
    <DynamicText x="200" y="70" displayType="Time: HH:mm:ss.ff" decimalPrecision="0" showPlus="False" restrictRange="False" convertFromGMT="False"
        fontRectWidth="60" fontRectHeight="20" fontFace="Arial" fontSize="12" fontBold="False" fontItalic="False" fontUnderline="False"
        fontDirection="horizontal" fontAlign="left" fontVAlign="top" fontTransparency="1" fontRed="255" fontGreen="255" fontBlue="255">
      <Inputs><Input name="Value">Time</Input></Inputs>
    </DynamicText>
    <ImageNeedle x="250" y="150" axisX="5" axisY="20" width="10" height="25" src="Needle.PNG" transparency="1" syncValueRangeWithParent="False"
        syncAngleRangeWithParent="False" minValue="0" maxValue="360" minAngle="0" maxAngle="360" loop="True">
      <Inputs><Input name="Value">Heading</Input></Inputs>
    </ImageNeedle>
    <TickerTape x="10" y="10" orientation="Vertial, Units on Left" reverse="False" minValue="0" maxValue="1000" loop="False" length="150" spacing="25"
        minorTickUnit="10" minorTickLength="3" minorTickPenWidth="1" minorTickTransparency="1" minorTickRed="255" minorTickGreen="255" minorTickBlue="255"
        majorTickUnit="50" majorTickLength="6" majorTickPenWidth="1" majorTickTransparency="1" majorTickRed="255" majorTickGreen="255" majorTickBlue="255"
        labelOffset="3" valueLineLength="20" valueLinePenWidth="2" valueLineTransparency="1" valueLineRed="255" valueLineGreen="0" valueLineBlue="0"
        fontFace="Arial" fontSize="7" fontBold="False" fontItalic="False" fontUnderline="False" fontDirection="horizontal" fontAlign="right" fontVAlign="center"
        fontTransparency="1" fontRed="255" fontGreen="255" fontBlue="255">
      <Inputs><Input name="Value">ALT</Input></Inputs>
    </TickerTape>
    <GPlot x="150" y="150" radius="40" maxG="2" flipXAxis="False" flipYAxis="False" majorUnit="1" minorUnit="0.5" showTail="True" tailDuration="3"
        axesPenWidth="1" axesTransparency="1" axesRed="255" axesGreen="255" axesBlue="255"
        majorPenWidth="1" majorTransparency="1" majorRed="255" majorGreen="255" majorBlue="255"
        minorPenWidth="1" minorTransparency="1" minorRed="128" minorGreen="128" minorBlue="128"
        stickPenWidth="1" stickTransparency="1" stickRed="0" stickGreen="0" stickBlue="255" ballRadius="4" ballTransparency="1" ballRed="255" ballGreen="0" ballBlue="0"
        fontFace="Arial" fontSize="7" fontBold="False" fontItalic="False" fontUnderline="False" fontDirection="horizontal" fontAlign="left" fontVAlign="top"
        fontTransparency="1" fontRed="255" fontGreen="255" fontBlue="255">
      <Inputs><Input name="Lateral G">LAT</Input><Input name="Acceleration G">ACC</Input></Inputs>
    </GPlot>
    <Map x="0" y="100" width="100" height="100" align="Center" vAlign="Center" EvenPenWidth="3" EvenTransparency="1" EvenRed="0" EvenGreen="0" EvenBlue="255"
        OddPenWidth="3" OddTransparency="1" OddRed="0" OddGreen="0" OddBlue="255">
      <Components>
        <MapDot x="1" y="2" radius="5" transparency="1" red="255" green="0" blue="0">
          <Inputs><Input name="X">LON</Input><Input name="Y">LAT</Input></Inputs>
        </MapDot>
        <Map x="2" y="2" width="100" height="100" align="Center" vAlign="Center" EvenPenWidth="5" EvenTransparency="0.5" EvenRed="0" EvenGreen="0" EvenBlue="0"
            OddPenWidth="5" OddTransparency="1" OddRed="0" OddGreen="0" OddBlue="0" />
      </Components>
      <Inputs><Input name="X">LON</Input><Input name="Y">LAT</Input></Inputs>
    </Map>
    <ChartComponent x="0" y="0" width="200" height="50" fillChart="True" showAllLaps="False" showAxisLines="True" showCurrentLine="True"
        xScaleAuto="False" xUnitsBackward="60" xUnitsForward="10" yScaleAuto="True" xAxisShowLabel="True"
        backgroundTransparency="0" backgroundRed="0" backgroundGreen="0" backgroundBlue="0"
        axisPenWidth="1" axisTransparency="1" axisRed="200" axisGreen="200" axisBlue="200"
        linePenWidth="2" lineTransparency="1" lineRed="0" lineGreen="0" lineBlue="255"
        fillTransparency="0.5" fillRed="0" fillGreen="0" fillBlue="128"
        currentLinePenWidth="1" currentLineTransparency="1" currentLineRed="255" currentLineGreen="0" currentLineBlue="0"
        currentDotRadius="3" currentDotTransparency="1" currentDotRed="255" currentDotGreen="0" currentDotBlue="0">
      <Inputs><Input name="Value">ALT</Input></Inputs>
    </ChartComponent>
    <Hologram x="0" y="0" />
  </Components>
  <Inputs>
    <Input category="Velocity" name="Speed (MPH)" fromFastestLapSamePos="False">MPH</Input>
    <Input category="Sports" name="Heart Rate" fromFastestLapSamePos="False">BPM</Input>
    <Input category="Positional" name="Altitude / Z Position (Feet)" fromFastestLapSamePos="False">ALT</Input>
    <Input category="Date Time" name="Time of Day" fromFastestLapSamePos="False">Time</Input>
    <Input category="Positional" name="Heading" fromFastestLapSamePos="False">Heading</Input>
    <Input category="G-Forces" name="Lateral Gs" fromFastestLapSamePos="False">LAT</Input>
    <Input category="G-Forces" name="Acceleration Gs" fromFastestLapSamePos="False">ACC</Input>
    <Input category="Positional" name="Longitude / X Position (Degrees)" fromFastestLapSamePos="False">LON</Input>
  </Inputs>
</Gauge>)xml";
        const QString folder = m_dir.filePath("xml/Test Cluster");
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(filled(Qt::white, 10).save(folder + "/needle.png"));
        QFile file(folder + "/gauge.xml");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(xml);
        file.close();

        const XmlGaugeImporter::Result result = XmlGaugeImporter::importFolder(folder);
        QVERIFY2(result.ok, qPrintable(result.error));
        const Gauge &gauge = result.gauge;
        QCOMPARE(gauge.name, QString("Test Cluster"));
        QCOMPARE(gauge.size, QSizeF(300, 200));
        QCOMPARE(gauge.supportedTypes, QStringList({"speed"}));
        const QString warnings = result.warnings.join('\n');
        QVERIFY2(warnings.contains("Hologram") && warnings.contains("G-plot") && warnings.contains("axis labels"), qPrintable(warnings));

        auto find = [&gauge](GaugeComponent::Type type, int skip = 0) -> const GaugeComponent *
        {
            for (const GaugeComponent &c : gauge.components)
                {
                    if (c.type == type && skip-- == 0)
                        {
                            return &c;
                        }
                }
            return nullptr;
        };
        // the gauge's transparency applies to everything
        const GaugeComponent &plate = gauge.components.first();
        QCOMPARE(plate.shape, GaugeComponent::Rectangle);
        QCOMPARE(plate.cornerRadius, 8.0);
        QCOMPARE(plate.color.custom, QColor(10, 20, 30, 128));

        const GaugeComponent *dial = find(GaugeComponent::Dial);
        QVERIFY(dial);
        QCOMPARE(dial->frame, QRectF(10, 10, 180, 180));
        QCOMPARE(dial->scale.startAngle, -135.0);   // counted from 12 o'clock here
        QCOMPARE(dial->scale.endAngle, 135.0);
        QCOMPARE(dial->scale.maxValue, 200.0);
        QCOMPARE(dial->scale.major.unit, 20.0);
        QCOMPARE(dial->scale.major.color.custom, QColor(255, 0, 0, 128));
        QCOMPARE(dial->scale.minor.length, 4.0);
        QCOMPARE(dial->scale.arcWidth, 2.0);
        QCOMPARE(dial->fontSize, 12.0);              // 9 points
        QVERIFY(dial->bold);
        // a number beside a point: "left" puts it to the left, "top" above
        QCOMPARE(dial->alignment, Qt::AlignRight | Qt::AlignBottom);

        // after the dial: the parts inside it, from its middle
        const GaugeComponent *needle = find(GaugeComponent::Shape, 1);
        QVERIFY(needle);
        QCOMPARE(needle->shape, GaugeComponent::Line);
        QCOMPARE(needle->points, QVector<QPointF>({QPointF(100, 110), QPointF(100, 20)}));
        QCOMPARE(needle->lineWidth, 3.0);
        QVERIFY(needle->channel.isEmpty());          // the gauge's own channel
        QCOMPARE(needle->unit, QString("mph"));
        const GaugeManipulation &turn = needle->manipulations.first();
        QCOMPARE(turn.type, GaugeManipulation::Rotate);
        QCOMPARE(turn.pivot, QPointF(100, 100));
        QCOMPARE(turn.minAngle, -135.0);             // the dial's
        QCOMPARE(turn.maxAngle, 135.0);
        QCOMPARE(turn.maxValue, 200.0);

        const GaugeComponent *arc = find(GaugeComponent::Shape, 2);
        QCOMPARE(arc->shape, GaugeComponent::Arc);
        QCOMPARE(arc->frame, QRectF(30, 30, 140, 140));
        QCOMPARE(arc->startAngle, -135.0);
        QCOMPARE(arc->sweepAngle, 270.0);
        QCOMPARE(arc->manipulations.first().type, GaugeManipulation::RadialClip);
        QCOMPARE(arc->manipulations.first().maxValue, 100.0);   // its own values, the dial's angles
        QVERIFY(arc->manipulations.first().wrapValue);

        const GaugeComponent *uneven = find(GaugeComponent::Shape, 3);
        QCOMPARE(uneven->channel, QString("heartRate"));
        QVERIFY(uneven->unit.isEmpty());
        QCOMPARE(uneven->manipulations.first().curve, QVector<QPointF>({QPointF(0, 0), QPointF(10, 0.5), QPointF(100, 1)}));

        const GaugeComponent *bar = find(GaugeComponent::Shape, 4);
        QCOMPARE(bar->shape, GaugeComponent::Rectangle);
        QCOMPARE(bar->gradientColor.custom, QColor(255, 0, 0, 128));
        QCOMPARE(bar->gradientLine, QLineF(200, 20, 200, 9));
        QCOMPARE(bar->manipulations.first().type, GaugeManipulation::HeightClip);
        QCOMPARE(bar->manipulations.first().minY, 20.0);
        QCOMPARE(bar->manipulations.first().maxY, 10.0);

        const GaugeComponent *label = find(GaugeComponent::Text);
        QCOMPARE(label->text, QString("MPH"));
        QVERIFY(label->boxed && label->italic);
        QCOMPARE(label->frame, QRectF(200, 30, 60, 20));
        QCOMPARE(label->alignment, Qt::AlignRight | Qt::AlignBottom);
        QCOMPARE(label->fontSize, 16.0);

        const GaugeComponent *running = find(GaugeComponent::Text, 1);
        QCOMPARE(running->autoText, GaugeComponent::Value);
        QCOMPARE(running->valueFormat, GaugeComponent::Duration);
        QCOMPARE(running->precision, 1);
        QVERIFY(running->showPlus && running->clampValue);
        QCOMPARE(running->clampMax, 99.0);
        QCOMPARE(running->fontFamily, QString("Courier New"));

        const GaugeComponent *clock = find(GaugeComponent::Text, 2);
        QCOMPARE(clock->valueFormat, GaugeComponent::Clock);
        QCOMPARE(clock->clockFormat, QString("HH:mm:ss.zzz"));
        QVERIFY(!clock->localTime);

        const GaugeComponent *picture = find(GaugeComponent::Image);
        QCOMPARE(picture->frame, QRectF(245, 130, 10, 25));
        QCOMPARE(picture->imagePath, QString("images/Needle.PNG"));
        QVERIFY(!picture->image.isNull());           // found ignoring case
        QVERIFY(std::abs(qAlpha(picture->image.pixel(5, 5)) - 128) <= 1);   // half: the gauge's transparency
        QCOMPARE(picture->channel, QString("heading"));
        QCOMPARE(picture->manipulations.first().minAngle, 0.0);   // the picture's own turn
        QCOMPARE(picture->manipulations.first().maxAngle, 360.0);

        const GaugeComponent *tape = find(GaugeComponent::Tape);
        QCOMPARE(tape->scale.orientation, GaugeScale::NumbersLeft);
        QCOMPARE(tape->scale.length, 150.0);
        QCOMPARE(tape->scale.spacing, 25.0);
        QCOMPARE(tape->scale.markerLength, 20.0);
        QCOMPARE(tape->channel, QString("elevation"));
        QCOMPARE(tape->unit, QString("ft"));

        // the G-plot: axes, rings, numbers and a ball that moves
        bool ball = false;
        for (const GaugeComponent &c : gauge.components)
            {
                for (const GaugeManipulation &m : c.manipulations)
                    {
                        if (m.type == GaugeManipulation::Translate2D)
                            {
                                ball = true;
                                QCOMPARE(c.frame, QRectF(146, 146, 8, 8));
                                QCOMPARE(m.maxRadius, 40.0);
                                QCOMPARE(m.maxValue, 2.0);
                                QCOMPARE(m.xChannel, QString("gForceLateral"));
                                QCOMPARE(m.yChannel, QString("gForceLongitudinal"));
                                QVERIFY(!m.invertX && m.invertY);
                            }
                    }
            }
        QVERIFY(ball);

        const GaugeComponent *map = find(GaugeComponent::TrackMap);
        QCOMPARE(map->frame, QRectF(0, 100, 100, 100));
        QCOMPARE(map->lineWidth, 3.0);
        QCOMPARE(map->knob.size(), 1);
        QCOMPARE(map->knob.first().frame, QRectF(-4, -3, 10, 10));
        const GaugeComponent *shadow = find(GaugeComponent::TrackMap, 1);
        QVERIFY(shadow);
        QCOMPARE(shadow->frame, QRectF(2, 102, 100, 100));
        QCOMPARE(shadow->lineWidth, 5.0);

        const GaugeComponent *chart = find(GaugeComponent::Graph);
        QCOMPARE(chart->windowBefore, 60.0);
        QCOMPARE(chart->windowAfter, 10.0);
        QCOMPARE(chart->fillAlpha, 64);
        QCOMPARE(chart->xMarker.size(), 1);
        QCOMPARE(chart->knob.size(), 1);
        QCOMPARE(chart->channel, QString("elevation"));

        // and it draws, and survives being saved
        QVERIFY(paintedPixels(draw(gauge, 5.0, ChannelId::Speed, makeSession(), 300)) > 1000);
        Gauge loaded;
        QVERIFY(GaugeFile::fromJson(GaugeFile::toJson(gauge), &loaded));
        QCOMPARE(GaugeFile::toJson(loaded), GaugeFile::toJson(gauge));
    }

    void xmlNeedlePointsWhereTheOriginalDoes()
    {
        // angles in these files count clockwise from 3 o'clock: 180 is 9 o'clock, 270 is 12
        const QByteArray xml = R"xml(<Gauge version="3" width="100" height="100"><Components>
            <Needle x="50" y="50" length="40" syncValueRangeWithParent="False" syncAngleRangeWithParent="False"
                minValue="0" maxValue="36" minAngle="180" maxAngle="270" penWidth="4" transparency="1" red="255" green="255" blue="255">
              <Inputs><Input name="Value">KPH</Input></Inputs>
            </Needle></Components>
            <Inputs><Input category="Velocity" name="Speed (KPH)">KPH</Input></Inputs></Gauge>)xml";
        const XmlGaugeImporter::Result result = XmlGaugeImporter::parse(xml, "Needle", QString());
        QVERIFY2(result.ok, qPrintable(result.error));
        const QImage atRest = draw(result.gauge, 0.0);
        QVERIFY(painted(atRest, 20, 50));
        QVERIFY(!painted(atRest, 50, 20));
        const QImage atTop = draw(result.gauge, 10.0);   // 36 km/h
        QVERIFY(painted(atTop, 50, 20));
        QVERIFY(!painted(atTop, 20, 50));
    }

    void xmlVersion2NamesGetTheirUnit()
    {
        const QString xml = "<Gauge version='2' width='100' height='100'><Components>"
                            "<DynamicText x='0' y='0' displayType='Integer' fontRectWidth='100' fontRectHeight='100' fontFace='Arial' fontSize='12'"
                            " fontBold='False' fontItalic='False' fontUnderline='False' fontDirection='horizontal' fontAlign='left' fontVAlign='top'"
                            " fontTransparency='1' fontRed='255' fontGreen='255' fontBlue='255'>"
                            "<Inputs><Input name='Value'>ALT</Input></Inputs></DynamicText></Components>"
                            "<Inputs><Input category='Velocity' name='Speed'>MPH</Input>"
                            "<Input category='Positional' name='Altitude / Z Position'>ALT</Input></Inputs></Gauge>";
        const XmlGaugeImporter::Result result = XmlGaugeImporter::parse(xml.toUtf8(), "Old", QString());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.gauge.supportedTypes, QStringList{"speed"});
        QCOMPARE(result.gauge.components.first().channel, QString("elevation"));
        QCOMPARE(result.gauge.components.first().unit, QString("ft"));
    }

    void xmlRejectsWhatIsNotAGauge()
    {
        QVERIFY(!XmlGaugeImporter::parse("not xml", "x", QString()).ok);
        QVERIFY(!XmlGaugeImporter::parse("<Project/>", "x", QString()).ok);
        QVERIFY(!XmlGaugeImporter::parse(R"(<Gauge version="1" width="10" height="10"/>)", "x", QString()).ok);
        QVERIFY(!XmlGaugeImporter::parse(R"(<Gauge version="3" width="0" height="10"/>)", "x", QString()).ok);
        QVERIFY(!XmlGaugeImporter::parse(R"(<Gauge version="3" width="10" height="10"><Components/></Gauge>)", "x", QString()).ok);
        QVERIFY(!XmlGaugeImporter::importFolder(m_dir.filePath("nowhere")).ok);
    }

    void libraryImportsXmlGauges()
    {
        const QString folder = m_dir.filePath("xml-import/Plate");
        QVERIFY(QDir().mkpath(folder));
        QFile file(folder + "/GAUGE.XML");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(<Gauge version="3" width="50" height="50"><Components>
            <Image x="0" y="0" width="50" height="50" src="plate.png" transparency="1"/></Components></Gauge>)");
        file.close();
        QVERIFY(filled(Qt::white, 20).save(folder + "/plate.png"));
        const GaugeLibrary::ImportSummary summary = GaugeLibrary::importXml({folder, m_dir.filePath("xml-import/none")});
        QCOMPARE(summary.imported, 1);
        QCOMPARE(summary.failed.size(), 1);
        GaugeLibrary::refresh();
        const Gauge imported = GaugeLibrary::gauge("library:Plate");
        QVERIFY(!imported.isEmpty());
        QVERIFY(QFile::exists(GaugeLibrary::userFolder() + "/Plate/images/plate.png"));
        QVERIFY(paintedPixels(draw(imported, 1.0)) > 0);
    }

    // Imports every gauge folder in RTV_XML_GAUGES and prints what was found.
    void xmlSampleGauges()
    {
        const QString folder = qEnvironmentVariable("RTV_XML_GAUGES");
        if (folder.isEmpty())
            {
                QSKIP("Set RTV_XML_GAUGES to a folder of XML gauges");
            }
        const QDir dir(folder);
        const TelemetrySessionPtr demo = DemoSession::create();
        int imported = 0;
        int failed = 0;
        for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                const XmlGaugeImporter::Result result = XmlGaugeImporter::importFolder(dir.filePath(name));
                if (!result.ok)
                    {
                        ++failed;
                        qInfo("%s: %s", qPrintable(name), qPrintable(result.error));
                        continue;
                    }
                ++imported;
                if (!result.warnings.isEmpty())
                    {
                        qInfo("%s: %s", qPrintable(name), qPrintable(result.warnings.join("; ")));
                    }
                QVERIFY2(paintedPixels(draw(result.gauge, DemoSession::ShowcaseTime, result.gauge.supportedTypes.value(0), demo)) > 0,
                         qPrintable(name));
            }
        qInfo("%d imported, %d failed", imported, failed);
        QCOMPARE(failed, 0);
    }
};

QTEST_MAIN(TestGauge)
#include "test_gauge.moc"
