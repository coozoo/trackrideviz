#pragma once

#include "video/soundtrack.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimeEdit;

// Music under the video: files played one after another from a moment of
// the video on, and how loud they and the video's own sound are.
//
// Changes go out as soundtrackEdited() (one undo step each); a volume
// slider being dragged sends volumesPreviewed() until it is let go.
class MusicPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MusicPanel(QWidget *parent = nullptr);

    // Shows `soundtrack` without sending anything.
    void setSoundtrack(const Soundtrack &soundtrack);
    // Where the video stands, for "Here", and how long it is (0: none).
    void setPosition(double videoTime);
    void setVideoDuration(double seconds);
    // Kept parts of the video: how long the export is, against the music's length.
    void setCuts(const QVector<TimeRange> &cuts);

signals:
    void soundtrackEdited(const Soundtrack &soundtrack, const QString &description);
    void volumesPreviewed(const Soundtrack &soundtrack);
    void seekRequested(double videoTime);

private:
    void addFiles();
    void removeSelected();
    void moveSelected(int step);
    void onRowsMoved();
    void onSelectionChanged();
    void onTrimEdited();
    void onVolumeMoved();
    void onVolumeReleased();
    void edit(const Soundtrack &soundtrack, const QString &description);
    void refreshList();
    void refreshSummary();
    // The soundtrack as the controls show it.
    Soundtrack shown() const;

    Soundtrack m_soundtrack;
    double m_position{0.0};
    double m_videoDuration{0.0};
    QVector<TimeRange> m_cuts;
    bool m_updating{false};
    int m_reading{0};   // files whose length is being found out

    QListWidget *m_list{nullptr};
    QPushButton *m_addButton{nullptr};
    QPushButton *m_removeButton{nullptr};
    QPushButton *m_upButton{nullptr};
    QPushButton *m_downButton{nullptr};
    QTimeEdit *m_fromEdit{nullptr};
    QTimeEdit *m_toEdit{nullptr};
    QTimeEdit *m_startEdit{nullptr};
    QPushButton *m_hereButton{nullptr};
    QDoubleSpinBox *m_fadeSpin{nullptr};
    QSlider *m_videoSlider{nullptr};
    QCheckBox *m_videoMute{nullptr};
    QLabel *m_videoLabel{nullptr};
    QSlider *m_musicSlider{nullptr};
    QCheckBox *m_musicMute{nullptr};
    QLabel *m_musicLabel{nullptr};
    QLabel *m_summary{nullptr};
};
