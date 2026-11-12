#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(qApp->property("appname").toString());
    resize(1280, 800);

    QLabel *canvasPlaceholder = new QLabel(tr("Open a video to start"), this);
    canvasPlaceholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(canvasPlaceholder);

    createDocks();
    createMenus();

    QSettings settings;
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
    restoreState(settings.value("mainwindow/state").toByteArray());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.setValue("mainwindow/state", saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::createDocks()
{
    m_sourcesDock = addPlaceholderDock("sourcesDock", tr("Sources"), Qt::LeftDockWidgetArea);
    m_libraryDock = addPlaceholderDock("libraryDock", tr("Gauge library"), Qt::LeftDockWidgetArea);
    m_propertiesDock = addPlaceholderDock("propertiesDock", tr("Properties"), Qt::RightDockWidgetArea);
    m_timelineDock = addPlaceholderDock("timelineDock", tr("Timeline"), Qt::BottomDockWidgetArea);
    tabifyDockWidget(m_sourcesDock, m_libraryDock);
    m_sourcesDock->raise();
}

QDockWidget *MainWindow::addPlaceholderDock(const QString &objectName, const QString &title, Qt::DockWidgetArea area)
{
    QDockWidget *dock = new QDockWidget(title, this);
    // objectName is what saveState()/restoreState() key on - must stay untranslated
    dock->setObjectName(objectName);
    dock->setWidget(new QWidget(dock));
    addDockWidget(area, dock);
    return dock;
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_sourcesDock->toggleViewAction());
    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addAction(m_timelineDock->toggleViewAction());

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About"),
                       tr("<b>%1</b><br>Version %2<br><br>"
                          "Telemetry overlays for action camera video.<br>"
                          "Licensed under the GNU GPL v3.")
                       .arg(qApp->property("appname").toString(), qApp->property("appversion").toString()));
}
