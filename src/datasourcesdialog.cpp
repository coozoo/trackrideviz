#include "datasourcesdialog.h"

#include "icons.h"
#include "telemetry/telemetryimporter.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

DataSourcesDialog::DataSourcesDialog(const QString &mainPath, const TelemetrySessionPtr &main,
                                     const QVector<DataSource> &sources, const Units &units, QWidget *parent)
    : QDialog(parent)
    , m_mainPath(mainPath)
    , m_main(main)
    , m_sources(sources)
    , m_units(units)
{
    setWindowTitle(tr("Data sources"));

    QLabel *mainLabel = new QLabel(tr("Main data: %1").arg(QFileInfo(mainPath).fileName()), this);
    QLabel *about = new QLabel(tr("Channels of other files are added to it, such as the heart rate of a chest strap "
                                  "or the power of a meter recorded on another device. A channel the main data "
                                  "has too is taken from the added file only when ticked."), this);
    about->setWordWrap(true);
    about->setEnabled(false); // grayed: information, not a control

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::currentRowChanged, this, &DataSourcesDialog::showSelected);
    QPushButton *addButton = new QPushButton(Icons::named("list-add"), tr("Add..."), this);
    connect(addButton, &QPushButton::clicked, this, &DataSourcesDialog::addFiles);
    m_removeButton = new QPushButton(Icons::named("list-remove"), tr("Remove"), this);
    connect(m_removeButton, &QPushButton::clicked, this, &DataSourcesDialog::removeSelected);
    QVBoxLayout *side = new QVBoxLayout;
    side->addWidget(addButton);
    side->addWidget(m_removeButton);
    side->addStretch(1);
    QHBoxLayout *listRow = new QHBoxLayout;
    listRow->addWidget(m_list, 1);
    listRow->addLayout(side);

    m_details = new QWidget(this);
    m_shiftSpin = new QDoubleSpinBox(m_details);
    m_shiftSpin->setRange(-7 * 86400.0, 7 * 86400.0);
    m_shiftSpin->setDecimals(1);
    m_shiftSpin->setSingleStep(0.5);
    m_shiftSpin->setSuffix(tr(" s"));
    m_shiftSpin->setKeyboardTracking(false);
    m_shiftSpin->setToolTip(tr("Seconds added to the times of this file to put them on the main data's: "
                               "positive when it started recording after the main data"));
    connect(m_shiftSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DataSourcesDialog::onShiftEdited);
    m_clockButton = new QPushButton(tr("From the clocks"), m_details);
    m_clockButton->setToolTip(tr("Line the file up with the main data by the time of day both recorded"));
    connect(m_clockButton, &QPushButton::clicked, this, &DataSourcesDialog::shiftFromClocks);
    QHBoxLayout *shiftRow = new QHBoxLayout;
    shiftRow->addWidget(m_shiftSpin);
    shiftRow->addWidget(m_clockButton);
    shiftRow->addStretch(1);
    m_clockNote = new QLabel(m_details);
    m_clockNote->setWordWrap(true);
    m_clockNote->setEnabled(false);
    m_channels = new QListWidget(m_details);
    m_channels->setToolTip(tr("The channels taken from this file"));
    connect(m_channels, &QListWidget::itemChanged, this, &DataSourcesDialog::onChannelChanged);
    QFormLayout *form = new QFormLayout(m_details);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Time shift:"), shiftRow);
    form->addRow(QString(), m_clockNote);
    form->addRow(tr("Channels:"), m_channels);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(mainLabel);
    layout->addWidget(about);
    layout->addLayout(listRow, 1);
    layout->addWidget(m_details, 2);
    layout->addWidget(buttons);
    resize(520, 560);

    fillList();
    m_list->setCurrentRow(m_sources.isEmpty() ? -1 : 0);
    showSelected();
}

TelemetrySessionPtr DataSourcesDialog::sessionOf(const QString &path)
{
    if (!m_loaded.contains(path))
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QString error;
            m_loaded.insert(path, TelemetryImporter::loadRecorded(path, &error));
            m_errors.insert(path, error);
            QApplication::restoreOverrideCursor();
        }
    return m_loaded.value(path);
}

void DataSourcesDialog::fillList()
{
    const int row = m_list->currentRow();
    m_list->clear();
    for (const DataSource &source : std::as_const(m_sources))
        {
            QListWidgetItem *item = new QListWidgetItem(QFileInfo(source.path).fileName(), m_list);
            item->setToolTip(QDir::toNativeSeparators(source.path));
            if (!QFileInfo::exists(source.path))
                {
                    item->setText(item->text() + "  " + tr("(missing)"));
                }
        }
    m_list->setCurrentRow(std::min(row, int(m_list->count()) - 1));
}

int DataSourcesDialog::selectedRow() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_sources.size() ? row : -1;
}

void DataSourcesDialog::addFiles()
{
    QSettings settings;
    const QString startDir = settings.value("paths/telemetry", QFileInfo(m_mainPath).absolutePath()).toString();
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add data"), startDir,
                                                            TelemetryImporter::fileDialogFilter());
    if (paths.isEmpty())
        {
            return;
        }
    settings.setValue("paths/telemetry", QFileInfo(paths.first()).absolutePath());
    QStringList problems;
    for (const QString &path : paths)
        {
            const bool present = std::any_of(m_sources.cbegin(), m_sources.cend(), [&](const DataSource &source)
            {
                return QFileInfo(source.path) == QFileInfo(path);
            });
            if (present || QFileInfo(path) == QFileInfo(m_mainPath))
                {
                    continue;
                }
            const TelemetrySessionPtr session = sessionOf(path);
            if (!session)
                {
                    problems << QString("%1: %2").arg(QFileInfo(path).fileName(), m_errors.value(path));
                    continue;
                }
            DataSource source;
            source.path = path;
            // what the main data lacks; a file adding nothing new offers all it has
            source.channels = m_main ? DataSources::missingChannels(*m_main, *session) : session->channelIds();
            if (source.channels.isEmpty())
                {
                    source.channels = session->channelIds();
                }
            if (m_main)
                {
                    DataSources::clockShift(*m_main, *session, &source.shift);
                }
            m_sources.append(source);
        }
    fillList();
    m_list->setCurrentRow(int(m_sources.size()) - 1);
    if (!problems.isEmpty())
        {
            QMessageBox::warning(this, tr("Add data"), problems.join('\n'));
        }
}

void DataSourcesDialog::removeSelected()
{
    const int row = selectedRow();
    if (row < 0)
        {
            return;
        }
    m_sources.remove(row);
    fillList();
    showSelected();
}

void DataSourcesDialog::showSelected()
{
    const int row = selectedRow();
    m_removeButton->setEnabled(row >= 0);
    m_details->setEnabled(row >= 0);
    m_showing = true;
    m_channels->clear();
    if (row < 0)
        {
            m_shiftSpin->setValue(0.0);
            m_clockNote->clear();
            m_showing = false;
            return;
        }
    const DataSource &source = m_sources.at(row);
    m_shiftSpin->setValue(source.shift);
    const TelemetrySessionPtr session = sessionOf(source.path);
    double clock = 0.0;
    const bool clocks = session && m_main && DataSources::clockShift(*m_main, *session, &clock);
    m_clockButton->setEnabled(clocks);
    if (!session)
        {
            m_clockNote->setText(m_errors.value(source.path));
        }
    else if (!clocks)
        {
            m_clockNote->setText(tr("One of the files has no time of day: set the shift by hand, comparing a gauge "
                                    "of this file's with the video."));
        }
    else if (std::abs(source.shift - clock) < 0.05)
        {
            m_clockNote->setText(tr("Lined up by the clocks."));
        }
    else
        {
            m_clockNote->setText(tr("%1 s from what the clocks say.").arg(QLocale().toString(source.shift - clock, 'f', 1)));
        }
    // those of the file, and any chosen before that it no longer has
    QStringList ids = session ? session->channelIds() : QStringList();
    for (const QString &id : source.channels)
        {
            if (!ids.contains(id))
                {
                    ids << id;
                }
        }
    for (const QString &id : std::as_const(ids))
        {
            const Channel *channel = session ? session->channel(id) : nullptr;
            const ChannelDisplay display = ChannelFormat::displayFor(id, channel ? channel->unit() : QString(), m_units);
            QString text = display.name;
            if (m_main && m_main->hasChannel(id))
                {
                    text += "  " + tr("(in place of the main data's)");
                }
            QListWidgetItem *item = new QListWidgetItem(text, m_channels);
            item->setData(Qt::UserRole, id);
            item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            item->setCheckState(source.channels.contains(id) ? Qt::Checked : Qt::Unchecked);
            if (channel)
                {
                    item->setToolTip(tr("%1: %n samples", nullptr, channel->count()).arg(id));
                }
        }
    m_showing = false;
}

void DataSourcesDialog::onShiftEdited()
{
    const int row = selectedRow();
    if (m_showing || row < 0)
        {
            return;
        }
    m_sources[row].shift = m_shiftSpin->value();
    m_sources[row].shiftIsManual = true;
    showSelected();
}

void DataSourcesDialog::shiftFromClocks()
{
    const int row = selectedRow();
    const TelemetrySessionPtr session = row >= 0 ? sessionOf(m_sources.at(row).path) : nullptr;
    double shift = 0.0;
    if (!session || !m_main || !DataSources::clockShift(*m_main, *session, &shift))
        {
            return;
        }
    m_sources[row].shift = shift;
    m_sources[row].shiftIsManual = false;
    showSelected();
}

void DataSourcesDialog::onChannelChanged()
{
    const int row = selectedRow();
    if (m_showing || row < 0)
        {
            return;
        }
    QStringList channels;
    for (int i = 0; i < m_channels->count(); ++i)
        {
            if (m_channels->item(i)->checkState() == Qt::Checked)
                {
                    channels << m_channels->item(i)->data(Qt::UserRole).toString();
                }
        }
    m_sources[row].channels = channels;
}
