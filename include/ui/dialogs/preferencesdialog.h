/*!
 * \file   preferencesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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

#include <QColor>
#include <QFont>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
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
    QWidget *buildMapDisplayPage();
    QWidget *buildMeasureToolPage();
    QWidget *buildNamingPage();

    QListWidget    *m_categoryList = nullptr;
    QStackedWidget *m_pages        = nullptr;

    // General
    QCheckBox  *m_showLicenseOnStartupBox = nullptr;

    // Selection
    QSpinBox   *m_clickTolerancePxSpin  = nullptr;
    QSpinBox   *m_dragThresholdPxSpin   = nullptr;
    QCheckBox  *m_clearOnMissBox        = nullptr;
    QPushButton *m_selColorLink         = nullptr;
    QPushButton *m_selColorNode         = nullptr;
    QPushButton *m_selColorSubcatch     = nullptr;
    QPushButton *m_selColorGage         = nullptr;
    QColor      m_pendingSelColorLink;
    QColor      m_pendingSelColorNode;
    QColor      m_pendingSelColorSubcatch;
    QColor      m_pendingSelColorGage;

    // Canvas
    QComboBox  *m_defaultToolCombo      = nullptr;
    QLineEdit  *m_crsAuthorityEdit      = nullptr;
    QSpinBox   *m_crsCodeSpin           = nullptr;

    // Rendering
    QDoubleSpinBox *m_labelLodSpin      = nullptr;
    QPushButton *m_linkColorConduit     = nullptr;
    QPushButton *m_linkColorPump        = nullptr;
    QPushButton *m_linkColorOrifice     = nullptr;
    QPushButton *m_linkColorWeir        = nullptr;
    QPushButton *m_linkColorOutlet      = nullptr;
    QColor      m_pendingLinkColorConduit;
    QColor      m_pendingLinkColorPump;
    QColor      m_pendingLinkColorOrifice;
    QColor      m_pendingLinkColorWeir;
    QColor      m_pendingLinkColorOutlet;

    // Simulation
    QSpinBox   *m_progressTickMsSpin    = nullptr;

    // Map Display / Scale Bar
    QPushButton   *m_scaleBarColorBtn         = nullptr;
    QColor         m_pendingScaleBarColor;
    QSpinBox      *m_scaleBarPenWidthSpin     = nullptr;
    QComboBox     *m_scaleBarPenStyleCombo    = nullptr;
    QPushButton   *m_scaleBarFontBtn          = nullptr;
    QFont          m_pendingScaleBarFont;
    QComboBox     *m_scaleBarUnitsCombo       = nullptr;
    QComboBox     *m_scaleBarPositionCombo    = nullptr;
    QSpinBox      *m_scaleBarMaxBarLengthSpin = nullptr;
    QSpinBox      *m_scaleBarLabelDecimalsSpin   = nullptr;   // -1..4
    QCheckBox     *m_scaleBarCompactNotationBox  = nullptr;

    // Measure Tool
    QPushButton   *m_measureLineColorBtn      = nullptr;
    QColor         m_pendingMeasureLineColor;
    QPushButton   *m_measureLabelFontBtn      = nullptr;
    QFont          m_pendingMeasureLabelFont;
    QSpinBox      *m_measureLabelDecimalsSpin = nullptr;
    QPushButton   *m_measureFillColorBtn      = nullptr;
    QColor         m_pendingMeasureFillColor;
    QSpinBox      *m_measureFillOpacitySpin   = nullptr;

    // Naming prefixes (one QLineEdit per element kind)
    QLineEdit *m_prefixJunction     = nullptr;
    QLineEdit *m_prefixOutfall      = nullptr;
    QLineEdit *m_prefixStorage      = nullptr;
    QLineEdit *m_prefixDivider      = nullptr;
    QLineEdit *m_prefixConduit      = nullptr;
    QLineEdit *m_prefixPump         = nullptr;
    QLineEdit *m_prefixOrifice      = nullptr;
    QLineEdit *m_prefixWeir         = nullptr;
    QLineEdit *m_prefixOutlet       = nullptr;
    QLineEdit *m_prefixRaingage     = nullptr;
    QLineEdit *m_prefixSubcatchment = nullptr;
};

#endif // PREFERENCESDIALOG_H
