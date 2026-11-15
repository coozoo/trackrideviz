#include "musicpanel.h"

#include "video/ffmpegtools.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace
{
QString formatLength(double seconds)
{
    const int total = int(std::lround(std::max(0.0, seconds)));
    const QString minSec = QString("%1:%2").arg(total / 60 % 60).arg(total % 60, 2, 10, QChar('0'));
    return total >= 3600 ? QString("%1:%2").arg(total / 3600).arg(minSec, 5, QChar('0')) : minSec;
}

QTime toTime(double seconds)
{
    return QTime(0, 0).addMSecs(int(std::clamp(seconds, 0.0, 86399.0) * 1000.0));
}

double toSeconds(const QTime &time)
{
    return QTime(0, 0).msecsTo(time) / 1000.0;
}
}

MusicPanel::MusicPanel(QWidget *parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setToolTip(tr("Played one after another. Drag to change the order."));
    connect(m_list, &QListWidget::itemSelectionChanged, this, &MusicPanel::onSelectionChanged);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &MusicPanel::onRowsMoved, Qt::QueuedConnection);

    m_addButton = new QPushButton(tr("Add..."), this);
    m_addButton->setToolTip(tr("Add music files: MP3, Ogg, Opus, FLAC, WAV, M4A"));
    connect(m_addButton, &QPushButton::clicked, this, &MusicPanel::addFiles);
    m_removeButton = new QPushButton(tr("Remove"), this);
    connect(m_removeButton, &QPushButton::clicked, this, &MusicPanel::removeSelected);
    m_upButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowUp), QString(), this);
    m_upButton->setToolTip(tr("Play earlier"));
    connect(m_upButton, &QPushButton::clicked, this, [this]()
    {
        moveSelected(-1);
    });
    m_downButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowDown), QString(), this);
    m_downButton->setToolTip(tr("Play later"));
    connect(m_downButton, &QPushButton::clicked, this, [this]()
    {
        moveSelected(1);
    });
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    buttons->addWidget(m_upButton);
    buttons->addWidget(m_downButton);

    // the part of the selected track that plays
    m_fromEdit = new QTimeEdit(this);
    m_toEdit = new QTimeEdit(this);
    for (QTimeEdit *edit : {m_fromEdit, m_toEdit})
        {
            edit->setDisplayFormat("m:ss");
            edit->setKeyboardTracking(false);
            connect(edit, &QTimeEdit::timeChanged, this, &MusicPanel::onTrimEdited);
        }
    m_fromEdit->setToolTip(tr("Where in the selected file the music begins"));
    m_toEdit->setToolTip(tr("Where in the selected file the music ends"));
    QHBoxLayout *trimRow = new QHBoxLayout;
    trimRow->addWidget(m_fromEdit);
    trimRow->addWidget(new QLabel(QString::fromUtf8("–"), this));
    trimRow->addWidget(m_toEdit);
    trimRow->addStretch(1);

    m_startEdit = new QTimeEdit(this);
    m_startEdit->setDisplayFormat("H:mm:ss");
    m_startEdit->setKeyboardTracking(false);
    m_startEdit->setToolTip(tr("The moment of the video the first track begins at"));
    connect(m_startEdit, &QTimeEdit::timeChanged, this, [this]()
    {
        edit(shown(), tr("Move the music"));
    });
    m_hereButton = new QPushButton(tr("Here"), this);
    m_hereButton->setToolTip(tr("Begin the music where the video stands"));
    connect(m_hereButton, &QPushButton::clicked, this, [this]()
    {
        Soundtrack soundtrack = shown();
        soundtrack.start = m_position;
        edit(soundtrack, tr("Move the music"));
    });
    QPushButton *goButton = new QPushButton(tr("Go"), this);
    goButton->setToolTip(tr("Show the moment the music begins"));
    connect(goButton, &QPushButton::clicked, this, [this]()
    {
        emit seekRequested(m_soundtrack.start);
    });
    QHBoxLayout *startRow = new QHBoxLayout;
    startRow->addWidget(m_startEdit);
    startRow->addWidget(m_hereButton);
    startRow->addWidget(goButton);
    startRow->addStretch(1);

    m_fadeSpin = new QDoubleSpinBox(this);
    m_fadeSpin->setRange(0.0, 30.0);
    m_fadeSpin->setDecimals(1);
    m_fadeSpin->setSingleStep(0.5);
    m_fadeSpin->setSuffix(tr(" s"));
    m_fadeSpin->setKeyboardTracking(false);
    m_fadeSpin->setToolTip(tr("Each track fades in at its beginning and out at its end"));
    connect(m_fadeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]()
    {
        edit(shown(), tr("Change the fades"));
    });

    auto volumeRow = [this](QSlider **slider, QCheckBox **mute, QLabel **label)
    {
        *slider = new QSlider(Qt::Horizontal, this);
        (*slider)->setRange(0, 100);
        (*slider)->setPageStep(10);
        *label = new QLabel(this);
        (*label)->setMinimumWidth(fontMetrics().horizontalAdvance("100 %"));
        *mute = new QCheckBox(tr("Off"), this);
        connect(*slider, &QSlider::valueChanged, this, &MusicPanel::onVolumeMoved);
        connect(*slider, &QSlider::sliderReleased, this, &MusicPanel::onVolumeReleased);
        connect(*mute, &QCheckBox::toggled, this, [this]()
        {
            edit(shown(), tr("Change the volume"));
        });
        QHBoxLayout *row = new QHBoxLayout;
        row->addWidget(*slider, 1);
        row->addWidget(*label);
        row->addWidget(*mute);
        return row;
    };
    QHBoxLayout *videoRow = volumeRow(&m_videoSlider, &m_videoMute, &m_videoLabel);
    QHBoxLayout *musicRow = volumeRow(&m_musicSlider, &m_musicMute, &m_musicLabel);
    m_videoSlider->setToolTip(tr("The sound the camera recorded"));

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setEnabled(false); // grayed: information, not a control

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("Play:"), trimRow);
    form->addRow(tr("Begins at:"), startRow);
    form->addRow(tr("Fade:"), m_fadeSpin);
    form->addRow(tr("Video sound:"), videoRow);
    form->addRow(tr("Music:"), musicRow);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);
    layout->addLayout(form);
    layout->addWidget(m_summary);

    setSoundtrack(Soundtrack());
}

void MusicPanel::setSoundtrack(const Soundtrack &soundtrack)
{
    m_soundtrack = soundtrack;
    m_updating = true;
    m_startEdit->setTime(toTime(soundtrack.start));
    m_fadeSpin->setValue(soundtrack.fade);
    m_videoSlider->setValue(soundtrack.videoVolume);
    m_videoMute->setChecked(soundtrack.videoMuted);
    m_musicSlider->setValue(soundtrack.musicVolume);
    m_musicMute->setChecked(soundtrack.musicMuted);
    m_updating = false;
    refreshList();
    onVolumeMoved();
    refreshSummary();
}

void MusicPanel::setPosition(double videoTime)
{
    m_position = videoTime;
}

void MusicPanel::setVideoDuration(double seconds)
{
    m_videoDuration = seconds;
    refreshSummary();
}

void MusicPanel::setCuts(const QVector<TimeRange> &cuts)
{
    m_cuts = cuts;
    refreshSummary();
}

Soundtrack MusicPanel::shown() const
{
    Soundtrack soundtrack = m_soundtrack;
    soundtrack.start = toSeconds(m_startEdit->time());
    soundtrack.fade = m_fadeSpin->value();
    soundtrack.videoVolume = m_videoSlider->value();
    soundtrack.videoMuted = m_videoMute->isChecked();
    soundtrack.musicVolume = m_musicSlider->value();
    soundtrack.musicMuted = m_musicMute->isChecked();
    return soundtrack;
}

void MusicPanel::edit(const Soundtrack &soundtrack, const QString &description)
{
    if (m_updating || soundtrack == m_soundtrack)
        {
            return;
        }
    // shown at once; the main window sends it back the same, or as it was on undo
    setSoundtrack(soundtrack);
    emit soundtrackEdited(soundtrack, description);
}

void MusicPanel::refreshList()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    QVector<int> selectedRows;
    for (QListWidgetItem *item : selected)
        {
            selectedRows.append(m_list->row(item));
        }
    m_updating = true;
    m_list->clear();
    for (int i = 0; i < m_soundtrack.tracks.size(); ++i)
        {
            const MusicTrack &track = m_soundtrack.tracks.at(i);
            const bool missing = !QFileInfo::exists(track.path);
            QString text = QString("%1. %2").arg(i + 1).arg(QFileInfo(track.path).completeBaseName());
            if (missing)
                {
                    text += "  " + tr("(missing)");
                }
            else if (track.length() > 0.0)
                {
                    text += QString("  %1").arg(formatLength(track.length()));
                }
            QListWidgetItem *item = new QListWidgetItem(text, m_list);
            item->setData(Qt::UserRole, i);
            item->setToolTip(QDir::toNativeSeparators(track.path));
            if (missing)
                {
                    item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
                }
        }
    for (int row : selectedRows)
        {
            if (row < m_list->count())
                {
                    m_list->item(row)->setSelected(true);
                }
        }
    m_updating = false;
    onSelectionChanged();
}

void MusicPanel::refreshSummary()
{
    if (m_soundtrack.tracks.isEmpty())
        {
            m_summary->setText(m_reading > 0 ? tr("Reading the music...")
                               : tr("No music. Add files to play them under the video, one after another."));
            return;
        }
    QString text = tr("%n track(s), %1 of music.", nullptr, int(m_soundtrack.tracks.size()))
                   .arg(formatLength(m_soundtrack.length()));
    if (m_videoDuration > 0.0)
        {
            // what of the video comes after the music's beginning, without the cuts
            const double wanted = CutList::keptLength(CutList::kept(m_cuts, std::min(m_soundtrack.start, m_videoDuration),
                                  m_videoDuration));
            text += " " + tr("The video has %1 from the music's beginning on.").arg(formatLength(wanted));
        }
    if (m_reading > 0)
        {
            text += " " + tr("Reading the music...");
        }
    m_summary->setText(text);
}

void MusicPanel::addFiles()
{
    QSettings settings;
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add music"), settings.value("paths/music").toString(),
                              SoundtrackMix::fileFilters().join(";;"));
    if (paths.isEmpty())
        {
            return;
        }
    settings.setValue("paths/music", QFileInfo(paths.first()).absolutePath());

    // the lengths are needed for the fades and for what follows; ffprobe tells them
    ++m_reading;
    m_addButton->setEnabled(false);
    refreshSummary();
    const QString ffprobe = FfmpegTools::ffprobeProgram();
    auto *watcher = new QFutureWatcher<QVector<MusicTrack>>(this);
    connect(watcher, &QFutureWatcher<QVector<MusicTrack>>::finished, this, [this, watcher]()
    {
        const QVector<MusicTrack> added = watcher->result();
        watcher->deleteLater();
        --m_reading;
        m_addButton->setEnabled(true);
        Soundtrack soundtrack = m_soundtrack;
        soundtrack.tracks += added;
        edit(soundtrack, tr("Add music"));
        refreshSummary();
    });
    watcher->setFuture(QtConcurrent::run([ffprobe, paths]()
    {
        QVector<MusicTrack> tracks;
        for (const QString &path : paths)
            {
                MusicTrack track;
                track.path = path;
                track.duration = SoundtrackMix::probeDuration(ffprobe, path);
                tracks.append(track);
            }
        return tracks;
    }));
}

void MusicPanel::removeSelected()
{
    QVector<int> rows;
    for (QListWidgetItem *item : m_list->selectedItems())
        {
            rows.append(m_list->row(item));
        }
    if (rows.isEmpty())
        {
            return;
        }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    Soundtrack soundtrack = shown();
    for (int row : rows)
        {
            soundtrack.tracks.removeAt(row);
        }
    m_list->clearSelection();
    edit(soundtrack, tr("Remove music"));
}

void MusicPanel::moveSelected(int step)
{
    const int row = m_list->currentRow();
    const int target = row + step;
    if (row < 0 || target < 0 || target >= m_soundtrack.tracks.size())
        {
            return;
        }
    Soundtrack soundtrack = shown();
    soundtrack.tracks.move(row, target);
    edit(soundtrack, tr("Reorder music"));
    m_list->clearSelection();
    m_list->setCurrentRow(target);
}

void MusicPanel::onRowsMoved()
{
    if (m_updating)
        {
            return;
        }
    // the items carry the place each track had
    Soundtrack soundtrack = shown();
    soundtrack.tracks.clear();
    for (int row = 0; row < m_list->count(); ++row)
        {
            const int index = m_list->item(row)->data(Qt::UserRole).toInt();
            if (index >= 0 && index < m_soundtrack.tracks.size())
                {
                    soundtrack.tracks.append(m_soundtrack.tracks.at(index));
                }
        }
    if (soundtrack.tracks.size() != m_soundtrack.tracks.size())
        {
            refreshList();
            return;
        }
    const int current = m_list->currentRow();
    edit(soundtrack, tr("Reorder music"));
    // the numbers follow the new order
    refreshList();
    m_list->setCurrentRow(current);
}

void MusicPanel::onSelectionChanged()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    const int row = selected.size() == 1 ? m_list->row(selected.first()) : -1;
    m_removeButton->setEnabled(!selected.isEmpty());
    m_upButton->setEnabled(row > 0);
    m_downButton->setEnabled(row >= 0 && row + 1 < m_soundtrack.tracks.size());
    // a track whose length is not known has no end to pick
    const bool trimmable = row >= 0 && m_soundtrack.tracks.at(row).duration > 0.0;
    m_fromEdit->setEnabled(trimmable);
    m_toEdit->setEnabled(trimmable);
    const bool updating = m_updating;
    m_updating = true;
    if (trimmable)
        {
            const MusicTrack &track = m_soundtrack.tracks.at(row);
            const QTime end = toTime(track.duration);
            m_fromEdit->setMaximumTime(end);
            m_toEdit->setMaximumTime(end);
            m_fromEdit->setTime(toTime(track.from));
            m_toEdit->setTime(toTime(track.to > 0.0 ? track.to : track.duration));
        }
    else
        {
            m_fromEdit->setTime(QTime(0, 0));
            m_toEdit->setTime(QTime(0, 0));
        }
    m_updating = updating;
}

void MusicPanel::onTrimEdited()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (m_updating || selected.size() != 1)
        {
            return;
        }
    const int row = m_list->row(selected.first());
    Soundtrack soundtrack = shown();
    MusicTrack &track = soundtrack.tracks[row];
    track.from = toSeconds(m_fromEdit->time());
    const double to = toSeconds(m_toEdit->time());
    // to the end of the file is kept as such: the file may be read a bit longer elsewhere
    track.to = to >= track.duration - 0.5 ? 0.0 : std::max(to, track.from);
    if (track.duration > 0.0 && track.length() <= 0.0)
        {
            // nothing left to play: the beginning moves back
            track.from = std::max(0.0, (track.to > 0.0 ? track.to : track.duration) - 1.0);
        }
    edit(soundtrack, tr("Trim music"));
}

void MusicPanel::onVolumeMoved()
{
    m_videoLabel->setText(QString("%1 %").arg(m_videoSlider->value()));
    m_musicLabel->setText(QString("%1 %").arg(m_musicSlider->value()));
    if (m_updating)
        {
            return;
        }
    if (m_videoSlider->isSliderDown() || m_musicSlider->isSliderDown())
        {
            // heard at once, kept when let go
            emit volumesPreviewed(shown());
            return;
        }
    edit(shown(), tr("Change the volume"));
}

void MusicPanel::onVolumeReleased()
{
    edit(shown(), tr("Change the volume"));
}
