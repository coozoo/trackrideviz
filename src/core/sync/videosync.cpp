#include "videosync.h"

#include <QRegularExpression>

#include <cmath>
#include <cstdlib>

namespace
{
QString formatUtcOffset(int seconds)
{
    const int minutes = std::abs(seconds) / 60;
    return QString("UTC%1%2:%3")
           .arg(seconds < 0 ? '-' : '+')
           .arg(minutes / 60)
           .arg(minutes % 60, 2, 10, QChar('0'));
}
}

QDateTime VideoSync::wallClockFromFileName(const QString &fileName)
{
    // date and time with optional separators; not glued to other digits, so a
    // counter like "GX010042" or a longer number is not mistaken for a date
    static const QRegularExpression pattern(
        R"((?<!\d)(\d{4})[-_.]?(\d{2})[-_.]?(\d{2})[-_. T]?(\d{2})[-_.:]?(\d{2})[-_.:]?(\d{2})(?!\d))");
    QRegularExpressionMatchIterator it = pattern.globalMatch(fileName);
    while (it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            const QString iso = QString("%1-%2-%3T%4:%5:%6Z")
                                .arg(m.captured(1), m.captured(2), m.captured(3),
                                     m.captured(4), m.captured(5), m.captured(6));
            const QDateTime result = QDateTime::fromString(iso, Qt::ISODate);
            if (result.isValid() && result.date().year() >= 1990)
                {
                    return result;
                }
        }
    return QDateTime();
}

VideoSync::Result VideoSync::match(const TelemetrySession &session, const QDateTime &videoWallClock,
                                   double videoDuration, int fallbackUtcOffsetSeconds)
{
    Result result;
    if (!videoWallClock.isValid())
        {
            result.explanation = tr("The video file name holds no recording time.");
            return result;
        }
    if (!session.startUtc().isValid())
        {
            result.explanation = tr("The data has no absolute time.");
            return result;
        }

    const int utcOffset = session.hasUtcOffset() ? session.utcOffsetSeconds() : fallbackUtcOffsetSeconds;
    const double offset = double(session.startUtc().secsTo(videoWallClock)) - utcOffset;
    const double dataDuration = session.duration();
    auto overlaps = [&](double candidate)
    {
        return candidate <= dataDuration && candidate + videoDuration >= 0.0;
    };

    const QString basis = session.hasUtcOffset()
                          ? tr("time zone from the data, %1").arg(formatUtcOffset(utcOffset))
                          : tr("time zone of this computer, %1").arg(formatUtcOffset(utcOffset));
    if (overlaps(offset))
        {
            result.ok = true;
            result.offsetSeconds = offset;
            result.explanation = tr("File name time (%1).").arg(basis);
            return result;
        }

    // Whole hours first: that is what nearly every time zone, and a camera
    // left on another zone, differs by. Then the half and quarter hour zones.
    for (int unit : {3600, 1800, 900})
        {
            for (int step = 1; step <= 14 * 3600 / unit; ++step)
                {
                    for (int shift : {step * unit, -step * unit})
                        {
                            if (overlaps(offset + shift))
                                {
                                    result.ok = true;
                                    result.offsetSeconds = offset + shift;
                                    result.explanation = tr("File name time (%1) did not overlap the data; shifted by %2 min to make it fit. Check it.")
                                                         .arg(basis).arg(shift / 60);
                                    return result;
                                }
                        }
                }
        }
    result.explanation = tr("The video and the data were not recorded at the same time.");
    return result;
}
