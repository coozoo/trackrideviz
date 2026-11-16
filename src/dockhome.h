#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

class QDockWidget;
class QMainWindow;

// Remembers where each dock of a window was last docked, the side and the tabs it shared,
// and puts a floating dock straight back there when its title is double-clicked. Qt does
// this by itself only while it still knows the place; a dock dragged around before has
// often lost it, and stays floating over the window.
class DockHome : public QObject
{
    Q_OBJECT

public:
    // For the docks `window` has now.
    explicit DockHome(QMainWindow *window);

    // Docked again where it was last; false when it never was.
    bool sendHome(QDockWidget *dock);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void remember(QDockWidget *dock);

    struct Home
    {
        int area{0};                        // Qt::DockWidgetArea
        QPointer<QDockWidget> tabbedWith;   // a dock of the tabs it was among
    };
    QMainWindow *m_window;
    QHash<QDockWidget *, Home> m_homes;
};
