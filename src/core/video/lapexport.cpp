#include "lapexport.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

QVector<TimeRange> LapExport::ranges(const TelemetrySession &session, const SyncMap &sync, double videoDuration)
{
    QVector<TimeRange> result;
    for (const TelemetrySession::Lap &lap : session.laps())
        {
            const double start = std::clamp(sync.videoTime(lap.startTime), 0.0, videoDuration);
            const double end = std::clamp(sync.videoTime(lap.endTime), 0.0, videoDuration);
            result.append(end > start ? TimeRange{start, end} : TimeRange{});
        }
    return result;
}

bool LapExport::numbers(const QString &text, int count, QVector<int> *laps, QString *error)
{
    laps->clear();
    const QString wanted = text.trimmed();
    if (wanted.isEmpty() || wanted.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0)
        {
            for (int lap = 1; lap <= count; ++lap)
                {
                    laps->append(lap);
                }
            return true;
        }
    static const QRegularExpression part(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$"));
    for (const QString &piece : wanted.split(','))
        {
            const QRegularExpressionMatch match = part.match(piece);
            const int from = match.hasMatch() ? match.captured(1).toInt() : 0;
            const int to = match.hasMatch() && !match.captured(2).isEmpty() ? match.captured(2).toInt() : from;
            if (from < 1 || to < from || to > count)
                {
                    if (error)
                        {
                            *error = QCoreApplication::translate("LapExport", "Laps are numbers from 1 to %1, as 3 or 1, 4-6.").arg(count);
                        }
                    laps->clear();
                    return false;
                }
            for (int lap = from; lap <= to; ++lap)
                {
                    laps->append(lap);
                }
        }
    std::sort(laps->begin(), laps->end());
    laps->erase(std::unique(laps->begin(), laps->end()), laps->end());
    return true;
}

QString LapExport::outputPath(const QString &path, int lap, int count)
{
    const QFileInfo file(path);
    const QString number = QString("%1").arg(lap, std::max(2, int(QString::number(count).size())), 10, QChar('0'));
    const QString name = QString("%1_lap%2").arg(file.completeBaseName(), number);
    const QString suffix = file.suffix();
    // the folder as it was written
    return path.left(path.size() - file.fileName().size()) + (suffix.isEmpty() ? name : name + "." + suffix);
}
