#include "dockhome.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QMouseEvent>

DockHome::DockHome(QMainWindow *window)
    : QObject(window)
    , m_window(window)
{
    for (QDockWidget *dock : window->findChildren<QDockWidget *>())
        {
            dock->installEventFilter(this);
            remember(dock);
            // once the layout has settled: the tabs are known only then
            connect(dock, &QDockWidget::dockLocationChanged, this, [this, dock]()
            {
                QMetaObject::invokeMethod(this, [this, dock]()
                {
                    remember(dock);
                }, Qt::QueuedConnection);
            });
            connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating)
            {
                if (!floating)
                    {
                        QMetaObject::invokeMethod(this, [this, dock]()
                        {
                            remember(dock);
                        }, Qt::QueuedConnection);
                    }
            });
        }
}

void DockHome::remember(QDockWidget *dock)
{
    if (dock->isFloating())
        {
            return;
        }
    const Qt::DockWidgetArea area = m_window->dockWidgetArea(dock);
    if (area == Qt::NoDockWidgetArea)
        {
            return;
        }
    Home &home = m_homes[dock];
    home.area = area;
    const QList<QDockWidget *> tabs = m_window->tabifiedDockWidgets(dock);
    home.tabbedWith = tabs.isEmpty() ? nullptr : tabs.first();
    // those it shares the tabs with have it as theirs
    for (QDockWidget *other : tabs)
        {
            m_homes[other] = {area, dock};
        }
}

bool DockHome::sendHome(QDockWidget *dock)
{
    const auto found = m_homes.constFind(dock);
    if (found == m_homes.constEnd() || found->area == 0)
        {
            return false;
        }
    const Home home = *found;
    dock->setFloating(false);
    QDockWidget *partner = home.tabbedWith;
    if (partner && partner != dock && !partner->isFloating() && partner->isVisible()
            && m_window->dockWidgetArea(partner) != Qt::NoDockWidgetArea)
        {
            m_window->tabifyDockWidget(partner, dock);
        }
    else
        {
            m_window->addDockWidget(Qt::DockWidgetArea(home.area), dock);
        }
    dock->show();
    dock->raise();
    return true;
}

bool DockHome::eventFilter(QObject *watched, QEvent *event)
{
    QDockWidget *dock = qobject_cast<QDockWidget *>(watched);
    // a title the system draws, where it tells (Windows); the rest leave its double-clicks to themselves
    if (dock && event->type() == QEvent::NonClientAreaMouseButtonDblClick && dock->isFloating() && sendHome(dock))
        {
            return true;
        }
    if (dock && event->type() == QEvent::MouseButtonDblClick && dock->isFloating())
        {
            // on the title, above what the dock holds
            const QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
            const int contentTop = dock->widget() ? dock->widget()->geometry().top() : 0;
            if (mouse->button() == Qt::LeftButton && mouse->position().y() < contentTop && sendHome(dock))
                {
                    return true;
                }
        }
    return QObject::eventFilter(watched, event);
}
