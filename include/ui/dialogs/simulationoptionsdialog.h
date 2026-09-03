/*!
 * \file   simulationoptionsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 3.9 (slice G-1) — SimulationOptionsDialog Tabs 1–2.
 * Round-trips OPTIONS keys via the engine's `swmm_options_get/set` C API.
 * Subsequent slices add Tabs 3–7 (Routing, Performance, CRS, 2D, Files).
 */
#ifndef SIMULATIONOPTIONSDIALOG_H
#define SIMULATIONOPTIONSDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QDateEdit;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTableWidget;
class QTabWidget;
class QListWidget;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QAction;
class QButtonGroup;
class QRadioButton;
class QCustomTimespanEdit;

class SWMMModelLayer;
class SWMMVisProjectWindow;
class HotstartSavesModel;
class HotstartSavesDateTimeDelegate;
class PathBrowseDelegate;
class PluginsTableModel;

namespace openswmmvis::ui {
    class RelativePathPicker;
}

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
     * \param engine        Open SWMM engine handle (required).
     * \param layer         Optional model layer — gives Tab 5 access to the layer
     *                      CRS + extent for the *Detect from coordinates* helper
     *                      and the read-only extent summary.
     * \param engineVersion Version string of the engine that will run the
     *                      simulation (e.g. "6.0.0" or "5.2.4").  Controls
     *                      which tabs and controls are enabled — options that
     *                      the selected engine does not support are disabled
     *                      with an explanatory tooltip.
     * \param projectWindow Optional MDI window — provides .oswp-persisted rich
     *                      HTML notes for the new "Title/Notes" tab. When
     *                      omitted, the tab still works but only round-trips
     *                      plain text through the engine.
     * \param parent        Qt parent.
     */
    explicit SimulationOptionsDialog(SWMM_Engine engine,
                                     SWMMModelLayer *layer = nullptr,
                                     const QString &engineVersion = QStringLiteral("6.0.0"),
                                     SWMMVisProjectWindow *projectWindow = nullptr,
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

    /*! \brief Canonical "fast preset" recipe for 1D/2D-coupled runs: all worker
     *         threads on, and the adaptive routing step floored so the 2D
     *         coupling can't collapse it. Benchmarked ~4x faster with a ~4–5%
     *         peak-depth trade on the Bellinge model (see FAST_RUN_RECIPE.md).
     *         Kept as a static so the recipe values are locked by a unit test. */
    static void fastPresetValues(int &out_threads, double &out_min_step_sec);

    /*! \brief THREADS the fast preset uses: the machine's performance-core
     *         count (macOS), else its logical-CPU count, else 8. */
    static int fastPresetThreads();

    /*! \brief Human-readable summary of the machine / OpenMP thread limits
     *         (from swmm_get_thread_info) for tooltips. */
    static QString threadLimitsSummary(const SWMM_ThreadInfo &ti);

    /*! \brief Format a Qt date+time as the engine's expected MM/DD/YYYY +
     *         HH:MM:SS pair (returned as `out_date` and `out_time`). */
    static void formatEngineDateTime(const QDateTime &dt,
                                     QString &out_date,
                                     QString &out_time);

    /*! \brief Inverse of formatEngineDateTime. Returns an invalid QDateTime if
     *         either string is malformed. */
    [[nodiscard]] static QDateTime parseEngineDateTime(const QString &date,
                                                       const QString &time);

    /*! \brief Convert a QDateTime to SWMM's OLE Automation Date (decimal days
     *         since 1899-12-30 00:00).  Used by the [EVENTS] section editor
     *         to round-trip through the swmm_events_* C API which speaks
     *         OADate directly.  Returns 0.0 for invalid input. */
    [[nodiscard]] static double    oaDateFromQDateTime(const QDateTime &dt);

    /*! \brief Inverse of oaDateFromQDateTime. */
    [[nodiscard]] static QDateTime qDateTimeFromOaDate(double oa);

    /*! \brief Parse a step value as returned by swmm_options_get() into whole
     *         seconds. The engine round-trip is loose: a step comes back as
     *         plain seconds ("900"), decimal seconds ("900.000000" — the
     *         `std::to_string(double)` form used for REPORT_STEP /
     *         ROUTING_STEP) or as HH:MM:SS ("00:15:00", "48:00:00").
     *         Returns \a fallback when \a s matches none of those. */
    [[nodiscard]] static qint64 parseStepSeconds(const QString &s,
                                                 qint64 fallback);

    /*! \brief Compare an option value from swmm_options_get() against a
     *         freshly formatted one, tolerating formatting differences.
     *         The engine renders numerics as `std::to_string(double)`
     *         ("0.000000") while the dialog formats with 'f'/'g' variants
     *         ("0.00"), so a plain string compare treats every unchanged
     *         numeric as an edit. Exact string equality → true; else if both
     *         sides parse as doubles they compare with a relative tolerance;
     *         else false. */
    [[nodiscard]] static bool optionValueEquals(const QString &a,
                                                const QString &b);

    /*! \brief Distinct selected row indices of \a table, sorted descending
     *         (safe order for removeRow()).  Reads the selection MODEL first —
     *         the [EVENTS] table populates cells exclusively with
     *         setCellWidget() editors, so item-based queries like
     *         selectedItems() see an always-empty selection — then falls back
     *         to selectedItems() for plain cell selections. */
    [[nodiscard]] static QList<int> selectedRowsDescending(
        const QTableWidget *table);

private slots:
    void onApply();
    void onAccept();

private:
    /*! Disable controls that the currently-selected engine does not support.
     *  Called once after buildUi() + readFromEngine() and re-callable when
     *  the engine version changes. */
    void applyEngineConstraints();

    void buildUi();
    /*! Register a sidebar row + stacked page (page wrapped in a scroll area). */
    void addCategory(const QString &title, QWidget *page);
    /*! Enable/disable the 2D Surface Routing sidebar row (QStackedWidget has
     *  no per-page enabled state, so gate at the list row). */
    void set2DRowEnabled(bool enabled);
    // Each build*Tab returns its page widget; buildUi adds it via addCategory.
    QWidget *buildTitleNotesTab();
    QWidget *buildModelsTab();
    QWidget *buildDatesTab();
    QWidget *buildHydraulicsTab();
    QWidget *buildQualityTransportTab();   ///< Y1 (G1g) — quality/transport options
    QWidget *buildPerformanceTab();
    QWidget *buildSpatialTab();
    QWidget *buildMeshTab();
    QWidget *buildFilesTab();   ///< Slice AA-3.5 — [PLUGINS] + [FILES] editor
    void readOutputPathsFromSettings();      ///< Slice AA-4 — per-project rpt/out paths
    void writeOutputPathsToSettings();       ///< Slice AA-4 — per-project rpt/out paths
    void refreshMeshList();                  ///< rescan project dir for *.2dm files
    void readPluginsFromEngine();
    int  writePluginsToEngine();             ///< returns count of changes written
    void readFilesSectionFromEngine();
    int  writeFilesSectionToEngine();        ///< returns count of changes written
    void readWriterCombosFromEngine();       ///< hydrate Input/Output/Report combos
    int  writeWriterCombosToEngine();        ///< returns count of [PLUGINS] rows added
    void updateSingleContainerEnabled();     ///< enable when chosen input plugin is tri-role
    void onSingleContainerToggled(bool on);  ///< force Output+Report combos to match input

#ifdef OPENSWMM_HAS_2D
    QWidget *build2DTab();
    void read2DFromEngine();
    int  write2DToEngine(int &n);
#endif

    void readFromEngine();
    int  writeToEngine();   ///< returns count of keys written

    // ---- [REPORT] contents editor — Slice BV.1 (2026-05-22) ---------------

    /*! \brief Add the "Report contents ([REPORT])" group to the given parent
     *         layout in buildFilesTab. */
    void buildReportContentsGroup(class QVBoxLayout *parentLayout, QWidget *page);

    /*! \brief Populate the report-contents widgets from the engine's
     *         RPT_* options keys. */
    void readReportContentsFromEngine();

    /*! \brief Write the report-contents widgets back through the engine's
     *         RPT_* options keys. Returns the count of keys whose value
     *         changed (folded into writeToEngine's running total). */
    int  writeReportContentsToEngine();

    // ---- [EVENTS] section editor — Slice CW (2026-05-21) ------------------

    /*! \brief Populate the Events table from swmm_events_count/get. */
    void readEventsFromEngine();

    /*! \brief Diff the Events table against the read-time snapshot; on any
     *         difference, clear + re-add via swmm_events_*. Returns the
     *         number of rows written (0 when unchanged). */
    int  writeEventsToEngine();

    /*! \brief Validate all event rows. Highlights invalid rows (Start >= End)
     *         in red and returns false if any row is invalid. Out-of-range
     *         and overlapping rows yield a non-blocking warning via @p warn.
     *         Not const — mutates per-cell styling for the inline error
     *         affordance. */
    bool validateEvents(QString *warn = nullptr);

    // ---- Files / Output / Plugins tab validation — Phase 3.10.4 -----------

    /*! \brief Validate the Files / Output / Plugins sub-tabs. Blocks Apply
     *         when any [PLUGINS] row has an empty plugin id (column 0) or
     *         any scheduled hot-start save row has an empty path (column
     *         0); rows are highlighted red for the inline error affordance.
     *         Non-blocking warnings (out-of-range datetimes, "Selected"
     *         report selector with empty list, .rpt / .out parent
     *         directory missing) are appended to @p warn.  Returns false
     *         only on blocking errors.  Not const — mutates per-cell
     *         styling. */
    bool validateFilesTab(QString *warn = nullptr);

    /*! \brief Append a new row defaulted to (project start, project end). */
    void addEventRow();

    /*! \brief Remove all selected rows. No-op when nothing is selected. */
    void removeSelectedEventRows();

    /*! \brief Snapshot of rows as last read from the engine — used for
     *         change detection in writeEventsToEngine(). */
    QList<QPair<QDateTime, QDateTime>> m_eventsSnapshot;

    /*! \brief Enable / disable the DPS_* row group based on the surcharge
     *         method selection. Called whenever the combo changes. */
    void updateSurchargeFieldsEnabled();

    /*! \brief Enable / disable the finite-volume option groups based on the
     *         flow-routing selection (FLOW_ROUTING FV), plus the intra-group
     *         dependencies (limiter needs 2nd order, LTS tiers need LTS).
     *         Called whenever the routing combo changes. */
    void updateFvFieldsEnabled();
    /*! Y1 — gate the per-engine transport groups on the solver combo, and
     *  the RWPT seed on the dispersion combo (updateFvFieldsEnabled idiom). */
    void updateQualitySolverFieldsEnabled();

    /*! \brief Refresh the "End +" duration label from Start/End edits.
     *         Format: "Xd HH:MM:SS" or "—" when End <= Start. */
    void updateDurationLabel();

    /*! \brief Refresh the CRS row text and the read-only extent summary
     *         from the layer; called on construction and after a CRS pick. */
    void refreshSpatialSummary();

private slots:
    void onSpatialPickCRS();
    void onSpatialDetectCRS();
    void onMeshSetActive();   ///< Retarget [2D_MESH_FILE] at the selected .2dm.
    void onMeshRemove();      ///< Delete the selected .2dm from disk.
    void onMeshImport();      ///< Browse for a .2dm anywhere and load it here.
    void browseForReportFile();
    void browseForOutputFile();
    void on2DModuleToggled(bool enabled);

    // Multi-row SAVE HOTSTART table slots (Slice BV-01).
    void onHotstartSaveAddRow();
    void onHotstartSaveRemoveRow();
    void onHotstartSaveBrowseRow();
    void onHotstartSaveMoveRowUp();
    void onHotstartSaveMoveRowDown();

private:

    // Swap the path/datetime contents of two rows in m_hotstartSavesTable
    // (avoids tearing down cell widgets that QTableWidget owns).
    void moveHotstartSaveRow(int from, int to);

    // Engine helpers — round-trip option values through swmm_options_get / _set.
    QString  getOption(const char *key, const QString &fallback = {}) const;
    bool     setOption(const char *key, const QString &value);

    SWMM_Engine     m_engine = nullptr;
    SWMMModelLayer *m_layer  = nullptr;
    SWMMVisProjectWindow *m_projectWindow = nullptr;  ///< owner of .oswp-persisted notes
    QString         m_engineVersion;        ///< e.g. "6.0.0" or "5.2.4"
    bool            m_wroteChanges = false;

    // Tab 0 — Title / Notes (rich text mirror of engine [TITLE] section)
    QTextEdit      *m_titleNotesEdit       = nullptr;
    QString         m_initialNotesHtml;     ///< snapshot for dirty detection
    QAction        *m_titleBoldAction      = nullptr;
    QAction        *m_titleItalicAction    = nullptr;
    QAction        *m_titleUnderlineAction = nullptr;

    // Group-box handles kept so applyEngineConstraints() can disable entire
    // sections (incl. their labels) with a single setEnabled() call.
    class QGroupBox *m_writersGroup = nullptr;  ///< Tab 7 — writer / container group.

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
    int             m_2DRow             = -1;        ///< 2D page sidebar row (-1 if not built).
    int             m_meshRow           = -1;        ///< Mesh-configurations sidebar row.
    QListWidget    *m_categoryList      = nullptr;   ///< Left sidebar (page selector).
    QStackedWidget *m_pages             = nullptr;   ///< Right page stack.

    // Mesh configurations tab — Slice AU module toggle.
    class QListWidget *m_meshList         = nullptr; ///< *.2dm files in project dir.
    class QLabel      *m_meshActiveLabel  = nullptr; ///< Currently-active [2D_MESH_FILE].
    class QLabel      *m_meshDirLabel     = nullptr; ///< Project mesh-search directory.

    // Tab 2 — Dates & Times
    QDateTimeEdit  *m_startEdit         = nullptr;
    QDateTimeEdit  *m_endEdit           = nullptr;
    QDateTimeEdit  *m_reportStartEdit   = nullptr;
    QLabel         *m_durationLabel     = nullptr;     // "1d 02:30:00"
    QCustomTimespanEdit *m_reportStepEdit = nullptr;   // (days, HH:mm:ss)
    QCustomTimespanEdit *m_dryStepEdit    = nullptr;   // (days, HH:mm:ss)
    QCustomTimespanEdit *m_wetStepEdit    = nullptr;   // (days, HH:mm:ss)
    QCustomTimespanEdit *m_ruleStepEdit = nullptr;     // (days, HH:mm:ss)
    QLineEdit      *m_routingStepEdit   = nullptr;     // seconds (float text)
    QDoubleSpinBox *m_dryDaysSpin       = nullptr;     // days
    QDateEdit      *m_sweepStartEdit    = nullptr;     // MM/DD only
    QDateEdit      *m_sweepEndEdit      = nullptr;     // MM/DD only

    // Tab 2 — Events ([EVENTS] section editor, Slice CW).  Each row is a
    // {start, end} QDateTime pair; engine stores decimal-day pairs in
    // SimulationContext::events round-tripped via swmm_events_*.
    QTableWidget   *m_eventsTable       = nullptr;
    QPushButton    *m_eventsAddBtn      = nullptr;
    QPushButton    *m_eventsRemoveBtn   = nullptr;

    // Tab 3 — Routing & Hydraulics
    QComboBox      *m_surchargeCombo    = nullptr;
    QDoubleSpinBox *m_dpsCelerSpin      = nullptr;
    QDoubleSpinBox *m_dpsAlphaSpin      = nullptr;
    QDoubleSpinBox *m_dpsDecaySpin      = nullptr;
    QDoubleSpinBox *m_tpaCeleritySpin   = nullptr;   // TPA_CELERITY (TPA only)
    QComboBox      *m_nodeContinuityCombo = nullptr;
    QCheckBox      *m_andersonAccelBox  = nullptr;
    QComboBox      *m_forceMainCombo    = nullptr;
    QComboBox      *m_normalFlowCombo   = nullptr;
    QComboBox      *m_inertialDampCombo = nullptr;
    QDoubleSpinBox *m_lengtheningSpin   = nullptr;
    QDoubleSpinBox *m_variableStepSpin  = nullptr;
    QDoubleSpinBox *m_minStepSpin       = nullptr;     // MINIMUM_STEP (seconds)
    QSpinBox       *m_maxTrialsSpin     = nullptr;
    QDoubleSpinBox *m_headTolSpin       = nullptr;
    QDoubleSpinBox *m_latFlowTolSpin    = nullptr;     // percent
    QDoubleSpinBox *m_sysFlowTolSpin    = nullptr;     // percent
    QDoubleSpinBox *m_minSurfAreaSpin   = nullptr;
    QDoubleSpinBox *m_minSlopeSpin      = nullptr;     // percent

    // Quality & Transport page (Y1 / GUI plan G1g). The groups are members
    // so updateQualitySolverFieldsEnabled() can gate whole sections on the
    // solver selection — the updateFvFieldsEnabled() idiom.
    QComboBox      *m_qualitySolverCombo  = nullptr;   // QUALITY_SOLVER
    QComboBox      *m_outfallBackflowCombo = nullptr;  // OUTFALL_BACKFLOW_QUALITY
    class QGroupBox *m_ardGroup           = nullptr;   // EULERIAN_ARD only
    class QGroupBox *m_lardGroup          = nullptr;   // LAGRANGIAN only
    QDoubleSpinBox *m_qualityStepSpin     = nullptr;   // QUALITY_STEP (s)
    QSpinBox       *m_maxSegmentsSpin     = nullptr;   // MAX_SEGMENTS_PER_LINK
    QComboBox      *m_dispersionCombo     = nullptr;   // DISPERSION OFF|RWPT
    QSpinBox       *m_rwptSeedSpin        = nullptr;   // RWPT_SEED (RWPT only)
    QCheckBox      *m_waterAgeBox         = nullptr;   // WATER_AGE
    QCheckBox      *m_heatTransportBox    = nullptr;   // HEAT_TRANSPORT
    QComboBox      *m_fvScalarSchemeCombo = nullptr;   // FV_SCALAR_SCHEME — on the Q&T page; the ARD engine reads it under any routing model

    // Tab 3 — Finite volume solver (FLOW_ROUTING FV). Both groups are kept
    // as members so updateFvFieldsEnabled() can gate whole sections on the
    // routing-combo selection with a single setEnabled() call each.
    class QGroupBox *m_fvGroup            = nullptr;
    class QGroupBox *m_fvPerfGroup        = nullptr;
    QDoubleSpinBox *m_fvCellLengthSpin    = nullptr;   // project length units; 0 = one cell/conduit
    QSpinBox       *m_fvMinCellsSpin      = nullptr;
    QDoubleSpinBox *m_fvCflSpin           = nullptr;
    QComboBox      *m_fvRiemannCombo      = nullptr;
    QComboBox      *m_fvOrderCombo        = nullptr;
    QComboBox      *m_fvLimiterCombo      = nullptr;   // 2nd order only
    QComboBox      *m_fvTimeIntCombo      = nullptr;
    QDoubleSpinBox *m_fvSlotCeleritySpin  = nullptr;   // project length units / s
    QComboBox      *m_fvPressureClosureCombo = nullptr; // FV_PRESSURE_CLOSURE (SLOT|TPA)
    QCheckBox      *m_fvPressImplicitBox  = nullptr;   // FV_PRESSURIZED_IMPLICIT (experimental)
    QComboBox      *m_fvStructCouplingCombo = nullptr;
    QCheckBox      *m_fvCompactionBox     = nullptr;
    QComboBox      *m_fvBackendCombo      = nullptr;
    QSpinBox       *m_fvMinParallelSpin   = nullptr;
    QCheckBox      *m_fvLtsBox            = nullptr;
    QSpinBox       *m_fvLtsTiersSpin      = nullptr;   // needs LTS on
    QSpinBox       *m_fvCflCensusSpin     = nullptr;

    // Tab 3 — Unsteady friction (engine issue #156; GUI issue #10). Applies
    // to BOTH dynamic-wave and FV routing, so it is a separate group gated
    // on FLOW_ROUTING ∈ {DYNWAVE, FV} in updateFvFieldsEnabled(). The
    // m_ufSupported flag carries the applyEngineConstraints() capability
    // probe into that gate so a routing-combo change cannot re-enable the
    // group on an engine that lacks the keys.
    class QGroupBox *m_ufGroup            = nullptr;
    QComboBox      *m_ufMethodCombo       = nullptr;   // UNSTEADY_FRICTION (NONE|VITKOVSKY)
    QDoubleSpinBox *m_ufK3Spin            = nullptr;   // UF_K3 (method != NONE only)
    bool            m_ufSupported         = true;

    // Tab 4 — System / Performance
    QSpinBox       *m_threadsSpin       = nullptr;
    QLabel         *m_threadsEffective  = nullptr;   // "Effective: 1D N · 2D N" + oversubscription flag
    SWMM_ThreadInfo m_threadInfo{};                  // machine / OpenMP limits (filled once)
    void refreshThreadsEffectiveLabel();

    // Tab 5 — Spatial & CRS
    QLabel         *m_crsLabel          = nullptr;
    QToolButton    *m_crsChangeButton   = nullptr;
    QToolButton    *m_crsDetectButton   = nullptr;
    QLabel         *m_extentLabel       = nullptr;

    // Tab 7 — Writer / Container combos (Slice AA-3.5 full design)
    QComboBox      *m_inputWriterCombo  = nullptr;
    QComboBox      *m_outputWriterCombo = nullptr;
    QComboBox      *m_reportWriterCombo = nullptr;
    QCheckBox      *m_singleContainerBox = nullptr;

    // Tab 7 — Output / Report file paths (Slice AA-4)
    QLineEdit      *m_reportFilePathEdit = nullptr;
    QLineEdit      *m_outputFilePathEdit = nullptr;

    // Tab 7 — Report contents ([REPORT] section, Slice BV.1 — 2026-05-22).
    // Six bool flags + three NONE/ALL/Selected radio groups with name
    // lists.  Round-trips via the engine's RPT_* keys exposed through
    // swmm_options_get / swmm_options_set.
    QCheckBox      *m_rptDisabledBox    = nullptr;
    // REPORT_SIGNED_HEADS ([OPTIONS], engine issue #156 O-6): .out HEAD
    // carries signed piezometric head; DEPTH stays floored. Lives with the
    // report-contents toggles but is NOT part of the RPT_DISABLED
    // short-circuit — it shapes the binary output, not the .rpt report.
    QCheckBox      *m_signedHeadsCheck  = nullptr;
    QCheckBox      *m_rptInputBox       = nullptr;
    QCheckBox      *m_rptContinuityBox  = nullptr;
    QCheckBox      *m_rptFlowstatsBox   = nullptr;
    QCheckBox      *m_rptControlsBox    = nullptr;
    QCheckBox      *m_rptAveragesBox    = nullptr;
    // Selector trios: radios + the comma-separated name list edit.
    QRadioButton   *m_rptSubcatchNoneRadio = nullptr;
    QRadioButton   *m_rptSubcatchAllRadio  = nullptr;
    QRadioButton   *m_rptSubcatchSomeRadio = nullptr;
    QLineEdit      *m_rptSubcatchListEdit  = nullptr;
    QRadioButton   *m_rptNodeNoneRadio  = nullptr;
    QRadioButton   *m_rptNodeAllRadio   = nullptr;
    QRadioButton   *m_rptNodeSomeRadio  = nullptr;
    QLineEdit      *m_rptNodeListEdit   = nullptr;
    QRadioButton   *m_rptLinkNoneRadio  = nullptr;
    QRadioButton   *m_rptLinkAllRadio   = nullptr;
    QRadioButton   *m_rptLinkSomeRadio  = nullptr;
    QLineEdit      *m_rptLinkListEdit   = nullptr;

    // Tab 7 — Files / Plugins (Slice AA-3.5)
    //
    // Phase 3.10.6 (2026-05-22): same MVC overhaul as the hot-start saves
    // table.  Column 0 (plugin path / id) uses PathBrowseDelegate so each
    // row carries an inline "…" browse button that opens an Open-file
    // dialog with the platform's shared-library filter.  Column 1
    // (arguments) uses the default QLineEdit delegate.
    QTableView         *m_pluginsView       = nullptr;
    PluginsTableModel  *m_pluginsModel      = nullptr;
    PathBrowseDelegate *m_pluginsPathDel    = nullptr;
    QPushButton        *m_pluginsAddBtn     = nullptr;
    QPushButton        *m_pluginsRemoveBtn  = nullptr;

    // Tab 7 — Secondary file references (Slice AA-3 [FILES] follow-up).
    // Slice IO-11a — paths now flow through RelativePathPicker so the
    // dialog displays each token relative to the project anchor and
    // emits the absolute form for round-trip with the engine C-API.
    openswmmvis::ui::RelativePathPicker *m_rainfallPathEdit  = nullptr;
    QComboBox                           *m_rainfallModeCombo = nullptr;
    openswmmvis::ui::RelativePathPicker *m_runoffPathEdit    = nullptr;
    QComboBox                           *m_runoffModeCombo   = nullptr;
    openswmmvis::ui::RelativePathPicker *m_rdiiPathEdit      = nullptr;
    QComboBox                           *m_rdiiModeCombo     = nullptr;
    openswmmvis::ui::RelativePathPicker *m_inflowsPathEdit   = nullptr;
    openswmmvis::ui::RelativePathPicker *m_outflowsPathEdit  = nullptr;
    openswmmvis::ui::RelativePathPicker *m_hotstartUseEdit   = nullptr;

    // Tab 7 — Multi-row SAVE HOTSTART table (Slice BV-01, 2026-05-21).
    // Replaces the single m_hotstartSaveEdit line edit so the user can
    // schedule multiple hot-start saves at different sim-time datetimes.
    // An empty datetime cell stores 0.0 → engine emits the row with no
    // trailing date string ("save at end of run").
    //
    // Phase 3.10.5 (2026-05-22): the legacy QTableWidget + QDateTimeEdit
    // cell-widget mix was replaced with a true QTableView + custom
    // QAbstractTableModel + per-column delegates so each row exposes a
    // browse "…" button next to the path field, and the date-time picker
    // is always visible (persistent editor).
    QTableView                       *m_hotstartSavesView     = nullptr;
    HotstartSavesModel               *m_hotstartSavesModel    = nullptr;
    PathBrowseDelegate               *m_hotstartSavesPathDel  = nullptr;
    HotstartSavesDateTimeDelegate    *m_hotstartSavesDtDel    = nullptr;
    QPushButton    *m_hotstartSavesAddBtn    = nullptr;
    QPushButton    *m_hotstartSavesBrowseBtn = nullptr;
    QPushButton    *m_hotstartSavesRemoveBtn = nullptr;
    QPushButton    *m_hotstartSavesUpBtn     = nullptr;
    QPushButton    *m_hotstartSavesDownBtn   = nullptr;

#ifdef OPENSWMM_HAS_2D
    // Tab 6 — 2D Surface Routing (time stepping / marcher / mesh / closure /
    // coupling / rainfall). The explicit local-inertial marcher is the only
    // 2D integrator (D2 retirement of the CVODE/ARKODE stack, 2026-07-29).
    QDoubleSpinBox *m_maxTimestepSpin   = nullptr;
    QGroupBox      *m_marcherGroup      = nullptr;
    QDoubleSpinBox *m_thetaSpin         = nullptr;
    QDoubleSpinBox *m_cflNumberSpin     = nullptr;
    QSpinBox       *m_ltsTiersSpin      = nullptr;
    QDoubleSpinBox *m_hMoveSpin         = nullptr;
    QDoubleSpinBox *m_froudeMaxSpin     = nullptr;
    QCheckBox      *m_advection2DBox    = nullptr;
    QComboBox      *m_backend2DCombo    = nullptr;   ///< [2D_OPTIONS] BACKEND
    QCheckBox      *m_couplingAreaAutoBox = nullptr;
    QDoubleSpinBox *m_dryDepthSpin      = nullptr;
    QDoubleSpinBox *m_limiterEpsSpin    = nullptr;
    QDoubleSpinBox *m_fluxDhEpsSpin     = nullptr;
    QComboBox      *m_cellClosureCombo  = nullptr;
    QComboBox      *m_faceReconCombo    = nullptr;
    QDoubleSpinBox *m_vfrMinWetFracSpin = nullptr;
    QDoubleSpinBox *m_couplingCdSpin    = nullptr;
    QDoubleSpinBox *m_couplingSyncSpin  = nullptr;
    QComboBox      *m_rainfall2DModeCombo = nullptr;
    QCheckBox      *m_report2DBox       = nullptr;
    class QLineEdit *m_output2DFileEdit = nullptr;
#endif
};

#endif // SIMULATIONOPTIONSDIALOG_H
