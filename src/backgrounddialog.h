#pragma once

#include "video/videobackground.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QPushButton;

// What a project with no video shows the gauges over: the picture's size and frame rate, and
// a color or nothing (see-through, the gauges alone for laying over another video).
class BackgroundDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BackgroundDialog(const VideoBackground &current, QWidget *parent = nullptr);

    VideoBackground background() const;

private:
    void chooseColor();
    void showColor();

    QComboBox *m_sizeCombo{nullptr};
    QComboBox *m_rateCombo{nullptr};
    QPushButton *m_colorButton{nullptr};
    QCheckBox *m_transparentCheck{nullptr};
    QColor m_color;
};
