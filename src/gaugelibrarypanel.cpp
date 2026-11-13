#include "gaugelibrarypanel.h"

#include "gauge/gaugelibrary.h"
#include "gauge/gaugerenderer.h"
#include "gauge/xmlgaugeimporter.h"
#include "overlay/templatelibrary.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace
{
const int kGaugeThumbnail = 96;
const QSize kTemplateThumbnail(160, 90);
const QColor kThumbnailBackground(60, 70, 85);

enum Tab { GaugesTab, TemplatesTab };

struct Thumbnail
{
    QString id;
    QImage image;
};

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
                    // shown with what it was made for, if the demo has it
                    QString channel = ChannelId::Speed;
                    for (const QString &supported : gauge.supportedTypes)
                        {
                            if (session->hasChannel(supported))
                                {
                                    channel = supported;
                                    break;
                                }
                        }
                    const BoundGauge bound(gauge, channel, session, Units(), GaugeTheme());
                    const QSizeF size = bound.size().scaled(kGaugeThumbnail - 4, kGaugeThumbnail - 4, Qt::KeepAspectRatio);
                    QPainter painter(&image);
                    bound.render(painter, QRectF((kGaugeThumbnail - size.width()) / 2, (kGaugeThumbnail - size.height()) / 2,
                                                 size.width(), size.height()), DemoSession::ShowcaseTime);
                }
            thumbnails.append({id, image});
        }
    return thumbnails;
}

// What a chosen folder holds: a program folder with Widgets and Templates in it, or one of those;
// XML gauges, each a folder with gauge.xml, or a folder of them, or one with Gauges in it.
struct ImportFolders
{
    QString widgets;
    QStringList gaugeFiles;
    QStringList templateFiles;
    QStringList xmlGauges;
};

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

bool isXmlGauge(const QString &folder)
{
    const QDir dir(folder);
    return !dir.entryList({XmlGaugeImporter::FileName}, QDir::Files).isEmpty();
}

QStringList xmlGaugesIn(const QString &chosen)
{
    if (isXmlGauge(chosen))
        {
            return {chosen};
        }
    QStringList folders;
    const QString gauges = childIgnoringCase(QDir(chosen), "Gauges");
    const QDir dir(gauges.isEmpty() ? chosen : gauges);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        {
            if (isXmlGauge(dir.filePath(name)))
                {
                    folders << dir.filePath(name);
                }
        }
    return folders;
}

ImportFolders importFoldersIn(const QString &chosen)
{
    ImportFolders found;
    found.xmlGauges = xmlGaugesIn(chosen);
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
    m_importButton->setToolTip(tr("Brings in .veg gauges, .vet templates and XML gauges (folders with a gauge.xml). "
                                  "Choose the folder that holds them, or a folder with Widgets and Templates "
                                  "or with Gauges in it."));
    connect(m_importButton, &QPushButton::clicked, this, &GaugeLibraryPanel::importFiles);
    m_removeButton = new QPushButton(tr("Remove"), this);
    connect(m_removeButton, &QPushButton::clicked, this, &GaugeLibraryPanel::removeSelected);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_useButton);
    buttons->addWidget(m_importButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_tabs);
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
    return item && item->isSelected() ? item->data(Qt::UserRole).toString() : QString();
}

void GaugeLibraryPanel::updateButtons()
{
    const QListWidgetItem *item = m_list->currentItem();
    const bool selected = item && item->isSelected();
    m_useButton->setEnabled(selected);
    // what came with the program is part of it
    m_removeButton->setEnabled(selected && !item->data(Qt::UserRole + 1).toBool());
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
    m_removeButton->setToolTip(templates ? tr("Deletes the selected template from the library")
                               : tr("Deletes the selected gauge from the library"));

    // something of the right size until the picture is drawn, so the list does not jump
    QPixmap blank(iconSize);
    blank.fill(kThumbnailBackground);
    const QIcon placeholder(blank);
    QStringList ids;
    auto addItem = [&](const QString &id, const QString &name, bool builtin, const QString &tooltip)
    {
        QListWidgetItem *item = new QListWidgetItem(placeholder, name, m_list);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, builtin);
        item->setToolTip(tooltip);
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
                            entry.category.isEmpty() ? entry.name : QString("%1 (%2)").arg(entry.name, entry.category));
                }
        }
    else
        {
            for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
                {
                    addItem(entry.id, entry.name, entry.builtin, entry.name);
                }
        }
    updateButtons();

    auto *watcher = new QFutureWatcher<QVector<Thumbnail>>(this);
    connect(watcher, &QFutureWatcher<QVector<Thumbnail>>::finished, this, [this, watcher, generation]()
    {
        watcher->deleteLater();
        if (generation != m_generation)
            {
                return; // the list was rebuilt meanwhile
            }
        QHash<QString, QImage> images;
        for (const Thumbnail &thumbnail : watcher->result())
            {
                images.insert(thumbnail.id, thumbnail.image);
            }
        for (int row = 0; row < m_list->count(); ++row)
            {
                QListWidgetItem *item = m_list->item(row);
                item->setIcon(QIcon(QPixmap::fromImage(images.value(item->data(Qt::UserRole).toString()))));
            }
    });
    watcher->setFuture(QtConcurrent::run(renderThumbnails, ids, templates));
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
    if (found.gaugeFiles.isEmpty() && found.templateFiles.isEmpty() && found.xmlGauges.isEmpty())
        {
            QMessageBox::information(this, tr("Import"),
                                     tr("There are no gauges (.veg, gauge.xml) or templates (.vet) in that folder."));
            return;
        }

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
        if (gauges > 0 || report.templates.imported == 0)
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
        const int failed = int(report.gauges.failed.size() + report.templates.failed.size());
        if (failed > 0)
            {
                lines << tr("%n file(s) could not be used.", nullptr, failed);
            }
        QMessageBox box(QMessageBox::Information, tr("Import"), lines.join('\n'), QMessageBox::Ok, this);
        const QStringList details = report.gauges.failed + report.templates.failed + report.gauges.notes + report.templates.notes;
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
        return report;
    }));
}
