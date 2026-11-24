#include "exportdialog.h"

#include "icons.h"
#include "settings/appsettings.h"
#include "video/libav.h"
#include "video/exportplan.h"
#include "video/lapexport.h"
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
#include <QInputDialog>
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

#include <algorithm>

namespace
{
struct ProbeResult
{
    bool ok{false};
    MediaInfo info;
    QString error;
};

// Testing the hardware encoders takes about a second and the answer does not
// change while the program runs, so it is asked once per engine and ffmpeg program.
// by engine and ffmpeg program: "own" or the program's path
QHash<QString, VideoEncoders::ForEngine> &encoderCache()
{
    static QHash<QString, VideoEncoders::ForEngine> cache;
    return cache;
}

// The user's own profiles in the profile list, so that their names never clash with the built-in ones.
const QString kOwnProfile = QStringLiteral("own:");

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

ExportDialog::ExportDialog(const QString &videoPath, const Overlay &overlay, const SyncMap &sync,
                           const QVector<TimeRange> &cuts, const Soundtrack &soundtrack, double currentPosition,
                           QWidget *parent)
    : QDialog(parent)
    , m_videoPath(videoPath)
    , m_namedAfter(videoPath)
    , m_overlay(overlay)
    , m_sync(sync)
    , m_cuts(cuts)
    , m_soundtrack(soundtrack)
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

    m_pictureCombo = new QComboBox(m_options);
    m_pictureCombo->addItem(tr("Video with gauges"), ExportSettings::VideoWithGauges);
    m_pictureCombo->addItem(tr("Only the gauges, on transparent"), ExportSettings::GaugesOnly);
    m_pictureCombo->setToolTip(tr("Only the gauges: a video with a see-through background, to lay over "
                                  "the camera's video in another editor. It has no sound."));

    m_profileCombo = new QComboBox(m_options);
    m_profileCombo->setToolTip(tr("Fills in the choices below; the bitrate of the built-in ones follows the video. "
                                  "Changing a choice afterwards makes it your own."));
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::activated), this, &ExportDialog::onProfileChosen);
    m_saveProfileButton = new QPushButton(Icons::named("document-save-as"), tr("Save as..."), m_options);
    m_saveProfileButton->setToolTip(tr("Keep the choices below under a name"));
    connect(m_saveProfileButton, &QPushButton::clicked, this, &ExportDialog::saveProfile);
    m_deleteProfileButton = new QPushButton(Icons::named("edit-delete"), tr("Delete"), m_options);
    connect(m_deleteProfileButton, &QPushButton::clicked, this, &ExportDialog::deleteProfile);
    QHBoxLayout *profileRow = new QHBoxLayout;
    profileRow->addWidget(m_profileCombo, 1);
    profileRow->addWidget(m_saveProfileButton);
    profileRow->addWidget(m_deleteProfileButton);

    m_outputEdit = new QLineEdit(suggestedOutput(), m_options);
    m_browseButton = new QPushButton(Icons::named("folder-open"), tr("Browse..."), m_options);
    connect(m_browseButton, &QPushButton::clicked, this, &ExportDialog::browseOutput);
    // the file type is the name's extension; picking one here changes it, typing one shows it here
    m_containerCombo = new QComboBox(m_options);
    // the entries change with the picture chosen
    m_containerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    fillContainers();
    connect(m_containerCombo, QOverload<int>::of(&QComboBox::activated), this, &ExportDialog::onContainerChosen);
    connect(m_outputEdit, &QLineEdit::textChanged, this, &ExportDialog::showContainer);
    QHBoxLayout *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(m_containerCombo);
    outputRow->addWidget(m_browseButton);

    m_encoderCombo = new QComboBox(m_options);
    // as wide as the names found later: sized only for "Looking for encoders...", it cut
    // "H.265 / HEVC (graphics card, VA-API)" after the slash
    m_encoderCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
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
    m_rateNote->setEnabled(false); // grayed: information, not a control
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
    // music, or a volume of the video's sound, cannot be copied: it is made anew
    m_audioCombo->addItem(m_soundtrack.changesSound() ? tr("Mix with the music, as AAC") : tr("Keep the original sound"),
                          ExportSettings::CopyAudio);
    m_audioCombo->addItem(tr("Convert to AAC"), ExportSettings::AacAudio);
    m_audioCombo->addItem(tr("Convert to Opus (MKV)"), ExportSettings::OpusAudio);
    m_audioCombo->addItem(tr("Convert to Vorbis (MKV)"), ExportSettings::VorbisAudio);
    m_audioCombo->addItem(tr("No sound"), ExportSettings::NoAudio);
    m_audioCombo->setCurrentIndex(qMax(0, m_audioCombo->findData(defaults.audio)));
    m_audioCombo->setToolTip(tr("Keeping the sound is exact and instant. Convert it if the export fails "
                                "because the chosen file type cannot hold the camera's sound format. "
                                "Opus sounds best for its size; the sound is only a small part of a video file, though."));
    m_audioBitrateCombo = new QComboBox(m_options);
    for (int kbps : {96, 128, 160, 192, 256, 320, 384})
        {
            m_audioBitrateCombo->addItem(tr("%1 kbit/s").arg(kbps), kbps);
        }
    const int audioBitrateIndex = m_audioBitrateCombo->findData(defaults.audioKbps);
    m_audioBitrateCombo->setCurrentIndex(audioBitrateIndex >= 0 ? audioBitrateIndex : m_audioBitrateCombo->findData(192));
    connect(m_audioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::onAudioChanged);
    QHBoxLayout *audioRow = new QHBoxLayout;
    audioRow->addWidget(m_audioCombo, 1);
    audioRow->addWidget(m_audioBitrateCombo);

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
    rangeRow->addWidget(new QLabel(QString::fromUtf8("-"), m_options));
    rangeRow->addWidget(m_endEdit);
    rangeRow->addStretch(1);

    m_perLapCheck = new QCheckBox(tr("One video per lap:"), m_options);
    m_perLapCheck->setToolTip(tr("Each lap its own file, named with the lap's number"));
    m_lapsEdit = new QLineEdit(m_options);
    // empty is all of them ("all" is understood too, untranslated as on the command line)
    m_lapsEdit->setPlaceholderText(tr("all laps"));
    m_lapsEdit->setToolTip(tr("Which laps: empty for all, or numbers as 3 or 1, 4-6"));
    m_lapsEdit->setEnabled(false);
    connect(m_perLapCheck, &QCheckBox::toggled, m_lapsEdit, &QWidget::setEnabled);
    connect(m_perLapCheck, &QCheckBox::toggled, this, [this](bool on)
    {
        // the laps are the range then
        m_rangeCheck->setEnabled(!on);
        m_startEdit->setEnabled(!on && m_rangeCheck->isChecked());
        m_endEdit->setEnabled(!on && m_rangeCheck->isChecked());
        updateEstimate();
    });
    connect(m_lapsEdit, &QLineEdit::textChanged, this, &ExportDialog::updateEstimate);
    m_lapsRow = new QWidget(m_options);
    QHBoxLayout *lapsRow = new QHBoxLayout(m_lapsRow);
    lapsRow->setContentsMargins(0, 0, 0, 0);
    lapsRow->addWidget(m_perLapCheck);
    lapsRow->addWidget(m_lapsEdit, 1);

    m_speedUpSpin = new QDoubleSpinBox(m_options);
    m_speedUpSpin->setRange(1.0, 1000.0);
    m_speedUpSpin->setDecimals(1);
    m_speedUpSpin->setPrefix(QStringLiteral("x"));
    m_speedUpSpin->setValue(1.0);
    m_speedUpSpin->setToolTip(tr("Plays this many times faster: a long, slow recording made short. "
                                 "The camera's sound is left out then; music plays as usual."));
    connect(m_speedUpSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ExportDialog::updateEstimate);
    m_speedUpNote = new QLabel(m_options);
    m_speedUpNote->setEnabled(false); // grayed: information, not a control
    QHBoxLayout *speedUpRow = new QHBoxLayout;
    speedUpRow->addWidget(m_speedUpSpin);
    speedUpRow->addWidget(m_speedUpNote, 1);

    QFormLayout *form = new QFormLayout(m_options);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Picture:"), m_pictureCombo);
    form->addRow(tr("Profile:"), profileRow);
    form->addRow(tr("Save as:"), outputRow);
    form->addRow(tr("Encoder:"), m_encoderCombo);
    // which engine runs it: the program's own or the ffmpeg program (Settings > Export)
    m_engineLabel = new QLabel(tr("Looking for encoders..."), m_options);
    m_engineLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Engine:"), m_engineLabel);
    form->addRow(tr("Quality:"), rateRow);
    form->addRow(tr("Speed:"), m_speedCombo);
    form->addRow(tr("Size:"), m_sizeCombo);
    form->addRow(tr("Sound:"), audioRow);
    form->addRow(tr("Range:"), rangeRow);
    // shown when there are laps (setLaps); QFormLayout::setRowVisible is newer than Qt 6.2
    m_lapsLabel = new QLabel(tr("Laps:"), m_options);
    form->addRow(m_lapsLabel, m_lapsRow);
    m_lapsLabel->hide();
    m_lapsRow->hide();
    form->addRow(tr("Faster:"), speedUpRow);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    m_exportButton = new QPushButton(Icons::named("document-export", "media-record"), tr("Export"), this);
    m_exportButton->setDefault(true);
    connect(m_exportButton, &QPushButton::clicked, this, &ExportDialog::startExport);
    m_closeButton = new QPushButton(Icons::named("window-close"), tr("Close"), this);
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
    updateAudioControls();
    fillProfiles(AppSettings::exportProfile());

    // a choice changed by hand is no longer the profile's
    for (QComboBox *combo : {m_encoderCombo, m_rateCombo, m_speedCombo, m_sizeCombo, m_audioCombo, m_audioBitrateCombo})
        {
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::onChoiceChanged);
        }
    connect(m_qualitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExportDialog::onChoiceChanged);
    connect(m_bitrateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ExportDialog::onChoiceChanged);
    connect(m_outputEdit, &QLineEdit::textChanged, this, &ExportDialog::onChoiceChanged);
    // connected last: it changes the file name, which is no choice of a profile
    connect(m_pictureCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::onPictureChanged);
}

bool ExportDialog::gaugesOnly() const
{
    return m_pictureCombo->currentData().toInt() == ExportSettings::GaugesOnly;
}

void ExportDialog::fillContainers()
{
    m_containerCombo->clear();
    if (gaugesOnly())
        {
            m_containerCombo->addItem(tr("MOV (ProRes 4444)"), "mov");
            m_containerCombo->addItem(tr("WebM (VP9)"), "webm");
            m_containerCombo->setToolTip(tr("ProRes is what most editors take with transparency; its files are big. "
                                            "WebM is small, and read by Kdenlive, Shotcut and browsers."));
        }
    else
        {
            m_containerCombo->addItem(tr("MP4"), "mp4");
            m_containerCombo->addItem(tr("MKV (Matroska)"), "mkv");
            m_containerCombo->addItem(tr("MOV (QuickTime)"), "mov");
            m_containerCombo->setToolTip(tr("MP4 plays everywhere and is what most sites want. "
                                            "MKV holds any sound, Opus and Vorbis too."));
        }
    showContainer();
}

void ExportDialog::onPictureChanged()
{
    const bool gauges = gaugesOnly();
    fillContainers();
    // the file type follows: a video type for the gauges alone would lose the transparency
    const QString path = m_outputEdit->text().trimmed();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (gauges)
        {
            // and back again when the video is chosen once more
            m_videoSuffix = suffix;
            if (suffix != QLatin1String("mov") && !ExportSettings::isVp9Alpha(suffix))
                {
                    m_outputEdit->setText(ExportSettings::withSuffix(path, "mov"));
                }
        }
    else if (!m_videoSuffix.isEmpty())
        {
            m_outputEdit->setText(ExportSettings::withSuffix(path, m_videoSuffix));
        }
    // nothing of these is used for the gauges alone
    for (QWidget *widget : std::initializer_list<QWidget *> {m_profileCombo, m_saveProfileButton, m_deleteProfileButton,
                                                             m_rateCombo, m_qualitySpin, m_bitrateSpin, m_speedCombo,
                                                             m_audioCombo, m_audioBitrateCombo})
        {
            widget->setEnabled(!gauges);
        }
    m_encoderCombo->setEnabled(!gauges && m_encodersKnown);
    if (!gauges)
        {
            m_deleteProfileButton->setEnabled(m_profileCombo->currentData().toString().startsWith(kOwnProfile));
            updateRateControls();
            updateAudioControls();
        }
    updateEstimate();
}

void ExportDialog::fillProfiles(const QString &selected)
{
    m_profileCombo->clear();
    m_profileCombo->addItem(tr("Custom"), QString());
    for (const ExportProfile &profile : ExportProfiles::builtIn())
        {
            m_profileCombo->addItem(profile.name, profile.id);
        }
    const QVector<ExportProfile> own = AppSettings::exportProfiles();
    if (!own.isEmpty())
        {
            m_profileCombo->insertSeparator(m_profileCombo->count());
        }
    for (const ExportProfile &profile : own)
        {
            m_profileCombo->addItem(profile.name, kOwnProfile + profile.id);
        }
    m_profileCombo->setCurrentIndex(qMax(0, m_profileCombo->findData(selected)));
    m_deleteProfileButton->setEnabled(m_profileCombo->currentData().toString().startsWith(kOwnProfile));
}

bool ExportDialog::chosenProfile(ExportProfile *profile) const
{
    const QString id = m_profileCombo->currentData().toString();
    if (id.isEmpty())
        {
            return false;
        }
    const QVector<ExportProfile> list = id.startsWith(kOwnProfile) ? AppSettings::exportProfiles() : ExportProfiles::builtIn();
    const QString wanted = id.startsWith(kOwnProfile) ? id.mid(kOwnProfile.size()) : id;
    for (const ExportProfile &candidate : list)
        {
            if (candidate.id == wanted)
                {
                    *profile = candidate;
                    return true;
                }
        }
    return false;
}

void ExportDialog::onProfileChosen()
{
    m_deleteProfileButton->setEnabled(m_profileCombo->currentData().toString().startsWith(kOwnProfile));
    m_profileShown = false;
    applyProfile();
}

void ExportDialog::applyProfile()
{
    ExportProfile profile;
    if (!m_probed || !m_encodersKnown || !chosenProfile(&profile))
        {
            return;
        }
    showSettings(ExportProfiles::apply(profile, currentSettings(), m_info, m_encoders));
    m_profileSettings = currentSettings();
    m_profileShown = true;
}

void ExportDialog::showSettings(const ExportSettings &settings)
{
    m_showing = true;
    const int encoderIndex = m_encoderCombo->findData(settings.encoder);
    if (encoderIndex >= 0)
        {
            m_encoderCombo->setCurrentIndex(encoderIndex);
        }
    m_rateCombo->setCurrentIndex(qMax(0, m_rateCombo->findData(settings.rateControl)));
    m_qualitySpin->setValue(settings.quality);
    m_bitrateSpin->setValue(settings.bitrateKbps / 1000.0);
    m_speedCombo->setCurrentIndex(qMax(0, m_speedCombo->findData(settings.speed)));
    // a size the video is too small for is the video's own
    m_sizeCombo->setCurrentIndex(qMax(0, m_sizeCombo->findData(settings.height)));
    m_audioCombo->setCurrentIndex(qMax(0, m_audioCombo->findData(settings.audio)));
    const int audioBitrateIndex = m_audioBitrateCombo->findData(settings.audioKbps);
    if (audioBitrateIndex >= 0)
        {
            m_audioBitrateCombo->setCurrentIndex(audioBitrateIndex);
        }
    m_outputEdit->setText(QDir::toNativeSeparators(settings.outputPath));
    m_showing = false;
    updateRateControls();
    updateAudioControls();
}

void ExportDialog::onChoiceChanged()
{
    if (m_showing || !m_profileShown || m_profileCombo->currentIndex() <= 0 || gaugesOnly())
        {
            return;
        }
    if (!ExportProfiles::sameChoices(currentSettings(), m_profileSettings))
        {
            m_profileCombo->setCurrentIndex(0);
            m_deleteProfileButton->setEnabled(false);
            m_profileShown = false;
        }
}

void ExportDialog::saveProfile()
{
    ExportProfile chosen;
    const bool own = chosenProfile(&chosen) && !chosen.builtIn;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save profile"), tr("Name:"), QLineEdit::Normal,
                                               own ? chosen.name : QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        {
            return;
        }
    QVector<ExportProfile> profiles = AppSettings::exportProfiles();
    const auto same = std::find_if(profiles.begin(), profiles.end(), [&](const ExportProfile &profile)
    {
        return profile.name == name;
    });
    const ExportProfile profile = ExportProfile::of(name, currentSettings());
    if (same != profiles.end())
        {
            if (QMessageBox::question(this, windowTitle(), tr("A profile called %1 is there already. Replace it?").arg(name))
                    != QMessageBox::Yes)
                {
                    return;
                }
            *same = profile;
        }
    else
        {
            profiles.append(profile);
        }
    AppSettings::setExportProfiles(profiles);
    fillProfiles(kOwnProfile + name);
    m_profileSettings = currentSettings();
    m_profileShown = true;
}

void ExportDialog::deleteProfile()
{
    ExportProfile chosen;
    if (!chosenProfile(&chosen) || chosen.builtIn
            || QMessageBox::question(this, windowTitle(), tr("Delete the profile %1?").arg(chosen.name)) != QMessageBox::Yes)
        {
            return;
        }
    QVector<ExportProfile> profiles = AppSettings::exportProfiles();
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(), [&](const ExportProfile &profile)
    {
        return profile.name == chosen.name;
    }), profiles.end());
    AppSettings::setExportProfiles(profiles);
    fillProfiles(QString());
    m_profileShown = false;
}

void ExportDialog::updateAudioControls()
{
    const auto audio = ExportSettings::Audio(m_audioCombo->currentData().toInt());
    m_audioBitrateCombo->setEnabled(!gaugesOnly() && audio != ExportSettings::NoAudio
                                    && (audio != ExportSettings::CopyAudio || m_soundtrack.changesSound()));
}

void ExportDialog::onAudioChanged()
{
    updateAudioControls();
    // Opus and Vorbis go into Matroska: the file name follows
    const auto audio = ExportSettings::Audio(m_audioCombo->currentData().toInt());
    const QString path = m_outputEdit->text().trimmed();
    if (ExportSettings::needsMatroska(audio) && ExportSettings::isMp4Family(QFileInfo(path).suffix()))
        {
            m_outputEdit->setText(ExportSettings::withSuffix(path, "mkv"));
        }
}

void ExportDialog::showContainer()
{
    // an extension the list does not know is left as it is: ffmpeg may know it
    m_containerCombo->setCurrentIndex(m_containerCombo->findData(QFileInfo(m_outputEdit->text().trimmed()).suffix().toLower()));
}

void ExportDialog::onContainerChosen()
{
    const QString suffix = m_containerCombo->currentData().toString();
    m_outputEdit->setText(ExportSettings::withSuffix(m_outputEdit->text().trimmed(), suffix));
    // MP4 and MOV cannot hold Opus or Vorbis: the sound is made AAC
    if (ExportSettings::isMp4Family(suffix) && ExportSettings::needsMatroska(ExportSettings::Audio(m_audioCombo->currentData().toInt())))
        {
            m_audioCombo->setCurrentIndex(m_audioCombo->findData(ExportSettings::AacAudio));
        }
}

QString ExportDialog::suggestedOutput() const
{
    const QFileInfo video(m_namedAfter);
    const QString configured = AppSettings::exportFolder();
    const QString folder = !configured.isEmpty() && QDir(configured).exists() ? configured : video.absolutePath();
    // the file type chosen so far, or that of the last export
    QString suffix = m_outputEdit ? QFileInfo(m_outputEdit->text().trimmed()).suffix() : QString();
    if (suffix.isEmpty())
        {
            suffix = AppSettings::exportFileType();
        }
    if (suffix.isEmpty())
        {
            suffix = ExportSettings::needsMatroska(AppSettings::exportDefaults().audio) ? "mkv" : "mp4";
        }
    // never the name of something that is already there
    QString path = QString("%1/%2_overlay.%3").arg(folder, video.completeBaseName(), suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        {
            path = QString("%1/%2_overlay_%3.%4").arg(folder, video.completeBaseName(), QString::number(n), suffix);
        }
    return QDir::toNativeSeparators(path);
}

void ExportDialog::setBackground(const VideoBackground &background, double length, const QString &namedAfter)
{
    m_background = background;
    m_namedAfter = namedAfter;
    m_outputEdit->setText(suggestedOutput());
    onProbed(true, background.mediaInfo(length), QString());
    m_sourceLabel->setText(tr("No video: %1 x %2, %3 fps, %4, over %5")
                           .arg(background.size.width()).arg(background.size.height()).arg(background.fps)
                           .arg(formatDuration(length),
                                background.isTransparent() ? tr("nothing (see-through)") : background.color.name()));
    m_pictureCombo->setItemText(0, tr("Gauges over the background"));
    if (background.isTransparent())
        {
            m_pictureCombo->setCurrentIndex(m_pictureCombo->findData(ExportSettings::GaugesOnly));
        }
}

void ExportDialog::startProbing()
{
    const QString ffprobe = FfmpegTools::ffprobeFor(m_ffmpeg);
    const QString video = m_videoPath;
    // the engine of the settings, or the other one when it cannot read the video or export here
    const bool preferOwn = AppSettings::ownVideoEngine();
    // no video: setBackground tells what there is to export
    if (!video.isEmpty())
        {
            auto *probeWatcher = new QFutureWatcher<ProbeResult>(this);
            connect(probeWatcher, &QFutureWatcher<ProbeResult>::finished, this, [this, probeWatcher]()
            {
                const ProbeResult result = probeWatcher->result();
                probeWatcher->deleteLater();
                onProbed(result.ok, result.info, result.error);
            });
            probeWatcher->setFuture(QtConcurrent::run([ffprobe, video, preferOwn]()
            {
                ProbeResult result;
                result.ok = MediaProbe::probe(ffprobe, video, &result.info, &result.error, preferOwn);
                return result;
            }));
        }

    const QString key = (preferOwn ? QStringLiteral("own|") : QStringLiteral("program|")) + m_ffmpeg;
    if (encoderCache().contains(key))
        {
            const VideoEncoders::ForEngine known = encoderCache().value(key);
            m_ownEngine = known.own;
            onEncodersDetected(known.encoders);
            return;
        }
    const QString ffmpeg = m_ffmpeg;
    auto *encoderWatcher = new QFutureWatcher<VideoEncoders::ForEngine>(this);
    connect(encoderWatcher, &QFutureWatcher<VideoEncoders::ForEngine>::finished, this, [this, encoderWatcher, key]()
    {
        const VideoEncoders::ForEngine found = encoderWatcher->result();
        encoderWatcher->deleteLater();
        if (!found.encoders.isEmpty())
            {
                encoderCache().insert(key, found);
            }
        m_ownEngine = found.own;
        onEncodersDetected(found.encoders);
    });
    encoderWatcher->setFuture(QtConcurrent::run([preferOwn, ffmpeg]()
    {
        return VideoEncoders::detectFor(preferOwn, ffmpeg);
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
    applyProfile();
}

void ExportDialog::onEncodersDetected(const QVector<VideoEncoder> &encoders)
{
    m_encodersKnown = !encoders.isEmpty();
    m_encoders = encoders;
    m_encoderCombo->clear();
    m_engineLabel->setText(m_ownEngine ? tr("Built in: %1").arg(Libav::version()) : tr("ffmpeg program: %1").arg(m_ffmpeg));
    if (encoders.isEmpty())
        {
            m_encoderCombo->addItem(tr("None found"));
            m_engineLabel->setText(tr("None"));
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
            if (encoders.at(i).hardware && encoders.at(i).codec == VideoEncoder::H264)
                {
                    preferred = i;
                }
        }
    m_encoderCombo->setCurrentIndex(qMax(0, preferred));
    m_encoderCombo->setEnabled(!gaugesOnly());
    onEncoderChanged();
    updateReadiness();
    applyProfile();
}

void ExportDialog::updateRateControls()
{
    const VideoEncoder encoder = VideoEncoders::byId(m_encoderCombo->currentData().toString());
    // an encoder without a quality mode leaves only the bitrate
    m_rateCombo->setEnabled(encoder.hasQuality && !gaugesOnly());
    const bool quality = encoder.hasQuality && m_rateCombo->currentData().toInt() == ExportSettings::Quality;
    m_qualitySpin->setVisible(quality);
    m_bitrateSpin->setVisible(!quality);
    m_speedCombo->setEnabled(encoder.hasSpeed && !gaugesOnly());
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
    // also called while the widgets are still being made, by the range's times
    if (!m_probed)
        {
            return;
        }
    const ExportSettings settings = currentSettings();
    const double start = m_rangeCheck->isChecked() ? settings.start : 0.0;
    const double end = m_rangeCheck->isChecked() ? settings.start + qMax(0.0, settings.duration) : m_info.duration;
    // how long the output plays: the range, or the laps chosen together
    double kept = CutList::keptLength(CutList::kept(m_cuts, start, end));
    if (m_perLapCheck->isChecked())
        {
            kept = 0.0;
            for (int lap : chosenLaps())
                {
                    kept += CutList::keptLength(CutList::kept(m_cuts, m_laps.at(lap - 1).start, m_laps.at(lap - 1).end));
                }
        }
    const double length = kept / qMax(1.0, settings.speedUp);
    m_speedUpNote->setText(settings.isSpedUp() ? tr("the video will be %1 long").arg(formatDuration(length)) : QString());
    if (!m_bitrateSpin->isVisible() || gaugesOnly())
        {
            m_rateNote->clear();
            return;
        }
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
    const QStringList filters = gaugesOnly() ? QStringList{tr("QuickTime video (*.mov)"), tr("WebM video (*.webm)")}
                                : QStringList{tr("MP4 video (*.mp4)"), tr("Matroska video (*.mkv)"), tr("QuickTime video (*.mov)")};
    // the type chosen so far comes first in the dialog
    QString filter = filters.value(m_containerCombo->currentIndex(), filters.first());
    const QString path = QFileDialog::getSaveFileName(this, tr("Export video"), m_outputEdit->text(), filters.join(";;"),
                         &filter, QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty())
        {
            m_outputEdit->setText(QDir::toNativeSeparators(path));
        }
}

ExportSettings ExportDialog::currentSettings() const
{
    ExportSettings settings;
    settings.outputPath = QDir::fromNativeSeparators(m_outputEdit->text().trimmed());
    settings.picture = ExportSettings::Picture(m_pictureCombo->currentData().toInt());
    if (QFileInfo(settings.outputPath).suffix().isEmpty())
        {
            settings.outputPath += settings.picture == ExportSettings::GaugesOnly ? ".mov"
                                   : ExportSettings::needsMatroska(ExportSettings::Audio(m_audioCombo->currentData().toInt()))
                                   ? ".mkv" : ".mp4";
        }
    settings.encoder = m_encoderCombo->currentData().toString();
    settings.rateControl = ExportSettings::RateControl(m_rateCombo->currentData().toInt());
    settings.quality = m_qualitySpin->value();
    settings.bitrateKbps = int(m_bitrateSpin->value() * 1000.0 + 0.5);
    settings.speed = ExportSettings::Speed(m_speedCombo->currentData().toInt());
    settings.height = m_sizeCombo->currentData().toInt();
    settings.audio = ExportSettings::Audio(m_audioCombo->currentData().toInt());
    settings.audioKbps = m_audioBitrateCombo->currentData().toInt();
    settings.background = m_background.color;
    settings.speedUp = m_speedUpSpin->value();
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
    // QFileInfo compares as the disk does: "Ride.MP4" is "ride.mp4" where case does not count
    QStringList inputs;
    if (!m_videoPath.isEmpty())
        {
            inputs << m_videoPath;
        }
    for (const MusicTrack &track : m_soundtrack.tracks)
        {
            inputs << track.path;
        }
    for (const QString &input : std::as_const(inputs))
        {
            if (output == QFileInfo(input))
                {
                    QMessageBox::warning(this, windowTitle(), tr("The export cannot replace a file it is made from (%1). "
                                         "Choose another name.").arg(QFileInfo(input).fileName()));
                    return;
                }
        }
    const bool gauges = settings.picture == ExportSettings::GaugesOnly;
    if (gauges && output.suffix().compare("mov", Qt::CaseInsensitive) != 0 && !ExportSettings::isVp9Alpha(output.suffix()))
        {
            QMessageBox::warning(this, windowTitle(), tr("A .%1 file cannot hold a see-through picture. Save the gauges "
                                 "as .mov or .webm.").arg(output.suffix()));
            return;
        }
    if (gauges && m_overlay.regions(ExportPlan::outputSizeFor(m_info.size, settings.height)).isEmpty())
        {
            QMessageBox::warning(this, windowTitle(), tr("There are no gauges on the video to export."));
            return;
        }
    if (!gauges && ExportSettings::needsMatroska(settings.audio) && ExportSettings::isMp4Family(output.suffix()))
        {
            QMessageBox::warning(this, windowTitle(), tr("A .%1 file cannot hold Opus or Vorbis sound. Save it as .mkv, "
                                 "or convert the sound to AAC.").arg(output.suffix()));
            return;
        }
    if (m_rangeCheck->isChecked() && !m_perLapCheck->isChecked() && settings.duration <= 0.0)
        {
            QMessageBox::warning(this, windowTitle(), tr("The end of the range has to be after its start."));
            return;
        }
    // one video, or one per lap chosen, each with the lap's number in its name
    QVector<QPair<QString, ExportSettings>> runs;
    if (m_perLapCheck->isChecked())
        {
            QString error;
            const QVector<int> laps = chosenLaps(&error);
            if (laps.isEmpty())
                {
                    QMessageBox::warning(this, windowTitle(), error.isEmpty() ? tr("None of these laps is in the video.") : error);
                    return;
                }
            for (int lap : laps)
                {
                    ExportSettings one = settings;
                    one.start = m_laps.at(lap - 1).start;
                    one.duration = m_laps.at(lap - 1).length();
                    one.outputPath = LapExport::outputPath(settings.outputPath, lap, int(m_laps.size()));
                    runs.append({tr("Lap %1").arg(lap), one});
                }
        }
    else
        {
            runs.append({QString(), settings});
        }
    QStringList existing;
    for (const auto &run : std::as_const(runs))
        {
            if (QFileInfo::exists(run.second.outputPath))
                {
                    existing << QFileInfo(run.second.outputPath).fileName();
                }
        }
    if (!existing.isEmpty()
            && QMessageBox::question(this, windowTitle(), existing.size() == 1 ? tr("%1 already exists. Replace it?").arg(existing.first())
                                     : tr("%1 files already exist (%2...). Replace them?").arg(existing.size()).arg(existing.first()))
            != QMessageBox::Yes)
        {
            return;
        }
    // the choices of a video: those of the gauges alone would be what the next video starts with
    if (!gauges)
        {
            AppSettings::setExportDefaults(settings);
            AppSettings::setExportProfile(m_profileCombo->currentData().toString());
        }
    m_queue = runs;
    m_runs = int(runs.size());
    m_saved.clear();
    setRunning(true);
    runNext();
}

void ExportDialog::runNext()
{
    const QPair<QString, ExportSettings> run = m_queue.takeFirst();
    m_currentLabel = run.first.isEmpty() ? QString() : tr("%1 of %2").arg(run.first).arg(m_runs);
    m_current = run.second;
    const ExportPlan plan = ExportPlan::make(m_videoPath, m_info, m_current, m_overlay, m_cuts, m_soundtrack);
    ExportJob *job = new ExportJob(m_ffmpeg, plan, m_overlay, m_sync, nullptr,
                                   m_ownEngine ? ExportJob::OwnEngine : ExportJob::ProgramEngine);
    m_thread = new QThread(this);
    job->moveToThread(m_thread);
    connect(m_thread, &QThread::started, job, &ExportJob::start);
    connect(m_thread, &QThread::finished, job, &QObject::deleteLater);
    connect(this, &ExportDialog::stopRequested, job, &ExportJob::stop);
    connect(job, &ExportJob::progress, this, &ExportDialog::onProgress);
    connect(job, &ExportJob::finished, this, &ExportDialog::onFinished);

    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText(m_currentLabel.isEmpty() ? tr("Starting...") : tr("%1: starting...").arg(m_currentLabel));
    m_thread->start();
}

QVector<int> ExportDialog::chosenLaps(QString *error) const
{
    QVector<int> laps;
    if (!m_perLapCheck->isChecked() || !LapExport::numbers(m_lapsEdit->text(), int(m_laps.size()), &laps, error))
        {
            return {};
        }
    // only those the video shows
    laps.erase(std::remove_if(laps.begin(), laps.end(), [this](int lap) { return m_laps.at(lap - 1).length() <= 0.0; }), laps.end());
    return laps;
}

void ExportDialog::setLaps(const QVector<TimeRange> &laps)
{
    m_laps = laps;
    const bool any = std::any_of(laps.cbegin(), laps.cend(), [](const TimeRange &lap) { return lap.length() > 0.0; });
    m_lapsLabel->setVisible(any);
    m_lapsRow->setVisible(any);
    if (!any)
        {
            m_perLapCheck->setChecked(false);
        }
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
    m_statusLabel->setText(m_currentLabel.isEmpty() ? text : tr("%1: %2").arg(m_currentLabel, text));
}

void ExportDialog::onFinished(ExportJob::Outcome outcome, const QString &message)
{
    m_thread->quit();
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;
    const QString output = m_current.outputPath;
    if (outcome == ExportJob::Succeeded)
        {
            m_saved << output;
            if (!m_queue.isEmpty())
                {
                    // the next lap
                    runNext();
                    return;
                }
        }
    // a stop or a failure ends the laps still to come too
    m_queue.clear();
    setRunning(false);
    m_closeButton->setEnabled(true);
    m_progressBar->setVisible(false);

    switch (outcome)
        {
        case ExportJob::Succeeded:
        {
            const bool several = m_saved.size() > 1;
            m_statusLabel->setText(several ? tr("Saved %1 videos in %2").arg(m_saved.size())
                                   .arg(QDir::toNativeSeparators(QFileInfo(output).absolutePath()))
                                   : tr("Saved %1").arg(QDir::toNativeSeparators(output)));
            QMessageBox box(QMessageBox::Information, windowTitle(),
                            several ? tr("%1 videos, one per lap, were exported to\n%2").arg(m_saved.size())
                            .arg(QDir::toNativeSeparators(QFileInfo(output).absolutePath()))
                            : tr("The video was exported to\n%1").arg(QDir::toNativeSeparators(output)),
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
            m_statusLabel->setText(m_saved.isEmpty() ? tr("Stopped. The part exported so far was saved as %1").arg(QDir::toNativeSeparators(output))
                                   : tr("Stopped. %1 videos were saved, and the part of the next one exported so far as %2")
                                   .arg(m_saved.size()).arg(QDir::toNativeSeparators(output)));
            m_outputEdit->setText(suggestedOutput());
            break;
        case ExportJob::Canceled:
            m_statusLabel->setText(tr("Stopped before anything could be saved."));
            break;
        case ExportJob::Failed:
            m_statusLabel->setText(m_saved.isEmpty() ? tr("The export failed.")
                                   : tr("The export of %1 failed; %2 videos were saved before it.").arg(m_currentLabel).arg(m_saved.size()));
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
