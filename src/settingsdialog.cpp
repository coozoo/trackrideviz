#include "settingsdialog.h"

#include "maps/tilestore.h"
#include "settings/appsettings.h"
#include "weather/weather.h"
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
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLocale>
#include <QScrollArea>
#include <QTableWidget>
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
    label->setEnabled(false); // grayed: an explanation, not a control
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
    addPage(tr("Maps"), createMapsPage());
    addPage(tr("Power"), createPowerPage());
    addPage(tr("Surfaces"), createSurfacesPage());
    addPage(tr("Weather"), createWeatherPage());
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
    // a page taller than the dialog scrolls rather than squeezing its rows
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    m_pages->addWidget(scroll);
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
    form->addRow(tr("Colors:"), m_colorSchemeCombo);
    form->addRow(QString(), note(tr("Light and dark colors look right with the Fusion style; "
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
    m_speedCombo->addItem(tr("Kilometers per hour (km/h)"), Units::KilometersPerHour);
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

QWidget *SettingsDialog::createMapsPage()
{
    QWidget *page = new QWidget(this);
    m_mapDownloadsCheck = new QCheckBox(tr("Download street maps for gauges that show one"), page);
    m_mapProviderCombo = new QComboBox(page);
    for (const TileStore::Provider &provider : TileStore::providers())
        {
            m_mapProviderCombo->addItem(provider.urlTemplate.contains("{apikey}") ? tr("%1 (needs a key)").arg(provider.name)
                                        : provider.name, provider.id);
        }
    m_mapProviderCombo->addItem(tr("Own server"), QString(AppSettings::OwnMapServer));
    m_mapApiKeyEdit = new QLineEdit(page);
    m_mapApiKeyEdit->setClearButtonEnabled(true);
    m_mapUrlEdit = new QLineEdit(page);
    m_mapUrlEdit->setPlaceholderText(TileStore::defaultUrlTemplate());
    m_mapUrlEdit->setClearButtonEnabled(true);
    m_mapAttributionEdit = new QLineEdit(page);
    m_mapAttributionEdit->setPlaceholderText(TileStore::defaultAttribution());
    m_mapAttributionEdit->setClearButtonEnabled(true);
    m_mapCreditCheck = new QCheckBox(tr("Show the map's credit on the gauge"), page);
    connect(m_mapProviderCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateMapFields);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(m_mapDownloadsCheck);
    form->addRow(note(tr("The map pieces around a route are fetched once and kept on this computer; "
                         "after that the gauge and the export work without a connection. "
                         "The server learns which area the route is in."), page));
    form->addRow(tr("Map:"), m_mapProviderCombo);
    form->addRow(tr("Key:"), m_mapApiKeyEdit);
    form->addRow(tr("Tile server:"), m_mapUrlEdit);
    form->addRow(note(tr("An address with {z}, {x} and {y} in it."), page));
    form->addRow(tr("Its credit:"), m_mapAttributionEdit);
    m_mapClearButton = new QPushButton(page);
    m_mapClearButton->setToolTip(tr("Deletes the map pieces and terrain heights kept on this computer. "
                                    "They are fetched again when a gauge needs them."));
    connect(m_mapClearButton, &QPushButton::clicked, this, [this]()
    {
        TileStore::instance()->clearStored();
        TileStore::terrain()->clearStored();
        updateStoredMaps();
    });
    updateStoredMaps();
    form->addRow(m_mapCreditCheck);
    form->addRow(note(tr("The makers of these maps ask to be named wherever the map is shown. "
                         "Switch the credit off only when the video names them elsewhere, "
                         "in its end credits or its description."), page));
    form->addRow(m_mapClearButton);
    return page;
}

QWidget *SettingsDialog::createPowerPage()
{
    QWidget *page = new QWidget(this);
    m_powerCheck = new QCheckBox(tr("Estimate the power of rides recorded without a power meter"), page);

    auto spin = [page](double low, double high, int decimals, double step, const QString &suffix)
    {
        QDoubleSpinBox *box = new QDoubleSpinBox(page);
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setSingleStep(step);
        box->setSuffix(suffix);
        return box;
    };
    m_riderWeightSpin = spin(30.0, 200.0, 1, 0.5, tr(" kg"));
    m_riderHeightSpin = spin(100.0, 230.0, 0, 1.0, tr(" cm"));
    m_bikeWeightSpin = spin(3.0, 60.0, 1, 0.5, tr(" kg"));
    m_bikeWeightSpin->setToolTip(tr("With everything on it: bottles, bags, tools"));

    m_tiresCombo = new QComboBox(page);
    m_tiresCombo->addItem(tr("Road, fast"), "road-fast");
    m_tiresCombo->addItem(tr("Road, typical"), "road-typical");
    m_tiresCombo->addItem(tr("Road, slow"), "road-slow");
    m_tiresCombo->addItem(tr("Gravel, fast"), "gravel-fast");
    m_tiresCombo->addItem(tr("Gravel, typical"), "gravel-typical");
    m_tiresCombo->addItem(tr("Gravel, slow"), "gravel-slow");
    m_tiresCombo->addItem(tr("Mountain bike, fast"), "mtb-fast");
    m_tiresCombo->addItem(tr("Mountain bike, typical"), "mtb-typical");
    m_tiresCombo->addItem(tr("Mountain bike, slow"), "mtb-slow");
    m_tiresCombo->addItem(tr("My own value"), PowerEstimate::OwnValue);
    m_tiresCombo->setToolTip(tr("Fast: racing tires, pumped hard. Slow: thick, puncture-proof or knobby ones."));
    m_tireSizeEdit = new QLineEdit(page);
    m_tireSizeEdit->setPlaceholderText(tr("700x25, 26x1.95, 40-622..."));
    m_tireSizeEdit->setToolTip(tr("The size printed on the tire picks road, gravel or mountain bike by its width"));
    m_crrSpin = spin(0.001, 0.05, 4, 0.0005, QString());
    connect(m_tiresCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePowerFields);
    connect(m_crrSpin, &QDoubleSpinBox::valueChanged, this, [this](double value)
    {
        if (m_crrSpin->isEnabled())
            {
                m_ownCrr = value;
            }
    });
    connect(m_tireSizeEdit, &QLineEdit::textEdited, this, [this](const QString &size)
    {
        // the kind by the size, as fast or as slow as it was
        const QString kind = PowerEstimate::tireKind(size);
        const QString now = m_tiresCombo->currentData().toString();
        if (!kind.isEmpty())
            {
                const QString level = now.contains('-') ? now.section('-', 1) : QString("typical");
                select(m_tiresCombo, kind + "-" + level);
            }
    });

    m_positionCombo = new QComboBox(page);
    m_positionCombo->addItem(tr("Road bike, hands on the tops"), "tops");
    m_positionCombo->addItem(tr("Road bike, hands on the hoods"), "hoods");
    m_positionCombo->addItem(tr("Road bike, in the drops"), "drops");
    m_positionCombo->addItem(tr("On aero bars"), "aerobars");
    m_positionCombo->addItem(tr("Upright: mountain or city bike"), "upright");
    m_positionCombo->addItem(tr("My own value"), PowerEstimate::OwnValue);
    m_cdaSpin = spin(0.10, 1.50, 3, 0.01, tr(" m²"));
    connect(m_positionCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePowerFields);
    connect(m_riderWeightSpin, &QDoubleSpinBox::valueChanged, this, &SettingsDialog::updatePowerFields);
    connect(m_riderHeightSpin, &QDoubleSpinBox::valueChanged, this, &SettingsDialog::updatePowerFields);
    connect(m_cdaSpin, &QDoubleSpinBox::valueChanged, this, [this](double value)
    {
        if (m_cdaSpin->isEnabled())
            {
                m_ownCda = value;
            }
    });

    m_lossSpin = spin(0.0, 20.0, 1, 0.5, tr(" %"));
    m_lossSpin->setToolTip(tr("What the chain and the bearings take: 2-3 % when clean, more when worn or dirty"));

    QFormLayout *form = new QFormLayout(page);
    form->addRow(m_powerCheck);
    form->addRow(tr("Rider's weight:"), m_riderWeightSpin);
    form->addRow(tr("Rider's height:"), m_riderHeightSpin);
    form->addRow(tr("Bike's weight:"), m_bikeWeightSpin);
    form->addRow(tr("Tires:"), m_tiresCombo);
    form->addRow(tr("Or the tire size:"), m_tireSizeEdit);
    form->addRow(tr("Rolling resistance:"), m_crrSpin);
    form->addRow(tr("Riding position:"), m_positionCombo);
    form->addRow(tr("Drag area (CdA):"), m_cdaSpin);
    form->addRow(tr("Drivetrain loss:"), m_lossSpin);
    form->addRow(note(tr("Worked out from the speed, the grade and the elevation. A recorded power is never replaced: "
                         "the estimate is then listed beside it as \"Power, estimated\", to compare the two. "
                         "Without the weather of the day (see the Weather page) the wind is not known: a headwind reads low and a "
                         "tailwind high."), page));
    return page;
}

QWidget *SettingsDialog::createSurfacesPage()
{
    QWidget *page = new QWidget(this);
    m_surfaceTable = new QTableWidget(0, 2, page);
    m_surfaceTable->setHorizontalHeaderLabels({tr("Surface"), tr("Rolling resistance ×")});
    m_surfaceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_surfaceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_surfaceTable->verticalHeader()->hide();
    m_surfaceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_surfaceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_surfaceTable->setMinimumHeight(220);

    QPushButton *add = new QPushButton(tr("Add"), page);
    connect(add, &QPushButton::clicked, this, [this]()
    {
        const int row = m_surfaceTable->rowCount();
        m_surfaceTable->insertRow(row);
        m_surfaceTable->setItem(row, 0, new QTableWidgetItem(tr("New surface")));
        m_surfaceTable->setItem(row, 1, new QTableWidgetItem(QLocale().toString(1.0, 'f', 2)));
        m_surfaceTable->setCurrentCell(row, 0);
        m_surfaceTable->editItem(m_surfaceTable->item(row, 0));
    });
    QPushButton *remove = new QPushButton(tr("Remove"), page);
    connect(remove, &QPushButton::clicked, this, [this]()
    {
        if (m_surfaceTable->currentRow() >= 0)
            {
                m_surfaceTable->removeRow(m_surfaceTable->currentRow());
            }
    });
    QPushButton *usual = new QPushButton(tr("Restore the usual ones"), page);
    connect(usual, &QPushButton::clicked, this, [this]() { showSurfaces(Surfaces::usual()); });
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(usual);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addWidget(m_surfaceTable, 1);
    layout->addLayout(buttons);
    layout->addWidget(note(tr("What a stretch of a ride can be marked as in the Laps panel (\"Mark surface\"). The number is "
                              "how much harder the tires roll there than on typical asphalt: the power estimate multiplies "
                              "their rolling resistance by it. Stretches marked already keep the number they were marked with."),
                           page));
    return page;
}

void SettingsDialog::showSurfaces(const QVector<SurfaceType> &types)
{
    m_surfaceTable->setRowCount(0);
    for (const SurfaceType &type : types)
        {
            const int row = m_surfaceTable->rowCount();
            m_surfaceTable->insertRow(row);
            m_surfaceTable->setItem(row, 0, new QTableWidgetItem(type.name));
            m_surfaceTable->setItem(row, 1, new QTableWidgetItem(QLocale().toString(type.factor, 'f', 2)));
        }
}

QWidget *SettingsDialog::createWeatherPage()
{
    QWidget *page = new QWidget(this);
    auto spin = [page](double low, double high, int decimals, double step, const QString &suffix)
    {
        QDoubleSpinBox *box = new QDoubleSpinBox(page);
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setSingleStep(step);
        box->setSuffix(suffix);
        return box;
    };
    m_weatherCheck = new QCheckBox(tr("Fetch the wind and the air of the day of each ride from weather history"), page);
    m_weatherCheck->setToolTip(tr("Asks a weather service for the wind, the temperature and the pressure at places along the ride, "
                                  "hour by hour. Sent to it: those places and the date."));
    m_weatherSpacingSpin = spin(1.0, 100.0, 0, 1.0, tr(" km"));
    m_weatherSpacingSpin->setToolTip(tr("The weather services know places 10-25 km apart: closer than that brings little"));
    m_windShareSpin = spin(0.0, 100.0, 0, 5.0, tr(" %"));
    m_windShareSpin->setToolTip(tr("The wind is measured 10 m above the ground. Near it about 60 % is left in the open, "
                                   "less between houses and trees."));
    connect(m_weatherCheck, &QCheckBox::toggled, m_weatherSpacingSpin, &QWidget::setEnabled);
    connect(m_weatherCheck, &QCheckBox::toggled, m_windShareSpin, &QWidget::setEnabled);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(m_weatherCheck);
    form->addRow(tr("A place every:"), m_weatherSpacingSpin);
    form->addRow(tr("Wind reaching the rider:"), m_windShareSpin);
    form->addRow(note(tr("The wind's speed and direction, the wind against the rider and the air's density are then "
                         "measurements of the ride like any other: for gauges, for the data viewer, and counted in "
                         "by the power estimate. Kept on disk: a ride asked about once needs no connection again."), page));
    form->addRow(note(WeatherStore::attribution(), page));
    return page;
}

void SettingsDialog::updatePowerFields()
{
    const QString tires = m_tiresCombo->currentData().toString();
    const bool ownCrr = tires == PowerEstimate::OwnValue;
    m_crrSpin->setEnabled(false);   // so that showing a kind's number is not taken for typing one
    m_crrSpin->setValue(ownCrr ? m_ownCrr : PowerEstimate::crrOf(tires));
    m_crrSpin->setEnabled(ownCrr);

    const QString position = m_positionCombo->currentData().toString();
    const bool ownCda = position == PowerEstimate::OwnValue;
    m_cdaSpin->setEnabled(false);
    m_cdaSpin->setValue(ownCda ? m_ownCda
                        : PowerEstimate::cdaOf(position, m_riderWeightSpin->value(), m_riderHeightSpin->value()));
    m_cdaSpin->setEnabled(ownCda);
}

void SettingsDialog::updateStoredMaps()
{
    const qint64 bytes = TileStore::instance()->storedBytes() + TileStore::terrain()->storedBytes();
    m_mapClearButton->setText(tr("Delete the stored maps (%1)").arg(QLocale().formattedDataSize(bytes)));
    m_mapClearButton->setEnabled(bytes > 0);
}

void SettingsDialog::updateMapFields()
{
    const QString id = m_mapProviderCombo->currentData().toString();
    const bool own = id == QLatin1String(AppSettings::OwnMapServer);
    m_mapUrlEdit->setEnabled(own);
    m_mapAttributionEdit->setEnabled(own);
    m_mapApiKeyEdit->setEnabled(!own && TileStore::provider(id).urlTemplate.contains("{apikey}"));
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

    m_mapDownloadsCheck->setChecked(AppSettings::mapDownloads());
    select(m_mapProviderCombo, AppSettings::mapProvider());
    m_mapApiKeyEdit->setText(AppSettings::mapApiKey());
    m_mapUrlEdit->setText(AppSettings::mapTileUrl());
    m_mapAttributionEdit->setText(AppSettings::mapAttribution());
    m_mapCreditCheck->setChecked(AppSettings::mapCreditShown());
    updateMapFields();

    const PowerEstimate::Rider rider = AppSettings::powerRider();
    m_ownCrr = rider.crr;
    m_ownCda = rider.cda;
    m_powerCheck->setChecked(rider.enabled);
    m_riderWeightSpin->setValue(rider.riderKg);
    m_riderHeightSpin->setValue(rider.heightCm);
    m_bikeWeightSpin->setValue(rider.bikeKg);
    select(m_tiresCombo, rider.tires);
    select(m_positionCombo, rider.position);
    m_lossSpin->setValue(rider.drivetrainLoss);

    showSurfaces(AppSettings::surfaceTypes());

    const WeatherChoice weather = AppSettings::weather();
    m_weatherCheck->setChecked(weather.enabled);
    m_weatherSpacingSpin->setValue(weather.spacingKm);
    m_windShareSpin->setValue(weather.windShare);
    m_weatherSpacingSpin->setEnabled(weather.enabled);
    m_windShareSpin->setEnabled(weather.enabled);
    updatePowerFields();

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

    AppSettings::setMapDownloads(m_mapDownloadsCheck->isChecked());
    AppSettings::setMapProvider(m_mapProviderCombo->currentData().toString());
    AppSettings::setMapApiKey(m_mapApiKeyEdit->text());
    AppSettings::setMapCreditShown(m_mapCreditCheck->isChecked());
    AppSettings::setMapTileUrl(m_mapUrlEdit->text().trimmed());
    AppSettings::setMapAttribution(m_mapAttributionEdit->text().trimmed());

    PowerEstimate::Rider rider;
    rider.enabled = m_powerCheck->isChecked();
    rider.riderKg = m_riderWeightSpin->value();
    rider.heightCm = m_riderHeightSpin->value();
    rider.bikeKg = m_bikeWeightSpin->value();
    rider.tires = m_tiresCombo->currentData().toString();
    rider.crr = m_ownCrr;
    rider.position = m_positionCombo->currentData().toString();
    rider.cda = m_ownCda;
    rider.drivetrainLoss = m_lossSpin->value();
    AppSettings::setPowerRider(rider);

    QVector<SurfaceType> surfaces;
    for (int row = 0; row < m_surfaceTable->rowCount(); ++row)
        {
            bool ok = false;
            const QString name = m_surfaceTable->item(row, 0)->text().trimmed();
            const double factor = QLocale().toDouble(m_surfaceTable->item(row, 1)->text(), &ok);
            if (!name.isEmpty() && ok && factor > 0.0)
                {
                    surfaces.append({name, factor});
                }
        }
    AppSettings::setSurfaceTypes(surfaces);

    WeatherChoice weather;
    weather.enabled = m_weatherCheck->isChecked();
    weather.spacingKm = m_weatherSpacingSpin->value();
    weather.windShare = m_windShareSpin->value();
    AppSettings::setWeather(weather);

    if (needsRestart)
        {
            QMessageBox::information(this, tr("Settings"),
                                     tr("Language and decoding changes take effect the next time the program starts."));
        }
    QDialog::accept();
}
