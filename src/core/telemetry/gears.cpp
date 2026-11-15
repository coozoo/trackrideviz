#include "gears.h"

#include "channelids.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const double kPi = 3.14159265358979323846;
// what a usual road wheel goes in a turn, when nothing better is known
const double kUsualWheelMm = 2105.0;
// below this the cranks are not driving the wheel, or the numbers are too small to tell gears apart
const double kLeastCadence = 25.0;   // rpm
const double kLeastSpeed = 1.5;      // m/s
// ratios this far off every gear are freewheeling or a glitch: nothing is decided
const double kMostOff = 0.08;
// the gear in use stays while it explains the ratio this well: speed and cadence never agree exactly
const double kStillFits = 0.05;
// the front changes only when no sprocket on the ring in use explains the ratio this well,
// and another ring does: a sprocket is the usual shift, a chainring the rare one
const double kRearFits = 0.065;
// seconds a new gear must hold before it counts
const int kHold = 3;

bool has(const TelemetrySession &session, const char *id)
{
    const Channel *channel = session.channel(id);
    return channel && !channel->isEmpty();
}

// How far apart two ratios are, as a fraction: the same either way round.
double offBy(double a, double b)
{
    return std::fabs(std::log(a / b));
}
}

double Drivetrain::circumferenceMm() const
{
    if (wheelMm > 0.0)
        {
            return wheelMm;
        }
    const double fromTire = Gears::circumferenceOf(tireSize);
    return fromTire > 0.0 ? fromTire : kUsualWheelMm;
}

QVector<int> Gears::parseTeeth(const QString &text)
{
    QVector<int> teeth;
    static const QRegularExpression number(R"(\d+)");
    for (auto it = number.globalMatch(text); it.hasNext();)
        {
            const int n = it.next().captured(0).toInt();
            // no chainring or sprocket has fewer than 8 teeth or more than 80
            if (n >= 8 && n <= 80 && !teeth.contains(n))
                {
                    teeth.append(n);
                }
        }
    std::sort(teeth.begin(), teeth.end());
    return teeth;
}

QString Gears::teethText(const QVector<int> &teeth)
{
    QStringList parts;
    for (const int n : teeth)
        {
            parts << QString::number(n);
        }
    return parts.join(", ");
}

double Gears::circumferenceOf(const QString &tireSize)
{
    // the rim's bead seat (ETRTO) by the names wheels go by
    const QString size = tireSize.trimmed().toLower().replace(',', '.');
    static const QRegularExpression etrto(R"(^(\d{2,3})\s*-\s*(\d{3})$)");
    static const QRegularExpression crossed(R"(^(\d{2,3}(?:\.\d+)?)\s*([a-c]?)\s*[x×]\s*(\d+(?:\.\d+)?)\s*([a-c]?)$)");
    double bead = 0.0;
    double width = 0.0;
    QRegularExpressionMatch match = etrto.match(size);
    if (match.hasMatch())
        {
            width = match.captured(1).toDouble();
            bead = match.captured(2).toDouble();
        }
    else if ((match = crossed.match(size)).hasMatch())
        {
            const double wheel = match.captured(1).toDouble();
            const QString letter = match.captured(2) + match.captured(4);
            width = match.captured(3).toDouble();
            if (width < 10.0)
                {
                    width *= 25.4;   // inches
                }
            if (wheel == 700.0)
                {
                    bead = letter == "c" || letter.isEmpty() ? 622.0 : letter == "b" ? 584.0 : 571.0;
                }
            else if (wheel == 650.0)
                {
                    bead = letter == "c" ? 571.0 : 584.0;
                }
            else if (wheel == 29.0 || wheel == 28.0)
                {
                    bead = 622.0;
                }
            else if (wheel == 27.5)
                {
                    bead = 584.0;
                }
            else if (wheel == 26.0)
                {
                    bead = 559.0;
                }
            else if (wheel == 24.0)
                {
                    bead = 507.0;
                }
            else if (wheel == 20.0)
                {
                    bead = 406.0;
                }
            else if (wheel == 16.0)
                {
                    bead = 305.0;
                }
        }
    if (bead <= 0.0 || width <= 0.0 || width > 130.0)
        {
            return 0.0;
        }
    // the tire stands about as high as it is wide
    return kPi * (bead + 2.0 * width);
}

QVector<Gears::Preset> Gears::chainringPresets()
{
    return
    {
        {tr("One ring, 32"), {32}}, {tr("One ring, 34"), {34}}, {tr("One ring, 40 (gravel)"), {40}},
        {tr("Compact, 50/34"), {34, 50}}, {tr("Semi-compact, 52/36"), {36, 52}}, {tr("Standard, 53/39"), {39, 53}},
        {tr("Gravel, 46/30"), {30, 46}}, {tr("Mountain bike triple, 44/32/22"), {22, 32, 44}},
        {tr("Road triple, 50/39/30"), {30, 39, 50}},
    };
}

QVector<Gears::Preset> Gears::cassettePresets()
{
    return
    {
        {tr("8 speed, 11-34"), {11, 13, 15, 18, 21, 24, 28, 34}},
        {tr("10 speed, 11-36"), {11, 13, 15, 17, 19, 21, 24, 28, 32, 36}},
        {tr("11 speed, 11-28"), {11, 12, 13, 14, 15, 17, 19, 21, 23, 25, 28}},
        {tr("11 speed, 11-32"), {11, 12, 13, 14, 16, 18, 20, 22, 25, 28, 32}},
        {tr("11 speed, 11-34"), {11, 13, 15, 17, 19, 21, 23, 25, 27, 30, 34}},
        {tr("12 speed, 10-50 (SRAM Eagle)"), {10, 12, 14, 16, 18, 21, 24, 28, 32, 36, 42, 50}},
        {tr("12 speed, 10-52 (SRAM Eagle)"), {10, 12, 14, 16, 18, 21, 24, 28, 32, 36, 42, 52}},
        {tr("12 speed, 10-51 (Shimano)"), {10, 12, 14, 16, 18, 21, 24, 28, 33, 39, 45, 51}},
    };
}

TelemetrySessionPtr Gears::apply(const TelemetrySessionPtr &original, const Drivetrain &drivetrain)
{
    if (!original)
        {
            return original;
        }
    const bool recordedRear = has(*original, ChannelId::RearGear);
    const bool recordedFront = has(*original, ChannelId::FrontGear);
    if (recordedRear)
        {
            // the gears the bike recorded: the ratio from them, the front of a single ring from the settings
            if (has(*original, ChannelId::GearRatio) || (!recordedFront && drivetrain.chainrings.size() != 1))
                {
                    return original;
                }
            auto session = std::make_shared<TelemetrySession>(*original);
            const Channel rear = *session->channel(ChannelId::RearGear);
            const Channel *front = recordedFront ? session->channel(ChannelId::FrontGear) : nullptr;
            const Channel frontCopy = front ? *front : Channel();
            Channel &ratio = session->channelForWriting(ChannelId::GearRatio, QString(), Channel::Step);
            for (int i = 0; i < rear.count(); ++i)
                {
                    const double t = rear.timeAt(i);
                    const double ring = recordedFront ? frontCopy.valueAt(t) : drivetrain.chainrings.first();
                    if (rear.valueAtIndex(i) > 0.0 && ring > 0.0)
                        {
                            ratio.append(t, ring / rear.valueAtIndex(i));
                        }
                }
            session->addDerivedChannel(ChannelId::GearRatio);
            return session;
        }
    const Channel *speed = original->channel(ChannelId::Speed);
    const Channel *cadence = original->channel(ChannelId::Cadence);
    if (drivetrain.isEmpty() || !speed || speed->count() < 2 || !cadence || cadence->count() < 2)
        {
            return original;
        }

    const double meters = drivetrain.circumferenceMm() / 1000.0;
    const QVector<int> &rings = drivetrain.chainrings;
    const QVector<int> &sprockets = drivetrain.cassette;
    struct Gear
    {
        int front{-1};   // index into rings
        int rear{-1};    // index into sprockets
        bool isValid() const { return front >= 0 && rear >= 0; }
        bool operator==(const Gear &other) const { return front == other.front && rear == other.rear; }
    };
    const auto ratioOf = [&](const Gear &gear) { return double(rings.at(gear.front)) / sprockets.at(gear.rear); };
    // the sprocket on `front` nearest to `ratio`, and how far off it is
    const auto bestRear = [&](int front, double ratio, double *off)
    {
        Gear best{front, 0};
        *off = std::numeric_limits<double>::max();
        for (int r = 0; r < sprockets.size(); ++r)
            {
                const double o = offBy(ratio, double(rings.at(front)) / sprockets.at(r));
                if (o < *off)
                    {
                        *off = o;
                        best.rear = r;
                    }
            }
        return best;
    };

    struct Change
    {
        double time;
        Gear gear;
    };
    QVector<Change> changes;
    Gear current;
    Gear pending;
    double pendingSince = 0.0;
    int pendingFor = 0;
    const double start = std::max(speed->startTime(), cadence->startTime());
    const double end = std::min(speed->endTime(), cadence->endTime());
    for (double t = start; t <= end; t += 1.0)
        {
            bool paused = false;
            for (const TelemetrySession::Pause &p : original->pauses())
                {
                    paused = paused || (t > p.startTime && t < p.endTime);
                }
            // three seconds of each, so that one sample out of step decides nothing
            double v = 0.0;
            double rpm = 0.0;
            for (const double d : {-1.0, 0.0, 1.0})
                {
                    v += speed->valueAt(t + d) / 3.0;
                    rpm += cadence->valueAt(t + d) / 3.0;
                }
            if (paused || rpm < kLeastCadence || v < kLeastSpeed)
                {
                    pendingFor = 0;
                    continue;
                }
            const double ratio = v / (rpm / 60.0 * meters);
            // the best everywhere, and the best without changing the front
            Gear best;
            double bestOff = std::numeric_limits<double>::max();
            for (int f = 0; f < rings.size(); ++f)
                {
                    double off = 0.0;
                    const Gear g = bestRear(f, ratio, &off);
                    if (off < bestOff)
                        {
                            bestOff = off;
                            best = g;
                        }
                }
            Gear chosen = best;
            double chosenOff = bestOff;
            if (current.isValid() && offBy(ratio, ratioOf(current)) < kStillFits)
                {
                    chosen = current;
                    chosenOff = 0.0;
                }
            else if (current.isValid() && best.front != current.front)
                {
                    double off = 0.0;
                    const Gear same = bestRear(current.front, ratio, &off);
                    if (off < kRearFits || off - bestOff < kRearFits)
                        {
                            chosen = same;
                            chosenOff = off;
                        }
                }
            if (chosenOff > kMostOff)
                {
                    // pedaling slower than the wheel turns, or a glitch
                    pendingFor = 0;
                    continue;
                }
            if (chosen == current)
                {
                    pendingFor = 0;
                    continue;
                }
            if (pendingFor > 0 && chosen == pending)
                {
                    ++pendingFor;
                }
            else
                {
                    pending = chosen;
                    pendingSince = t;
                    pendingFor = 1;
                }
            if (pendingFor >= kHold)
                {
                    current = pending;
                    changes.append({pendingSince, current});
                    pendingFor = 0;
                }
        }
    if (changes.isEmpty())
        {
            return original;
        }
    auto session = std::make_shared<TelemetrySession>(*original);
    Channel &frontTeeth = session->channelForWriting(ChannelId::FrontGear, "T", Channel::Step);
    Channel &rearTeeth = session->channelForWriting(ChannelId::RearGear, "T", Channel::Step);
    Channel &frontNumber = session->channelForWriting(ChannelId::FrontGearNumber, QString(), Channel::Step);
    Channel &rearNumber = session->channelForWriting(ChannelId::RearGearNumber, QString(), Channel::Step);
    Channel &ratio = session->channelForWriting(ChannelId::GearRatio, QString(), Channel::Step);
    for (const Change &change : std::as_const(changes))
        {
            frontTeeth.append(change.time, rings.at(change.gear.front));
            rearTeeth.append(change.time, sprockets.at(change.gear.rear));
            frontNumber.append(change.time, change.gear.front + 1);
            // the largest sprocket is the first gear
            rearNumber.append(change.time, sprockets.size() - change.gear.rear);
            ratio.append(change.time, ratioOf(change.gear));
        }
    // held to the end
    const double last = end;
    if (last > changes.last().time)
        {
            const Gear g = changes.last().gear;
            frontTeeth.append(last, rings.at(g.front));
            rearTeeth.append(last, sprockets.at(g.rear));
            frontNumber.append(last, g.front + 1);
            rearNumber.append(last, sprockets.size() - g.rear);
            ratio.append(last, ratioOf(g));
        }
    for (const char *id : {ChannelId::FrontGear, ChannelId::RearGear, ChannelId::FrontGearNumber, ChannelId::RearGearNumber,
                           ChannelId::GearRatio})
        {
            session->addDerivedChannel(id);
        }
    return session;
}
