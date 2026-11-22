#include "project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace
{
const char kFormat[] = "rtvproj";
// 2: gauges are bound to a channel, the project has a color theme
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
            QJsonObject object
            {
                {"gauge", item.gaugeId},
                {"channel", item.channel},
                {"x", item.x},
                {"y", item.y},
                {"height", item.height},
            };
            if (item.streets != OverlayItem::AsDesigned)
                {
                    object.insert("streets", item.streets == OverlayItem::Shown);
                }
            if (item.closeUp >= 0.0)
                {
                    object.insert("closeUp", item.closeUp);
                }
            if (item.speedZoom.isOn())
                {
                    object.insert("speedZoom", item.speedZoom.toJson());
                }
            if (item.headingUp >= 0)
                {
                    object.insert("headingUp", item.headingUp > 0);
                }
            if (item.flyover >= 0)
                {
                    object.insert("flyover", item.flyover > 0);
                }
            if (item.flyoverCamera.isSet())
                {
                    object.insert("flyoverCamera", item.flyoverCamera.toJson());
                }
            if (item.units != OverlayItem::AsSettings)
                {
                    object.insert("units", item.units == OverlayItem::ImperialUnits ? "imperial" : "metric");
                }
            item.writeChoices(object);
            overlay.append(object);
        }
    QJsonObject root
    {
        {"format", kFormat},
        {"version", kVersion},
        {"video", toStored(project.videoPath, dir)},
        {"data", toStored(project.telemetryPath, dir)},
        {"sync", QJsonObject{{"offset", project.syncOffset}, {"manual", project.syncIsManual}}},
        {"overlay", overlay},
        {"theme", project.theme.toJson()},
    };
    if (project.layoutAspect > 0.0)
        {
            root.insert("layoutAspect", project.layoutAspect);
        }
    if (project.background.isOn())
        {
            root.insert("background", project.background.toJson());
        }
    if (!project.moreData.isEmpty())
        {
            QJsonArray sources;
            for (const DataSource &source : project.moreData)
                {
                    sources.append(QJsonObject
                    {
                        {"file", toStored(source.path, dir)},
                        {"shift", source.shift},
                        {"shiftManual", source.shiftIsManual},
                        {"channels", QJsonArray::fromStringList(source.channels)},
                    });
                }
            root["moreData"] = sources;
        }
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
    if (project.elevation != Terrain::WhenMissing)
        {
            root["elevation"] = project.elevation == Terrain::Always ? "terrain" : "recorded";
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
    const Soundtrack &soundtrack = project.soundtrack;
    if (soundtrack != Soundtrack())
        {
            QJsonArray tracks;
            for (const MusicTrack &track : soundtrack.tracks)
                {
                    QJsonObject object{{"file", toStored(track.path, dir)}, {"duration", track.duration}};
                    if (track.from > 0.0)
                        {
                            object["from"] = track.from;
                        }
                    if (track.to > 0.0)
                        {
                            object["to"] = track.to;
                        }
                    tracks.append(object);
                }
            root["music"] = QJsonObject
            {
                {"tracks", tracks},
                {"start", soundtrack.start},
                {"fade", soundtrack.fade},
                {"videoVolume", soundtrack.videoVolume},
                {"musicVolume", soundtrack.musicVolume},
                {"videoMuted", soundtrack.videoMuted},
                {"musicMuted", soundtrack.musicMuted},
                {"ducking", soundtrack.ducking},
            };
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
    if (project.wind.used)
        {
            root["wind"] = QJsonObject{{"speed", project.wind.speed}, {"from", project.wind.from}};
        }
    if (!project.surfaces.isEmpty())
        {
            QJsonArray surfaces;
            for (const SurfaceStretch &stretch : project.surfaces)
                {
                    surfaces.append(QJsonObject{{"from", stretch.from}, {"to", stretch.to}, {"surface", stretch.surface},
                        {"factor", stretch.factor}});
                }
            root["surfaces"] = surfaces;
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
    loaded.background = VideoBackground::fromJson(root.value("background").toObject());
    loaded.telemetryPath = fromStored(root.value("data").toString(), dir);
    for (const QJsonValue &value : root.value("moreData").toArray())
        {
            const QJsonObject object = value.toObject();
            DataSource source;
            source.path = fromStored(object.value("file").toString(), dir);
            source.shift = object.value("shift").toDouble();
            source.shiftIsManual = object.value("shiftManual").toBool();
            for (const QJsonValue &channel : object.value("channels").toArray())
                {
                    source.channels << channel.toString();
                }
            if (!source.path.isEmpty())
                {
                    loaded.moreData.append(source);
                }
        }
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
    const QString elevation = root.value("elevation").toString();
    loaded.elevation = elevation == "terrain" ? Terrain::Always : elevation == "recorded" ? Terrain::Never : Terrain::WhenMissing;
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
    if (root.contains("music"))
        {
            const QJsonObject music = root.value("music").toObject();
            Soundtrack &soundtrack = loaded.soundtrack;
            for (const QJsonValue &value : music.value("tracks").toArray())
                {
                    const QJsonObject object = value.toObject();
                    MusicTrack track;
                    track.path = fromStored(object.value("file").toString(), dir);
                    track.duration = std::max(0.0, object.value("duration").toDouble());
                    track.from = std::max(0.0, object.value("from").toDouble());
                    track.to = std::max(0.0, object.value("to").toDouble());
                    if (!track.path.isEmpty())
                        {
                            soundtrack.tracks.append(track);
                        }
                }
            soundtrack.start = std::max(0.0, music.value("start").toDouble());
            soundtrack.fade = std::clamp(music.value("fade").toDouble(soundtrack.fade), 0.0, 30.0);
            soundtrack.videoVolume = std::clamp(music.value("videoVolume").toInt(100), 0, Soundtrack::kMaxVolume);
            soundtrack.musicVolume = std::clamp(music.value("musicVolume").toInt(100), 0, Soundtrack::kMaxVolume);
            soundtrack.videoMuted = music.value("videoMuted").toBool();
            soundtrack.musicMuted = music.value("musicMuted").toBool();
            soundtrack.ducking = music.value("ducking").toBool();
        }
    if (root.contains("wind"))
        {
            const QJsonObject wind = root.value("wind").toObject();
            loaded.wind = {true, wind.value("speed").toDouble(), wind.value("from").toDouble()};
        }
    for (const QJsonValue &value : root.value("surfaces").toArray())
        {
            const QJsonObject o = value.toObject();
            const SurfaceStretch stretch{o.value("from").toDouble(), o.value("to").toDouble(), o.value("surface").toString(),
                                         o.value("factor").toDouble(1.0)};
            if (stretch.to > stretch.from && stretch.factor > 0.0)
                {
                    loaded.surfaces.append(stretch);
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
            if (object.contains("streets"))
                {
                    item.streets = object.value("streets").toBool() ? OverlayItem::Shown : OverlayItem::Hidden;
                }
            item.closeUp = object.value("closeUp").toDouble(-1.0);
            item.speedZoom = GaugeComponent::SpeedZoom::fromJson(object.value("speedZoom"));
            if (object.contains("headingUp"))
                {
                    item.headingUp = object.value("headingUp").toBool() ? 1 : 0;
                }
            if (object.contains("flyover"))
                {
                    item.flyover = object.value("flyover").toBool() ? 1 : 0;
                }
            if (object.contains("flyoverCamera"))
                {
                    item.flyoverCamera = GaugeComponent::FlyoverCamera::fromJson(object.value("flyoverCamera"));
                }
            if (object.contains("units"))
                {
                    item.units = object.value("units").toString() == QLatin1String("imperial") ? OverlayItem::ImperialUnits
                                 : OverlayItem::MetricUnits;
                }
            item.readChoices(object);
            if (!item.gaugeId.isEmpty() && item.height > 0.0)
                {
                    loaded.overlay.append(item);
                }
        }
    loaded.theme = GaugeTheme::fromJson(root.value("theme").toObject());
    loaded.layoutAspect = std::max(0.0, root.value("layoutAspect").toDouble());
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
