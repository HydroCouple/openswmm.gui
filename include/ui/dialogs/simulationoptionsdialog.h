/*!
 * \file   simulationoptionsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Phase 3.9 (slice G-1) — SimulationOptionsDialog Tabs 1–2.
 * Round-trips OPTIONS keys via the engine's `swmm_options_get/set` C API.
 * Subsequent slices add Tabs 3–7 (Routing, Performance, CRS, 2D, Files).
 */
#ifndef SIMULATIONOPTIONSDIALOG_H
#define SIMULATIONOPTIONSDIALOG_H

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTabWidget;
class QToolButton;

class SWMMModelLayer;

#include <openswmm/engine/openswmm_engine.h>

/*!
 * \class SimulationOptionsDialog
 * \brief Edit OPTIONS for the active SWMM project.
 *
 * Round-trip layout:
 *   - On construction: read every option from the engine via
 *     `swmm_options_get` and populate the controls.
 *   - On **Apply** / **OK**: diff against the last-read snapshot and write
 *     only changed keys via `swmm_options_set`. Marks the project dirty if
 *     any key was written.
 *   - On **Cancel**: discard pending edits.
 */
class SimulationOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param engine  Open SWMM engine handle (required).
     * \param layer   Optional model layer — gives Tab 5 access to the layer
     *                CRS + extent for the *Detect from coordinates* helper
     *                and the read-only extent summary.
     * \param parent  Qt parent.
     */
    explicit SimulationOptionsDialog(SWMM_Engine engine,
                                     SWMMModelLayer *layer = nullptr,
                                     QWidget *parent = nullptr);
    // Inline so leaf tests that only compile simulationoptionshelpers.cpp can
    // still link the moc-generated vtable. Defining it out-of-line in the
    // main .cpp would force the test to drag in the entire dialog (and its
    // OGR/GDAL deps via the spatial-tab code).
    ~SimulationOptionsDialog() override = default;

    /*! \brief True after a successful Apply / OK that wrote at least one key. */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteChanges; }

    // ---- Pure helpers (testable without an engine) ------------------------

    /*! \brief Map an engine boolean string ("YES"/"NO"/"TRUE"/"FALSE"/"1"/"0")
     *         to a Qt::CheckState. Unknown values → Qt::PartiallyChecked. */
    [[nodiscard]] static int parseEngineBool(const QString &s);

    /*! \brief Render a checkbox state as the canonical engine string. */
    [[nodiscard]] static QString engineBoolString(bool on);

    /*! \brief Format a Qt date+time as the engine's expected MM/DD/YYYY +
     *         HH:MM:SS pair (returned as `out_date` and `out_time`). */
    static void formatEngineDateTime(const QDateTime &dt,
                                     QString &out_date,
                                     QString &out_time);

    /*! \brief Inverse of formatEngineDateTime. Returns an invalid QDateTime if
     *         either string is malformed. */
    [[nodiscard]] static QDateTime parseEngineDateTime(const QString &date,
                                                       const QString &time);

private slots:
    void onApply();
    void onAccept();

private:
    void buildUi();
    void buildModelsTab(QTabWidget *tabs);
    void buildDatesTab(QTabWidget *tabs);
    void buildHydraulicsTab(QTabWidget *tabs);
    void buildPerformanceTab(QTabWidget *tabs);
    void buildSpatialTab(QTabWidget *tabs);
    void buildMeshTab(QTabWidget *tabs);
    void refreshMeshList();   ///< rescan project dir for *.2dm files

#ifdef OPENSWMM_HAS_2D
    void build2DTab(QTabWidget *tabs);
    void read2DFromEngine();
    int  write2DToEngine(int &n);
#endif

    void readFromEngine();
    int  writeToEngine();   ///< returns count of keys written

    /*! \brief Enable / disable the DPS_* row group based on the surcharge
     *         method selection. Called whenever the combo changes. */
    void updateSurchargeFieldsEnabled();

    /*! \brief Refresh the CRS row text and the read-only extent summary
     *         from the layer; called on construction and after a CRS pick. */
    void refreshSpatialSummary();

private slots:
    void onSpatialPickCRS();
    void onSpatialDetectCRS();
    void on2DModuleToggled(bool enabled);

private:

    // Engine helpers — round-trip option values through swmm_options_get / _set.
    QString  getOption(const char *key, const QString &fallback = {}) const;
    bool     setOption(const char *key, const QString &value);

    SWMM_Engine     m_engine = nullptr;
    SWMMModelLayer *m_layer  = nullptr;
    bool            m_wroteChanges = false;

    // Tab 1 — Models / Processes
    QComboBox      *m_infiltrationCombo = nullptr;
    QComboBox      *m_routingCombo      = nullptr;
    QCheckBox      *m_allowPondingBox   = nullptr;
    QCheckBox      *m_skipSteadyBox     = nullptr;
    QCheckBox      *m_ignoreRainfallBox = nullptr;
    QCheckBox      *m_ignoreSnowmeltBox = nullptr;
    QCheckBox      *m_ignoreGroundwaterBox = nullptr;
    QCheckBox      *m_ignoreRDIIBox     = nullptr;
    QCheckBox      *m_ignoreQualityBox  = nullptr;
    QCheckBox      *m_ignoreRoutingBox  = nullptr;

    // Tab 1 — Modules group (2D toggle)
    QCheckBox      *m_module1DBox       = nullptr;   ///< Always-on, disabled (1D core).
    QCheckBox      *m_module2DBox       = nullptr;   ///< Toggle 2D surface routing.
    int             m_2DTabIndex        = -1;        ///< -1 if 2D tab not built.
    int             m_meshTabIndex      = -1;        ///< Mesh-configurations tab.
    QTabWidget     *m_tabs              = nullptr;   ///< Captured for runtime tab-enable.

    // Mesh configurations tab — Slice AU module toggle.
    class QListWidget *m_meshList         = nullptr; ///< *.2dm files in project dir.
    class QLabel      *m_meshActiveLabel  = nullptr; ///< Currently-active [2D_MESH_FILE].
    class QLabel      *m_meshDirLabel     = nullptr; ///< Project mesh-search directory.

    // Tab 2 — Dates & Times
    QDateTimeEdit  *m_startEdit         = nullptr;
    QDateTimeEdit  *m_endEdit           = nullptr;
    QDateTimeEdit  *m_reportStartEdit   = nullptr;
    QSpinBox       *m_reportStepSpin    = nullptr;     // seconds
    QSpinBox       *m_dryStepSpin       = nullptr;     // seconds
    QSpinBox       *m_wetStepSpin       = nullptr;     // seconds
    QDoubleSpinBox *m_dryDaysSpin       = nullptr;     // days
    QDoubleSpinBox *m_routingStepSpin   = nullptr;     // seconds

    // Tab 3 — Routing & Hydraulics
    QComboBox      *m_surchargeCombo    = nullptr;
    QDoubleSpinBox *m_dpsCelerSpin      = nullptr;
    QDoubleSpinBox *m_dpsAlphaSpin      = nullptr;
    QDoubleSpinBox *m_dpsDecaySpin      = nullptr;
    QComboBox      *m_nodeContinuityCombo = nullptr;
    QCheckBox      *m_andersonAccelBox  = nullptr;
    QComboBox      *m_forceMainCombo    = nullptr;
    QComboBox      *m_normalFlowCombo   = nullptr;
    QComboBox      *m_inertialDampCombo = nullptr;
    QDoubleSpinBox *m_lengtheningSpin   = nullptr;
    QDoubleSpinBox *m_variableStepSpin  = nullptr;
    QSpinBox       *m_maxTrialsSpin     = nullptr;
    QDoubleSpinBox *m_headTolSpin       = nullptr;
    QDoubleSpinBox *m_latFlowTolSpin    = nullptr;     // percent
    QDoubleSpinBox *m_sysFlowTolSpin    = nullptr;     // percent
    QDoubleSpinBox *m_minSurfAreaSpin   = nullptr;
    QDoubleSpinBox *m_minSlopeSpin      = nullptr;     // percent

    // Tab 4 — System / Performance
    QSpinBox       *m_threadsSpin       = nullptr;

    // Tab 5 — Spatial & CRS
    QLabel         *m_crsLabel          = nullptr;
    QToolButton    *m_crsChangeButton   = nullptr;
    QToolButton    *m_crsDetectButton   = nullptr;
    QLabel         *m_extentLabel       = nullptr;

#ifdef OPENSWMM_HAS_2D
    // Tab 6 — 2D Surface Routing (CVODE / mesh / coupling / linear solver)
    QDoubleSpinBox *m_cvodeMaxStepSpin  = nullptr;
    QDoubleSpinBox *m_cvodeMinStepSpin  = nullptr;
    QDoubleSpinBox *m_cvodeRelTolSpin   = nullptr;
    QDoubleSpinBox *m_cvodeAbsTolSpin   = nullptr;
    QSpinBox       *m_cvodeMaxStepsSpin = nullptr;
    QDoubleSpinBox *m_dryDepthSpin      = nullptr;
    QDoubleSpinBox *m_limiterEpsSpin    = nullptr;
    QDoubleSpinBox *m_couplingCdSpin    = nullptr;
    QDoubleSpinBox *m_couplingIntervalSpin = nullptr;
    QComboBox      *m_linearSolverCombo = nullptr;
    QComboBox      *m_preconditionerCombo = nullptr;
    QSpinBox       *m_maxKrylovDimSpin  = nullptr;
    QCheckBox      *m_report2DBox       = nullptr;
#endif
};

#endif // SIMULATIONOPTIONSDIALOG_H
