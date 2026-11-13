#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;
class QTabBar;

// The library dock: the gauges and templates on this computer with small
// pictures, how they get in (import) and how they get onto the video.
class GaugeLibraryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GaugeLibraryPanel(QWidget *parent = nullptr);

    // What an import found to bring in (see the .cpp).
    struct ImportFolders;

public slots:
    // Reads the library again. Pictures are drawn in a worker thread and
    // appear as they are ready.
    void reload();

signals:
    void addRequested(const QString &gaugeId);
    void applyTemplateRequested(const QString &templateId);
    // Gauges were imported or removed: what is on the video may look different now.
    void libraryChanged();

private slots:
    void useSelected();
    void importFiles();
    void importPackages();
    void exportSelected();
    void removeSelected();
    void updateButtons();

private:
    bool showingTemplates() const;
    QString selectedId() const;
    void startImport(const ImportFolders &found);

    QTabBar *m_tabs{nullptr};
    QListWidget *m_list{nullptr};
    QPushButton *m_useButton{nullptr};
    QPushButton *m_importButton{nullptr};
    QPushButton *m_exportButton{nullptr};
    QPushButton *m_removeButton{nullptr};
    // Only the newest reload may fill in pictures.
    int m_generation{0};
};
