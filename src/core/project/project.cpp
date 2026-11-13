#include "project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
const char kFormat[] = "rtvproj";
// 2: gauges are bound to a channel, the project has a colour theme
const int kVersion = 2;

QString toStored(const QString &path, const QDir &projectDir)
{
    if (path.isEmpty())
        {
            return QString();
        }
    // Relative only for what lies in the project's folder or below it. A
    // chain of "../../.." to some unrelated place would break on the first
    // move just the same and is harder to read than the full path.
    const QString relative = QDir::fromNativeSeparators(projectDir.relativeFilePath(path));
    const bool below = !relative.startsWith("../") && relative != ".." && !QFileInfo(relative).isAbsolute();
    return below ? relative : QDir::fromNativeSeparators(path);
}

QString fromStored(const QString &stored, const QDir &projectDir)
{
    if (stored.isEmpty())
        {
            return QString();
        }
    const QString path = QDir::cleanPath(QFileInfo(stored).isAbsolute() ? stored : projectDir.absoluteFilePath(stored));
    if (!QFileInfo::exists(path))
        {
            // moved together with the project, but was stored with its full path
            const QString beside = projectDir.absoluteFilePath(QFileInfo(path).fileName());
            if (QFileInfo::exists(beside))
                {
                    return QDir::cleanPath(beside);
                }
        }
    return path;
}
}

QByteArray ProjectFile::toJson(const Project &project, const QString &projectDir)
{
    const QDir dir(projectDir);
    QJsonArray overlay;
    for (const OverlayItem &item : project.overlay)
        {
            overlay.append(QJsonObject
            {
                {"gauge", item.gaugeId},
                {"channel", item.channel},
                {"x", item.x},
                {"y", item.y},
                {"height", item.height},
            });
        }
    QJsonObject root
    {
        {"format", kFormat},
        {"version", kVersion},
        {"video", toStored(project.videoPath, dir)},
        {"data", toStored(project.telemetryPath, dir)},
        {"sync", QJsonObject{{"offset", project.syncOffset}, {"manual", project.syncIsManual}}},
        {"overlay", overlay},
        {"theme", QJsonObject{
                {"foreground", project.theme.foreground.name(QColor::HexArgb)},
                {"background", project.theme.background.name(QColor::HexArgb)},
                {"accent", project.theme.accent.name(QColor::HexArgb)},
                {"accent2", project.theme.accent2.name(QColor::HexArgb)},
            }
        },
    };
    if (project.startFinish.isValid())
        {
            const StartFinishLine &l = project.startFinish;
            root["startFinish"] = QJsonArray{l.lat1, l.lon1, l.lat2, l.lon2};
        }
    if (!project.videoShifts.isEmpty())
        {
            QJsonArray shifts;
            for (const TimeShift &step : project.videoShifts)
                {
                    shifts.append(QJsonArray{step.at, step.shift});
                }
            root["videoShifts"] = shifts;
        }
    if (!project.dataEdits.isEmpty())
        {
            QJsonArray edits;
            for (const DataEdit &edit : project.dataEdits)
                {
                    edits.append(QJsonObject{{"channel", edit.channel}, {"start", edit.start}, {"end", edit.end}});
                }
            root["dataEdits"] = edits;
        }
    if (!project.cuts.isEmpty())
        {
            QJsonArray cuts;
            for (const TimeRange &cut : project.cuts)
                {
                    cuts.append(QJsonArray{cut.start, cut.end});
                }
            root["cuts"] = cuts;
        }
    if (!project.sectors.isEmpty())
        {
            QJsonArray sectors;
            for (const StartFinishLine &l : project.sectors)
                {
                    sectors.append(QJsonArray{l.lat1, l.lon1, l.lat2, l.lon2});
                }
            root["sectors"] = sectors;
        }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool ProjectFile::fromJson(const QByteArray &json, const QString &projectDir, Project *project, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            {
                *error = message;
            }
        return false;
    };

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject())
        {
            return fail(tr("This is not a project file: %1").arg(parseError.errorString()));
        }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != QLatin1String(kFormat))
        {
            return fail(tr("This is not a Race Track Visualizer project file."));
        }
    if (root.value("version").toInt() > kVersion)
        {
            return fail(tr("The project was saved by a newer version of the program (file version %1).")
                        .arg(root.value("version").toInt()));
        }

    const QDir dir(projectDir);
    Project loaded;
    loaded.videoPath = fromStored(root.value("video").toString(), dir);
    loaded.telemetryPath = fromStored(root.value("data").toString(), dir);
    const QJsonObject sync = root.value("sync").toObject();
    loaded.syncOffset = sync.value("offset").toDouble();
    loaded.syncIsManual = sync.value("manual").toBool();
    const QJsonArray line = root.value("startFinish").toArray();
    if (line.size() == 4)
        {
            loaded.startFinish = {line.at(0).toDouble(), line.at(1).toDouble(), line.at(2).toDouble(), line.at(3).toDouble()};
        }
    for (const QJsonValue &value : root.value("videoShifts").toArray())
        {
            const QJsonArray step = value.toArray();
            loaded.videoShifts.append({step.at(0).toDouble(), step.at(1).toDouble()});
        }
    for (const QJsonValue &value : root.value("dataEdits").toArray())
        {
            const QJsonObject edit = value.toObject();
            loaded.dataEdits.append({edit.value("channel").toString(), edit.value("start").toDouble(),
                                     edit.value("end").toDouble()});
        }
    for (const QJsonValue &value : root.value("cuts").toArray())
        {
            const QJsonArray cut = value.toArray();
            loaded.cuts.append({cut.at(0).toDouble(), cut.at(1).toDouble()});
        }
    loaded.cuts = CutList::normalized(loaded.cuts);
    for (const QJsonValue &value : root.value("sectors").toArray())
        {
            const QJsonArray sector = value.toArray();
            const StartFinishLine l{sector.at(0).toDouble(), sector.at(1).toDouble(), sector.at(2).toDouble(),
                                    sector.at(3).toDouble()};
            if (sector.size() == 4 && l.isValid())
                {
                    loaded.sectors.append(l);
                }
        }
    loaded.overlay.clear();
    const QJsonArray overlay = root.value("overlay").toArray();
    for (const QJsonValue &value : overlay)
        {
            const QJsonObject object = value.toObject();
            OverlayItem item;
            item.gaugeId = object.value("gauge").toString();
            // version 1 knew only the speedometer
            item.channel = object.value("channel").toString(item.channel);
            item.x = object.value("x").toDouble();
            item.y = object.value("y").toDouble();
            item.height = object.value("height").toDouble(item.height);
            if (!item.gaugeId.isEmpty() && item.height > 0.0)
                {
                    loaded.overlay.append(item);
                }
        }
    const QJsonObject theme = root.value("theme").toObject();
    auto readColor = [&theme](const char *key, QColor *color)
    {
        const QColor stored(theme.value(QLatin1String(key)).toString());
        if (stored.isValid())
            {
                *color = stored;
            }
    };
    readColor("foreground", &loaded.theme.foreground);
    readColor("background", &loaded.theme.background);
    readColor("accent", &loaded.theme.accent);
    readColor("accent2", &loaded.theme.accent2);
    *project = loaded;
    return true;
}

bool ProjectFile::save(const QString &path, const Project &project, QString *error)
{
    // written to a temporary file and renamed: a crash or a full disk cannot
    // leave a half-written project in place of the good one
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
            || file.write(toJson(project, QFileInfo(path).absolutePath())) < 0
            || !file.commit())
        {
            if (error)
                {
                    *error = tr("Cannot write %1: %2").arg(path, file.errorString());
                }
            return false;
        }
    return true;
}

bool ProjectFile::load(const QString &path, Project *project, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = tr("Cannot open %1: %2").arg(path, file.errorString());
                }
            return false;
        }
    return fromJson(file.readAll(), QFileInfo(path).absolutePath(), project, error);
}
