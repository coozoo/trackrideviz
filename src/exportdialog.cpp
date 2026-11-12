#include "exportdialog.h"

#include "settings/appsettings.h"
#include "video/exportplan.h"
#include "video/ffmpegtools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTimeEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace
{
struct ProbeResult
{
    bool ok{false};
    MediaInfo info;
    QString error;
};

// Testing the hardware encoders takes about a second and the answer does not
// change while the program runs, so it is asked once per ffmpeg program.
QHash<QString, QVector<VideoEncoder>> &encoderCache()
{
    static QHash<QString, QVector<VideoEncoder>> cache;
    return cache;
}

QTime toTime(double seconds)
{
    return QTime(0, 0).addMSecs(int(qBound(0.0, seconds, 86399.0) * 1000.0));
}

double toSeconds(const QTime &time)
{
    return QTime(0, 0).msecsTo(time) / 1000.0;
}

QString formatDuration(double seconds)
{
    const int total = int(seconds + 0.5);
    const QString minSec = QString("%1:%2").arg(total / 60 % 60, 2, 10, QChar('0')).arg(total % 60, 2, 10, QChar('0'));
    return total >= 3600 ? QString("%1:%2").arg(total / 3600).arg(minSec) : minSec;
}
}

ExportDialog::ExportDialog(const QString &videoPath, const Overlay &overlay, const TelemetrySessionPtr &session,
                           double syncOffset, double currentPosition, QWidget *parent)
    : QDialog(parent)
    , m_videoPath(videoPath)
    , m_overlay(overlay)
    , m_session(session)
    , m_syncOffset(syncOffset)
    , m_currentPosition(currentPosition)
    , m_ffmpeg(FfmpegTools::ffmpegProgram())
{
    setWindowTitle(tr("Export video"));
    createWidgets();
    startProbing();
    updateReadiness();
}

ExportDialog::~ExportDialog()
{
    if (m_thread)
        {
            // the job is deleted as its thread ends, and cleans up after itself
            m_thread->quit();
            m_thread->wait();
        }
}

void ExportDialog::createWidgets()
{
    const ExportSettings defaults = AppSettings::exportDefaults();

    m_sourceLabel = new QLabel(tr("Reading the video..."), this);
    m_sourceLabel->setWordWrap(true);

    m_options = new QWidget(this);

    m_outputEdit = new QLineEdit(suggestedOutput(), m_options);
    m_browseButton = new QPushButton(tr("Browse..."), m_options);
    connect(m_browseButton, &QPushButton::clicked, this, &ExportDialog::browseOutput);
    QHBoxLayout *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(m_browseButton);

    m_encoderCombo = new QComboBox(m_options);
    m_encoderCombo->addItem(tr("Looking for encoders..."));
    m_encoderCombo->setEnabled(false);
    connect(m_encoderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::onEncoderChanged);

    m_rateCombo = new QComboBox(m_options);
    m_rateCombo->addItem(tr("Constant quality"), ExportSettings::Quality);
    m_rateCombo->addItem(tr("Bitrate"), ExportSettings::Bitrate);
    m_rateCombo->setCurrentIndex(m_rateCombo->findData(defaults.rateControl));
    connect(m_rateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::updateRateControls);

    m_qualitySpin = new QSpinBox(m_options);
    m_qualitySpin->setRange(0, 51);
    m_qualitySpin->setValue(defaults.quality);
    m_qualitySpin->setToolTip(tr("Lower is better and bigger. 18-23 is hard to tell from the original; "
                                 "each step of 6 roughly halves or doubles the file."));

    m_bitrateSpin = new QDoubleSpinBox(m_options);
    m_bitrateSpin->setRange(0.5, 400.0);
    m_bitrateSpin->setDecimals(1);
    m_bitrateSpin->setSuffix(tr(" Mbit/s"));
    m_bitrateSpin->setValue(defaults.bitrateKbps / 1000.0);

    QHBoxLayout *rateRow = new QHBoxLayout;
    rateRow->addWidget(m_rateCombo);
    rateRow->addWidget(m_qualitySpin);
    rateRow->addWidget(m_bitrateSpin);
    m_rateNote = new QLabel(m_options);
    m_rateNote->setEnabled(false); // greyed: information, not a control
    rateRow->addWidget(m_rateNote, 1);
    connect(m_bitrateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ExportDialog::updateEstimate);

    m_speedCombo = new QComboBox(m_options);
    m_speedCombo->addItem(tr("Fast, bigger file"), ExportSettings::Fast);
    m_speedCombo->addItem(tr("Balanced"), ExportSettings::Balanced);
    m_speedCombo->addItem(tr("Slow, smaller file"), ExportSettings::Small);
    m_speedCombo->setCurrentIndex(m_speedCombo->findData(defaults.speed));

    m_sizeCombo = new QComboBox(m_options);
    m_sizeCombo->addItem(tr("Same as the video"), 0);
    for (int height : {2160, 1440, 1080, 720, 480})
        {
            m_sizeCombo->addItem(QString("%1p").arg(height), height);
        }
    const int sizeIndex = m_sizeCombo->findData(defaults.height);
    m_sizeCombo->setCurrentIndex(sizeIndex >= 0 ? sizeIndex : 0);

    m_audioCombo = new QComboBox(m_options);
    m_audioCombo->addItem(tr("Keep the original sound"), ExportSettings::CopyAudio);
    m_audioCombo->addItem(tr("Convert to AAC"), ExportSettings::AacAudio);
    m_audioCombo->addItem(tr("No sound"), ExportSettings::NoAudio);
    m_audioCombo->setCurrentIndex(m_audioCombo->findData(defaults.audio));
    m_audioCombo->setToolTip(tr("Keeping the sound is exact and instant. Convert it if the export fails "
                                "because the chosen file type cannot hold the camera's sound format."));

    m_rangeCheck = new QCheckBox(tr("Only a part:"), m_options);
    m_startEdit = new QTimeEdit(m_options);
    m_endEdit = new QTimeEdit(m_options);
    for (QTimeEdit *edit : {m_startEdit, m_endEdit})
        {
            edit->setDisplayFormat("H:mm:ss");
            edit->setEnabled(false);
            connect(m_rangeCheck, &QCheckBox::toggled, edit, &QWidget::setEnabled);
            connect(edit, &QTimeEdit::timeChanged, this, &ExportDialog::updateEstimate);
        }
    connect(m_rangeCheck, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
    m_startEdit->setTime(toTime(m_currentPosition));
    m_endEdit->setTime(toTime(m_currentPosition + 60.0));
    QHBoxLayout *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(m_rangeCheck);
    rangeRow->addWidget(m_startEdit);
    rangeRow->addWidget(new QLabel(QString::fromUtf8("–"), m_options));
    rangeRow->addWidget(m_endEdit);
    rangeRow->addStretch(1);

    QFormLayout *form = new QFormLayout(m_options);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Save as:"), outputRow);
    form->addRow(tr("Encoder:"), m_encoderCombo);
    form->addRow(tr("Quality:"), rateRow);
    form->addRow(tr("Speed:"), m_speedCombo);
    form->addRow(tr("Size:"), m_sizeCombo);
    form->addRow(tr("Sound:"), m_audioCombo);
    form->addRow(tr("Range:"), rangeRow);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    m_exportButton = new QPushButton(tr("Export"), this);
    m_exportButton->setDefault(true);
    connect(m_exportButton, &QPushButton::clicked, this, &ExportDialog::startExport);
    m_closeButton = new QPushButton(tr("Close"), this);
    connect(m_closeButton, &QPushButton::clicked, this, &ExportDialog::reject);
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_exportButton);
    buttons->addWidget(m_closeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_sourceLabel);
    layout->addWidget(m_options);
    layout->addStretch(1);
    layout->addWidget(m_progressBar);
    layout->addWidget(m_statusLabel);
    layout->addLayout(buttons);
    resize(560, 0);
    updateRateControls();
}

QString ExportDialog::suggestedOutput() const
{
    const QFileInfo video(m_videoPath);
    const QString configured = AppSettings::exportFolder();
    const QString folder = !configured.isEmpty() && QDir(configured).exists() ? configured : video.absolutePath();
    // never the name of something that is already there
    QString path = QString("%1/%2_overlay.mp4").arg(folder, video.completeBaseName());
    for (int n = 2; QFileInfo::exists(path); ++n)
        {
            path = QString("%1/%2_overlay_%3.mp4").arg(folder, video.completeBaseName()).arg(n);
        }
    return QDir::toNativeSeparators(path);
}

void ExportDialog::startProbing()
{
    const QString ffprobe = FfmpegTools::ffprobeFor(m_ffmpeg);
    const QString video = m_videoPath;
    auto *probeWatcher = new QFutureWatcher<ProbeResult>(this);
    connect(probeWatcher, &QFutureWatcher<ProbeResult>::finished, this, [this, probeWatcher]()
    {
        const ProbeResult result = probeWatcher->result();
        probeWatcher->deleteLater();
        onProbed(result.ok, result.info, result.error);
    });
    probeWatcher->setFuture(QtConcurrent::run([ffprobe, video]()
    {
        ProbeResult result;
        result.ok = MediaProbe::probe(ffprobe, video, &result.info, &result.error);
        return result;
    }));

    if (encoderCache().contains(m_ffmpeg))
        {
            onEncodersDetected(encoderCache().value(m_ffmpeg));
            return;
        }
    const QString ffmpeg = m_ffmpeg;
    auto *encoderWatcher = new QFutureWatcher<QVector<VideoEncoder>>(this);
    connect(encoderWatcher, &QFutureWatcher<QVector<VideoEncoder>>::finished, this, [this, encoderWatcher, ffmpeg]()
    {
        const QVector<VideoEncoder> encoders = encoderWatcher->result();
        encoderWatcher->deleteLater();
        if (!encoders.isEmpty())
            {
                encoderCache().insert(ffmpeg, encoders);
            }
        onEncodersDetected(encoders);
    });
    encoderWatcher->setFuture(QtConcurrent::run([ffmpeg]()
    {
        return VideoEncoders::detect(ffmpeg);
    }));
}

void ExportDialog::onProbed(bool ok, const MediaInfo &info, const QString &error)
{
    m_probed = ok;
    m_info = info;
    if (!ok)
        {
            m_sourceLabel->setText(error);
            updateReadiness();
            return;
        }
    m_sourceLabel->setText(tr("%1: %2 x %3, %4 fps, %5")
                           .arg(QFileInfo(m_videoPath).fileName())
                           .arg(info.size.width()).arg(info.size.height())
                           .arg(QLocale().toString(info.fps(), 'f', 2), formatDuration(info.duration)));
    // sizes that would enlarge the picture make no sense
    for (int i = m_sizeCombo->count() - 1; i > 0; --i)
        {
            if (m_sizeCombo->itemData(i).toInt() >= info.size.height())
                {
                    if (m_sizeCombo->currentIndex() == i)
                        {
                            m_sizeCombo->setCurrentIndex(0);
                        }
                    m_sizeCombo->removeItem(i);
                }
        }
    if (info.bitrateKbps > 0)
        {
            // the natural target: what the camera itself used
            m_bitrateSpin->setValue(info.bitrateKbps / 1000.0);
        }
    const QTime last = toTime(info.duration);
    m_startEdit->setMaximumTime(last);
    m_endEdit->setMaximumTime(last);
    if (m_startEdit->time() >= last)
        {
            m_startEdit->setTime(QTime(0, 0));
        }
    updateEstimate();
    updateReadiness();
}

void ExportDialog::onEncodersDetected(const QVector<VideoEncoder> &encoders)
{
    m_encodersKnown = !encoders.isEmpty();
    m_encoderCombo->clear();
    if (encoders.isEmpty())
        {
            m_encoderCombo->addItem(tr("None found"));
            m_statusLabel->setText(tr("ffmpeg could not be run (%1). Install it, or set its location in the settings.")
                                   .arg(m_ffmpeg));
            updateReadiness();
            return;
        }
    for (const VideoEncoder &encoder : encoders)
        {
            m_encoderCombo->addItem(encoder.label, encoder.id);
        }
    // the encoder of the last export; before the first one, the graphics
    // card if it can do H.264 - several times faster than software
    int preferred = AppSettings::hasExportDefaults() ? m_encoderCombo->findData(AppSettings::exportDefaults().encoder) : -1;
    for (int i = 0; preferred < 0 && i < encoders.size(); ++i)
        {
            if (encoders.at(i).hardware && !encoders.at(i).hevc)
                {
                    preferred = i;
                }
        }
    m_encoderCombo->setCurrentIndex(qMax(0, preferred));
    m_encoderCombo->setEnabled(true);
    onEncoderChanged();
    updateReadiness();
}

void ExportDialog::updateRateControls()
{
    const VideoEncoder encoder = VideoEncoders::byId(m_encoderCombo->currentData().toString());
    // an encoder without a quality mode leaves only the bitrate
    m_rateCombo->setEnabled(encoder.hasQuality);
    const bool quality = encoder.hasQuality && m_rateCombo->currentData().toInt() == ExportSettings::Quality;
    m_qualitySpin->setVisible(quality);
    m_bitrateSpin->setVisible(!quality);
    m_speedCombo->setEnabled(encoder.hasSpeed);
    updateEstimate();
}

void ExportDialog::onEncoderChanged()
{
    if (!m_encodersKnown)
        {
            return;
        }
    // quality numbers mean different things to different encoders
    const VideoEncoder encoder = VideoEncoders::byId(m_encoderCombo->currentData().toString());
    const ExportSettings defaults = AppSettings::exportDefaults();
    const bool remembered = AppSettings::hasExportDefaults() && defaults.encoder == encoder.id;
    m_qualitySpin->setValue(remembered ? defaults.quality : encoder.defaultQuality);
    updateRateControls();
}

void ExportDialog::updateEstimate()
{
    if (!m_probed || !m_bitrateSpin->isVisible())
        {
            m_rateNote->clear();
            return;
        }
    const ExportSettings settings = currentSettings();
    const double length = m_rangeCheck->isChecked() ? qMax(0.0, settings.duration) : m_info.duration;
    const double megabytes = m_bitrateSpin->value() * length / 8.0;
    const QString size = megabytes >= 1000.0 ? tr("about %1 GB").arg(QLocale().toString(megabytes / 1000.0, 'f', 1))
                         : tr("about %1 MB").arg(QLocale().toString(megabytes, 'f', 0));
    m_rateNote->setText(m_info.bitrateKbps > 0
                        ? tr("%1; the video itself is %2 Mbit/s").arg(size, QLocale().toString(m_info.bitrateKbps / 1000.0, 'f', 1))
                        : size);
}

void ExportDialog::updateReadiness()
{
    m_exportButton->setEnabled(!m_running && m_probed && m_encodersKnown);
}

void ExportDialog::browseOutput()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export video"), m_outputEdit->text(),
                         tr("MP4 video (*.mp4);;Matroska video (*.mkv);;QuickTime video (*.mov)"),
                         nullptr, QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty())
        {
            m_outputEdit->setText(QDir::toNativeSeparators(path));
        }
}

ExportSettings ExportDialog::currentSettings() const
{
    ExportSettings settings;
    settings.outputPath = QDir::fromNativeSeparators(m_outputEdit->text().trimmed());
    if (QFileInfo(settings.outputPath).suffix().isEmpty())
        {
            settings.outputPath += ".mp4";
        }
    settings.encoder = m_encoderCombo->currentData().toString();
    settings.rateControl = ExportSettings::RateControl(m_rateCombo->currentData().toInt());
    settings.quality = m_qualitySpin->value();
    settings.bitrateKbps = int(m_bitrateSpin->value() * 1000.0 + 0.5);
    settings.speed = ExportSettings::Speed(m_speedCombo->currentData().toInt());
    settings.height = m_sizeCombo->currentData().toInt();
    settings.audio = ExportSettings::Audio(m_audioCombo->currentData().toInt());
    if (m_rangeCheck->isChecked())
        {
            settings.start = toSeconds(m_startEdit->time());
            settings.duration = toSeconds(m_endEdit->time()) - settings.start;
        }
    return settings;
}

void ExportDialog::startExport()
{
    const ExportSettings settings = currentSettings();
    const QFileInfo output(settings.outputPath);
    if (settings.outputPath.isEmpty() || !output.absoluteDir().exists())
        {
            QMessageBox::warning(this, windowTitle(), tr("The folder to save into does not exist."));
            return;
        }
    if (output.absoluteFilePath() == QFileInfo(m_videoPath).absoluteFilePath())
        {
            QMessageBox::warning(this, windowTitle(), tr("The export cannot replace the video it is made from. Choose another name."));
            return;
        }
    if (m_rangeCheck->isChecked() && settings.duration <= 0.0)
        {
            QMessageBox::warning(this, windowTitle(), tr("The end of the range has to be after its start."));
            return;
        }
    if (output.exists()
            && QMessageBox::question(this, windowTitle(), tr("%1 already exists. Replace it?").arg(output.fileName()))
            != QMessageBox::Yes)
        {
            return;
        }
    AppSettings::setExportDefaults(settings);

    const ExportPlan plan = ExportPlan::make(m_videoPath, m_info, settings, m_overlay);
    ExportJob *job = new ExportJob(m_ffmpeg, plan, m_overlay, m_session, m_syncOffset);
    m_thread = new QThread(this);
    job->moveToThread(m_thread);
    connect(m_thread, &QThread::started, job, &ExportJob::start);
    connect(m_thread, &QThread::finished, job, &QObject::deleteLater);
    connect(this, &ExportDialog::stopRequested, job, &ExportJob::stop);
    connect(job, &ExportJob::progress, this, &ExportDialog::onProgress);
    connect(job, &ExportJob::finished, this, &ExportDialog::onFinished);

    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText(tr("Starting..."));
    setRunning(true);
    m_thread->start();
}

void ExportDialog::setRunning(bool running)
{
    m_running = running;
    m_options->setEnabled(!running);
    m_closeButton->setText(running ? tr("Stop") : tr("Close"));
    updateReadiness();
}

void ExportDialog::onProgress(qint64 frame, qint64 total, double framesPerSecond)
{
    if (total <= 0)
        {
            return;
        }
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(int(frame * 1000 / total));
    QString text = tr("Frame %1 of %2, %3 frames per second")
                   .arg(frame).arg(total).arg(QLocale().toString(framesPerSecond, 'f', 0));
    if (framesPerSecond > 0.0 && frame < total)
        {
            text += tr(", about %1 left").arg(formatDuration((total - frame) / framesPerSecond));
        }
    m_statusLabel->setText(text);
}

void ExportDialog::onFinished(ExportJob::Outcome outcome, const QString &message)
{
    m_thread->quit();
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;
    setRunning(false);
    m_closeButton->setEnabled(true);
    m_progressBar->setVisible(false);

    const QString output = currentSettings().outputPath;
    switch (outcome)
        {
        case ExportJob::Succeeded:
        {
            m_statusLabel->setText(tr("Saved %1").arg(QDir::toNativeSeparators(output)));
            QMessageBox box(QMessageBox::Information, windowTitle(),
                            tr("The video was exported to\n%1").arg(QDir::toNativeSeparators(output)),
                            QMessageBox::Ok, this);
            QPushButton *openFolder = box.addButton(tr("Open folder"), QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == openFolder)
                {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(output).absolutePath()));
                }
            // the next export from this dialog must not aim at the file just written
            m_outputEdit->setText(suggestedOutput());
            break;
        }
        case ExportJob::Stopped:
            m_statusLabel->setText(tr("Stopped. The part exported so far was saved as %1").arg(QDir::toNativeSeparators(output)));
            m_outputEdit->setText(suggestedOutput());
            break;
        case ExportJob::Cancelled:
            m_statusLabel->setText(tr("Stopped before anything could be saved."));
            break;
        case ExportJob::Failed:
            m_statusLabel->setText(tr("The export failed."));
            QMessageBox::warning(this, windowTitle(), tr("The export failed.\n\n%1").arg(message));
            break;
        }
}

void ExportDialog::reject()
{
    if (!m_running)
        {
            QDialog::reject();
            return;
        }
    if (QMessageBox::question(this, windowTitle(), tr("Stop the export? The part done so far is kept as a playable video."))
            == QMessageBox::Yes && m_running)
        {
            m_statusLabel->setText(tr("Stopping: finishing the file..."));
            m_closeButton->setEnabled(false);
            emit stopRequested();
        }
}
