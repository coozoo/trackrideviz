#include "gauge/builtingauges.h"
#include "gauge/gaugerenderer.h"
#include "telemetry/channelids.h"
#include "telemetry/telemetrysession.h"

#include <QPainter>
#include <QtTest>

class TestGauge : public QObject
{
    Q_OBJECT

private:
    // speed 0 -> 10 m/s over 10 s
    static TelemetrySession makeSession()
    {
        TelemetrySession session;
        Channel &speed = session.channelForWriting(ChannelId::Speed, "m/s");
        speed.append(0.0, 0.0);
        speed.append(10.0, 10.0);
        return session;
    }

    static QImage canvas(int size = 100)
    {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        return image;
    }

    // A 100x100 gauge with a red bar from the centre up to 12 o'clock,
    // turned around the centre: 0..36 km/h -> 0..180 degrees.
    static Gauge needleGauge()
    {
        QImage bar = canvas();
        QPainter painter(&bar);
        painter.fillRect(QRect(46, 10, 8, 40), Qt::red);
        painter.end();

        GaugeComponent hand;
        hand.type = GaugeComponent::DynamicRotate;
        hand.rect = QRectF(0, 0, 100, 100);
        hand.image = bar;
        hand.pivot = QPointF(50, 50);
        hand.channel = ChannelId::Speed;
        hand.valueScale = 3.6;
        hand.minValue = 0;
        hand.maxValue = 36;
        hand.minAngle = 0;
        hand.maxAngle = 180;

        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(hand);
        return gauge;
    }

    static bool isRed(const QImage &image, int x, int y)
    {
        const QColor c = image.pixelColor(x, y);
        return c.alpha() > 200 && c.red() > 200 && c.green() < 60;
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

private slots:
    void angleIsLinearAndClamped()
    {
        GaugeComponent c;
        c.minValue = 0;
        c.maxValue = 80;
        c.minAngle = -135;
        c.maxAngle = 135;
        QCOMPARE(GaugeRenderer::angleFor(c, 0), -135.0);
        QCOMPARE(GaugeRenderer::angleFor(c, 40), 0.0);
        QCOMPARE(GaugeRenderer::angleFor(c, 80), 135.0);
        QCOMPARE(GaugeRenderer::angleFor(c, -5), -135.0);
        QCOMPARE(GaugeRenderer::angleFor(c, 500), 135.0);
        QCOMPARE(GaugeRenderer::angleFor(c, std::nan("")), -135.0);
    }

    void angleWithReversedScale()
    {
        GaugeComponent c;
        c.minValue = 0;
        c.maxValue = 10;
        c.minAngle = 90;
        c.maxAngle = -90; // counter-clockwise gauge
        QCOMPARE(GaugeRenderer::angleFor(c, 2.5), 45.0);
        c.maxValue = 0; // degenerate range must not divide by zero
        QCOMPARE(GaugeRenderer::angleFor(c, 2.5), 90.0);
    }

    void needleFollowsTheChannel()
    {
        const TelemetrySession session = makeSession();
        const Gauge gauge = needleGauge();

        QImage image = canvas();
        {
            QPainter painter(&image);
            GaugeRenderer::render(painter, gauge, QRectF(0, 0, 100, 100), &session, 0.0);
        }
        QVERIFY(isRed(image, 50, 25));   // up
        QVERIFY(!isRed(image, 75, 50));

        image = canvas();
        {
            QPainter painter(&image);
            GaugeRenderer::render(painter, gauge, QRectF(0, 0, 100, 100), &session, 5.0); // 18 km/h -> 90 degrees
        }
        QVERIFY(isRed(image, 75, 50));   // right
        QVERIFY(!isRed(image, 50, 25));

        image = canvas();
        {
            QPainter painter(&image);
            GaugeRenderer::render(painter, gauge, QRectF(0, 0, 100, 100), &session, 10.0); // 36 km/h -> 180 degrees
        }
        QVERIFY(isRed(image, 50, 75));   // down
    }

    void needleRestsAtMinimumWithoutData()
    {
        const TelemetrySession session = makeSession();
        const Gauge gauge = needleGauge();
        // after the end of the recording the last value (36 km/h) must not stay on the dial
        for (const TelemetrySession *s : {&session, static_cast<const TelemetrySession *>(nullptr)})
            {
                QImage image = canvas();
                QPainter painter(&image);
                GaugeRenderer::render(painter, gauge, QRectF(0, 0, 100, 100), s, 60.0);
                painter.end();
                QVERIFY(isRed(image, 50, 25));
                QVERIFY(!isRed(image, 50, 75));
            }
    }

    void gaugeIsScaledIntoTarget()
    {
        const TelemetrySession session = makeSession();
        QImage image = canvas(200);
        QPainter painter(&image);
        // same size, moved to the lower right quarter
        GaugeRenderer::render(painter, needleGauge(), QRectF(100, 100, 100, 100), &session, 0.0);
        painter.end();
        QVERIFY(isRed(image, 150, 125));
        QVERIFY(!isRed(image, 50, 25));

        image = canvas(200);
        painter.begin(&image);
        GaugeRenderer::render(painter, needleGauge(), QRectF(0, 0, 200, 200), &session, 0.0);
        painter.end();
        QVERIFY(isRed(image, 100, 50));
        QVERIFY(isRed(image, 93, 30));   // the 8 px bar is 16 px wide now
        QVERIFY(isRed(image, 106, 30));
    }

    void painterStateIsRestored()
    {
        QImage image = canvas();
        QPainter painter(&image);
        const QTransform before = painter.transform();
        const QFont font = painter.font();
        GaugeRenderer::render(painter, BuiltinGauges::speedometer(), QRectF(10, 10, 50, 50), nullptr, 0.0);
        QCOMPARE(painter.transform(), before);
        QCOMPARE(painter.font(), font);
    }

    void textShowsValueOrPlaceholder()
    {
        const TelemetrySession session = makeSession();
        GaugeComponent text;
        text.type = GaugeComponent::Text;
        text.rect = QRectF(0, 0, 100, 100);
        text.channel = ChannelId::Speed;
        text.valueScale = 3.6;
        text.fontSize = 40;
        Gauge gauge;
        gauge.size = QSizeF(100, 100);
        gauge.components.append(text);

        auto draw = [&](double time)
        {
            QImage image = canvas();
            QPainter painter(&image);
            GaugeRenderer::render(painter, gauge, QRectF(0, 0, 100, 100), &session, time);
            painter.end();
            return image;
        };
        const QImage atStart = draw(0.0);    // "0"
        const QImage atEnd = draw(10.0);     // "36"
        const QImage outside = draw(60.0);   // "--"
        QVERIFY(paintedPixels(atStart) > 0);
        QVERIFY(paintedPixels(outside) > 0);
        QVERIFY(atStart != atEnd);
        QVERIFY(atEnd != outside);
    }

    void builtinSpeedometer()
    {
        const Gauge gauge = BuiltinGauges::speedometer();
        QVERIFY(!gauge.components.isEmpty());
        const TelemetrySession session = makeSession();
        QImage slow = canvas(400);
        QImage fast = canvas(400);
        {
            QPainter painter(&slow);
            GaugeRenderer::render(painter, gauge, QRectF(0, 0, 400, 400), &session, 1.0);
        }
        {
            QPainter painter(&fast);
            GaugeRenderer::render(painter, gauge, QRectF(0, 0, 400, 400), &session, 9.0);
        }
        QVERIFY(paintedPixels(slow) > 400 * 400 / 2); // the dial covers most of the square
        QVERIFY(slow != fast);
        QCOMPARE(qAlpha(slow.pixel(2, 2)), 0);        // corners stay transparent

        // the same speed reads differently on a mph dial
        Units imperial;
        imperial.speed = Units::MilesPerHour;
        QImage mph = canvas(400);
        {
            QPainter painter(&mph);
            GaugeRenderer::render(painter, BuiltinGauges::speedometer(imperial), QRectF(0, 0, 400, 400), &session, 9.0);
        }
        QVERIFY(mph != fast);
    }
};

QTEST_MAIN(TestGauge)
#include "test_gauge.moc"
