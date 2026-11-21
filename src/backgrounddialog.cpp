#include "backgrounddialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

BackgroundDialog::BackgroundDialog(const VideoBackground &current, QWidget *parent)
    : QDialog(parent)
    , m_color(current.isOn() && !current.isTransparent() ? current.color : QColor(24, 24, 28))
{
    setWindowTitle(tr("Video without a camera video"));
    QLabel *about = new QLabel(tr("The gauges drawn over a plain color, or over nothing to lay on another video, "
                                  "as long as the data. A map of the route made big makes a video of the ride "
                                  "by itself."), this);
    about->setWordWrap(true);

    m_sizeCombo = new QComboBox(this);
    const QVector<QPair<QString, QSize>> sizes
    {
        {tr("1920 x 1080 (landscape)"), QSize(1920, 1080)},
        {tr("1080 x 1920 (upright, for phones)"), QSize(1080, 1920)},
        {tr("1080 x 1080 (square)"), QSize(1080, 1080)},
        {tr("3840 x 2160 (4K)"), QSize(3840, 2160)},
        {tr("1280 x 720"), QSize(1280, 720)},
    };
    for (const auto &size : sizes)
        {
            m_sizeCombo->addItem(size.first, size.second);
        }
    if (current.isOn() && m_sizeCombo->findData(current.size) < 0)
        {
            m_sizeCombo->addItem(QString("%1 x %2").arg(current.size.width()).arg(current.size.height()), current.size);
        }
    m_sizeCombo->setCurrentIndex(current.isOn() ? m_sizeCombo->findData(current.size) : 0);

    m_rateCombo = new QComboBox(this);
    for (const int fps : {24, 25, 30, 50, 60})
        {
            m_rateCombo->addItem(tr("%1 frames per second").arg(fps), fps);
        }
    const int rate = m_rateCombo->findData(current.isOn() ? current.fps : 30);
    m_rateCombo->setCurrentIndex(rate >= 0 ? rate : m_rateCombo->findData(30));

    m_colorButton = new QPushButton(this);
    connect(m_colorButton, &QPushButton::clicked, this, &BackgroundDialog::chooseColor);
    m_transparentCheck = new QCheckBox(tr("See-through: the gauges alone"), this);
    m_transparentCheck->setToolTip(tr("Exported as a video with transparency (.mov or .webm), to lay over "
                                      "another video in an editor"));
    m_transparentCheck->setChecked(current.isOn() && current.isTransparent());
    connect(m_transparentCheck, &QCheckBox::toggled, this, &BackgroundDialog::showColor);
    showColor();

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QFormLayout *form = new QFormLayout(this);
    form->addRow(about);
    form->addRow(tr("Size:"), m_sizeCombo);
    form->addRow(tr("Frame rate:"), m_rateCombo);
    form->addRow(tr("Color:"), m_colorButton);
    form->addRow(QString(), m_transparentCheck);
    form->addRow(buttons);
}

VideoBackground BackgroundDialog::background() const
{
    VideoBackground background;
    background.size = m_sizeCombo->currentData().toSize();
    background.fps = m_rateCombo->currentData().toInt();
    background.color = m_transparentCheck->isChecked() ? QColor(0, 0, 0, 0) : m_color;
    return background;
}

void BackgroundDialog::chooseColor()
{
    const QColor chosen = QColorDialog::getColor(m_color, this, tr("Background color"));
    if (chosen.isValid())
        {
            m_color = chosen;
            m_color.setAlpha(255);
            showColor();
        }
}

void BackgroundDialog::showColor()
{
    QPixmap swatch(32, 16);
    swatch.fill(m_color);
    m_colorButton->setIcon(swatch);
    m_colorButton->setText(m_color.name());
    m_colorButton->setEnabled(!m_transparentCheck->isChecked());
}
