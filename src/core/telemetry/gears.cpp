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
// How well a gear explains a second: its ratio this far off (as a fraction) costs 1, twice as far 4,
// and no more than kWorst - a second that fits no gear (coasting with the cranks turning) tells little.
const double kMatch = 0.05;
const double kWorst = 9.0;
// What a shift costs against seconds that fit: a sprocket now and then, a chainring seldom.
const double kRearShift = 10.0;      // and as much again for every sprocket further
const double kFrontShift = 60.0;
// A crossed chain (the small ring on a small sprocket, the big ring on a big one) costs this much a
// second at its worst: riders keep the chain straight where two gears are alike. The big ring on the
// middle sprockets is not crossed yet: only the outer half of the way counts.
const double kCrossed = 0.5;

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
            auto session = std::make_shared<TelemetrySession>(*original);
            // the bike's teeth: as entered, else every one the recording went through
            if (!drivetrain.isEmpty())
                {
                    session->setGearing(drivetrain.chainrings, drivetrain.cassette);
                }
            else
                {
                    const auto seen = [&](const char *id)
                    {
                        QVector<int> teeth;
                        if (const Channel *channel = session->channel(id))
                            {
                                for (int i = 0; i < channel->count(); ++i)
                                    {
                                        teeth.append(int(channel->valueAtIndex(i)));
                                    }
                            }
                        std::sort(teeth.begin(), teeth.end());
                        teeth.erase(std::unique(teeth.begin(), teeth.end()), teeth.end());
                        return teeth;
                    };
                    session->setGearing(seen(ChannelId::FrontGear), seen(ChannelId::RearGear));
                }
            // the ratio from them, the front of a single ring from the settings
            if (has(*original, ChannelId::GearRatio) || (!recordedFront && drivetrain.chainrings.size() != 1))
                {
                    return session;
                }
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

    struct Change
    {
        double time;
        Gear gear;
    };
    // Every gear the bike has; for every second the ride gives, how well each explains it, and the
    // cheapest way through the whole ride to each (Viterbi): one wrong guess at the start cannot
    // hold the rest, and a shift is taken only where the seconds after it pay for it.
    const int ringCount = int(rings.size());
    const int sprocketCount = int(sprockets.size());
    const int gearCount = ringCount * sprocketCount;
    const auto gearAt = [&](int state) { return Gear{state / sprocketCount, state % sprocketCount}; };
    QVector<double> crossed(gearCount);
    for (int g = 0; g < gearCount; ++g)
        {
            const Gear gear = gearAt(g);
            // 0 the smallest, 1 the largest of each; straight is a large ring on a small sprocket
            const double ringPlace = ringCount > 1 ? double(gear.front) / (ringCount - 1) : 0.5;
            const double sprocketPlace = sprocketCount > 1 ? double(gear.rear) / (sprocketCount - 1) : 0.5;
            const double off = ringCount > 1 ? std::fabs(ringPlace - (1.0 - sprocketPlace)) : 0.0;
            const double beyond = std::max(0.0, off - 0.5) * 2.0;
            crossed[g] = kCrossed * beyond * beyond;
        }
    const auto shiftCost = [&](int from, int to)
    {
        const Gear a = gearAt(from);
        const Gear b = gearAt(to);
        return (a.rear != b.rear ? kRearShift * (1 + std::abs(a.rear - b.rear)) : 0.0) + (a.front != b.front ? kFrontShift : 0.0);
    };

    QVector<double> seconds;            // the times looked at
    QVector<double> ratios;             // what was ridden; 0 where nothing can be told
    const double start = std::max(speed->startTime(), cadence->startTime());
    const double end = std::min(speed->endTime(), cadence->endTime());
    for (double t = start; t <= end; t += 1.0)
        {
            bool paused = false;
            for (const TelemetrySession::Pause &p : original->pauses())
                {
                    paused = paused || (t > p.startTime && t < p.endTime);
                }
            // three seconds of each, so that one sample out of step decides little
            double v = 0.0;
            double rpm = 0.0;
            for (const double d : {-1.0, 0.0, 1.0})
                {
                    v += speed->valueAt(t + d) / 3.0;
                    rpm += cadence->valueAt(t + d) / 3.0;
                }
            seconds.append(t);
            ratios.append(paused || rpm < kLeastCadence || v < kLeastSpeed ? 0.0 : v / (rpm / 60.0 * meters));
        }
    // the first second anything can be told
    int first = 0;
    while (first < ratios.size() && ratios.at(first) <= 0.0)
        {
            ++first;
        }
    if (first >= ratios.size())
        {
            return original;
        }
    QVector<double> cost(gearCount, 0.0);
    QVector<QVector<qint8>> from(ratios.size(), QVector<qint8>(gearCount, -1));
    for (int k = first; k < ratios.size(); ++k)
        {
            QVector<double> next(gearCount);
            for (int g = 0; g < gearCount; ++g)
                {
                    double best = cost.at(g);
                    int bestFrom = g;
                    if (k > first)
                        {
                            for (int h = 0; h < gearCount; ++h)
                                {
                                    if (h != g)
                                        {
                                            const double c = cost.at(h) + shiftCost(h, g);
                                            if (c < best)
                                                {
                                                    best = c;
                                                    bestFrom = h;
                                                }
                                        }
                                }
                        }
                    const double ratio = ratios.at(k);
                    double fit = 0.0;
                    if (ratio > 0.0)
                        {
                            const double off = offBy(ratio, ratioOf(gearAt(g))) / kMatch;
                            fit = std::min(off * off, kWorst) + crossed.at(g);
                        }
                    next[g] = best + fit;
                    from[k][g] = qint8(bestFrom);
                }
            // only differences matter: kept small
            const double least = *std::min_element(next.cbegin(), next.cend());
            for (double &c : next)
                {
                    c -= least;
                }
            cost = next;
        }
    // back from the cheapest end
    QVector<int> path(ratios.size(), -1);
    int state = int(std::min_element(cost.cbegin(), cost.cend()) - cost.cbegin());
    for (int k = int(ratios.size()) - 1; k >= first; --k)
        {
            path[k] = state;
            state = from.at(k).at(state);
        }
    QVector<Change> changes;
    for (int k = first; k < ratios.size(); ++k)
        {
            if (k == first || path.at(k) != path.at(k - 1))
                {
                    changes.append({seconds.at(k), gearAt(path.at(k))});
                }
        }
    if (changes.isEmpty())
        {
            return original;
        }
    auto session = std::make_shared<TelemetrySession>(*original);
    session->setGearing(rings, sprockets);
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
