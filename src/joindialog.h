#pragma once

#include "sync/syncmap.h"

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QProgressBar;
class QTemporaryDir;

// Joins several videos into one file by copying their streams, and works out
// the breaks between them so the data stays in step after each.
class JoinDialog : public QDialog
{
    Q_OBJECT

public:
    explicit JoinDialog(const QStringList &paths = {}, QWidget *parent = nullptr);
    ~JoinDialog() override;

    // After it was accepted: the joined file and the breaks in it.
    QString outputPath() const { return m_outputPath; }
    QVector<TimeShift> shifts() const { return m_shifts; }

public slots:
    void reject() override;

private:
    void addFiles(const QStringList &paths);
    void addFilesFromDialog();
    void move(int step);
    void browseOutput();
    void suggestOutput();
    void join();
    void onJoined();
    void setBusy(bool busy);
    QStringList paths() const;

    QListWidget *m_list{nullptr};
    QLineEdit *m_outputEdit{nullptr};
    QLabel *m_status{nullptr};
    QProgressBar *m_progress{nullptr};
    QDialogButtonBox *m_buttons{nullptr};
    QWidget *m_editors{nullptr};
    QProcess *m_process{nullptr};
    QTemporaryDir *m_listDir{nullptr};
    bool m_outputEdited{false};
    QString m_outputPath;
    QVector<TimeShift> m_shifts;
};
