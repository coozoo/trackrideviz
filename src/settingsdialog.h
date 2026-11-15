#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
struct SurfaceType;

// Application preferences: a page list on the left, the page on the right.
// Nothing is written until Save; the caller applies what can change at run
// time (appearance, units) after exec() returns Accepted.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

public slots:
    void accept() override;

private:
    void addPage(const QString &title, QWidget *page);
    QWidget *createAppearancePage();
    QWidget *createPreviewPage();
    QWidget *createUnitsPage();
    QWidget *createExportPage();
    QWidget *createMapsPage();
    QWidget *createPowerPage();
    QWidget *createWeatherPage();
    QWidget *createSurfacesPage();
    void showSurfaces(const QVector<SurfaceType> &types);
    // Runs the ffmpeg named on the Export page and shows its version, or that it cannot be run.
    void checkFfmpeg();
    void updateMapFields();
    void updateStoredMaps();
    // Shows what the chosen tires and position stand for; the numbers can be typed only for "my own".
    void updatePowerFields();
    void updateStoredWeather();
    void load();

    QListWidget *m_pageList{nullptr};
    QStackedWidget *m_pages{nullptr};

    QComboBox *m_styleCombo{nullptr};
    QComboBox *m_colorSchemeCombo{nullptr};
    QComboBox *m_languageCombo{nullptr};
    QCheckBox *m_saveLayoutCheck{nullptr};

    QCheckBox *m_hardwareDecodingCheck{nullptr};
    QComboBox *m_decoderCombo{nullptr};

    QComboBox *m_speedCombo{nullptr};
    QComboBox *m_lengthCombo{nullptr};
    QComboBox *m_temperatureCombo{nullptr};

    QLineEdit *m_ffmpegEdit{nullptr};
    QLabel *m_ffmpegStatus{nullptr};
    QLineEdit *m_exportFolderEdit{nullptr};
    QCheckBox *m_mapDownloadsCheck{nullptr};
    QComboBox *m_mapProviderCombo{nullptr};
    QLineEdit *m_mapApiKeyEdit{nullptr};
    QCheckBox *m_mapCreditCheck{nullptr};
    QPushButton *m_mapClearButton{nullptr};
    QLineEdit *m_mapUrlEdit{nullptr};
    QLineEdit *m_mapAttributionEdit{nullptr};

    QCheckBox *m_powerCheck{nullptr};
    QDoubleSpinBox *m_riderWeightSpin{nullptr};
    QDoubleSpinBox *m_riderHeightSpin{nullptr};
    QDoubleSpinBox *m_bikeWeightSpin{nullptr};
    QLineEdit *m_tireSizeEdit{nullptr};
    QComboBox *m_tiresCombo{nullptr};
    QDoubleSpinBox *m_crrSpin{nullptr};
    QComboBox *m_positionCombo{nullptr};
    QDoubleSpinBox *m_cdaSpin{nullptr};
    QDoubleSpinBox *m_lossSpin{nullptr};
    QTableWidget *m_surfaceTable{nullptr};
    QCheckBox *m_weatherCheck{nullptr};
    QPushButton *m_weatherClearButton{nullptr};
    // weights in pounds and the height in inches, as the units of the settings were when the dialog opened
    bool m_imperialBody{false};
    QDoubleSpinBox *m_weatherSpacingSpin{nullptr};
    QDoubleSpinBox *m_windShareSpin{nullptr};
    // the user's own numbers, kept while a ready kind is shown in their place
    double m_ownCrr{0.0};
    double m_ownCda{0.0};
};
