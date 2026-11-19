#include "joindialog.h"

#include "icons.h"
#include "video/ffmpegtools.h"
#include "settings/appsettings.h"
#include "video/videojoin.h"
#include "video/videojoinjob.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <QtConcurrent>

namespace
{
struct Probed
{
    QVector<VideoJoin::Part> parts;
    QString error;
};

const char kVideoFilter[] = "*.mp4 *.MP4 *.mov *.MOV *.mkv *.MKV *.m4v *.avi *.AVI";
}

JoinDialog::JoinDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Join videos"));
    m_editors = new QWidget(this);
    m_list = new QListWidget(m_editors);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setToolTip(tr("Joined in this order; drag to change it"));
    // a file dragged to the top names the output as Up would
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &JoinDialog::suggestOutput, Qt::QueuedConnection);

    auto button = [this](const QString &text, auto slot)
    {
        QPushButton *b = new QPushButton(text, m_editors);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };
    QVBoxLayout *side = new QVBoxLayout;
    side->addWidget(button(tr("Add..."), &JoinDialog::addFilesFromDialog));
    side->addWidget(button(tr("Remove"), [this]() { qDeleteAll(m_list->selectedItems()); suggestOutput(); }));
    side->addWidget(button(tr("Up"), [this]() { move(-1); }));
    side->addWidget(button(tr("Down"), [this]() { move(1); }));
    side->addStretch(1);
    QHBoxLayout *listRow = new QHBoxLayout;
    listRow->addWidget(m_list, 1);
    listRow->addLayout(side);

    m_outputEdit = new QLineEdit(m_editors);
    connect(m_outputEdit, &QLineEdit::textEdited, this, [this]() { m_outputEdited = true; });
    QToolButton *browse = new QToolButton(m_editors);
    browse->setText(tr("..."));
    browse->setIcon(Icons::named("folder-open"));
    browse->setToolTip(tr("Choose where the joined video goes"));
    connect(browse, &QToolButton::clicked, this, &JoinDialog::browseOutput);
    QHBoxLayout *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(browse);
    QFormLayout *form = new QFormLayout;
    form->addRow(tr("Save as:"), outputRow);

    QLabel *about = new QLabel(tr("Videos of the same kind, such as the chapters a camera splits a long recording "
                                  "into, are copied: joining is quick and loses nothing. Others are encoded again, "
                                  "which takes longer."), m_editors);
    about->setWordWrap(true);
    QVBoxLayout *editors = new QVBoxLayout(m_editors);
    editors->setContentsMargins(0, 0, 0, 0);
    editors->addWidget(about);
    editors->addLayout(listRow, 1);
    editors->addLayout(form);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->hide();
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_buttons->addButton(tr("Join"), QDialogButtonBox::AcceptRole);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &JoinDialog::join);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &JoinDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_editors, 1);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addWidget(m_buttons);
    resize(560, 420);
    addFiles(paths);
}

JoinDialog::~JoinDialog()
{
    // a join still running stops and leaves no file behind
    delete m_job;
    delete m_listDir;
}

void JoinDialog::reject()
{
    if (m_job)
        {
            m_job->cancel();
        }
    QDialog::reject();
}

QStringList JoinDialog::paths() const
{
    QStringList paths;
    for (int i = 0; i < m_list->count(); ++i)
        {
            paths << m_list->item(i)->data(Qt::UserRole).toString();
        }
    return paths;
}

void JoinDialog::addFiles(const QStringList &added)
{
    const QStringList present = paths();
    QStringList fresh;
    for (const QString &path : added)
        {
            if (!present.contains(path) && !fresh.contains(path))
                {
                    fresh << path;
                }
        }
    // the order the camera recorded them in, after those there already: an order the user
    // made stays
    for (const QString &path : VideoJoin::ordered(fresh))
        {
            QListWidgetItem *item = new QListWidgetItem(QFileInfo(path).fileName(), m_list);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
        }
    suggestOutput();
}

void JoinDialog::addFilesFromDialog()
{
    const QStringList chosen = QFileDialog::getOpenFileNames(this, tr("Add videos"),
                                                             paths().isEmpty() ? QString() : QFileInfo(paths().first()).path(),
                                                             tr("Videos (%1)").arg(kVideoFilter));
    addFiles(chosen);
}

void JoinDialog::move(int step)
{
    // all those selected, one place each, keeping their order among themselves
    QVector<int> rows;
    for (QListWidgetItem *item : m_list->selectedItems())
        {
            rows.append(m_list->row(item));
        }
    std::sort(rows.begin(), rows.end());
    if (rows.isEmpty() || rows.first() + step < 0 || rows.last() + step >= m_list->count())
        {
            return;
        }
    if (step > 0)
        {
            std::reverse(rows.begin(), rows.end());
        }
    QList<QListWidgetItem *> moved;
    for (int row : rows)
        {
            QListWidgetItem *item = m_list->takeItem(row);
            m_list->insertItem(row + step, item);
            moved.append(item);
        }
    m_list->clearSelection();
    for (QListWidgetItem *item : moved)
        {
            item->setSelected(true);
        }
    m_list->setCurrentItem(moved.last());
    suggestOutput();
}

void JoinDialog::suggestOutput()
{
    if (m_outputEdited || m_list->count() == 0)
        {
            return;
        }
    // next to the first, named after it: a date and time in the name still sync
    const QFileInfo first(paths().first());
    m_outputEdit->setText(first.dir().filePath(first.completeBaseName() + "_joined." + first.suffix()));
}

void JoinDialog::browseOutput()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save joined video"), m_outputEdit->text(),
                                                      tr("Videos (%1)").arg(kVideoFilter));
    if (!path.isEmpty())
        {
            m_outputEdit->setText(path);
            m_outputEdited = true;
        }
}

void JoinDialog::setBusy(bool busy)
{
    m_editors->setEnabled(!busy);
    m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(true);
    for (QAbstractButton *b : m_buttons->buttons())
        {
            if (m_buttons->buttonRole(b) == QDialogButtonBox::AcceptRole)
                {
                    b->setEnabled(!busy);
                }
        }
    m_progress->setVisible(busy);
}

void JoinDialog::join()
{
    const QStringList files = paths();
    m_outputPath = m_outputEdit->text().trimmed();
    if (files.size() < 2)
        {
            m_status->setText(tr("Choose two or more videos."));
            return;
        }
    // as the disk compares names: where case does not count, "A.MP4" is one of "a.mp4"
    const bool isInput = std::any_of(files.cbegin(), files.cend(), [this](const QString &file)
    {
        return QFileInfo(file) == QFileInfo(m_outputPath);
    });
    if (m_outputPath.isEmpty() || isInput)
        {
            m_status->setText(tr("Choose a new file to save the joined video in."));
            return;
        }
    if (QFile::exists(m_outputPath)
            && QMessageBox::question(this, windowTitle(), tr("%1 exists. Replace it?").arg(QFileInfo(m_outputPath).fileName()))
            != QMessageBox::Yes)
        {
            return;
        }
    setBusy(true);
    m_status->setText(tr("Reading the videos..."));
    const QString ffprobe = FfmpegTools::ffprobeProgram();
    const bool preferOwn = AppSettings::ownVideoEngine();
    auto *watcher = new QFutureWatcher<Probed>(this);
    connect(watcher, &QFutureWatcher<Probed>::finished, this, [this, watcher]()
    {
        const Probed probed = watcher->result();
        watcher->deleteLater();
        if (!probed.error.isEmpty())
            {
                m_status->setText(probed.error);
                setBusy(false);
                return;
            }
        m_shifts = VideoJoin::shifts(probed.parts);
        m_joinedLength = 0.0;
        for (const VideoJoin::Part &part : probed.parts)
            {
                m_joinedLength += part.info.duration;
            }
        const QString why = VideoJoin::whyNot(probed.parts);
        if (why.isEmpty())
            {
                copyParts(probed.parts);
            }
        else
            {
                encodeParts(probed.parts, why);
            }
    });
    watcher->setFuture(QtConcurrent::run([ffprobe, files, preferOwn]()
    {
        Probed probed;
        for (const QString &path : files)
            {
                VideoJoin::Part part{path, MediaInfo()};
                QString error;
                if (!MediaProbe::probe(ffprobe, path, &part.info, &error, preferOwn))
                    {
                        probed.error = QString("%1: %2").arg(QFileInfo(path).fileName(), error);
                        break;
                    }
                probed.parts.append(part);
            }
        return probed;
    }));
}

void JoinDialog::copyParts(const QVector<VideoJoin::Part> &parts)
{
    delete m_listDir;
    m_listDir = new QTemporaryDir;
    const QString list = m_listDir->filePath("join.ffconcat");
    QFile file(list);
    if (!file.open(QIODevice::WriteOnly) || file.write(VideoJoin::listFile(paths())) < 0)
        {
            m_status->setText(tr("A temporary file could not be written."));
            setBusy(false);
            return;
        }
    file.close();
    m_status->setText(tr("Joining..."));
    newJob()->startCopy(list, m_outputPath, VideoJoin::keepsCameraData(parts, m_outputPath));
}

void JoinDialog::encodeParts(const QVector<VideoJoin::Part> &parts, const QString &why)
{
    const MediaInfo &first = parts.first().info;
    QString question = tr("%1\n\nThey can be joined by encoding them again. That takes a while; the joined video "
                          "gets the size and the frame rate of the first one (%2×%3, %4 frames per second), the "
                          "others scaled into it. Encode them?")
                       .arg(why).arg(first.size.width()).arg(first.size.height()).arg(first.fps(), 0, 'f', 2);
    if (std::any_of(parts.cbegin(), parts.cend(), [](const VideoJoin::Part &part) { return part.info.hasCameraData; }))
        {
            question += "\n\n" + tr("The camera's data in the videos does not come along: open it from the videos "
                                     "themselves.");
        }
    if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes)
        {
            m_status->setText(why);
            setBusy(false);
            return;
        }
    // the encoder and rate of the last export, at the first video's bitrate
    ExportSettings settings = AppSettings::hasExportDefaults() ? AppSettings::exportDefaults() : ExportSettings();
    if (first.bitrateKbps > 0)
        {
            settings.bitrateKbps = first.bitrateKbps;
        }
    m_status->setText(tr("Encoding..."));
    newJob()->startEncode(parts, m_outputPath, VideoEncoders::byId(settings.encoder), settings);
}

VideoJoinJob *JoinDialog::newJob()
{
    delete m_job;
    m_job = new VideoJoinJob(AppSettings::ownVideoEngine() ? ExportJob::OwnEngine : ExportJob::ProgramEngine, this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    connect(m_job, &VideoJoinJob::progress, this, [this](double seconds)
    {
        if (m_joinedLength > 0.0)
            {
                m_progress->setValue(int(std::clamp(seconds / m_joinedLength, 0.0, 1.0) * 1000.0));
            }
    });
    connect(m_job, &VideoJoinJob::finished, this, [this](bool ok, const QString &error)
    {
        if (!ok)
            {
                m_status->setText(tr("Joining failed: %1").arg(error));
                setBusy(false);
                return;
            }
        accept();
    });
    return m_job;
}
