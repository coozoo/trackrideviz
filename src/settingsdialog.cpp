#include "settingsdialog.h"

#include "settings/appsettings.h"
#include "video/ffmpegtools.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace
{
void select(QComboBox *combo, const QVariant &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QLabel *note(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setEnabled(false); // greyed: an explanation, not a control
    return label;
}
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));

    m_pageList = new QListWidget(this);
    m_pageList->setFixedWidth(150);
    m_pages = new QStackedWidget(this);
    connect(m_pageList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    addPage(tr("Appearance"), createAppearancePage());
    addPage(tr("Preview"), createPreviewPage());
    addPage(tr("Units"), createUnitsPage());
    addPage(tr("Export"), createExportPage());
    m_pageList->setCurrentRow(0);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    QHBoxLayout *body = new QHBoxLayout;
    body->addWidget(m_pageList);
    body->addWidget(m_pages, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
    resize(620, 360);

    load();
}

void SettingsDialog::addPage(const QString &title, QWidget *page)
{
    m_pageList->addItem(title);
    m_pages->addWidget(page);
}

QWidget *SettingsDialog::createAppearancePage()
{
    QWidget *page = new QWidget(this);

    m_styleCombo = new QComboBox(page);
    m_styleCombo->addItem(tr("System default"), QString());
    for (const QString &style : QStyleFactory::keys())
        {
            m_styleCombo->addItem(style, style);
        }

    m_colorSchemeCombo = new QComboBox(page);
    m_colorSchemeCombo->addItem(tr("System default"), AppSettings::SystemColors);
    m_colorSchemeCombo->addItem(tr("Light"), AppSettings::Light);
    m_colorSchemeCombo->addItem(tr("Dark"), AppSettings::Dark);

    // languages are named in themselves, so the list is readable in any of them
    m_languageCombo = new QComboBox(page);
    m_languageCombo->addItem(tr("System default"), "system");
    m_languageCombo->addItem("English", "en_US");
    m_languageCombo->addItem(QString::fromUtf8("Українська"), "uk_UA");

    m_saveLayoutCheck = new QCheckBox(tr("Save and restore the window layout"), page);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(tr("Style:"), m_styleCombo);
    form->addRow(tr("Colours:"), m_colorSchemeCombo);
    form->addRow(QString(), note(tr("Light and dark colours look right with the Fusion style; "
                                    "other styles may follow them only in part."), page));
    form->addRow(tr("Language:"), m_languageCombo);
    form->addRow(QString(), note(tr("The language changes after a restart."), page));
    form->addRow(QString(), m_saveLayoutCheck);
    return page;
}

QWidget *SettingsDialog::createPreviewPage()
{
    QWidget *page = new QWidget(this);

    m_hardwareDecodingCheck = new QCheckBox(tr("Decode video on the graphics card when possible"), page);
    m_hardwareDecodingCheck->setToolTip(tr("Uses far less processor time. Turn it off if the picture is wrong or missing."));

    m_decoderCombo = new QComboBox(page);
    m_decoderCombo->addItem(tr("Automatic"), AppSettings::AutoDecoder);
    m_decoderCombo->addItem("VA-API (Intel, AMD)", AppSettings::VaApi);
    m_decoderCombo->addItem("NVIDIA (CUDA)", AppSettings::Cuda);
    m_decoderCombo->addItem("Intel Quick Sync (QSV)", AppSettings::Qsv);
    m_decoderCombo->addItem("VDPAU", AppSettings::Vdpau);
    m_decoderCombo->setToolTip(tr("Which decoder is tried first. The others are still used if it is not available."));
    connect(m_hardwareDecodingCheck, &QCheckBox::toggled, m_decoderCombo, &QWidget::setEnabled);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(m_hardwareDecodingCheck);
#ifdef Q_OS_LINUX
    form->addRow(tr("Preferred decoder:"), m_decoderCombo);
#else
    // one obvious decoder per system here; nothing to choose
    m_decoderCombo->hide();
#endif
    form->addRow(note(tr("Decoding changes after a restart."), page));
    if (qApp->property("decoderFromEnvironment").toBool())
        {
            form->addRow(note(tr("These settings are overridden now: the environment variable "
                                 "QT_FFMPEG_DECODING_HW_DEVICE_TYPES is set."), page));
        }
    return page;
}

QWidget *SettingsDialog::createUnitsPage()
{
    QWidget *page = new QWidget(this);

    m_speedCombo = new QComboBox(page);
    m_speedCombo->addItem(tr("Kilometres per hour (km/h)"), Units::KilometresPerHour);
    m_speedCombo->addItem(tr("Miles per hour (mph)"), Units::MilesPerHour);

    m_lengthCombo = new QComboBox(page);
    m_lengthCombo->addItem(tr("Metric (m, km)"), Units::Metric);
    m_lengthCombo->addItem(tr("Imperial (ft, mi)"), Units::Imperial);

    m_temperatureCombo = new QComboBox(page);
    m_temperatureCombo->addItem(tr("Celsius (°C)"), Units::Celsius);
    m_temperatureCombo->addItem(tr("Fahrenheit (°F)"), Units::Fahrenheit);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(tr("Speed:"), m_speedCombo);
    form->addRow(tr("Distance and elevation:"), m_lengthCombo);
    form->addRow(tr("Temperature:"), m_temperatureCombo);
    form->addRow(note(tr("Used in the data tables and by the built-in gauges."), page));
    return page;
}

QWidget *SettingsDialog::createExportPage()
{
    QWidget *page = new QWidget(this);

    m_ffmpegEdit = new QLineEdit(page);
    m_ffmpegEdit->setPlaceholderText(tr("Found automatically"));
    m_ffmpegEdit->setClearButtonEnabled(true);
    QPushButton *browseFfmpeg = new QPushButton(tr("Browse..."), page);
    connect(browseFfmpeg, &QPushButton::clicked, this, [this]()
    {
        const QString path = QFileDialog::getOpenFileName(this, tr("Locate ffmpeg"), m_ffmpegEdit->text());
        if (!path.isEmpty())
            {
                m_ffmpegEdit->setText(QDir::toNativeSeparators(path));
                checkFfmpeg();
            }
    });
    QPushButton *check = new QPushButton(tr("Check"), page);
    connect(check, &QPushButton::clicked, this, &SettingsDialog::checkFfmpeg);
    connect(m_ffmpegEdit, &QLineEdit::editingFinished, this, &SettingsDialog::checkFfmpeg);
    QHBoxLayout *ffmpegRow = new QHBoxLayout;
    ffmpegRow->addWidget(m_ffmpegEdit, 1);
    ffmpegRow->addWidget(browseFfmpeg);
    ffmpegRow->addWidget(check);
    m_ffmpegStatus = note(QString(), page);

    m_exportFolderEdit = new QLineEdit(page);
    m_exportFolderEdit->setPlaceholderText(tr("Next to the source video"));
    m_exportFolderEdit->setClearButtonEnabled(true);
    QPushButton *browseFolder = new QPushButton(tr("Browse..."), page);
    connect(browseFolder, &QPushButton::clicked, this, [this]()
    {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder for exported videos"), m_exportFolderEdit->text());
        if (!folder.isEmpty())
            {
                m_exportFolderEdit->setText(QDir::toNativeSeparators(folder));
            }
    });
    QHBoxLayout *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_exportFolderEdit, 1);
    folderRow->addWidget(browseFolder);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(tr("ffmpeg program:"), ffmpegRow);
    form->addRow(QString(), m_ffmpegStatus);
    form->addRow(tr("Save videos to:"), folderRow);
    form->addRow(note(tr("Encoder, quality and size are chosen in the export window and remembered from the last export."), page));
    return page;
}

void SettingsDialog::checkFfmpeg()
{
    const QString entered = QDir::fromNativeSeparators(m_ffmpegEdit->text().trimmed());
    const QString program = entered.isEmpty() ? QStringLiteral("ffmpeg") : entered;
    // a fraction of a second: not worth a worker thread, but worth a busy cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString version = FfmpegTools::versionLine(program);
    QApplication::restoreOverrideCursor();
    m_ffmpegStatus->setText(version.isEmpty()
                            ? tr("Not found, or it cannot be run. Videos cannot be exported without it.")
                            : version);
}

void SettingsDialog::load()
{
    select(m_styleCombo, AppSettings::style());
    select(m_colorSchemeCombo, AppSettings::colorScheme());
    select(m_languageCombo, AppSettings::language());
    m_saveLayoutCheck->setChecked(AppSettings::saveWindowLayout());

    m_hardwareDecodingCheck->setChecked(AppSettings::hardwareDecoding());
    select(m_decoderCombo, AppSettings::preferredDecoder());
    m_decoderCombo->setEnabled(m_hardwareDecodingCheck->isChecked());

    m_ffmpegEdit->setText(QDir::toNativeSeparators(AppSettings::ffmpegPath()));
    m_exportFolderEdit->setText(QDir::toNativeSeparators(AppSettings::exportFolder()));
    checkFfmpeg();

    const Units units = AppSettings::units();
    select(m_speedCombo, units.speed);
    select(m_lengthCombo, units.length);
    select(m_temperatureCombo, units.temperature);
}

void SettingsDialog::accept()
{
    const QString language = m_languageCombo->currentData().toString();
    const bool hardwareDecoding = m_hardwareDecodingCheck->isChecked();
    const auto decoder = AppSettings::Decoder(m_decoderCombo->currentData().toInt());
    const bool needsRestart = language != AppSettings::language()
                              || hardwareDecoding != AppSettings::hardwareDecoding()
                              || decoder != AppSettings::preferredDecoder();

    AppSettings::setStyle(m_styleCombo->currentData().toString());
    AppSettings::setColorScheme(AppSettings::ColorScheme(m_colorSchemeCombo->currentData().toInt()));
    AppSettings::setLanguage(language);
    AppSettings::setSaveWindowLayout(m_saveLayoutCheck->isChecked());
    AppSettings::setHardwareDecoding(hardwareDecoding);
    AppSettings::setPreferredDecoder(decoder);

    Units units;
    units.speed = Units::Speed(m_speedCombo->currentData().toInt());
    units.length = Units::Length(m_lengthCombo->currentData().toInt());
    units.temperature = Units::Temperature(m_temperatureCombo->currentData().toInt());
    AppSettings::setUnits(units);

    AppSettings::setFfmpegPath(QDir::fromNativeSeparators(m_ffmpegEdit->text().trimmed()));
    AppSettings::setExportFolder(QDir::fromNativeSeparators(m_exportFolderEdit->text().trimmed()));

    if (needsRestart)
        {
            QMessageBox::information(this, tr("Settings"),
                                     tr("Language and decoding changes take effect the next time the program starts."));
        }
    QDialog::accept();
}
