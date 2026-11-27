#include "settingsdialog.h"

#include "icons.h"
#include "maps/tilestore.h"
#include "settings/appsettings.h"
#include "telemetry/gears.h"
#include "weather/weather.h"
#include "video/ffmpegtools.h"
#include "video/libav.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLocale>
#include <QScrollArea>
#include <QMenu>
#include <QToolButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QStackedWidget>
#include <QStyle>
#include <QSpinBox>
#include <QScreen>
#include <QStyleFactory>
#include <QVBoxLayout>

#include <utility>

namespace
{
void select(QComboBox *combo, const QVariant &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

const double kPoundsPerKg = 2.2046226;
const double kCmPerInch = 2.54;

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
    m_pages = new QStackedWidget(this);
    connect(m_pageList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    m_pageList->setIconSize(QSize(32, 32));
    m_pageList->setSpacing(4);
    addPage(tr("Appearance"), "preferences-desktop-theme", createAppearancePage());
    addPage(tr("Preview"), "video-display", createPreviewPage());
    addPage(tr("Units"), "accessories-calculator", createUnitsPage());
    addPage(tr("Export"), "media-record", createExportPage());
    addPage(tr("Maps"), "applications-internet", createMapsPage());
    addPage(tr("Bike"), "bicycle", createBikePage());
    addPage(tr("Power"), "power", createPowerPage());
    addPage(tr("Surfaces"), "road", createSurfacesPage());
    addPage(tr("Weather"), "weather-few-clouds", createWeatherPage());
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

    m_buttons = buttons;
    m_body = body;
    resize(560, 360);

    load();
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_fitted)
        {
            return;
        }
    // only now are the pages styled and their sizes true
    m_fitted = true;
    QLayout *layout = this->layout();
    QHBoxLayout *body = m_body;
    QDialogButtonBox *buttons = m_buttons;
    // the list as wide as its longest name, in any language
    m_pageList->setFixedWidth(m_pageList->sizeHintForColumn(0) + 2 * (m_pageList->frameWidth() + m_pageList->spacing()) + 24);
    // as large as the largest page needs, as far as the screen allows; what still does not
    // fit scrolls
    QSize largest;
    for (int i = 0; i < m_pages->count(); ++i)
        {
            // the pages not shown yet too: styled before they are measured
            QWidget *page = static_cast<QScrollArea *>(m_pages->widget(i))->widget();
            page->ensurePolished();
            largest = largest.expandedTo(page->sizeHint());
        }
    const int scrollBar = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const QMargins margins = layout->contentsMargins();
    QSize wanted(m_pageList->width() + body->spacing() + largest.width() + scrollBar + margins.left() + margins.right(),
                 largest.height() + layout->spacing() + buttons->sizeHint().height() + margins.top() + margins.bottom());
    const QSize room = screen()->availableGeometry().size() * 0.9;
    resize(wanted.boundedTo(room).expandedTo(QSize(560, 360)));
    // still on the screen when it grew
    move(pos().x() - std::max(0, frameGeometry().right() - screen()->availableGeometry().right()),
         pos().y() - std::max(0, frameGeometry().bottom() - screen()->availableGeometry().bottom()));
}

void SettingsDialog::addPage(const QString &title, const QString &icon, QWidget *page)
{
    QListWidgetItem *item = new QListWidgetItem(Icons::bundled(icon), title);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_pageList->addItem(item);
    // a page taller than the dialog scrolls rather than squeezing its rows
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    // and one too wide, in a small window or a long language, scrolls sideways
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
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
    m_recentCountSpin = new QSpinBox(page);
    m_recentCountSpin->setRange(0, AppSettings::kMaxRecentFiles);
    m_recentCountSpin->setSpecialValueText(tr("None"));
    m_recentCountSpin->setToolTip(tr("How many recent videos, data files and projects the File menu lists"));

    QFormLayout *form = new QFormLayout(page);
    form->addRow(tr("Style:"), m_styleCombo);
    form->addRow(tr("Colors:"), m_colorSchemeCombo);
    form->addRow(QString(), note(tr("Light and dark colors suit the Fusion style best."), page));
    form->addRow(tr("Language:"), m_languageCombo);
    form->addRow(QString(), note(tr("The language changes after a restart."), page));
    form->addRow(QString(), m_saveLayoutCheck);
    form->addRow(tr("Recent files:"), m_recentCountSpin);
    m_checkUpdatesCheck = new QCheckBox(tr("Look for a newer version once a day"), page);
    m_checkUpdatesCheck->setToolTip(tr("Once a day, quietly: a newer version shows in the status bar. Help > Check for updates asks at "
                                       "once."));
    form->addRow(QString(), m_checkUpdatesCheck);
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
            form->addRow(note(tr("Overridden now by the environment variable QT_FFMPEG_DECODING_HW_DEVICE_TYPES."), page));
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
    QPushButton *browseFfmpeg = new QPushButton(Icons::named("folder-open"), tr("Browse..."), page);
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
    QPushButton *browseFolder = new QPushButton(Icons::named("folder-open"), tr("Browse..."), page);
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

    m_engineCombo = new QComboBox(page);
    if (Libav::available())
        {
            m_engineCombo->addItem(tr("Built in (%1)").arg(Libav::version().section(' ', 0, 1)), true);
        }
    m_engineCombo->addItem(tr("ffmpeg program"), false);
    m_engineCombo->setEnabled(m_engineCombo->count() > 1);

    QFormLayout *form = new QFormLayout(page);
    form->addRow(tr("Video engine:"), m_engineCombo);
    form->addRow(note(Libav::available()
                      ? tr("The built-in engine needs no ffmpeg program. The program is used when chosen here, "
                           "or when the built-in engine has no encoder that works.")
                      : tr("This build has no engine of its own: it exports with the ffmpeg program."), page));
    form->addRow(tr("ffmpeg program:"), ffmpegRow);
    form->addRow(QString(), m_ffmpegStatus);
    form->addRow(tr("Save videos to:"), folderRow);
    m_allCoresCheck = new QCheckBox(tr("Draw on all processor cores"), page);
    form->addRow(QString(), m_allCoresCheck);
    form->addRow(note(tr("An export draws its frames on half of the cores unless chosen here: a little slower, "
                         "and the computer stays usable meanwhile."), page));
    form->addRow(note(tr("Encoder, quality and size are chosen in the export window."), page));
    return page;
}

QWidget *SettingsDialog::createMapsPage()
{
    QWidget *page = new QWidget(this);
    m_mapDownloadsCheck = new QCheckBox(tr("Download street maps for gauges that show one"), page);
    m_mapProviderCombo = new QComboBox(page);
    // services whose maps need a key of the user's own: their maps are offered once it is there
    QGridLayout *keys = new QGridLayout;
    keys->setColumnStretch(1, 1);
    const QVector<TileStore::KeyService> services = TileStore::keyServices();
    for (int i = 0; i < services.size(); ++i)
        {
            const TileStore::KeyService &service = services.at(i);
            QLineEdit *edit = new QLineEdit(page);
            edit->setClearButtonEnabled(true);
            QStringList maps;
            for (const TileStore::Provider &provider : TileStore::providers())
                {
                    if (provider.service == service.id)
                        {
                            maps << provider.name;
                        }
                }
            edit->setToolTip(maps.join('\n'));
            QLabel *signUp = new QLabel(QString("<a href=\"%1\">%2</a>").arg(service.signUp, tr("Get a key")), page);
            signUp->setOpenExternalLinks(true);
            keys->addWidget(new QLabel(service.name + ':', page), i, 0);
            keys->addWidget(edit, i, 1);
            keys->addWidget(signUp, i, 2);
            m_mapKeyEdits.insert(service.id, edit);
            connect(edit, &QLineEdit::textChanged, this, &SettingsDialog::fillMapChoice);
        }
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
    form->addRow(note(tr("Map pieces around a route are fetched once and kept here; then the gauges and exports work "
                         "offline. The map server sees which area the route is in."), page));
    form->addRow(tr("Map:"), m_mapProviderCombo);
    form->addRow(new QLabel(QString("<b>%1</b>").arg(tr("Keys")), page));
    form->addRow(note(tr("Some map services need a key of your own, most of them free. Their maps appear once it is "
                         "entered."), page));
    form->addRow(keys);
    form->addRow(tr("Tile server:"), m_mapUrlEdit);
    form->addRow(note(tr("An address with {z}, {x} and {y} in it."), page));
    form->addRow(tr("Its credit:"), m_mapAttributionEdit);
    m_mapClearButton = new QPushButton(Icons::named("edit-clear"), QString(), page);
    m_mapClearButton->setToolTip(tr("Deletes the kept map pieces and terrain heights; they are fetched again when needed."));
    connect(m_mapClearButton, &QPushButton::clicked, this, [this]()
    {
        TileStore::instance()->clearStored();
        TileStore::terrain()->clearStored();
        updateStoredMaps();
    });
    updateStoredMaps();
    form->addRow(m_mapCreditCheck);
    form->addRow(note(tr("Map makers ask to be named where their map is shown. Switch it off only if the video names them "
                         "elsewhere."), page));
    form->addRow(m_mapClearButton);
    return page;
}

QWidget *SettingsDialog::createBikePage()
{
    QWidget *page = new QWidget(this);
    m_bikeWeightBikeSpin = new QDoubleSpinBox(page);
    m_bikeWeightBikeSpin->setRange(3.0, 60.0);
    m_bikeWeightBikeSpin->setDecimals(1);
    m_bikeWeightBikeSpin->setSingleStep(0.5);
    m_bikeWeightBikeSpin->setSuffix(tr(" kg"));
    m_bikeWeightBikeSpin->setToolTip(tr("With everything on it: bottles, bags, tools. The same as on the Power page."));

    m_tireSizeEdit = new QLineEdit(page);
    m_tireSizeEdit->setPlaceholderText(tr("700x25, 29x2.25, 26x1.95, 40-622..."));
    m_tireSizeEdit->setToolTip(tr("As printed on the tire: sets the wheel size, and the tire type on the Power page"));
    m_wheelSpin = new QDoubleSpinBox(page);
    m_wheelSpin->setRange(0.0, 3500.0);
    m_wheelSpin->setDecimals(0);
    m_wheelSpin->setSingleStep(5.0);
    m_wheelSpin->setSuffix(tr(" mm"));
    m_wheelSpin->setSpecialValueText(tr("from the tire size"));
    m_wheelSpin->setToolTip(tr("Distance of one wheel turn. Rolled out once with the rider on, it is more exact than the tire "
                               "size."));
    m_wheelNote = note(QString(), page);
    const auto showWheel = [this]()
    {
        const double fromTire = Gears::circumferenceOf(m_tireSizeEdit->text());
        m_wheelNote->setText(fromTire > 0.0 ? tr("From the tire size: %1 mm").arg(qRound(fromTire))
                             : m_tireSizeEdit->text().trimmed().isEmpty()
                             ? tr("Without a tire size: %1 mm, a usual road wheel").arg(qRound(Drivetrain().circumferenceMm()))
                             : tr("This tire size cannot be read; a usual road wheel is taken"));
    };
    connect(m_tireSizeEdit, &QLineEdit::textChanged, this, showWheel);

    // teeth typed, or one of the usual sets picked from the button's menu
    const auto teeth = [this, page](QLineEdit **edit, const QString &placeholder, const QVector<Gears::Preset> &presets)
    {
        *edit = new QLineEdit(page);
        (*edit)->setPlaceholderText(placeholder);
        QToolButton *usual = new QToolButton(page);
        usual->setText(tr("Usual"));
        usual->setPopupMode(QToolButton::InstantPopup);
        QMenu *menu = new QMenu(usual);
        QLineEdit *target = *edit;
        for (const Gears::Preset &preset : presets)
            {
                const QString text = Gears::teethText(preset.teeth);
                menu->addAction(preset.name, target, [target, text]() { target->setText(text); });
            }
        usual->setMenu(menu);
        QHBoxLayout *row = new QHBoxLayout;
        row->addWidget(*edit, 1);
        row->addWidget(usual);
        return row;
    };
    QHBoxLayout *chainrings = teeth(&m_chainringsEdit, tr("teeth of each ring: 34 or 50, 34"), Gears::chainringPresets());
    QHBoxLayout *cassette = teeth(&m_cassetteEdit, tr("teeth of each sprocket: 11, 13, 15, ..."), Gears::cassettePresets());

    QFormLayout *form = new QFormLayout(page);
    form->addRow(tr("Bike's weight:"), m_bikeWeightBikeSpin);
    form->addRow(tr("Tire size:"), m_tireSizeEdit);
    form->addRow(tr("Wheel circumference:"), m_wheelSpin);
    form->addRow(QString(), m_wheelNote);
    form->addRow(tr("Chainrings:"), chainrings);
    form->addRow(tr("Sprockets:"), cassette);
    form->addRow(note(tr("With your chainrings and sprockets, the gear is estimated from speed and cadence. Electronic "
                         "shifting's recorded gears are used as they are."), page));
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
    m_bikeWeightSpin->setToolTip(tr("With everything on it: bottles, bags, tools. The same as on the Bike page."));
    // the tire size again: it picks the kind of tires here
    m_tireSizePowerEdit = new QLineEdit(page);
    m_tireSizePowerEdit->setPlaceholderText(m_tireSizeEdit->placeholderText());
    m_tireSizePowerEdit->setToolTip(tr("Picks road, gravel or mountain bike tires by width. The same as on the Bike page."));
    for (auto [from, to] : {std::pair{m_tireSizeEdit, m_tireSizePowerEdit}, std::pair{m_tireSizePowerEdit, m_tireSizeEdit}})
        {
            connect(from, &QLineEdit::textChanged, to, [to](const QString &text)
            {
                if (to->text() != text)
                    {
                        const QSignalBlocker blocker(to);
                        to->setText(text);
                    }
            });
        }
    // one weight, shown on both pages
    for (auto [from, to] : {std::pair{m_bikeWeightSpin, m_bikeWeightBikeSpin}, std::pair{m_bikeWeightBikeSpin, m_bikeWeightSpin}})
        {
            connect(from, &QDoubleSpinBox::valueChanged, to, [to](double value)
            {
                const QSignalBlocker blocker(to);
                to->setValue(value);
            });
        }

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
    m_crrSpin = spin(0.001, 0.05, 4, 0.0005, QString());
    connect(m_tiresCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePowerFields);
    connect(m_crrSpin, &QDoubleSpinBox::valueChanged, this, [this](double value)
    {
        if (m_crrSpin->isEnabled())
            {
                m_ownCrr = value;
            }
    });
    const auto pickKind = [this](const QString &size)
    {
        // the kind by the size, as fast or as slow as it was
        const QString kind = PowerEstimate::tireKind(size);
        const QString now = m_tiresCombo->currentData().toString();
        if (!kind.isEmpty())
            {
                const QString level = now.contains('-') ? now.section('-', 1) : QString("typical");
                select(m_tiresCombo, kind + "-" + level);
            }
    };
    // typed on either page; not when the settings are filled in, which keeps the kind chosen
    connect(m_tireSizePowerEdit, &QLineEdit::textEdited, this, pickKind);
    connect(m_tireSizeEdit, &QLineEdit::textEdited, this, pickKind);

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
    m_lossSpin->setToolTip(tr("Power lost in the chain and bearings: 2-3 % when clean, more when worn or dirty."));

    QFormLayout *form = new QFormLayout(page);
    form->addRow(m_powerCheck);
    form->addRow(tr("Rider's weight:"), m_riderWeightSpin);
    form->addRow(tr("Rider's height:"), m_riderHeightSpin);
    form->addRow(tr("Bike's weight:"), m_bikeWeightSpin);
    form->addRow(tr("Tire size:"), m_tireSizePowerEdit);
    form->addRow(tr("Tires:"), m_tiresCombo);
    form->addRow(tr("Rolling resistance:"), m_crrSpin);
    form->addRow(tr("Riding position:"), m_positionCombo);
    form->addRow(tr("Drag area (CdA):"), m_cdaSpin);
    form->addRow(tr("Drivetrain loss:"), m_lossSpin);
    form->addRow(note(tr("Estimated from speed, grade and elevation. A recorded power is kept; the estimate shows beside "
                         "it as \"Power (est.)\". Without the day's weather (Weather page) a headwind reads low and a "
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

    QPushButton *add = new QPushButton(Icons::named("list-add"), tr("Add"), page);
    connect(add, &QPushButton::clicked, this, [this]()
    {
        const int row = m_surfaceTable->rowCount();
        m_surfaceTable->insertRow(row);
        m_surfaceTable->setItem(row, 0, new QTableWidgetItem(tr("New surface")));
        m_surfaceTable->setItem(row, 1, new QTableWidgetItem(QLocale().toString(1.0, 'f', 2)));
        m_surfaceTable->setCurrentCell(row, 0);
        m_surfaceTable->editItem(m_surfaceTable->item(row, 0));
    });
    QPushButton *remove = new QPushButton(Icons::named("list-remove"), tr("Remove"), page);
    connect(remove, &QPushButton::clicked, this, [this]()
    {
        if (m_surfaceTable->currentRow() >= 0)
            {
                m_surfaceTable->removeRow(m_surfaceTable->currentRow());
            }
    });
    QPushButton *usual = new QPushButton(Icons::named("view-refresh"), tr("Restore the usual ones"), page);
    connect(usual, &QPushButton::clicked, this, [this]() { showSurfaces(Surfaces::usual()); });
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(usual);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addWidget(m_surfaceTable, 1);
    layout->addLayout(buttons);
    layout->addWidget(note(tr("Surfaces a stretch of a ride can be marked as (Laps panel, \"Mark surface\"). The number is how "
                              "much harder tires roll there than on asphalt. Stretches already marked keep their number."),
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
    m_weatherCheck = new QCheckBox(tr("Fetch each ride's wind and air from weather history"), page);
    m_weatherCheck->setToolTip(tr("Wind, temperature and pressure along the ride, hour by hour. The service is sent those places "
                                  "and the date."));
    m_weatherSpacingSpin = spin(1.0, 100.0, 0, 1.0, tr(" km"));
    m_weatherSpacingSpin->setToolTip(tr("The weather services know places 10-25 km apart: closer than that brings little"));
    m_windShareSpin = spin(0.0, 100.0, 0, 5.0, tr(" %"));
    m_windShareSpin->setToolTip(tr("Measured 10 m up; near the ground about 60 % is left in the open, less among houses and trees."));
    connect(m_weatherCheck, &QCheckBox::toggled, m_weatherSpacingSpin, &QWidget::setEnabled);
    connect(m_weatherCheck, &QCheckBox::toggled, m_windShareSpin, &QWidget::setEnabled);

    m_weatherProviderCombo = new QComboBox(page);
    for (const WeatherStore::Provider &provider : WeatherStore::providers())
        {
            m_weatherProviderCombo->addItem(provider.needsKey ? tr("%1 (with a key of your own)").arg(provider.name)
                                            : tr("%1 (no key needed)").arg(provider.name), provider.id);
        }
    m_weatherKeyEdit = new QLineEdit(page);
    m_weatherKeyEdit->setPlaceholderText(tr("from the service's website, after signing up"));
    m_weatherCredit = note(QString(), page);
    const auto showProvider = [this]()
    {
        const WeatherStore::Provider provider = WeatherStore::provider(m_weatherProviderCombo->currentData().toString());
        const QSignalBlocker blocker(m_weatherKeyEdit);
        m_weatherKeyEdit->setText(m_weatherKeys.value(provider.id));
        m_weatherKeyEdit->setEnabled(provider.needsKey && m_weatherCheck->isChecked());
        m_weatherCredit->setText(provider.attribution);
    };
    connect(m_weatherProviderCombo, &QComboBox::currentIndexChanged, this, showProvider);
    connect(m_weatherCheck, &QCheckBox::toggled, this, showProvider);
    connect(m_weatherCheck, &QCheckBox::toggled, m_weatherProviderCombo, &QWidget::setEnabled);
    connect(m_weatherKeyEdit, &QLineEdit::textEdited, this, [this](const QString &key)
    {
        m_weatherKeys.insert(m_weatherProviderCombo->currentData().toString(), key.trimmed());
    });

    QFormLayout *form = new QFormLayout(page);
    form->addRow(m_weatherCheck);
    form->addRow(tr("Service:"), m_weatherProviderCombo);
    form->addRow(tr("Key:"), m_weatherKeyEdit);
    form->addRow(tr("A place every:"), m_weatherSpacingSpin);
    form->addRow(tr("Wind reaching the rider:"), m_windShareSpin);
    form->addRow(note(tr("Wind, headwind and air density then become measurements like any other: for gauges, the data "
                         "viewer and the power estimate. Kept on disk once fetched."), page));
    m_weatherClearButton = new QPushButton(Icons::named("edit-clear"), QString(), page);
    m_weatherClearButton->setToolTip(tr("Deletes the kept weather of rides; it is fetched again when needed."));
    connect(m_weatherClearButton, &QPushButton::clicked, this, [this]()
    {
        WeatherStore::instance()->clearStored();
        updateStoredWeather();
    });
    updateStoredWeather();
    form->addRow(m_weatherClearButton);
    form->addRow(m_weatherCredit);
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
                        : PowerEstimate::cdaOf(position,
                                               m_imperialBody ? m_riderWeightSpin->value() / kPoundsPerKg : m_riderWeightSpin->value(),
                                               m_imperialBody ? m_riderHeightSpin->value() * kCmPerInch : m_riderHeightSpin->value()));
    m_cdaSpin->setEnabled(ownCda);
}

void SettingsDialog::updateStoredWeather()
{
    const qint64 bytes = WeatherStore::instance()->storedBytes();
    m_weatherClearButton->setText(tr("Delete the stored weather (%1)").arg(QLocale().formattedDataSize(bytes)));
    m_weatherClearButton->setEnabled(bytes > 0);
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
}

void SettingsDialog::fillMapChoice()
{
    const QString chosen = m_mapProviderCombo->count() > 0 ? m_mapProviderCombo->currentData().toString()
                           : AppSettings::mapProvider();
    QHash<QString, QString> keys;
    for (auto it = m_mapKeyEdits.cbegin(); it != m_mapKeyEdits.cend(); ++it)
        {
            keys.insert(it.key(), it.value()->text());
        }
    const QSignalBlocker blocker(m_mapProviderCombo);
    m_mapProviderCombo->clear();
    for (const TileStore::Provider &provider : TileStore::usableProviders(keys))
        {
            m_mapProviderCombo->addItem(provider.name, provider.id);
        }
    m_mapProviderCombo->addItem(tr("Own server"), QString(AppSettings::OwnMapServer));
    const int index = m_mapProviderCombo->findData(chosen);
    m_mapProviderCombo->setCurrentIndex(std::max(0, index));
    updateMapFields();
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
    m_recentCountSpin->setValue(AppSettings::recentFileCount());
    m_checkUpdatesCheck->setChecked(AppSettings::checkForUpdates());

    m_hardwareDecodingCheck->setChecked(AppSettings::hardwareDecoding());
    select(m_decoderCombo, AppSettings::preferredDecoder());
    m_decoderCombo->setEnabled(m_hardwareDecodingCheck->isChecked());

    select(m_engineCombo, AppSettings::ownVideoEngine() && Libav::available());
    m_ffmpegEdit->setText(QDir::toNativeSeparators(AppSettings::ffmpegPath()));
    m_exportFolderEdit->setText(QDir::toNativeSeparators(AppSettings::exportFolder()));
    m_allCoresCheck->setChecked(AppSettings::exportAllCores());
    checkFfmpeg();

    m_mapDownloadsCheck->setChecked(AppSettings::mapDownloads());
    for (auto it = m_mapKeyEdits.cbegin(); it != m_mapKeyEdits.cend(); ++it)
        {
            const QSignalBlocker blocker(it.value());
            it.value()->setText(AppSettings::mapApiKey(it.key()));
        }
    m_mapProviderCombo->clear();
    fillMapChoice();
    m_mapUrlEdit->setText(AppSettings::mapTileUrl());
    m_mapAttributionEdit->setText(AppSettings::mapAttribution());
    m_mapCreditCheck->setChecked(AppSettings::mapCreditShown());
    updateMapFields();

    const Drivetrain drivetrain = AppSettings::drivetrain();
    m_tireSizeEdit->setText(drivetrain.tireSize);
    m_wheelSpin->setValue(drivetrain.wheelMm);
    m_chainringsEdit->setText(Gears::teethText(drivetrain.chainrings));
    m_cassetteEdit->setText(Gears::teethText(drivetrain.cassette));

    const PowerEstimate::Rider rider = AppSettings::powerRider();
    m_ownCrr = rider.crr;
    m_ownCda = rider.cda;
    m_powerCheck->setChecked(rider.enabled);
    // kept in kilograms and centimeters whatever is shown
    m_imperialBody = AppSettings::units().length == Units::Imperial;
    if (m_imperialBody)
        {
            m_riderWeightSpin->setRange(66.0, 440.0);
            m_riderWeightSpin->setSuffix(tr(" lb"));
            m_riderWeightSpin->setSingleStep(1.0);
            m_bikeWeightSpin->setRange(6.0, 132.0);
            m_bikeWeightSpin->setSuffix(tr(" lb"));
            m_bikeWeightSpin->setSingleStep(1.0);
            m_riderHeightSpin->setRange(39.0, 91.0);
            m_riderHeightSpin->setDecimals(1);
            m_riderHeightSpin->setSingleStep(0.5);
            m_riderHeightSpin->setSuffix(tr(" in"));
        }
    {
        // not while they are filled: the drag area is worked out from them once all are in
        const QSignalBlocker weight(m_riderWeightSpin);
        const QSignalBlocker height(m_riderHeightSpin);
        m_riderWeightSpin->setValue(m_imperialBody ? rider.riderKg * kPoundsPerKg : rider.riderKg);
        m_riderHeightSpin->setValue(m_imperialBody ? rider.heightCm / kCmPerInch : rider.heightCm);
    }
    m_bikeWeightSpin->setValue(m_imperialBody ? rider.bikeKg * kPoundsPerKg : rider.bikeKg);
    // the Bike page's copy, in the same units
    {
        const QSignalBlocker blocker(m_bikeWeightBikeSpin);
        m_bikeWeightBikeSpin->setRange(m_bikeWeightSpin->minimum(), m_bikeWeightSpin->maximum());
        m_bikeWeightBikeSpin->setSingleStep(m_bikeWeightSpin->singleStep());
        m_bikeWeightBikeSpin->setSuffix(m_bikeWeightSpin->suffix());
        m_bikeWeightBikeSpin->setValue(m_bikeWeightSpin->value());
    }
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
    m_weatherProviderCombo->setEnabled(weather.enabled);
    for (const WeatherStore::Provider &provider : WeatherStore::providers())
        {
            m_weatherKeys.insert(provider.id, AppSettings::weatherApiKey(provider.id));
        }
    select(m_weatherProviderCombo, weather.provider);
    // the key and the credit of the service shown, also when it is the first and so did not change
    m_weatherKeyEdit->setText(m_weatherKeys.value(weather.provider));
    m_weatherKeyEdit->setEnabled(weather.enabled && WeatherStore::provider(weather.provider).needsKey);
    m_weatherCredit->setText(WeatherStore::attribution(weather.provider));
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
    AppSettings::setRecentFileCount(m_recentCountSpin->value());
    AppSettings::setCheckForUpdates(m_checkUpdatesCheck->isChecked());
    AppSettings::setHardwareDecoding(hardwareDecoding);
    AppSettings::setPreferredDecoder(decoder);

    Units units;
    units.speed = Units::Speed(m_speedCombo->currentData().toInt());
    units.length = Units::Length(m_lengthCombo->currentData().toInt());
    units.temperature = Units::Temperature(m_temperatureCombo->currentData().toInt());
    AppSettings::setUnits(units);

    // a build without the engine leaves the choice for one that has it
    if (Libav::available())
        {
            AppSettings::setOwnVideoEngine(m_engineCombo->currentData().toBool());
        }
    AppSettings::setFfmpegPath(QDir::fromNativeSeparators(m_ffmpegEdit->text().trimmed()));
    AppSettings::setExportFolder(QDir::fromNativeSeparators(m_exportFolderEdit->text().trimmed()));
    AppSettings::setExportAllCores(m_allCoresCheck->isChecked());

    AppSettings::setMapDownloads(m_mapDownloadsCheck->isChecked());
    AppSettings::setMapProvider(m_mapProviderCombo->currentData().toString());
    for (auto it = m_mapKeyEdits.cbegin(); it != m_mapKeyEdits.cend(); ++it)
        {
            AppSettings::setMapApiKey(it.key(), it.value()->text());
        }
    AppSettings::setMapCreditShown(m_mapCreditCheck->isChecked());
    AppSettings::setMapTileUrl(m_mapUrlEdit->text().trimmed());
    AppSettings::setMapAttribution(m_mapAttributionEdit->text().trimmed());

    PowerEstimate::Rider rider;
    rider.enabled = m_powerCheck->isChecked();
    rider.riderKg = m_imperialBody ? m_riderWeightSpin->value() / kPoundsPerKg : m_riderWeightSpin->value();
    rider.heightCm = m_imperialBody ? m_riderHeightSpin->value() * kCmPerInch : m_riderHeightSpin->value();
    rider.bikeKg = m_imperialBody ? m_bikeWeightSpin->value() / kPoundsPerKg : m_bikeWeightSpin->value();
    rider.tires = m_tiresCombo->currentData().toString();
    rider.crr = m_ownCrr;
    rider.position = m_positionCombo->currentData().toString();
    rider.cda = m_ownCda;
    rider.drivetrainLoss = m_lossSpin->value();
    AppSettings::setPowerRider(rider);

    Drivetrain drivetrain;
    drivetrain.tireSize = m_tireSizeEdit->text().trimmed();
    drivetrain.wheelMm = m_wheelSpin->value();
    drivetrain.chainrings = Gears::parseTeeth(m_chainringsEdit->text());
    drivetrain.cassette = Gears::parseTeeth(m_cassetteEdit->text());
    AppSettings::setDrivetrain(drivetrain);

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
    weather.provider = m_weatherProviderCombo->currentData().toString();
    weather.apiKey = m_weatherKeys.value(weather.provider);
    for (auto it = m_weatherKeys.cbegin(); it != m_weatherKeys.cend(); ++it)
        {
            AppSettings::setWeatherApiKey(it.key(), it.value());
        }
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
