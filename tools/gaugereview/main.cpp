// Pictures for comparing gauges side by side (see gaugereview.pro).
//
// --sheet: every gauge in a folder on contact sheets, 24 to a picture.
// --pairs: for each entry of a mapping file
//     [{"theirs": "<folder>", "ours": "<folder>", "channel": "speed", "note": "..."}]
// the two gauges at three moments of the sample ride, and index.html showing
// them row by row.

#include "gauge/gaugefile.h"
#include "gauge/gaugerenderer.h"
#include "laps/lapengine.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"
#include "telemetry/derivedchannels.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <cstdio>

namespace
{
const double kMoments[] = {60.0, DemoSession::ShowcaseTime, 420.0};

TelemetrySessionPtr sampleRide()
{
    const TelemetrySessionPtr ride = DerivedChannels::complete(DemoSession::create());
    // laps too, for lap gauges
    return LapEngine::apply(ride, LapEngine::lineAt(*ride, 20.0));
}

// The first channel the gauge was made for that the sample ride has.
QString channelFor(const Gauge &gauge, const QString &wanted, const TelemetrySessionPtr &ride)
{
    if (!wanted.isEmpty())
        {
            return wanted;
        }
    for (const QString &type : gauge.supportedTypes)
        {
            if (ride->channel(type))
                {
                    return type;
                }
        }
    return ChannelId::Speed;
}

// The gauge fitted into a square of `size`, on a dark checked ground so
// transparent parts show.
void paint(QPainter &painter, const Gauge &gauge, const QString &channel, const TelemetrySessionPtr &ride,
           double time, int size)
{
    const int cell = 16;
    for (int y = 0; y < size; y += cell)
        {
            for (int x = 0; x < size; x += cell)
                {
                    painter.fillRect(x, y, cell, cell, ((x + y) / cell) % 2 ? QColor(70, 78, 70) : QColor(60, 66, 60));
                }
        }
    if (gauge.isEmpty())
        {
            painter.setPen(Qt::white);
            painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, "missing");
            return;
        }
    const double scale = std::min(size / gauge.size.width(), size / gauge.size.height()) * 0.92;
    const QSizeF shown = gauge.size * scale;
    const QRectF target(QPointF((size - shown.width()) / 2.0, (size - shown.height()) / 2.0), shown);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const BoundGauge bound(gauge, channel, ride, Units(), GaugeTheme());
    bound.render(painter, target, time);
}

QImage render(const Gauge &gauge, const QString &channel, const TelemetrySessionPtr &ride, double time, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    paint(painter, gauge, channel, ride, time, size);
    return image;
}

Gauge load(const QString &folder)
{
    Gauge gauge;
    QString error;
    if (!GaugeFile::load(folder, &gauge, &error))
        {
            std::fprintf(stderr, "%s: %s\n", qPrintable(folder), qPrintable(error));
        }
    return gauge;
}

int sheets(const QString &folder, const QString &out)
{
    const TelemetrySessionPtr ride = sampleRide();
    QDir().mkpath(out);
    const QStringList names = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    const int perSheet = 24;
    const int columns = 6;
    const int size = 200;
    const int label = 18;
    for (int first = 0; first < names.size(); first += perSheet)
        {
            const int count = std::min(perSheet, int(names.size()) - first);
            const int rows = (count + columns - 1) / columns;
            QImage sheet(columns * size, rows * (size + label), QImage::Format_ARGB32_Premultiplied);
            sheet.fill(QColor(30, 30, 30));
            QPainter painter(&sheet);
            for (int i = 0; i < count; ++i)
                {
                    const QString name = names.at(first + i);
                    const Gauge gauge = load(folder + "/" + name);
                    const QPoint at((i % columns) * size, (i / columns) * (size + label));
                    painter.drawImage(at, render(gauge, channelFor(gauge, QString(), ride), ride, DemoSession::ShowcaseTime, size));
                    painter.setPen(Qt::white);
                    painter.drawText(QRect(at.x(), at.y() + size, size, label), Qt::AlignCenter,
                                     QString("%1 %2").arg(first + i + 1).arg(name));
                }
            const QString file = QString("%1/sheet%2.png").arg(out).arg(first / perSheet + 1, 2, 10, QChar('0'));
            sheet.save(file);
            std::printf("%s\n", qPrintable(file));
        }
    return 0;
}

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

int pairs(const QString &mappingFile, const QString &theirs, const QString &ours, const QString &out)
{
    QFile file(mappingFile);
    if (!file.open(QIODevice::ReadOnly))
        {
            std::fprintf(stderr, "cannot read %s\n", qPrintable(mappingFile));
            return 1;
        }
    const QJsonArray mapping = QJsonDocument::fromJson(file.readAll()).array();
    const TelemetrySessionPtr ride = sampleRide();
    QDir().mkpath(out + "/img");
    QString html = "<!doctype html><html><head><meta charset='utf-8'><title>Gauge review</title><style>"
                   "body{background:#1e1e1e;color:#ddd;font:14px sans-serif;margin:16px}"
                   "table{border-collapse:collapse}td{padding:6px;vertical-align:top;border-bottom:1px solid #444}"
                   "img{width:220px;height:220px;display:inline-block;margin-right:4px}"
                   "h3{margin:4px 0}.n{color:#999;max-width:260px}"
                   "</style></head><body><h1>Gauge review</h1>"
                   "<p>Left: the gauge it replaces. Right: ours. Same sample data, three moments. These are snapshots; the gauges themselves are drawings.</p><table>";
    int row = 0;
    for (const QJsonValue &value : mapping)
        {
            const QJsonObject entry = value.toObject();
            const QString theirName = entry.value("theirs").toString();
            const QString ourName = entry.value("ours").toString();
            const Gauge their = load(theirs + "/" + theirName);
            const Gauge our = ourName.isEmpty() ? Gauge() : load(ours + "/" + ourName);
            const QString channel = channelFor(their, entry.value("channel").toString(), ride);
            ++row;
            html += QString("<tr><td><h3>%1. %2</h3><div class='n'>theirs: %3<br>channel: %4<br>%5</div></td>")
                    .arg(row).arg(escaped(ourName.isEmpty() ? QString("(not made yet)") : ourName), escaped(theirName),
                                  escaped(channel), escaped(entry.value("note").toString()));
            for (const bool mine : {false, true})
                {
                    html += "<td>";
                    for (int m = 0; m < 3; ++m)
                        {
                            // twice the size shown, so it is sharp on any screen and when zoomed
                            const QString name = QString("img/%1-%2-%3.png").arg(row, 3, 10, QChar('0')).arg(mine ? "ours" : "theirs").arg(m);
                            render(mine ? our : their, channel, ride, kMoments[m], 880).save(out + "/" + name);
                            html += QString("<img src='%1'>").arg(name);
                        }
                    html += "</td>";
                }
            html += "</tr>";
        }
    html += "</table></body></html>";
    QFile index(out + "/index.html");
    if (!index.open(QIODevice::WriteOnly))
        {
            return 1;
        }
    index.write(html.toUtf8());
    std::printf("%d pairs: %s/index.html\n", row, qPrintable(out));
    return 0;
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() == 3 && args.first() == "--sheet")
        {
            return sheets(args.at(1), args.at(2));
        }
    if (args.size() == 5 && args.first() == "--pairs")
        {
            return pairs(args.at(1), args.at(2), args.at(3), args.at(4));
        }
    std::fprintf(stderr, "usage: gaugereview --sheet <gauge folders> <out>\n"
                 "       gaugereview --pairs <mapping.json> <their gauges> <our gauges> <out>\n");
    return 2;
}
