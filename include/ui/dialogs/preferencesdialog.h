/*!
 * \file   preferencesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice V — user-facing editor for PreferencesManager state.
 * Categorized left-list / right-stack layout (matches VSCode /
 * macOS Preferences convention). Apply writes through the singleton,
 * which emits `preferenceChanged` so live-bound call sites refresh
 * without restart.
 */

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    ~PreferencesDialog() override = default;

private slots:
    void onApply();
    void onAccept();
    void onResetToDefaults();

private:
    void buildUi();
    void readFromManager();
    void writeToManager();

    QWidget *buildGeneralPage();
    QWidget *buildSelectionPage();
    QWidget *buildCanvasPage();
    QWidget *buildRenderingPage();
    QWidget *buildSimulationPage();

    QListWidget    *m_categoryList = nullptr;
    QStackedWidget *m_pages        = nullptr;

    // Selection
    QSpinBox   *m_clickTolerancePxSpin  = nullptr;
    QSpinBox   *m_dragThresholdPxSpin   = nullptr;
    QCheckBox  *m_clearOnMissBox        = nullptr;

    // Canvas
    QComboBox  *m_defaultToolCombo      = nullptr;
    QLineEdit  *m_crsAuthorityEdit      = nullptr;
    QSpinBox   *m_crsCodeSpin           = nullptr;

    // Rendering
    QDoubleSpinBox *m_labelLodSpin      = nullptr;

    // Simulation
    QSpinBox   *m_progressTickMsSpin    = nullptr;
};

#endif // PREFERENCESDIALOG_H
