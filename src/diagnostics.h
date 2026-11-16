#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QPlainTextEdit;
class QTimer;

// What the program and Qt say while it runs, kept for the Diagnostics dock: every message of
// Qt and its plugins (GStreamer among them), and notes of the program's own steps. Messages
// still go to the console as before. Safe from any thread.
namespace DiagnosticLog
{
// Takes over Qt's message handler, passing every message on to the one before.
void install();
// A step of the program worth seeing: "[music] seek to 12.3 s".
void note(const QString &area, const QString &text);
// The lines after line `from` (counted since the start), and the count now.
QStringList linesSince(qint64 from, qint64 *count);
void clear();
}

// The Diagnostics dock: how the program was started, the state of the players as it changes,
// and the log. For finding out why something does not work on a given system.
class DiagnosticsPanel : public QWidget
{
    Q_OBJECT

public:
    // `state`: the players' state now, as text; asked a few times a second while shown.
    explicit DiagnosticsPanel(std::function<QString()> state, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refresh();
    static QString environment();

    std::function<QString()> m_state;
    QLabel *m_environment{nullptr};
    QLabel *m_stateLabel{nullptr};
    QPlainTextEdit *m_log{nullptr};
    QTimer *m_timer{nullptr};
    qint64 m_shown{0};
};
