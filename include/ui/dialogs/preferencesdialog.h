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
class QRadioButton;
class QSpinBox;
class QStackedWidget;

class LinkRenderingPrefs;
class NodeRenderingPrefs;
class SelectionRenderingPrefs;
class QPropertyModel;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    ~PreferencesDialog() override = default;

    /*! Select the category page whose list label matches \a label
     *  (e.g. "Appearance"). No-op when no category matches. */
    void openAtCategory(const QString &label);

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
    QWidget *buildSimulationDefaultsPage();
    QWidget *buildDynamicWaveDefaultsPage();
    QWidget *buildMapDisplayPage();
    QWidget *buildMeasureToolPage();
    QWidget *buildPlotsPage();
    QWidget *buildNamingPage();
    QWidget *buildAppearancePage();
    QWidget *buildKeyboardPage();

    QListWidget    *m_categoryList = nullptr;
    QStackedWidget *m_pages        = nullptr;

    // Appearance
    QRadioButton *m_appearanceSystemRadio = nullptr;
    QRadioButton *m_appearanceLightRadio  = nullptr;
    QRadioButton *m_appearanceDarkRadio   = nullptr;

    // General
    QCheckBox  *m_showLicenseOnStartupBox = nullptr;
    QCheckBox  *m_autoLengthBox           = nullptr;
    QComboBox  *m_defaultEngineCombo      = nullptr;
    QSpinBox   *m_profileMaxPathsSpin     = nullptr;
    QSpinBox   *m_profileHaloRadiusSpin   = nullptr;
    QPushButton *m_profileStartColorBtn   = nullptr;
    QColor      m_pendingProfileStartColor;
    QDoubleSpinBox *m_profileStartWidthSpin = nullptr;
    QPushButton *m_profileEndColorBtn     = nullptr;
    QColor      m_pendingProfileEndColor;
    QDoubleSpinBox *m_profileEndWidthSpin = nullptr;

    // Selection
    QSpinBox   *m_clickTolerancePxSpin  = nullptr;
    QSpinBox   *m_dragThresholdPxSpin   = nullptr;
    QCheckBox  *m_clearOnMissBox        = nullptr;
    // Per-class pens + brushes are edited through a QPropertyModel-
    // backed QTreeView. Edits route through SelectionRenderingPrefs's
    // setters directly into PreferencesManager, so writeToManager()
    // leaves them alone.
    SelectionRenderingPrefs *m_selectionPrefs  = nullptr;
    QPropertyModel          *m_selectionModel  = nullptr;

    // Canvas
    QComboBox    *m_defaultToolCombo    = nullptr;
    QRadioButton *m_crsAutoRadio        = nullptr;
    QRadioButton *m_crsCustomRadio      = nullptr;
    QLineEdit    *m_crsAuthorityEdit    = nullptr;
    QSpinBox     *m_crsCodeSpin         = nullptr;

    // Snapping
    QCheckBox    *m_snapEnabledBox      = nullptr;
    QSpinBox     *m_snapToleranceSpin   = nullptr;
    QCheckBox    *m_snapToVerticesBox   = nullptr;

    // Rendering — link pens are edited through a QPropertyModel-backed
    // QTreeView so colour, width, cap, join and dash are all exposed
    // by QPenPropertyItem's standard expandable children. Edits route
    // through LinkRenderingPrefs's Q_PROPERTY setters straight into
    // PreferencesManager::setLinkPen(), so there is no pending-state
    // to apply on OK and writeToManager() leaves them alone.
    QDoubleSpinBox     *m_labelLodSpin    = nullptr;
    LinkRenderingPrefs *m_linkPrefs       = nullptr;
    QPropertyModel     *m_linkPenModel    = nullptr;
    // Node symbols — same QPropertyModel pattern as link pens. The bridge
    // exposes pen, fill brush, and size per node type so QPenPropertyItem
    // and QBrushPropertyItem handle every sub-attribute (colour / width /
    // dash / cap / join / brush style), and a plain double row drives the
    // marker diameter.
    NodeRenderingPrefs *m_nodePrefs       = nullptr;
    QPropertyModel     *m_nodeStyleModel  = nullptr;
    QCheckBox          *m_qsgNodesBox     = nullptr;
    QCheckBox          *m_qsgMeshBox      = nullptr;

    // Simulation
    QSpinBox   *m_progressTickMsSpin    = nullptr;

    // Simulation Defaults (applied to fresh blank projects).
    QComboBox      *m_simFlowUnitsCombo       = nullptr;
    QComboBox      *m_simInfiltrationCombo    = nullptr;
    QComboBox      *m_simFlowRoutingCombo     = nullptr;
    QCheckBox      *m_simIgnoreRainfallBox    = nullptr;
    QCheckBox      *m_simIgnoreRdiiBox        = nullptr;
    QCheckBox      *m_simIgnoreSnowmeltBox    = nullptr;
    QCheckBox      *m_simIgnoreGroundwaterBox = nullptr;
    QCheckBox      *m_simIgnoreQualityBox     = nullptr;
    QCheckBox      *m_simIgnoreRoutingBox     = nullptr;
    QCheckBox      *m_simModule2DBox          = nullptr;
    QCheckBox      *m_simAllowPondingBox      = nullptr;
    QCheckBox      *m_simSkipSteadyStateBox   = nullptr;
    QDoubleSpinBox *m_simMinSlopePctSpin      = nullptr;
    QDoubleSpinBox *m_simDryDaysSpin          = nullptr;
    QSpinBox       *m_simReportStepSpin       = nullptr;   // minutes
    QSpinBox       *m_simDryStepSpin          = nullptr;   // minutes
    QSpinBox       *m_simWetStepSpin          = nullptr;   // minutes
    QSpinBox       *m_simRuleStepSpin         = nullptr;   // seconds
    QDoubleSpinBox *m_simRoutingStepSpin      = nullptr;   // seconds
    QDoubleSpinBox *m_simSysFlowTolSpin       = nullptr;
    QDoubleSpinBox *m_simLatFlowTolSpin       = nullptr;
    QSpinBox       *m_simMaxTrialsSpin        = nullptr;

    // Dynamic-Wave-specific defaults.
    QComboBox      *m_simInertialDampCombo    = nullptr;
    QComboBox      *m_simNormalFlowCombo      = nullptr;
    QComboBox      *m_simForceMainCombo       = nullptr;
    QComboBox      *m_simSurchargeCombo       = nullptr;
    QCheckBox      *m_simVariableStepBox      = nullptr;
    QDoubleSpinBox *m_simVariableStepFactorSpin = nullptr;   // 0–1
    QDoubleSpinBox *m_simMinRoutingStepSpin   = nullptr;     // seconds
    QDoubleSpinBox *m_simLengtheningStepSpin  = nullptr;     // seconds
    QDoubleSpinBox *m_simHeadToleranceSpin    = nullptr;
    QComboBox      *m_simNodeContinuityCombo  = nullptr;
    QCheckBox      *m_simAndersonAccelBox     = nullptr;
    QSpinBox       *m_simThreadsSpin          = nullptr;

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

    // Plots — default numeric precision (X / Y axis)
    QComboBox     *m_plotXFormatModeCombo     = nullptr;   // 0=Decimals, 1=Sig figs
    QSpinBox      *m_plotXPrecisionSpin       = nullptr;   // 0..10
    QComboBox     *m_plotYFormatModeCombo     = nullptr;
    QSpinBox      *m_plotYPrecisionSpin       = nullptr;

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
