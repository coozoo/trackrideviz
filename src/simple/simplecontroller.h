#pragma once

#include "overlay/overlay.h"
#include "project/project.h"
#include "video/mediainfo.h"
#include "video/videoencoders.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>

class ExportJob;
class QThread;

// What the simplified interface does, for its QML: a video and its data, put in step by the
// clocks, a template over it and an export with a profile; the rest automatic. The same core
// the full window and the command line use, without their choices.
class SimpleController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canGoBack READ canGoBack CONSTANT)
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    Q_PROPERTY(QString dataName READ dataName NOTIFY dataChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY dataChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(double syncOffset READ syncOffset NOTIFY syncChanged)
    Q_PROPERTY(QString syncText READ syncText NOTIFY syncChanged)
    Q_PROPERTY(QStringList templateNames READ templateNames CONSTANT)
    Q_PROPERTY(int templateIndex READ templateIndex NOTIFY overlayChanged)
    Q_PROPERTY(QStringList profileNames READ profileNames CONSTANT)
    Q_PROPERTY(int profileIndex READ profileIndex WRITE setProfileIndex NOTIFY profileChanged)
    Q_PROPERTY(double aspect READ aspect NOTIFY videoChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportText READ exportText NOTIFY exportChanged)
    Q_PROPERTY(QUrl exportedUrl READ exportedUrl NOTIFY exportChanged)

public:
    explicit SimpleController(bool canGoBack, QObject *parent = nullptr);
    ~SimpleController() override;

    bool canGoBack() const { return m_canGoBack; }
    QUrl videoUrl() const;
    QString videoName() const;
    QString dataName() const;
    bool hasData() const { return bool(m_session); }
    bool busy() const { return m_loading > 0; }
    QString message() const { return m_message; }
    double syncOffset() const { return m_project.syncOffset; }
    QString syncText() const { return m_syncText; }
    QStringList templateNames() const;
    int templateIndex() const { return m_templateIndex; }
    QStringList profileNames() const;
    int profileIndex() const { return m_profileIndex; }
    void setProfileIndex(int index);
    double aspect() const;
    bool exporting() const { return m_thread != nullptr; }
    double exportProgress() const { return m_exportProgress; }
    QString exportText() const { return m_exportText; }
    QUrl exportedUrl() const;

    // What is drawn over the picture, for GaugeOverlay; never null.
    std::shared_ptr<const Overlay> overlay() const { return m_overlay; }
    // The data's time at a moment of the video.
    Q_INVOKABLE double telemetryTime(qint64 videoMs) const;

    // A video; its data is read out of it when it has some and none is open.
    Q_INVOKABLE void openVideo(const QUrl &url);
    // Data from a file of its own (FIT, GPX, CSV...).
    Q_INVOKABLE void openData(const QUrl &url);
    // The data moved against the video by this many seconds (+: later).
    Q_INVOKABLE void nudgeSync(double seconds);
    // In step by the clocks of the camera and the data again.
    Q_INVOKABLE void syncByClocks();
    Q_INVOKABLE void chooseTemplate(int index);
    Q_INVOKABLE void startExport();
    Q_INVOKABLE void stopExport();
    // Back to the full window (only when it was left for this).
    Q_INVOKABLE void goBack();

signals:
    void videoChanged();
    void dataChanged();
    void busyChanged();
    void messageChanged();
    void syncChanged();
    void overlayChanged();
    void profileChanged();
    void exportChanged();
    void backRequested();

private:
    void setMessage(const QString &message);
    void setBusy(bool more);
    void loadData(const QString &path, bool fromVideo);
    void dataLoaded(const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session, const QString &path, bool fromVideo,
                    const QString &error, bool waiting);
    void rebuildOverlay();
    void setSync(double offset, const QString &text);
    void exportFinished(int outcome, const QString &message);

    const bool m_canGoBack;
    Project m_project;
    MediaInfo m_info;
    bool m_probed{false};
    TelemetrySessionPtr m_raw;
    TelemetrySessionPtr m_session;
    std::shared_ptr<const Overlay> m_overlay;
    int m_templateIndex{-1};
    int m_profileIndex{0};
    int m_loading{0};
    quint64 m_dataGeneration{0};
    QString m_message;
    QString m_syncText;
    // the heights of the terrain and the weather, while they come
    QTimer m_arrivals;
    int m_arrivalTries{0};

    // the encoders of this computer, found once
    bool m_encodersFound{false};
    VideoEncoders::ForEngine m_encoders;
    QThread *m_thread{nullptr};
    QPointer<ExportJob> m_job;     // on m_thread
    bool m_exportWaitsForEncoders{false};
    double m_exportProgress{0.0};
    QString m_exportText;
    QString m_exportedPath;
    QString m_runningPath;
};
