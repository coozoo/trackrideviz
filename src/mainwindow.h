#pragma once

#include <QMainWindow>

class QDockWidget;

// Main window shell: central canvas plus the docks described in
// docs/planning/03-architecture.md. The docks are empty placeholders until
// their milestones land.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showAbout();

private:
    void createDocks();
    void createMenus();
    QDockWidget *addPlaceholderDock(const QString &objectName, const QString &title, Qt::DockWidgetArea area);

    QDockWidget *m_sourcesDock{nullptr};
    QDockWidget *m_libraryDock{nullptr};
    QDockWidget *m_propertiesDock{nullptr};
    QDockWidget *m_timelineDock{nullptr};
};
