/**
 * @file      preferencesdialog.h
 * @brief     user-facing editor for PreferencesManager state.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice V — user-facing editor for PreferencesManager state.
 * Categorized left-list / right-stack layout (matches VSCode /
 * macOS Preferences convention). Apply writes through the singleton,
 * which emits `preferenceChanged` so live-bound call sites refresh
 * without restart.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>

class MeshResolutionWidget;

#include <QColor>
#include <QFont>

#include "core/preferencesmanager.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;

class LinkRenderingPrefs;
class NodeRenderingPrefs;
class MeshBcRenderingPrefs;
class SelectionRenderingPrefs;
class QPropertyModel;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Build and hydrate GUI-thread application preferences, initially selecting Selection.
     * @param[in] parent Optional Qt owner; singleton PreferencesManager/ThemeManager remain
     * externally owned.
     * Most controls are drafts until Apply/OK. Rendering/selection property bridges and shortcut
     * edits persist immediately and Cancel does not roll them back; Reset also immediately resets
     * selected live style keys. No global rollback snapshot is held.
     */
    explicit PreferencesDialog(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned pages, property models/delegates and connections; does not reset
     * persistent/live preference edits.
     */
    ~PreferencesDialog() override = default;

    /*! Select the category page whose list label matches \a label
     *  (e.g. "Appearance"). No-op when no category matches. */
    /**
     * @brief Restore persisted dialog layout once, then select an exact sidebar caption.
     * @param[in] label Translated visible category label; unknown labels leave selection unchanged.
     * Missing list is a no-op.
     */
    void openAtCategory(const QString &label);

private slots:
    /**
     * @brief Validate mesh-resolution settings and write pending controls to the singleton.
     * Invalid settings show a warning and skip the batch; prior live property/shortcut changes
     * remain. Leaves the dialog open, without a rollback checkpoint.
     */
    void onApply();
    /**
     * @brief Validate mesh-resolution settings, persist pending controls and accept the dialog.
     * Invalid settings warn and leave it open; calls accept only after the complete write path.
     */
    void onAccept();
    /**
     * @brief Reset draft widgets to compiled defaults and immediately reset live
     * selection/link/node style keys.
     * Updates their property models, but does not globally clear all preferences: mesh-BC and
     * keyboard live settings are not reset here. Most remaining seeds require Apply/OK, object
     * defaults reset their local page, and worker threads reset to engine auto (zero). Cancel
     * cannot undo the live style resets.
     */
    void onResetToDefaults();

private:
    /**
     * @brief Create owned category pages, scrolling stack and Reset/Apply/Cancel/OK controls once.
     * Starts on Selection, synchronizes sidebar/stack and connects Cancel directly to reject
     * without restoring live edits.
     */
    void buildUi();
    /**
     * @brief Hydrate draft widgets and refresh live property models from the singleton.
     * Requires all page controls constructed. Resolves absent combo keys to first entries, converts
     * report/dry/wet seconds to whole minutes for display (minimum one), applies 2D seeds and loads
     * both object-default unit systems. Does not globally block signals; connected gating helpers
     * may run.
     */
    void readFromManager();
    /**
     * @brief Persist pending page values through singleton setters and apply the selected app
     * theme.
     * Requires all controls and caller mesh-resolution validation. Converts report/dry/wet minutes
     * to seconds, packages simulation/2D defaults, writes both object-default unit sets and saves
     * only nonempty trimmed naming prefixes. Live rendering/selection/keyboard edits are already
     * persisted and are not replayed. Writes are sequential, with normal notifications, not an
     * atomic transaction.
     */
    void writeToManager();
    /**
     * @brief Populate 2D solver/coupling/rainfall/mesh default controls without saving the manager.
     * @param[in] d Borrowed seed value; distances are metres, angles degrees and times seconds.
     * Resolution settings are copied, widget bounds may clamp display values, and unmatched combo
     * data falls back to index zero.
     */
    void applyTwoDDefaultsToWidgets(const PreferencesManager::TwoDDefaults &d);

    /**
     * @brief Create license-startup, auto-length, project-open folder, engine choice and
     * profile-selection defaults. Profile path cap [1,1000000], halo [1,200] px, pen widths [.5,20]
     * px; color pickers update pending swatches, not settings.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildGeneralPage();
    /**
     * @brief Create draft pick/drag thresholds [1,200] px and clear-on-miss, plus a live
     * SelectionRenderingPrefs property tree. Property edits write immediately; link selection
     * widths are additive halos.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildSelectionPage();
    /**
     * @brief Create draft initial map tool, auto/custom CRS authority/code [1,999999], and snapping
     * controls. Snap tolerance [4,64] px; gating only enables custom/snap controls, without
     * validating a CRS or changing a project.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildCanvasPage();
    /**
     * @brief Create draft label threshold [.01,100] view scale and GPU flags, with live
     * link/node/mesh-BC property trees. Builds four concern tabs; bridge setters immediately affect
     * manager settings/normal listeners, independently of Apply.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildRenderingPage();
    /**
     * @brief Create draft progress update [50,10000] ms, live-history frame cap [100,200000] and
     * budget [64,32768] MB. These are manager defaults; construction does not start or resize a
     * running history.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildSimulationPage();
    /*! \brief The Simulation Defaults page: seven inner tabs (PLAN §3).
     *
     *  Hosts what used to be the separate Dynamic Wave Defaults and 2D
     *  Defaults sidebar rows, which now fill tab layouts this hands them
     *  rather than returning pages of their own. */
    /**
     * @brief Create seven inner tabs for new-project process/schedule/hydraulic, dynamic-wave and
     * 2D seeds. Report/dry/wet spins [1,1440] minutes convert to seconds when saved; routing
     * [.1,3600] s, rule [0,86400] s (zero follows routing), slope/tolerances percent. No existing
     * model is edited.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildSimulationDefaultsPage();
    /**
     * @brief Create dynamic-wave default groups inside existing tab layouts.
     * @param[in,out] stepsLay Required borrowed layout receiving variable-step controls.
     * @param[in,out] solverLay Required borrowed layout receiving conduit/solver controls and
     * explanatory text.
     * Layouts reparent groups into their owning pages. Timing fields use seconds, factor [0,1],
     * UF_K3 [0,.05], head tolerance [1e-6,1], threads [0,256] with zero auto. Queries thread limits
     * only for UI hints; no simulation starts.
     */
    void addDynamicWaveDefaultGroups(QVBoxLayout *stepsLay, QVBoxLayout *solverLay);
    /**
     * @brief Create 2D solver, coupling and mesh seed controls for future projects.
     * @param[in,out] lay Required borrowed layout for solver/wet-dry groups.
     * @param[in,out] cplLay Required borrowed layout for coupling/rainfall/reporting.
     * @param[in,out] meshLay Required borrowed layout for resolution and geometry/elevation seeds.
     * Layouts reparent groups; distance values remain SI metres here (Generate Mesh handles project
     * conversion), time seconds and angles degrees. Numeric UI bounds and enum catalogs gate
     * inputs; no model/mesh is generated by construction.
     */
    void addTwoDDefaultGroups(QVBoxLayout *lay, QVBoxLayout *cplLay, QVBoxLayout *meshLay);
    /**
     * @brief Create the self-contained ObjectDefaultsPage; later load/apply/reset forwards both US
     * and SI seeds.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildObjectDefaultsPage();
    /**
     * @brief Create draft scale-bar color/font, unit/position choices, line width [1,20] px,
     * reference length [20,500] px and decimal [-1,4] precision (-1 auto). Dialog pickers update
     * pending values only.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildMapDisplayPage();
    /**
     * @brief Create draft measurement line/fill colors, label font/precision [0,6] and fill opacity
     * [0,100] percent. Color/font cancellation preserves pending values; no map measurement is
     * performed.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildMeasureToolPage();
    /**
     * @brief Create draft X/Y number-format presets, representing stored mode plus precision.
     * Time/date axes are unaffected; these seed newly opened charts rather than rewriting existing
     * plot overrides.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildPlotsPage();
    /**
     * @brief Create draft prefixes for the eleven supported element kinds. Placeholders show seed
     * examples; whitespace-only values are ignored on Apply and no elements are renamed.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildNamingPage();
    /**
     * @brief Create draft System/Light/Dark radio choices. ThemeManager is changed on Apply/OK, not
     * during construction.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildAppearancePage();
    /**
     * @brief Create the self-contained ShortcutEditorWidget. Edits persist immediately through
     * ActionRegistry; no Apply batch or Cancel rollback is provided here.
     * @return Newly allocated dialog-owned page (reparented into a scrolling stack); child
     * controls/models remain Qt-owned.
     */
    QWidget *buildKeyboardPage();

    QListWidget *m_categoryList = nullptr;
    QLineEdit *m_projectOpenDirectoryEdit = nullptr;
    QStackedWidget *m_pages = nullptr;

    // Object Defaults page is self-contained (see objectdefaultspage.h);
    // the dialog only forwards load/apply/reset.
    class ObjectDefaultsPage *m_objectDefaultsPage = nullptr;

    // Appearance
    QRadioButton *m_appearanceSystemRadio = nullptr;
    QRadioButton *m_appearanceLightRadio = nullptr;
    QRadioButton *m_appearanceDarkRadio = nullptr;

    // General
    QCheckBox *m_showLicenseOnStartupBox = nullptr;
    QCheckBox *m_autoLengthBox = nullptr;
    QComboBox *m_defaultEngineCombo = nullptr;
    QSpinBox *m_profileMaxPathsSpin = nullptr;
    QSpinBox *m_profileHaloRadiusSpin = nullptr;
    QPushButton *m_profileStartColorBtn = nullptr;
    QColor m_pendingProfileStartColor;
    QDoubleSpinBox *m_profileStartWidthSpin = nullptr;
    QPushButton *m_profileEndColorBtn = nullptr;
    QColor m_pendingProfileEndColor;
    QDoubleSpinBox *m_profileEndWidthSpin = nullptr;

    // Selection
    QSpinBox *m_clickTolerancePxSpin = nullptr;
    QSpinBox *m_dragThresholdPxSpin = nullptr;
    QCheckBox *m_clearOnMissBox = nullptr;
    // Per-class pens + brushes are edited through a QPropertyModel-
    // backed QTreeView. Edits route through SelectionRenderingPrefs's
    // setters directly into PreferencesManager, so writeToManager()
    // leaves them alone.
    SelectionRenderingPrefs *m_selectionPrefs = nullptr;

    // 2D mesh BC edge defaults — same QPropertyModel treatment as the
    // selection pens above.
    MeshBcRenderingPrefs *m_meshBcPrefs = nullptr;
    QPropertyModel *m_meshBcModel = nullptr;
    QPropertyModel *m_selectionModel = nullptr;

    // Canvas
    QComboBox *m_defaultToolCombo = nullptr;
    QRadioButton *m_crsAutoRadio = nullptr;
    QRadioButton *m_crsCustomRadio = nullptr;
    QLineEdit *m_crsAuthorityEdit = nullptr;
    QSpinBox *m_crsCodeSpin = nullptr;

    // Snapping
    QCheckBox *m_snapEnabledBox = nullptr;
    QSpinBox *m_snapToleranceSpin = nullptr;
    QCheckBox *m_snapToVerticesBox = nullptr;

    // Rendering — link pens are edited through a QPropertyModel-backed
    // QTreeView so colour, width, cap, join and dash are all exposed
    // by QPenPropertyItem's standard expandable children. Edits route
    // through LinkRenderingPrefs's Q_PROPERTY setters straight into
    // PreferencesManager::setLinkPen(), so there is no pending-state
    // to apply on OK and writeToManager() leaves them alone.
    QDoubleSpinBox *m_labelLodSpin = nullptr;
    LinkRenderingPrefs *m_linkPrefs = nullptr;
    QPropertyModel *m_linkPenModel = nullptr;
    // Node symbols — same QPropertyModel pattern as link pens. The bridge
    // exposes pen, fill brush, and size per node type so QPenPropertyItem
    // and QBrushPropertyItem handle every sub-attribute (colour / width /
    // dash / cap / join / brush style), and a plain double row drives the
    // marker diameter.
    NodeRenderingPrefs *m_nodePrefs = nullptr;
    QPropertyModel *m_nodeStyleModel = nullptr;
    QCheckBox *m_qsgNodesBox = nullptr;
    QCheckBox *m_qsgMeshBox = nullptr;

    // Simulation
    QSpinBox *m_progressTickMsSpin = nullptr;
    QSpinBox *m_live2DHistoryCapSpin = nullptr;
    QSpinBox *m_live2DHistoryMBSpin = nullptr;

    // Simulation Defaults (applied to fresh blank projects).
    QComboBox *m_simFlowUnitsCombo = nullptr;
    QComboBox *m_simInfiltrationCombo = nullptr;
    QComboBox *m_simFlowRoutingCombo = nullptr;
    QCheckBox *m_simIgnoreRainfallBox = nullptr;
    QCheckBox *m_simIgnoreRdiiBox = nullptr;
    QCheckBox *m_simIgnoreSnowmeltBox = nullptr;
    QCheckBox *m_simIgnoreGroundwaterBox = nullptr;
    QCheckBox *m_simIgnoreQualityBox = nullptr;
    QCheckBox *m_simIgnoreRoutingBox = nullptr;
    QCheckBox *m_simModule2DBox = nullptr;
    QCheckBox *m_simAllowPondingBox = nullptr;
    QCheckBox *m_simSkipSteadyStateBox = nullptr;
    QDoubleSpinBox *m_simMinSlopePctSpin = nullptr;
    QDoubleSpinBox *m_simDryDaysSpin = nullptr;
    QSpinBox *m_simReportStepSpin = nullptr;        // minutes
    QSpinBox *m_simDryStepSpin = nullptr;           // minutes
    QSpinBox *m_simWetStepSpin = nullptr;           // minutes
    QSpinBox *m_simRuleStepSpin = nullptr;          // seconds
    QDoubleSpinBox *m_simRoutingStepSpin = nullptr; // seconds
    QDoubleSpinBox *m_simSysFlowTolSpin = nullptr;
    QDoubleSpinBox *m_simLatFlowTolSpin = nullptr;
    QSpinBox *m_simMaxTrialsSpin = nullptr;

    // Dynamic-Wave-specific defaults.
    QComboBox *m_simInertialDampCombo = nullptr;
    QComboBox *m_simNormalFlowCombo = nullptr;
    QComboBox *m_simForceMainCombo = nullptr;
    QComboBox *m_simSurchargeCombo = nullptr;
    QCheckBox *m_simVariableStepBox = nullptr;
    QDoubleSpinBox *m_simVariableStepFactorSpin = nullptr; // 0–1
    QDoubleSpinBox *m_simMinRoutingStepSpin = nullptr;     // seconds
    QDoubleSpinBox *m_simLengtheningStepSpin = nullptr;    // seconds
    QDoubleSpinBox *m_simHeadToleranceSpin = nullptr;
    QComboBox *m_simNodeContinuityCombo = nullptr;
    QCheckBox *m_simAndersonAccelBox = nullptr;
    QSpinBox *m_simThreadsSpin = nullptr;
    QComboBox *m_simUnsteadyFrictionCombo = nullptr; // UNSTEADY_FRICTION
    QDoubleSpinBox *m_simUfK3Spin = nullptr;         // UF_K3 (0–0.05)

    // Simulation Defaults > 2D tabs — [2D_OPTIONS] solver keys plus the
    // mesh-generation seeds.
    QDoubleSpinBox *m_twoDMaxTimestepSpin = nullptr; // seconds
    QDoubleSpinBox *m_twoDThetaSpin = nullptr;
    QDoubleSpinBox *m_twoDCflSpin = nullptr;
    QSpinBox *m_twoDLtsTiersSpin = nullptr;
    QDoubleSpinBox *m_twoDHMoveSpin = nullptr;
    QDoubleSpinBox *m_twoDFroudeMaxSpin = nullptr;
    QCheckBox *m_twoDAdvectionBox = nullptr;
    QDoubleSpinBox *m_twoDDryDepthSpin = nullptr;
    QDoubleSpinBox *m_twoDLimiterEpsSpin = nullptr;
    QDoubleSpinBox *m_twoDFluxDhEpsSpin = nullptr;
    QComboBox *m_twoDCellClosureCombo = nullptr;
    QComboBox *m_twoDFaceReconCombo = nullptr;
    QDoubleSpinBox *m_twoDVfrMinWetFracSpin = nullptr;
    QDoubleSpinBox *m_twoDCouplingCdSpin = nullptr;
    QDoubleSpinBox *m_twoDCouplingSyncSpin = nullptr;
    QCheckBox *m_twoDCouplingAreaAutoBox = nullptr;
    QComboBox *m_twoDCouplingLawCombo = nullptr;
    QComboBox *m_twoDCouplingHeadCombo = nullptr;
    QComboBox *m_twoDCouplingPondCombo = nullptr;
    QComboBox *m_twoDRainfallModeCombo = nullptr;
    QCheckBox *m_twoDReport2DBox = nullptr;
    MeshResolutionWidget *m_meshResolution = nullptr;
    QDoubleSpinBox *m_twoDMeshMinAngleSpin = nullptr;
    QCheckBox *m_twoDMeshStreetQuadsBox = nullptr;
    QDoubleSpinBox *m_twoDMeshTrimTurnSpin = nullptr;
    QDoubleSpinBox *m_twoDMeshTrimDeviationSpin = nullptr;
    QDoubleSpinBox *m_twoDMeshIdwPowerSpin = nullptr;
    QDoubleSpinBox *m_twoDMeshFlattenRadSpin = nullptr;
    QCheckBox *m_twoDMeshNodesBox = nullptr;    ///< nodes → Steiner vertices
    QCheckBox *m_twoDMeshNodesRimBox = nullptr; ///< rim elevation instead of terrain
    QCheckBox *m_twoDMeshMinSepBox = nullptr;
    QDoubleSpinBox *m_twoDMeshMinSepSpin = nullptr;
    QDoubleSpinBox *m_twoDMeshManningsSpin = nullptr;
    QDoubleSpinBox *m_twoDMeshInitDepthSpin = nullptr;
    QCheckBox *m_twoDMeshOutputExternalBox = nullptr;

    // Map Display / Scale Bar
    QPushButton *m_scaleBarColorBtn = nullptr;
    QColor m_pendingScaleBarColor;
    QSpinBox *m_scaleBarPenWidthSpin = nullptr;
    QComboBox *m_scaleBarPenStyleCombo = nullptr;
    QPushButton *m_scaleBarFontBtn = nullptr;
    QFont m_pendingScaleBarFont;
    QComboBox *m_scaleBarUnitsCombo = nullptr;
    QComboBox *m_scaleBarPositionCombo = nullptr;
    QSpinBox *m_scaleBarMaxBarLengthSpin = nullptr;
    QSpinBox *m_scaleBarLabelDecimalsSpin = nullptr; // -1..4
    QCheckBox *m_scaleBarCompactNotationBox = nullptr;

    // Measure Tool
    QPushButton *m_measureLineColorBtn = nullptr;
    QColor m_pendingMeasureLineColor;
    QPushButton *m_measureLabelFontBtn = nullptr;
    QFont m_pendingMeasureLabelFont;
    QSpinBox *m_measureLabelDecimalsSpin = nullptr;
    QPushButton *m_measureFillColorBtn = nullptr;
    QColor m_pendingMeasureFillColor;
    QSpinBox *m_measureFillOpacitySpin = nullptr;

    // Plots — default numeric precision (X / Y axis)
    // One dropdown per axis carrying an openswmmvis::plot::
    // AxisNumberFormatPreset in each item's data. The preference itself is
    // still stored as a mode + digit count, so no settings migration.
    QComboBox *m_plotXFormatCombo = nullptr;
    QComboBox *m_plotYFormatCombo = nullptr;

    // Naming prefixes (one QLineEdit per element kind)
    QLineEdit *m_prefixJunction = nullptr;
    QLineEdit *m_prefixOutfall = nullptr;
    QLineEdit *m_prefixStorage = nullptr;
    QLineEdit *m_prefixDivider = nullptr;
    QLineEdit *m_prefixConduit = nullptr;
    QLineEdit *m_prefixPump = nullptr;
    QLineEdit *m_prefixOrifice = nullptr;
    QLineEdit *m_prefixWeir = nullptr;
    QLineEdit *m_prefixOutlet = nullptr;
    QLineEdit *m_prefixRaingage = nullptr;
    QLineEdit *m_prefixSubcatchment = nullptr;
};

#endif // PREFERENCESDIALOG_H
