#include "gaugelibrarypanel.h"

#include "gaugeeditor.h"

#include "gauge/gaugelibrary.h"
#include "gauge/gaugerenderer.h"
#include "gauge/xmlgaugeimporter.h"
#include "overlay/xmltemplateimporter.h"
#include "telemetry/csv/dataprofilelibrary.h"
#include "telemetry/csv/xmldataprofileimporter.h"
#include "overlay/librarypackage.h"
#include "overlay/templatelibrary.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QTabBar>
#include <QVBoxLayout>
#include <QtConcurrent>

struct GaugeLibraryPanel::ImportFolders
{
    QString widgets;
    QStringList gaugeFiles;
    QStringList templateFiles;
    QStringList xmlGauges;
    QStringList xmlTemplates;
    QStringList dataProfiles;
    QStringList packages;      // .rtvgauge, .rtvtemplate
};

namespace
{
// A small padlock in the top right corner: this one comes with the program.
QImage withLock(QImage image)
{
    if (image.isNull())
        {
            return image;
        }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double size = std::max(14.0, image.height() / 6.0);
    const QRectF badge(image.width() - size - 3, 3, size, size);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(badge, size * 0.25, size * 0.25);
    // the shackle and the body
    const double u = size / 14.0;
    QPen shackle(QColor(235, 235, 235), 1.6 * u);
    painter.setPen(shackle);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(badge.left() + 4.5 * u, badge.top() + 2.5 * u, 5 * u, 6 * u), 0, 180 * 16);
    painter.drawLine(QPointF(badge.left() + 4.5 * u, badge.top() + 5.5 * u), QPointF(badge.left() + 4.5 * u, badge.top() + 7 * u));
    painter.drawLine(QPointF(badge.left() + 9.5 * u, badge.top() + 5.5 * u), QPointF(badge.left() + 9.5 * u, badge.top() + 7 * u));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(235, 235, 235));
    painter.drawRoundedRect(QRectF(badge.left() + 3 * u, badge.top() + 6.5 * u, 8 * u, 5.5 * u), u, u);
    return image;
}

const int kGaugeThumbnail = 96;
const QSize kTemplateThumbnail(160, 90);
const QColor kThumbnailBackground(60, 70, 85);

enum Tab { GaugesTab, TemplatesTab };

struct Thumbnail
{
    QString id;
    QImage image;
    QStringList groups{};   // gauges: what they show (see groupsOf)
};

// item data: the id, whether it comes with the program, the groups it is in
const int kIdRole = Qt::UserRole;
const int kBuiltinRole = Qt::UserRole + 1;
const int kGroupsRole = Qt::UserRole + 2;

// Groups every entry is in one of: where it comes from.
const char kBuiltinGroup[] = "@builtin";
const char kUserGroup[] = "@user";

// What gauges show, coarsely enough for a short list to choose from.
struct GaugeGroup
{
    const char *key;
    const char *name;
};
const GaugeGroup kGaugeGroups[] =
{
    {"map", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Maps")},
    {"speed", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Speed")},
    {"height", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Elevation and grade")},
    {"distance", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Distance and time")},
    {"heart", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Heart rate")},
    {"cadence", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Cadence")},
    {"power", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Power and pedaling")},
    {"laps", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Laps and sectors")},
    {"g", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "G-force")},
    {"heading", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Heading")},
    {"engine", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Engine and vehicle")},
    {"other", QT_TRANSLATE_NOOP("GaugeLibraryPanel", "Other")},
};

QString groupOfChannel(const QString &channel)
{
    const QString c = channel.toLower();
    auto has = [&c](std::initializer_list<const char *> words)
    {
        for (const char *word : words)
            {
                if (c.contains(QLatin1String(word)))
                    {
                        return true;
                    }
            }
        return false;
    };
    // the order matters: "lapTime" is a lap's, "verticalSpeed" a height's, "powerPhase" the power's
    if (has({"lap", "sector"}))
        {
            return "laps";
        }
    if (has({"gforce", "acceleration", "braking", "lean"}))
        {
            return "g";
        }
    if (has({"power", "torque", "pedal", "platform"}))
        {
            return "power";
        }
    if (has({"elevation", "altitude", "grade", "vertical", "ascent", "descent", "glide", "falling"}))
        {
            return "height";
        }
    if (has({"rpm", "engine", "throttle", "gear", "brake", "steering", "fuel", "manifold", "coolant", "oil", "lambda"}))
        {
            return "engine";
        }
    if (has({"speed", "pace"}))
        {
            return "speed";
        }
    if (has({"distance", "time", "date"}))
        {
            return "distance";
        }
    if (has({"heart"}))
        {
            return "heart";
        }
    if (has({"cadence"}))
        {
            return "cadence";
        }
    if (has({"heading", "compass", "course"}))
        {
            return "heading";
        }
    return "other";
}

bool hasRoute(const QVector<GaugeComponent> &components)
{
    for (const GaugeComponent &component : components)
        {
            if (component.type == GaugeComponent::TrackMap || component.type == GaugeComponent::LapMap
                    || hasRoute(component.children))
                {
                    return true;
                }
        }
    return false;
}

QStringList groupsOf(const Gauge &gauge)
{
    if (hasRoute(gauge.components))
        {
            return {"map"};
        }
    QStringList groups;
    for (const QString &channel : gauge.supportedTypes)
        {
            const QString group = groupOfChannel(channel);
            if (!groups.contains(group))
                {
                    groups << group;
                }
        }
    return groups.isEmpty() ? QStringList{"other"} : groups;
}

QVector<Thumbnail> renderThumbnails(const QStringList &ids, bool templates)
{
    // gauges are made for video; on a pale panel white numbers would vanish
    const TelemetrySessionPtr session = DemoSession::create();
    QVector<Thumbnail> thumbnails;
    for (const QString &id : ids)
        {
            if (templates)
                {
                    const GaugeTemplate t = TemplateLibrary::get(id);
                    QImage image(kTemplateThumbnail, QImage::Format_ARGB32_Premultiplied);
                    image.fill(kThumbnailBackground);
                    const QRectF frame(QPointF(0, 0), QSizeF(kTemplateThumbnail));
                    const Overlay overlay(t.overlayItems(frame.width() / frame.height()), session, Units(), t.theme);
                    QPainter painter(&image);
                    painter.setClipRect(frame);
                    overlay.render(painter, frame, DemoSession::ShowcaseTime);
                    thumbnails.append({id, image});
                    continue;
                }
            const Gauge gauge = GaugeLibrary::gauge(id);
            QImage image(kGaugeThumbnail, kGaugeThumbnail, QImage::Format_ARGB32_Premultiplied);
            image.fill(kThumbnailBackground);
            if (!gauge.isEmpty())
                {
                    // shown with what it was made for
                    const BoundGauge bound(gauge, GaugeLibrary::defaultChannel(gauge, session->channelIds()), session,
                                           Units(), GaugeTheme());
                    const QSizeF size = bound.size().scaled(kGaugeThumbnail - 4, kGaugeThumbnail - 4, Qt::KeepAspectRatio);
                    QPainter painter(&image);
                    bound.render(painter, QRectF((kGaugeThumbnail - size.width()) / 2, (kGaugeThumbnail - size.height()) / 2,
                                                 size.width(), size.height()), DemoSession::ShowcaseTime);
                }
            thumbnails.append({id, image, groupsOf(gauge)});
        }
    return thumbnails;
}

// What a chosen folder holds: a program folder with Widgets and Templates in it, or one of those;
// XML gauges and templates, each a folder (with gauge.xml, project.cdp), or a folder of
// them, or one with Gauges and Templates in it.
using ImportFolders = GaugeLibraryPanel::ImportFolders;


QString childIgnoringCase(const QDir &dir, const QString &name)
{
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (entry.compare(name, Qt::CaseInsensitive) == 0)
                {
                    return dir.filePath(entry);
                }
        }
    return QString();
}

QStringList filesIn(const QString &folder, const QString &pattern)
{
    QStringList files;
    if (folder.isEmpty())
        {
            return files;
        }
    const QDir dir(folder);
    for (const QString &name : dir.entryList({pattern}, QDir::Files, QDir::Name))
        {
            files << dir.filePath(name);
        }
    return files;
}

// XML data profiles: in the chosen folder, or in its DataProfiles.
QStringList dataProfilesIn(const QString &chosen)
{
    const QString child = childIgnoringCase(QDir(chosen), "DataProfiles");
    QStringList found;
    for (const QString &file : filesIn(child.isEmpty() ? chosen : child, "*.xml"))
        {
            if (XmlDataProfileImporter::isDataProfile(file))
                {
                    found << file;
                }
        }
    return found;
}

bool holds(const QString &folder, const QString &fileName)
{
    return !QDir(folder).entryList({fileName}, QDir::Files).isEmpty();
}

// Folders holding `fileName`: the chosen one, those in it, or those in its child `childName`.
QStringList foldersWith(const QString &chosen, const QString &fileName, const QString &childName)
{
    if (holds(chosen, fileName))
        {
            return {chosen};
        }
    QStringList folders;
    const QString child = childIgnoringCase(QDir(chosen), childName);
    const QDir dir(child.isEmpty() ? chosen : child);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        {
            if (holds(dir.filePath(name), fileName))
                {
                    folders << dir.filePath(name);
                }
        }
    return folders;
}

ImportFolders importFoldersIn(const QString &chosen)
{
    ImportFolders found;
    found.packages = filesIn(chosen, QString("*.%1").arg(LibraryPackage::GaugeSuffix))
                     + filesIn(chosen, QString("*.%1").arg(LibraryPackage::TemplateSuffix));
    found.dataProfiles = dataProfilesIn(chosen);
    found.xmlTemplates = foldersWith(chosen, XmlTemplateImporter::FileName, "Templates");
    // a template's own gauges come in with it
    found.xmlGauges = found.xmlTemplates.contains(chosen) ? QStringList()
                      : foldersWith(chosen, XmlGaugeImporter::FileName, "Gauges");
    const QDir dir(chosen);
    const QString widgets = childIgnoringCase(dir, "Widgets");
    const QString templates = childIgnoringCase(dir, "Templates");
    if (!widgets.isEmpty() || !templates.isEmpty())
        {
            // a folder holding both: everything
            found.widgets = widgets;
            found.gaugeFiles = filesIn(widgets, "*.veg");
            found.templateFiles = filesIn(templates, "*.vet");
            return found;
        }
    found.gaugeFiles = filesIn(chosen, "*.veg");
    found.templateFiles = filesIn(chosen, "*.vet");
    // templates alone: their gauges are next door, in Widgets
    found.widgets = found.gaugeFiles.isEmpty() ? childIgnoringCase(QDir(chosen + "/.."), "Widgets") : chosen;
    return found;
}

struct ImportReport
{
    GaugeLibrary::ImportSummary gauges;
    TemplateLibrary::ImportSummary templates;
    DataProfileLibrary::ImportSummary profiles;
};
}

GaugeLibraryPanel::GaugeLibraryPanel(QWidget *parent)
    : QWidget(parent)
{
    m_tabs = new QTabBar(this);
    m_tabs->addTab(tr("Gauges"));
    m_tabs->addTab(tr("Templates"));
    m_tabs->setExpanding(false);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabBar::currentChanged, this, &GaugeLibraryPanel::reload);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Find by name"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &GaugeLibraryPanel::applyFilter);
    m_groups = new QComboBox(this);
    connect(m_groups, &QComboBox::activated, this, &GaugeLibraryPanel::applyFilter);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &GaugeLibraryPanel::useSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &GaugeLibraryPanel::updateButtons);

    m_useButton = new QPushButton(this);
    connect(m_useButton, &QPushButton::clicked, this, &GaugeLibraryPanel::useSelected);
    m_importButton = new QPushButton(tr("Import..."), this);
    m_importButton->setToolTip(tr("Brings in .veg gauges, .vet templates, XML gauges and templates (folders with a "
                                  "gauge.xml or project.cdp) and XML data profiles. Choose the folder that holds them, "
                                  "or a folder with Widgets and Templates or with Gauges, Templates and DataProfiles in it."));
    QMenu *importMenu = new QMenu(m_importButton);
    importMenu->addAction(tr("Gauge or template files (.rtvgauge, .rtvtemplate)..."), this, &GaugeLibraryPanel::importPackages);
    importMenu->addAction(tr("A folder of gauges, templates or data profiles..."), this, &GaugeLibraryPanel::importFiles);
    m_importButton->setMenu(importMenu);
    m_exportButton = new QPushButton(tr("Export..."), this);
    connect(m_exportButton, &QPushButton::clicked, this, &GaugeLibraryPanel::exportSelected);
    m_removeButton = new QPushButton(tr("Remove"), this);
    connect(m_removeButton, &QPushButton::clicked, this, &GaugeLibraryPanel::removeSelected);

    m_newButton = new QPushButton(tr("New gauge..."), this);
    m_newButton->setToolTip(tr("Opens the gauge editor with an empty gauge"));
    connect(m_newButton, &QPushButton::clicked, this, [this]() { editGauge(true); });
    m_editButton = new QPushButton(tr("Edit..."), this);
    connect(m_editButton, &QPushButton::clicked, this, [this]() { editGauge(false); });

    // rows of two: one row would make the dock, and so the window, too wide for small screens
    QGridLayout *buttons = new QGridLayout;
    buttons->addWidget(m_useButton, 0, 0);
    buttons->addWidget(m_removeButton, 0, 1);
    buttons->addWidget(m_importButton, 1, 0);
    buttons->addWidget(m_exportButton, 1, 1);
    buttons->addWidget(m_newButton, 2, 0);
    buttons->addWidget(m_editButton, 2, 1);
    buttons->setColumnStretch(2, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    QHBoxLayout *filter = new QHBoxLayout;
    filter->addWidget(m_search, 1);
    filter->addWidget(m_groups);
    layout->addWidget(m_tabs);
    layout->addLayout(filter);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);

    reload();
}

bool GaugeLibraryPanel::showingTemplates() const
{
    return m_tabs->currentIndex() == TemplatesTab;
}

QString GaugeLibraryPanel::selectedId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item && item->isSelected() ? item->data(kIdRole).toString() : QString();
}

void GaugeLibraryPanel::updateButtons()
{
    const QListWidgetItem *item = m_list->currentItem();
    const bool selected = item && item->isSelected();
    m_useButton->setEnabled(selected);
    m_exportButton->setEnabled(selected);
    m_exportButton->setToolTip(showingTemplates() ? tr("Saves the selected template, with the gauges it uses, as one file to pass on")
                               : tr("Saves the selected gauge as one file to pass on"));
    // what came with the program is part of it
    const bool builtin = selected && item->data(kBuiltinRole).toBool();
    m_newButton->setVisible(!showingTemplates());
    m_editButton->setVisible(!showingTemplates());
    m_editButton->setEnabled(selected);
    m_editButton->setToolTip(builtin ? tr("Opens a copy of this gauge in the gauge editor: what comes with the program stays as it is")
                             : tr("Opens the selected gauge in the gauge editor"));
    m_removeButton->setEnabled(selected && !builtin);
    const bool templates = showingTemplates();
    m_removeButton->setToolTip(builtin ? (templates ? tr("This template comes with the program and cannot be deleted")
                                                    : tr("This gauge comes with the program and cannot be deleted"))
                               : templates ? tr("Deletes the selected template from the library")
                               : tr("Deletes the selected gauge from the library"));
}

void GaugeLibraryPanel::reload()
{
    const int generation = ++m_generation;
    const bool templates = showingTemplates();
    const QString keep = selectedId();
    m_list->clear();

    const QSize iconSize = templates ? kTemplateThumbnail : QSize(kGaugeThumbnail, kGaugeThumbnail);
    m_list->setIconSize(iconSize);
    m_list->setGridSize(iconSize + QSize(24, 40));
    m_useButton->setText(templates ? tr("Apply to video") : tr("Add to video"));
    m_useButton->setToolTip(templates ? tr("Replaces the gauges on the video with this template. A double click does the same.")
                            : tr("Puts the selected gauge on the video. A double click does the same."));

    // something of the right size until the picture is drawn, so the list does not jump
    QPixmap blank(iconSize);
    blank.fill(kThumbnailBackground);
    const QIcon placeholder(blank);
    QStringList ids;
    auto addItem = [&](const QString &id, const QString &name, bool builtin, const QString &tooltip, const QString &category)
    {
        QListWidgetItem *item = new QListWidgetItem(placeholder, name, m_list);
        item->setData(kIdRole, id);
        item->setData(kBuiltinRole, builtin);
        QStringList groups{builtin ? kBuiltinGroup : kUserGroup};
        if (!category.isEmpty())
            {
                groups << category;
            }
        item->setData(kGroupsRole, groups);
        item->setToolTip(builtin ? tr("%1\nComes with the program; it cannot be deleted.").arg(tooltip) : tooltip);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setSizeHint(m_list->gridSize());
        if (id == keep)
            {
                m_list->setCurrentItem(item);
            }
        ids << id;
    };
    if (templates)
        {
            for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
                {
                    addItem(entry.id, entry.name, entry.builtin,
                            entry.category.isEmpty() ? entry.name : QString("%1 (%2)").arg(entry.name, entry.category),
                            entry.category);
                }
        }
    else
        {
            for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
                {
                    addItem(entry.id, entry.name, entry.builtin, entry.name, QString());
                }
        }
    rebuildGroups();
    updateButtons();

    auto *watcher = new QFutureWatcher<QVector<Thumbnail>>(this);
    connect(watcher, &QFutureWatcher<QVector<Thumbnail>>::finished, this, [this, watcher, generation]()
    {
        watcher->deleteLater();
        if (generation != m_generation)
            {
                return; // the list was rebuilt meanwhile
            }
        QHash<QString, Thumbnail> drawn;
        for (const Thumbnail &thumbnail : watcher->result())
            {
                drawn.insert(thumbnail.id, thumbnail);
            }
        for (int row = 0; row < m_list->count(); ++row)
            {
                QListWidgetItem *item = m_list->item(row);
                const Thumbnail thumbnail = drawn.value(item->data(kIdRole).toString());
                item->setIcon(QIcon(QPixmap::fromImage(item->data(kBuiltinRole).toBool() ? withLock(thumbnail.image)
                                                       : thumbnail.image)));
                // what a gauge shows is known once it has been read
                item->setData(kGroupsRole, item->data(kGroupsRole).toStringList() + thumbnail.groups);
            }
        rebuildGroups();
    });
    watcher->setFuture(QtConcurrent::run(renderThumbnails, ids, templates));
}

void GaugeLibraryPanel::rebuildGroups()
{
    QSet<QString> present;
    for (int row = 0; row < m_list->count(); ++row)
        {
            const QStringList groups = m_list->item(row)->data(kGroupsRole).toStringList();
            present.unite(QSet<QString>(groups.begin(), groups.end()));
        }
    const QString chosen = m_groups->currentData().toString();
    m_groups->clear();
    m_groups->addItem(tr("All"), QString());
    m_groups->addItem(tr("Built-in"), QString(kBuiltinGroup));
    m_groups->addItem(tr("Imported"), QString(kUserGroup));
    m_groups->insertSeparator(m_groups->count());
    present.remove(kBuiltinGroup);
    present.remove(kUserGroup);
    if (showingTemplates())
        {
            // templates: their categories, as written in them
            QStringList categories(present.begin(), present.end());
            categories.sort(Qt::CaseInsensitive);
            for (const QString &category : std::as_const(categories))
                {
                    m_groups->addItem(category, category);
                }
        }
    else
        {
            for (const GaugeGroup &group : kGaugeGroups)
                {
                    if (present.contains(QLatin1String(group.key)))
                        {
                            m_groups->addItem(tr(group.name), QString(group.key));
                        }
                }
        }
    const int index = m_groups->findData(chosen);
    m_groups->setCurrentIndex(index >= 0 ? index : 0);
    applyFilter();
}

void GaugeLibraryPanel::applyFilter()
{
    const QString text = m_search->text().trimmed();
    const QString group = m_groups->currentData().toString();
    for (int row = 0; row < m_list->count(); ++row)
        {
            QListWidgetItem *item = m_list->item(row);
            const bool shown = (text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive))
                               && (group.isEmpty() || item->data(kGroupsRole).toStringList().contains(group));
            item->setHidden(!shown);
        }
    if (m_list->currentItem() && m_list->currentItem()->isHidden())
        {
            // nothing out of sight is added or removed
            m_list->setCurrentItem(nullptr);
        }
    updateButtons();
}

void GaugeLibraryPanel::useSelected()
{
    const QString id = selectedId();
    if (id.isEmpty())
        {
            return;
        }
    if (showingTemplates())
        {
            emit applyTemplateRequested(id);
        }
    else
        {
            emit addRequested(id);
        }
}

void GaugeLibraryPanel::editGauge(bool fresh)
{
    Gauge gauge;
    QStringList taken;
    QStringList reserved;
    const QString id = selectedId();
    for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
        {
            (entry.builtin ? reserved : taken) << entry.name;
            if (!fresh && entry.id == id)
                {
                    gauge = GaugeLibrary::gauge(id);
                    gauge.name = entry.builtin ? tr("%1 copy").arg(entry.name) : entry.name;
                }
        }
    if (!fresh && gauge.isEmpty())
        {
            return;
        }
    GaugeEditor editor(gauge, taken, this);
    editor.setReservedNames(reserved);
    if (editor.exec() != QDialog::Accepted)
        {
            return;
        }
    const Gauge made = editor.gauge();
    QString error;
    const QString stored = GaugeLibrary::add(made, made.name, &error);
    if (stored.isEmpty())
        {
            QMessageBox::warning(this, tr("Gauge editor"), error);
            return;
        }
    GaugeLibrary::refresh();
    reload();
    // show it, whatever filter was on
    for (int row = 0; row < m_list->count(); ++row)
        {
            if (m_list->item(row)->data(kIdRole).toString() == stored)
                {
                    m_list->item(row)->setHidden(false);
                    m_list->setCurrentRow(row);
                    m_list->scrollToItem(m_list->item(row));
                }
        }
    emit libraryChanged();
}

void GaugeLibraryPanel::removeSelected()
{
    const QString id = selectedId();
    if (id.isEmpty())
        {
            return;
        }
    const bool templates = showingTemplates();
    const QString question = templates
                             ? tr("Delete the template \"%1\" from the library? Its gauges stay.")
                             : tr("Delete \"%1\" from the library? Projects that use it will show an empty place.");
    if (QMessageBox::question(this, templates ? tr("Remove template") : tr("Remove gauge"),
                              question.arg(m_list->currentItem()->text())) != QMessageBox::Yes)
        {
            return;
        }
    if (templates)
        {
            TemplateLibrary::remove(id);
        }
    else
        {
            GaugeLibrary::remove(id);
        }
    reload();
    if (!templates)
        {
            emit libraryChanged();
        }
}

void GaugeLibraryPanel::importFiles()
{
    QSettings settings;
    const QString folder = QFileDialog::getExistingDirectory(this,
                           tr("Folder with gauges and templates"),
                           settings.value("paths/importGauges").toString());
    if (folder.isEmpty())
        {
            return;
        }
    settings.setValue("paths/importGauges", folder);
    const ImportFolders found = importFoldersIn(folder);
    if (found.gaugeFiles.isEmpty() && found.templateFiles.isEmpty() && found.xmlGauges.isEmpty() && found.xmlTemplates.isEmpty()
            && found.dataProfiles.isEmpty() && found.packages.isEmpty())
        {
            QMessageBox::information(this, tr("Import"),
                                     tr("There are no gauges (.rtvgauge, .veg, gauge.xml), templates (.rtvtemplate, .vet, "
                                        "project.cdp) or data profiles in that folder."));
            return;
        }
    startImport(found);
}

void GaugeLibraryPanel::importPackages()
{
    QSettings settings;
    ImportFolders found;
    found.packages = QFileDialog::getOpenFileNames(this, tr("Gauges and templates"), settings.value("paths/importGauges").toString(),
                     tr("Gauges and templates (*.%1 *.%2)").arg(LibraryPackage::GaugeSuffix, LibraryPackage::TemplateSuffix));
    if (found.packages.isEmpty())
        {
            return;
        }
    settings.setValue("paths/importGauges", QFileInfo(found.packages.first()).path());
    startImport(found);
}

void GaugeLibraryPanel::exportSelected()
{
    const QString id = selectedId();
    if (id.isEmpty())
        {
            return;
        }
    const bool templates = showingTemplates();
    QString error;
    QString name;
    QByteArray package;
    if (templates)
        {
            const GaugeTemplate t = TemplateLibrary::get(id);
            name = t.name;
            package = LibraryPackage::packTemplate(t, &error);
        }
    else
        {
            const Gauge gauge = GaugeLibrary::gauge(id);
            name = gauge.name;
            package = LibraryPackage::packGauge(gauge, &error);
        }
    if (package.isEmpty())
        {
            QMessageBox::warning(this, tr("Export"), error);
            return;
        }
    QSettings settings;
    const char *suffix = templates ? LibraryPackage::TemplateSuffix : LibraryPackage::GaugeSuffix;
    const QString start = QDir(settings.value("paths/exportGauges", QDir::homePath()).toString()).filePath(name + "." + suffix);
    QString path = QFileDialog::getSaveFileName(this, templates ? tr("Export template") : tr("Export gauge"), start,
                   templates ? tr("Templates (*.%1)").arg(suffix) : tr("Gauges (*.%1)").arg(suffix));
    if (path.isEmpty())
        {
            return;
        }
    if (!path.endsWith(QString(".") + suffix, Qt::CaseInsensitive))
        {
            path += QString(".") + suffix;
        }
    settings.setValue("paths/exportGauges", QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(package) != package.size() || !file.commit())
        {
            QMessageBox::warning(this, tr("Export"), tr("Cannot write %1: %2").arg(path, file.errorString()));
        }
}

void GaugeLibraryPanel::startImport(const ImportFolders &found)
{
    m_importButton->setEnabled(false);
    m_importButton->setText(tr("Importing..."));
    auto *watcher = new QFutureWatcher<ImportReport>(this);
    connect(watcher, &QFutureWatcher<ImportReport>::finished, this, [this, watcher]()
    {
        const ImportReport report = watcher->result();
        watcher->deleteLater();
        m_importButton->setEnabled(true);
        m_importButton->setText(tr("Import..."));
        GaugeLibrary::refresh();
        // show what came in: templates if there were any
        const int tab = report.templates.imported > 0 ? TemplatesTab : GaugesTab;
        if (m_tabs->currentIndex() != tab)
            {
                m_tabs->setCurrentIndex(tab); // reloads
            }
        else
            {
                reload();
            }
        emit libraryChanged();

        QStringList lines;
        const int gauges = report.gauges.imported + report.templates.gaugesImported;
        if (gauges > 0 || (report.templates.imported == 0 && report.profiles.imported == 0))
            {
                lines << tr("%n gauge(s) imported.", nullptr, gauges);
            }
        if (report.gauges.incomplete > 0)
            {
                lines << tr("%n of them have parts that cannot be shown.", nullptr, report.gauges.incomplete);
            }
        if (report.templates.imported > 0)
            {
                lines << tr("%n template(s) imported.", nullptr, report.templates.imported);
            }
        if (report.profiles.imported > 0)
            {
                lines << tr("%n data profile(s) imported: CSV files of those loggers can now be opened as data.", nullptr,
                            report.profiles.imported);
            }
        const int failed = int(report.gauges.failed.size() + report.templates.failed.size() + report.profiles.failed.size());
        if (failed > 0)
            {
                lines << tr("%n file(s) could not be used.", nullptr, failed);
            }
        QMessageBox box(QMessageBox::Information, tr("Import"), lines.join('\n'), QMessageBox::Ok, this);
        const QStringList details = report.gauges.failed + report.templates.failed + report.profiles.failed
                                    + report.gauges.notes + report.templates.notes + report.profiles.notes;
        if (!details.isEmpty())
            {
                box.setDetailedText(details.join('\n'));
            }
        box.exec();
    });
    watcher->setFuture(QtConcurrent::run([found]()
    {
        ImportReport report;
        report.gauges = GaugeLibrary::importVeg(found.gaugeFiles);
        const GaugeLibrary::ImportSummary xml = GaugeLibrary::importXml(found.xmlGauges);
        report.gauges.imported += xml.imported;
        report.gauges.incomplete += xml.incomplete;
        report.gauges.failed += xml.failed;
        report.gauges.notes += xml.notes;
        GaugeLibrary::refresh();
        report.templates = TemplateLibrary::importVet(found.templateFiles, found.widgets);
        const TemplateLibrary::ImportSummary xmlTemplates = TemplateLibrary::importXml(found.xmlTemplates);
        report.templates.imported += xmlTemplates.imported;
        report.templates.gaugesImported += xmlTemplates.gaugesImported;
        report.templates.failed += xmlTemplates.failed;
        report.templates.notes += xmlTemplates.notes;
        report.profiles = DataProfileLibrary::importXml(found.dataProfiles);
        // our own files, one gauge or one template with its gauges each
        for (const QString &path : found.packages)
            {
                QFile file(path);
                QString error;
                const bool isTemplate = path.endsWith(QString(".") + LibraryPackage::TemplateSuffix, Qt::CaseInsensitive);
                int gaugesAdded = 0;
                const QString id = !file.open(QIODevice::ReadOnly) ? QString()
                                   : isTemplate ? LibraryPackage::importTemplate(file.readAll(), &gaugesAdded, &error)
                                   : LibraryPackage::importGauge(file.readAll(), &error);
                if (id.isEmpty())
                    {
                        (isTemplate ? report.templates.failed : report.gauges.failed)
                                << QString("%1: %2").arg(QFileInfo(path).fileName(), error.isEmpty() ? file.errorString() : error);
                    }
                else if (isTemplate)
                    {
                        ++report.templates.imported;
                        report.templates.gaugesImported += gaugesAdded;
                    }
                else
                    {
                        ++report.gauges.imported;
                    }
                GaugeLibrary::refresh();
            }
        return report;
    }));
}
