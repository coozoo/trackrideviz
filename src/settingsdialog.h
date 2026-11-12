#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QListWidget;
class QStackedWidget;

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
};
