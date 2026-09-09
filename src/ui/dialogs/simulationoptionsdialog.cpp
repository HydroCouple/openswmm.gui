/*!
 * \file   simulationoptionsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/wateragesourcesdialog.h"
#include "ui/dialogs/initialqualitydialog.h"
#include "ui/theme/themehelpers.h"

#include "ui/uiscrollhelpers.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "ui/dialogs/hotstartsavesmodel.h"
#include "ui/widgets/relativepathpicker.h"
#include "ui/dialogs/pathbrowsedelegate.h"
#include "ui/dialogs/pluginstablemodel.h"
#include "ui/dialogs/processcomponentsmodel.h"   // U1
#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "mesh/inpmeshwriter.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "plugins/filefilterregistry.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/plugin_sdk/PluginDiscovery.hpp>

#include <qcustomeditors.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDoubleValidator>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QStandardItemModel>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>             // Slice RC.4 — built-in plugin-id lookup
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QRadioButton>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QAction>
#include <QEvent>

#include <openswmm/engine/openswmm_hotstart.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_spatial.h>
#ifdef OPENSWMM_HAS_2D
#include <openswmm/engine/openswmm_infil2d.h>   // U1: INFILTRATION AUTO label
#include <openswmm/engine/openswmm_gw_transport.h>   // U5: [GW_*] row count
#endif

#include <QApplication>
#include <QLocale>
#include <algorithm>   // E1 — std::max / std::clamp in build2DTab
#include <vector>

// ---------------------------------------------------------------------------
// Pure-helpers (parseEngineBool / engineBoolString / format/parse DateTime)
// live in simulationoptionshelpers.cpp so leaf QtTests can compile them
// without dragging in GDAL/OGR via the spatial-tab CRS code below.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SimulationOptionsDialog::SimulationOptionsDialog(SWMM_Engine engine,
                                                 SWMMModelLayer *layer,
                                                 const QString &engineVersion,
                                                 SWMMVisProjectWindow *projectWindow,
                                                 QWidget *parent)
    : QDialog(parent),
      m_engine(engine),
      m_layer(layer),
      m_projectWindow(projectWindow),
      m_engineVersion(engineVersion)
{
    setWindowTitle(tr("Simulation Options"));
    resize(620, 600);
    buildUi();
    readFromEngine();
    refreshSpatialSummary();
    applyEngineConstraints();
}

// Destructor is now inline in the header to keep the moc vtable self-contained.

// ---------------------------------------------------------------------------
// Engine constraints
// ---------------------------------------------------------------------------

namespace {

// Mesh-list item classification (stored under Qt::UserRole). The inline row
// is synthetic — it has no .2dm file on disk — so the Set Active / Remove
// handlers must branch on it.
constexpr int kMeshKindRole = Qt::UserRole;
constexpr int kMeshExternal = 0;   ///< sibling *.2dm file
constexpr int kMeshInline   = 1;   ///< mesh embedded in the project .inp

/*!
 * \brief True when the .inp already carries 2D content.
 *
 * The engine activates the 2D solver from the presence of mesh sections,
 * not from any module key — so a file with [2D_OPTIONS] / [2D_VERTICES] /
 * [2D_TRIANGLES] / [2D_MESH_FILE] has the module enabled by construction.
 * Used as the module-checkbox default when the per-project QSettings flag
 * has never been written (pre-built demos, files authored outside this
 * GUI's mesh-generation flow).
 */
bool inpCarries2DSections(const QString &inpPath)
{
    if (inpPath.isEmpty()) return false;
    QFile f(inpPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString text = QString::fromUtf8(f.readAll());
    for (const auto *sect : { "[2D_OPTIONS]", "[2D_VERTICES]",
                              "[2D_TRIANGLES]", "[2D_MESH_FILE]" }) {
        if (text.indexOf(QLatin1String(sect), 0, Qt::CaseInsensitive) >= 0)
            return true;
    }
    return false;
}

} // namespace

/*!
 * Disables widgets / tabs that the currently-selected engine does not support.
 *
 * Legacy SWMM 5.x limits:
 *   - No DYNAMIC_SLOT surcharge method
 *   - No SEMI_IMPLICIT node-continuity scheme
 *   - No Anderson acceleration
 *   - No plugin writers / containers
 *   - No [PLUGINS] section
 *
 * The 2D module checkbox + Mesh tab + 2D Surface Routing tab stay editable
 * even on legacy SWMM 5: mesh preparation is a pure GUI concern and the
 * engine simply ignores 2D inputs at run time. New engine (6.x) supports
 * all of the above, so all controls stay enabled.
 */
void SimulationOptionsDialog::applyEngineConstraints()
{
    const bool legacy = m_engineVersion.startsWith(QLatin1String("5."));

    // ── Models tab: FV flow routing ────────────────────────────────────────
    // Probe the option surface rather than the version string: the C ABI is
    // string-keyed and unchanged, so a 6.x engine installed before the FV
    // solver landed is only detectable by asking it for an FV_* key.
    const bool fvSupported = !legacy && !getOption("FV_CFL").isEmpty();
    if (!fvSupported && m_routingCombo) {
        auto *model = qobject_cast<QStandardItemModel *>(m_routingCombo->model());
        if (model) {
            for (int i = 0; i < m_routingCombo->count(); ++i) {
                if (m_routingCombo->itemData(i).toString() == QLatin1String("FV")) {
                    model->item(i)->setEnabled(false);
                    model->item(i)->setToolTip(
                        legacy ? tr("Not available in SWMM 5 (legacy engine).")
                               : tr("This engine build predates the finite-volume solver."));
                    if (m_routingCombo->currentIndex() == i) {
                        const int dyn = m_routingCombo->findData(QStringLiteral("DYNWAVE"));
                        m_routingCombo->setCurrentIndex(dyn >= 0 ? dyn : 0);
                    }
                    break;
                }
            }
        }
        updateFvFieldsEnabled(); // ensure the FV groups follow
    }

    // ── Quality & Transport page (Y1) ──────────────────────────────────────
    // Same capability-probe rule, and for the same reason the FV block gives:
    // the C ABI is string-keyed, so an engine built before the transport keys
    // reached swmm_options_get (subplan Y0) is only detectable by asking it.
    // Probe QUALITY_SOLVER rather than the version string.
    const bool transportSupported =
        !legacy && !getOption("QUALITY_SOLVER").isEmpty();
    if (!transportSupported) {
        const QString ttip =
            legacy ? tr("Not available in SWMM 5 (legacy engine).")
                   : tr("This engine build predates the quality/transport "
                        "option surface.");
        if (m_qualitySolverCombo) {
            m_qualitySolverCombo->setEnabled(false);
            m_qualitySolverCombo->setToolTip(ttip);
        }
        // OUTFALL_BACKFLOW_QUALITY exists in BOTH engines, but a legacy
        // build predating it would drop the key on save — same gate as
        // the rest of the quality surface.
        if (m_outfallBackflowCombo) {
            m_outfallBackflowCombo->setEnabled(false);
            m_outfallBackflowCombo->setToolTip(ttip);
        }
        // Disable the groups directly: with the solver combo frozen,
        // updateQualitySolverFieldsEnabled() would re-enable whichever group
        // matches its (stale) selection.
        if (m_ardGroup)         m_ardGroup->setEnabled(false);
        if (m_lardGroup)        m_lardGroup->setEnabled(false);
        if (m_waterAgeBox)    { m_waterAgeBox->setEnabled(false);
                                m_waterAgeBox->setToolTip(ttip); }
        if (m_heatTransportBox) { m_heatTransportBox->setEnabled(false);
                                  m_heatTransportBox->setToolTip(ttip); }
    }

    // ── Mixed-flow options (engine issue #156; GUI issue #10) ──────────────
    // Same capability-probe rule again: a 6.x engine installed before the
    // TPA / unsteady-friction surface landed refuses the keys, and is only
    // detectable by asking it. The #156 keys landed across engine phases,
    // so each control probes its own key rather than one family sentinel.
    const QString mixedFlowTip =
        legacy ? tr("Not available in SWMM 5 (legacy engine).")
               : tr("This engine build predates the mixed-flow "
                    "(TPA / unsteady-friction) option surface.");

    // TPA surcharge method + its celerity (SURCHARGE_METHOD=TPA and
    // TPA_CELERITY landed together — one probe covers both).
    if (legacy || getOption("TPA_CELERITY").isEmpty()) {
        // Disable the TPA surcharge item the way the FV routing item is
        // disabled above — the value cannot exist on this engine.
        if (m_surchargeCombo) {
            auto *model = qobject_cast<QStandardItemModel *>(m_surchargeCombo->model());
            if (model) {
                for (int i = 0; i < m_surchargeCombo->count(); ++i) {
                    if (m_surchargeCombo->itemData(i).toString() == QLatin1String("TPA")) {
                        model->item(i)->setEnabled(false);
                        model->item(i)->setToolTip(mixedFlowTip);
                        if (m_surchargeCombo->currentIndex() == i)
                            m_surchargeCombo->setCurrentIndex(0); // fall back to EXTRAN
                        break;
                    }
                }
            }
        }
        if (m_tpaCeleritySpin) {
            m_tpaCeleritySpin->setEnabled(false);
            m_tpaCeleritySpin->setToolTip(mixedFlowTip);
        }
        updateSurchargeFieldsEnabled();
    }

    // FV pressure closure. Explicit child disable is sticky in Qt, so
    // updateFvFieldsEnabled() re-enabling m_fvGroup cannot resurrect it.
    if (legacy || getOption("FV_PRESSURE_CLOSURE").isEmpty()) {
        if (m_fvPressureClosureCombo) {
            m_fvPressureClosureCombo->setEnabled(false);
            m_fvPressureClosureCombo->setToolTip(mixedFlowTip);
        }
    }

    // Unsteady friction. The UF group is gated by updateFvFieldsEnabled()
    // on every routing-combo change — carry the probe through the flag
    // instead of a direct setEnabled it would overwrite.
    if (legacy || getOption("UNSTEADY_FRICTION").isEmpty()) {
        m_ufSupported = false;
        if (m_ufGroup) m_ufGroup->setToolTip(mixedFlowTip);
        updateFvFieldsEnabled();
    }

    // Signed-heads output option.
    if (legacy || getOption("REPORT_SIGNED_HEADS").isEmpty()) {
        if (m_signedHeadsCheck) {
            m_signedHeadsCheck->setEnabled(false);
            m_signedHeadsCheck->setToolTip(mixedFlowTip);
        }
    }

    if (!legacy)
        return;   // new engine: everything else already enabled

    const QString tip = tr("Not available in SWMM 5 (legacy engine).");

    // ── Hydraulics tab: DYNAMIC_SLOT surcharge ─────────────────────────────
    // EXTRAN and SLOT exist in SWMM 5.x; DYNAMIC_SLOT is new-engine-only.
    if (m_surchargeCombo) {
        auto *model = qobject_cast<QStandardItemModel *>(m_surchargeCombo->model());
        if (model) {
            for (int i = 0; i < m_surchargeCombo->count(); ++i) {
                if (m_surchargeCombo->itemData(i).toString() == QLatin1String("DYNAMIC_SLOT")) {
                    model->item(i)->setEnabled(false);
                    model->item(i)->setToolTip(tip);
                    if (m_surchargeCombo->currentIndex() == i)
                        m_surchargeCombo->setCurrentIndex(0); // fall back to EXTRAN
                    break;
                }
            }
        }
        updateSurchargeFieldsEnabled(); // ensure DPS_* spins follow
    }

    // ── Hydraulics tab: node-continuity and Anderson acceleration ──────────
    if (m_nodeContinuityCombo) {
        auto *model = qobject_cast<QStandardItemModel *>(m_nodeContinuityCombo->model());
        if (model) {
            for (int i = 0; i < m_nodeContinuityCombo->count(); ++i) {
                if (m_nodeContinuityCombo->itemData(i).toString() == QLatin1String("SEMI_IMPLICIT")) {
                    model->item(i)->setEnabled(false);
                    model->item(i)->setToolTip(tip);
                    if (m_nodeContinuityCombo->currentIndex() == i)
                        m_nodeContinuityCombo->setCurrentIndex(0); // fall back to EXPLICIT
                    break;
                }
            }
        }
    }
    if (m_andersonAccelBox) {
        m_andersonAccelBox->setEnabled(false);
        m_andersonAccelBox->setToolTip(tip);
    }

    // ── Files tab: plugin writers and [PLUGINS] table ──────────────────────
    if (m_writersGroup) {
        m_writersGroup->setEnabled(false);
        m_writersGroup->setToolTip(tip);
    }
    for (QWidget *w : {(QWidget *)m_pluginsView,
                       (QWidget *)m_pluginsAddBtn,
                       (QWidget *)m_pluginsRemoveBtn}) {
        if (w) {
            w->setEnabled(false);
            w->setToolTip(tip);
        }
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Settings-style navigation — a category list on the left switching a
    // stacked page on the right (mirrors PreferencesDialog), replacing the
    // former wide tab bar. Each build*Tab() returns its page; addCategory
    // wraps it in a scroll area (so tall pages don't force the dialog tall)
    // and registers the sidebar row.
    auto *split = new QHBoxLayout();
    split->setSpacing(8);
    root->addLayout(split, 1);

    m_categoryList = new QListWidget(this);
    // Named so the restructure's gate tests can reach the sidebar by
    // objectName instead of a friend declaration. A QListWidget is not one of
    // the types dialoglayoutpersistence saves, so this does not enrol it.
    m_categoryList->setObjectName(QStringLiteral("categories"));
    m_categoryList->setMinimumWidth(190);
    split->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence;
    // restoring the page needs the category highlight kept in sync
    // (same-row set is a no-op, so no signal loop).
    setObjectName(QStringLiteral("SimulationOptionsDialog"));
    m_pages->setObjectName(QStringLiteral("pages"));
    connect(m_pages, &QStackedWidget::currentChanged,
            m_categoryList, qOverload<int>(&QListWidget::setCurrentRow));
    split->addWidget(m_pages, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);

    addCategory(tr("Title / Notes"),          buildTitleNotesTab());
    addCategory(tr("Models / Processes"),     buildModelsTab());
    addCategory(tr("Dates & Times"),          buildDatesTab());
    addCategory(tr("Routing & Hydraulics"),   buildHydraulicsTab());
    addCategory(tr("Quality & Transport"),    buildQualityTransportTab());
    addCategory(tr("System / Performance"),   buildPerformanceTab());
    addCategory(tr("Spatial & CRS"),          buildSpatialTab());

    // Mesh configurations — file-management UI, lives outside any
    // OPENSWMM_HAS_2D guard because picking a *.2dm reference is a pure
    // GUI concern (the engine 2D solver isn't required to organise mesh
    // candidates). Always editable: creating/selecting a mesh is what
    // turns on the 2D module, not the other way around.
    addCategory(tr("Mesh"), buildMeshTab());
    m_meshRow = m_categoryList->count() - 1;

#ifdef OPENSWMM_HAS_2D
    addCategory(tr("2D Surface Routing"), build2DTab());
    m_2DRow = m_categoryList->count() - 1;  // sidebar row for the 2D page.
    if (m_module2DBox)
        set2DRowEnabled(m_module2DBox->isChecked());
#endif

    // Slice AA-3.5 — Files / Plugins page (its own inner sub-tabs). Lives at
    // the end so existing ordering is preserved.
    addCategory(tr("Files / Output / Plugins"), buildFilesTab());

    m_categoryList->setCurrentRow(0);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SimulationOptionsDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SimulationOptionsDialog::onApply);

    // Every page exists by now, so every option editor can be tagged in one
    // pass (see tagOptionWidgets()).
    tagOptionWidgets();
}

void SimulationOptionsDialog::tagOption(QWidget *w, const char *key)
{
    // Null-tolerant: 2D editors do not exist when OPENSWMM_HAS_2D is off, and
    // a page that failed to build should not take the dialog down with it.
    if (!w) return;
    // A few editors own more than one key — a QDateTimeEdit writes both the
    // DATE and the TIME half — so the property is a comma-separated list and
    // repeated tags accumulate rather than overwrite.
    const QString add = QString::fromLatin1(key);
    const QString cur = w->property("optionKey").toString();
    if (cur.isEmpty()) {
        w->setProperty("optionKey", add);
    } else if (!cur.split(QLatin1Char(',')).contains(add)) {
        w->setProperty("optionKey", cur + QLatin1Char(',') + add);
    }
}

void SimulationOptionsDialog::tagOptionWidgets()
{
    // Derived mechanically from the writeIfChanged() key lists in
    // writeToEngine() and write2DToEngine(): one tag per editor, so the
    // reachability test can assert that every key written has a laid-out
    // widget behind it. Keep this in step with those two functions.
    // ---- [OPTIONS] — writeToEngine() ----
    tagOption(m_infiltrationCombo, "INFILTRATION");
    tagOption(m_routingCombo, "FLOW_ROUTING");
    tagOption(m_allowPondingBox, "ALLOW_PONDING");
    tagOption(m_skipSteadyBox, "SKIP_STEADY_STATE");
    tagOption(m_ignoreRainfallBox, "IGNORE_RAINFALL");
    tagOption(m_ignoreSnowmeltBox, "IGNORE_SNOWMELT");
    tagOption(m_ignoreGroundwaterBox, "IGNORE_GROUNDWATER");
    tagOption(m_ignoreRDIIBox, "IGNORE_RDII");
    tagOption(m_ignoreQualityBox, "IGNORE_QUALITY");
    tagOption(m_ignoreRoutingBox, "IGNORE_ROUTING");
    tagOption(m_startEdit, "START_DATE");
    tagOption(m_startEdit, "START_TIME");
    tagOption(m_endEdit, "END_DATE");
    tagOption(m_endEdit, "END_TIME");
    tagOption(m_reportStartEdit, "REPORT_START_DATE");
    tagOption(m_reportStartEdit, "REPORT_START_TIME");
    tagOption(m_reportStepEdit, "REPORT_STEP");
    tagOption(m_dryStepEdit, "DRY_STEP");
    tagOption(m_wetStepEdit, "WET_STEP");
    tagOption(m_ruleStepEdit, "RULE_STEP");
    tagOption(m_routingStepEdit, "ROUTING_STEP");
    tagOption(m_dryDaysSpin, "DRY_DAYS");
    tagOption(m_sweepStartEdit, "SWEEP_START");
    tagOption(m_sweepEndEdit, "SWEEP_END");
    tagOption(m_surchargeCombo, "SURCHARGE_METHOD");
    tagOption(m_dpsCelerSpin, "DPS_CELERITY");
    tagOption(m_dpsAlphaSpin, "DPS_ALPHA");
    tagOption(m_dpsDecaySpin, "DPS_DECAY_TIME");
    tagOption(m_tpaCeleritySpin, "TPA_CELERITY");
    tagOption(m_ufMethodCombo, "UNSTEADY_FRICTION");
    tagOption(m_ufK3Spin, "UF_K3");
    tagOption(m_nodeContinuityCombo, "NODE_CONTINUITY");
    tagOption(m_andersonAccelBox, "ANDERSON_ACCEL");
    tagOption(m_forceMainCombo, "FORCE_MAIN_EQUATION");
    tagOption(m_normalFlowCombo, "NORMAL_FLOW_LIMITED");
    tagOption(m_inertialDampCombo, "INERTIAL_DAMPING");
    tagOption(m_lengtheningSpin, "LENGTHENING_STEP");
    tagOption(m_variableStepSpin, "VARIABLE_STEP");
    tagOption(m_minStepSpin, "MINIMUM_STEP");
    tagOption(m_maxTrialsSpin, "MAX_TRIALS");
    tagOption(m_headTolSpin, "HEAD_TOLERANCE");
    tagOption(m_latFlowTolSpin, "LAT_FLOW_TOL");
    tagOption(m_sysFlowTolSpin, "SYS_FLOW_TOL");
    tagOption(m_minSurfAreaSpin, "MIN_SURFAREA");
    tagOption(m_minSlopeSpin, "MIN_SLOPE");
    tagOption(m_fvCellLengthSpin, "FV_CELL_LENGTH");
    tagOption(m_fvMinCellsSpin, "FV_MIN_CELLS");
    tagOption(m_fvCflSpin, "FV_CFL");
    tagOption(m_fvRiemannCombo, "FV_RIEMANN");
    tagOption(m_fvOrderCombo, "FV_ORDER");
    tagOption(m_fvLimiterCombo, "FV_LIMITER");
    tagOption(m_fvTimeIntCombo, "FV_TIME_INTEGRATION");
    tagOption(m_fvSlotCeleritySpin, "FV_SLOT_CELERITY");
    tagOption(m_fvPressureClosureCombo, "FV_PRESSURE_CLOSURE");
    tagOption(m_fvPressImplicitBox, "FV_PRESSURIZED_IMPLICIT");
    tagOption(m_fvScalarSchemeCombo, "FV_SCALAR_SCHEME");
    tagOption(m_fvStructCouplingCombo, "FV_STRUCTURE_COUPLING");
    tagOption(m_fvCompactionBox, "FV_COMPACTION");
    tagOption(m_fvBackendCombo, "FV_BACKEND");
    tagOption(m_fvMinParallelSpin, "FV_MIN_PARALLEL_CELLS");
    tagOption(m_fvLtsBox, "FV_LTS");
    tagOption(m_fvLtsTiersSpin, "FV_LTS_MAX_TIERS");
    tagOption(m_fvCflCensusSpin, "FV_CFL_CENSUS_INTERVAL");
    tagOption(m_qualitySolverCombo, "QUALITY_SOLVER");
    tagOption(m_outfallBackflowCombo, "OUTFALL_BACKFLOW_QUALITY");
    tagOption(m_qualityStepSpin, "QUALITY_STEP");
    tagOption(m_maxSegmentsSpin, "MAX_SEGMENTS_PER_LINK");
    tagOption(m_dispersionCombo, "DISPERSION");
    tagOption(m_rwptSeedSpin, "RWPT_SEED");
    tagOption(m_waterAgeBox, "WATER_AGE");
    tagOption(m_heatTransportBox, "HEAT_TRANSPORT");
    tagOption(m_threadsSpin, "THREADS");
    tagOption(m_module2DBox, "IGNORE_2D");

#ifdef OPENSWMM_HAS_2D
    // ---- [2D_OPTIONS] — write2DToEngine() ----
    // Built in a loop, so the key is not a literal at the call site.
    for (int c = 0; c < 4; ++c)
        tagOption(m_transport2DBox[c], transport2DKey(c));
    tagOption(m_maxTimestepSpin, "MAX_TIMESTEP");
    tagOption(m_dryDepthSpin, "DRY_DEPTH");
    tagOption(m_limiterEpsSpin, "LIMITER_EPSILON");
    tagOption(m_fluxDhEpsSpin, "FLUX_DH_EPS");
    tagOption(m_cellClosureCombo, "CELL_CLOSURE");
    tagOption(m_faceReconCombo, "FACE_RECONSTRUCTION");
    tagOption(m_vfrMinWetFracSpin, "VFR_MIN_WET_FRAC");
    tagOption(m_couplingCdSpin, "COUPLING_CD");
    tagOption(m_couplingSyncSpin, "COUPLING_SYNC");
    tagOption(m_rainfall2DModeCombo, "RAINFALL_MODE");
    tagOption(m_report2DBox, "REPORT_2D");
    tagOption(m_thetaSpin, "THETA");
    tagOption(m_cflNumberSpin, "CFL_NUMBER");
    tagOption(m_ltsTiersSpin, "LTS_TIERS");
    tagOption(m_hMoveSpin, "H_MOVE");
    tagOption(m_froudeMaxSpin, "FROUDE_MAX");
    tagOption(m_momentum2DCombo, "MOMENTUM_EQUATION");
    tagOption(m_reconOrder2DSpin, "RECONSTRUCTION_ORDER");
    tagOption(m_advection2DBox, "ADVECTION");
    tagOption(m_backend2DCombo, "BACKEND");
    tagOption(m_couplingAreaAutoBox, "COUPLING_AREA");
    tagOption(m_output2DFileEdit, "OUTPUT_FILE");
    tagOption(m_output2DPrecisionCombo, "OUTPUT_PRECISION");
    tagOption(m_output2DCompressionSpin, "OUTPUT_COMPRESSION");
    tagOption(m_infil2DModeCombo, "INFILTRATION");
    tagOption(m_infil2DMethodCombo, "INFIL_DEFAULT_METHOD");
    tagOption(m_infil2DDestCombo, "INFIL_DESTINATION");
    tagOption(m_evap2DCombo, "EVAPORATION");
    tagOption(m_gw2DEnableCombo, "GROUNDWATER");
    tagOption(m_gw2DEtCombo, "GW_ET");
    tagOption(m_rptDisabledBox, "RPT_DISABLED");
    tagOption(m_rptInputBox, "RPT_INPUT");
    tagOption(m_rptContinuityBox, "RPT_CONTINUITY");
    tagOption(m_rptFlowstatsBox, "RPT_FLOWSTATS");
    tagOption(m_rptControlsBox, "RPT_CONTROLS");
    tagOption(m_rptAveragesBox, "RPT_AVERAGES");
    tagOption(m_signedHeadsCheck, "REPORT_SIGNED_HEADS");
    tagOption(m_rptSubcatchNoneRadio, "RPT_SUBCATCHMENTS");
    tagOption(m_rptNodeNoneRadio, "RPT_NODES");
    tagOption(m_rptLinkNoneRadio, "RPT_LINKS");
    tagOption(m_rainfallModeCombo, "RAINFALL_MODE");
    tagOption(m_runoffModeCombo, "RUNOFF_MODE");
    tagOption(m_rdiiModeCombo, "RDII_MODE");
#endif
}

void SimulationOptionsDialog::addCategory(const QString &title, QWidget *page)
{
    m_categoryList->addItem(title);
    m_pages->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_pages));
}

void SimulationOptionsDialog::set2DRowEnabled(bool enabled)
{
    if (m_2DRow < 0) return;
    QListWidgetItem *item = m_categoryList->item(m_2DRow);
    if (!item) return;
    // A QStackedWidget has no per-page "enabled tab", so gate at the sidebar
    // row instead: keep it visible but non-selectable when 2D is off.
    item->setFlags(enabled
                       ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled)
                       : Qt::ItemFlags(Qt::NoItemFlags));
    // If the disabled row was current, move focus off it.
    if (!enabled && m_categoryList->currentRow() == m_2DRow)
        m_categoryList->setCurrentRow(0);
}

QWidget *SimulationOptionsDialog::buildTitleNotesTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(16, 16));

    m_titleBoldAction = toolbar->addAction(tr("Bold"));
    m_titleBoldAction->setShortcut(QKeySequence::Bold);
    m_titleBoldAction->setCheckable(true);
    m_titleBoldAction->setToolTip(tr("Bold (Ctrl+B)"));

    m_titleItalicAction = toolbar->addAction(tr("Italic"));
    m_titleItalicAction->setShortcut(QKeySequence::Italic);
    m_titleItalicAction->setCheckable(true);
    m_titleItalicAction->setToolTip(tr("Italic (Ctrl+I)"));

    m_titleUnderlineAction = toolbar->addAction(tr("Underline"));
    m_titleUnderlineAction->setShortcut(QKeySequence::Underline);
    m_titleUnderlineAction->setCheckable(true);
    m_titleUnderlineAction->setToolTip(tr("Underline (Ctrl+U)"));

    toolbar->addSeparator();
    auto *bulletAction = toolbar->addAction(tr("Bulleted list"));
    bulletAction->setToolTip(tr("Insert bulleted list"));
    auto *numberedAction = toolbar->addAction(tr("Numbered list"));
    numberedAction->setToolTip(tr("Insert numbered list"));

    vlay->addWidget(toolbar);

    m_titleNotesEdit = new QTextEdit(page);
    m_titleNotesEdit->setAcceptRichText(true);
    m_titleNotesEdit->setPlaceholderText(
        tr("Enter project title and notes (mirrors the SWMM [TITLE] section)."));
    vlay->addWidget(m_titleNotesEdit, 1);

    connect(m_titleBoldAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_titleNotesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        m_titleNotesEdit->mergeCurrentCharFormat(fmt);
    });
    connect(m_titleItalicAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_titleNotesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontItalic(checked);
        m_titleNotesEdit->mergeCurrentCharFormat(fmt);
    });
    connect(m_titleUnderlineAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_titleNotesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontUnderline(checked);
        m_titleNotesEdit->mergeCurrentCharFormat(fmt);
    });
    auto applyListStyle = [this](QTextListFormat::Style style) {
        if (!m_titleNotesEdit) return;
        QTextCursor c = m_titleNotesEdit->textCursor();
        c.createList(style);
    };
    connect(bulletAction,   &QAction::triggered, this,
            [applyListStyle]() { applyListStyle(QTextListFormat::ListDisc); });
    connect(numberedAction, &QAction::triggered, this,
            [applyListStyle]() { applyListStyle(QTextListFormat::ListDecimal); });

    connect(m_titleNotesEdit, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &fmt) {
                if (m_titleBoldAction)
                    m_titleBoldAction->setChecked(fmt.fontWeight() >= QFont::Bold);
                if (m_titleItalicAction)
                    m_titleItalicAction->setChecked(fmt.fontItalic());
                if (m_titleUnderlineAction)
                    m_titleUnderlineAction->setChecked(fmt.fontUnderline());
            });

    return page;
}

QWidget *SimulationOptionsDialog::buildModelsTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *procGroup = new QGroupBox(tr("Process models"), page);
    auto *procForm  = new QFormLayout(procGroup);

    m_infiltrationCombo = new QComboBox(procGroup);
    m_infiltrationCombo->addItem(tr("Horton"),                        QStringLiteral("HORTON"));
    m_infiltrationCombo->addItem(tr("Modified Horton"),               QStringLiteral("MOD_HORTON"));
    m_infiltrationCombo->addItem(tr("Green-Ampt"),                    QStringLiteral("GREEN_AMPT"));
    m_infiltrationCombo->addItem(tr("Modified Green-Ampt"),           QStringLiteral("MOD_GREEN_AMPT"));
    m_infiltrationCombo->addItem(tr("Curve Number"),                  QStringLiteral("CURVE_NUMBER"));
    m_infiltrationCombo->setToolTip(
        tr("Infiltration model used on every subcatchment (option INFILTRATION)."));
    procForm->addRow(tr("Infiltr&ation model:"), m_infiltrationCombo);

    m_routingCombo = new QComboBox(procGroup);
    m_routingCombo->addItem(tr("Steady"),            QStringLiteral("STEADY"));
    m_routingCombo->addItem(tr("Kinematic Wave"),    QStringLiteral("KINWAVE"));
    m_routingCombo->addItem(tr("Dynamic Wave"),      QStringLiteral("DYNWAVE"));
    m_routingCombo->addItem(tr("Finite Volume"),     QStringLiteral("FV"));
    m_routingCombo->setToolTip(
        tr("Flow-routing method for conduits (option FLOW_ROUTING).\n"
           "Finite Volume is the explicit Godunov solver; its parameters live "
           "on the Routing & Hydraulics page and apply only when selected."));
    procForm->addRow(tr("Flow routing:"), m_routingCombo);

    vlay->addWidget(procGroup);

    // ── Modules ────────────────────────────────────────────────────────
    // 1D is the always-on core. 2D Surface Routing is an optional module
    // gated by a project-level toggle here. When enabled, the dedicated
    // "2D Surface Routing" tab becomes interactive; when disabled, the
    // tab stays in place but is greyed out so users can still see what
    // the parameters would look like. Toggle persists per-.inp under
    // QSettings so reopening a project remembers the choice.
    auto *modulesGroup = new QGroupBox(tr("Modules"), page);
    auto *modulesLay   = new QVBoxLayout(modulesGroup);

    m_module1DBox = new QCheckBox(tr("1D Hydraulics (always on)"), modulesGroup);
    m_module1DBox->setChecked(true);
    m_module1DBox->setEnabled(false);
    m_module1DBox->setToolTip(
        tr("The 1D pipe-network solver is the SWMM core and cannot be disabled."));
    modulesLay->addWidget(m_module1DBox);

    m_module2DBox = new QCheckBox(tr("2D Surface Routing"), modulesGroup);
    // Module toggle is a project-level flag (QSettings-backed) — always
    // editable so the user can prepare meshes / configurations even when
    // the engine 2D solver isn't compiled in. The engine-side gate at
    // compile time only affects the parameter knobs on the 2D Surface
    // Routing tab; the Mesh tab + this toggle are GUI concerns.
#ifdef OPENSWMM_HAS_2D
    m_module2DBox->setToolTip(
        tr("Enable the optional 2D surface-routing module. When on, the "
           "Mesh + 2D Surface Routing tabs become editable and the engine "
           "runs the 2D solver coupled to the 1D network."));
#else
    m_module2DBox->setToolTip(
        tr("Project-level 2D module flag. Mesh selection tab becomes "
           "interactive when checked. The engine 2D solver itself is not "
           "compiled in this binary — rebuild with -DOPENSWMM_BUILD_2D=ON "
           "(requires SUNDIALS) for end-to-end coupled runs; mesh "
           "generation works regardless."));
#endif
    modulesLay->addWidget(m_module2DBox);

    vlay->addWidget(modulesGroup);

    connect(m_module2DBox, &QCheckBox::toggled,
            this, &SimulationOptionsDialog::on2DModuleToggled);

    auto *flagsGroup = new QGroupBox(tr("Options / flags"), page);
    auto *flagsLay   = new QVBoxLayout(flagsGroup);

    m_allowPondingBox  = new QCheckBox(tr("Allow ponding at nodes (ALLOW_PONDING)"), flagsGroup);
    flagsLay->addWidget(m_allowPondingBox);

    vlay->addWidget(flagsGroup);

    // Process activation. Checked = process runs (engine writes IGNORE_X NO);
    // unchecked = engine ignores the process (writes IGNORE_X YES). The
    // .inp surface keeps the legacy IGNORE_* keys — only the UI flips.
    auto *ignoreGroup = new QGroupBox(tr("Active processes"), page);
    auto *ignoreLay   = new QVBoxLayout(ignoreGroup);
    m_ignoreRainfallBox    = new QCheckBox(tr("Rainfall / runoff"),  ignoreGroup);
    m_ignoreSnowmeltBox    = new QCheckBox(tr("Snowmelt"),           ignoreGroup);
    m_ignoreGroundwaterBox = new QCheckBox(tr("Groundwater"),        ignoreGroup);
    m_ignoreRDIIBox        = new QCheckBox(tr("RDII"),               ignoreGroup);
    m_ignoreQualityBox     = new QCheckBox(tr("Water quality"),      ignoreGroup);
    m_ignoreRoutingBox     = new QCheckBox(tr("Flow routing"),       ignoreGroup);
    m_ignoreRainfallBox   ->setToolTip(tr("Unchecking writes IGNORE_RAINFALL YES — engine skips runoff entirely."));
    m_ignoreSnowmeltBox   ->setToolTip(tr("Unchecking writes IGNORE_SNOWMELT YES — engine skips snowmelt."));
    m_ignoreGroundwaterBox->setToolTip(tr("Unchecking writes IGNORE_GROUNDWATER YES — engine skips groundwater."));
    m_ignoreRDIIBox       ->setToolTip(tr("Unchecking writes IGNORE_RDII YES — engine skips RDII."));
    m_ignoreQualityBox    ->setToolTip(tr("Unchecking writes IGNORE_QUALITY YES — engine skips water-quality routing."));
    m_ignoreRoutingBox    ->setToolTip(tr("Unchecking writes IGNORE_ROUTING YES — engine skips flow routing."));
    ignoreLay->addWidget(m_ignoreRainfallBox);
    ignoreLay->addWidget(m_ignoreSnowmeltBox);
    ignoreLay->addWidget(m_ignoreGroundwaterBox);
    ignoreLay->addWidget(m_ignoreRDIIBox);
    ignoreLay->addWidget(m_ignoreQualityBox);
    ignoreLay->addWidget(m_ignoreRoutingBox);
    vlay->addWidget(ignoreGroup);

    // U1 (2026-09-07) — the Domain × Species transport matrix, computed by
    // the engine (swmm_get_transport_matrix; the .rpt prints the same
    // table). Cells show on(n) / off:KEY / n/a with the reason as tooltip.
    // The 2D column is the one editable column: its cells mirror the 2D
    // page's TRANSPORT_* boxes (one model, two views). The other columns
    // follow this page's Water quality box, WATER_AGE / HEAT_TRANSPORT on
    // the Quality page and the reactions component — they re-evaluate on
    // Apply.
    auto *matrixGroup = new QGroupBox(tr("Transport by domain"), page);
    auto *matrixLay   = new QVBoxLayout(matrixGroup);
    m_transportMatrixTable = new QTableWidget(SWMM_TRANSPORT_DOMAIN_COUNT,
                                              SWMM_TRANSPORT_CLASS_COUNT, matrixGroup);
    m_transportMatrixTable->setObjectName(QStringLiteral("transportMatrixTable"));
    {
        QStringList cols, rows;
        for (int c = 0; c < SWMM_TRANSPORT_CLASS_COUNT; ++c)
            cols << QString::fromUtf8(swmm_transport_class_name(c));
        for (int d = 0; d < SWMM_TRANSPORT_DOMAIN_COUNT; ++d)
            rows << QString::fromUtf8(swmm_transport_domain_name(d));
        m_transportMatrixTable->setHorizontalHeaderLabels(cols);
        m_transportMatrixTable->setVerticalHeaderLabels(rows);
    }
    m_transportMatrixTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_transportMatrixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transportMatrixTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_transportMatrixTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_transportMatrixTable->setToolTip(
        tr("Which species classes the engine will carry in each domain for "
           "the options as last applied. on(n): n rows carried; off:KEY: "
           "switched off by that option; n/a: nothing to carry or the solver "
           "lacks it (hover a cell for the reason). Tick or untick a 2D cell "
           "to set the [2D_OPTIONS] TRANSPORT_* keys."));
    m_transportMatrixTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_transportMatrixTable->setMaximumHeight(200);
    matrixLay->addWidget(m_transportMatrixTable);
    auto *matrixNote = new QLabel(
        tr("Inherit-on: a class enabled for the project runs in every domain "
           "whose solver carries it. Groundwater has no transported quality "
           "(legacy parity); MSX needs a reactions component."),
        matrixGroup);
    matrixNote->setWordWrap(true);
    matrixLay->addWidget(matrixNote);
    vlay->addWidget(matrixGroup);
    connect(m_transportMatrixTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
#ifdef OPENSWMM_HAS_2D
                if (m_matrixSyncing || !item) return;
                if (item->row() != SWMM_TRANSPORT_DOMAIN_SURFACE_2D) return;
                QCheckBox *box = m_transport2DBox[item->column()];
                if (!box || !box->isEnabled()) return;
                m_matrixSyncing = true;
                box->setChecked(item->checkState() == Qt::Checked);
                m_matrixSyncing = false;
                refreshTransportMatrix();
#else
                Q_UNUSED(item);
#endif
            });
    connect(m_ignoreQualityBox, &QCheckBox::toggled, this,
            [this](bool) { refreshTransportMatrix(); });

    vlay->addStretch();

    return page;
}

void SimulationOptionsDialog::refreshTransportMatrix()
{
    if (!m_transportMatrixTable) return;
    m_matrixSyncing = true;
    SWMM_TransportMatrix m{};
    const bool ok = m_engine && swmm_get_transport_matrix(m_engine, &m) == SWMM_OK;
    for (int d = 0; d < SWMM_TRANSPORT_DOMAIN_COUNT; ++d) {
        for (int c = 0; c < SWMM_TRANSPORT_CLASS_COUNT; ++c) {
            auto *item = m_transportMatrixTable->item(d, c);
            if (!item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                m_transportMatrixTable->setItem(d, c, item);
            }
            item->setFlags(Qt::ItemIsEnabled);
            if (!ok) { item->setText(QStringLiteral("—")); item->setToolTip(QString()); continue; }
            const SWMM_TransportCell &cell = m.cell[d][c];
            QString text, tip;
            switch (cell.state) {
            case SWMM_TRANSPORT_ENABLED:
                text = tr("on (%1)").arg(cell.count);
                tip  = cell.reason[0] ? QString::fromUtf8(cell.reason)
                                      : tr("%1 row(s) carried").arg(cell.count);
                break;
            case SWMM_TRANSPORT_DISABLED_BY_USER:
                text = tr("off");
                tip  = tr("Switched off by %1").arg(QString::fromUtf8(cell.reason));
                break;
            default:
                text = tr("n/a");
                tip  = QString::fromUtf8(cell.reason);
                break;
            }
            item->setText(text);
            item->setToolTip(tip);
            item->setForeground(cell.state == SWMM_TRANSPORT_UNAVAILABLE
                                    ? QBrush(Qt::gray) : QBrush());
#ifdef OPENSWMM_HAS_2D
            // The 2D column is editable whenever the class is available on
            // the surface at all: the checkbox IS the TRANSPORT_* key, and
            // the dialog value (not the engine's last-applied one) drives
            // its check state.
            if (d == SWMM_TRANSPORT_DOMAIN_SURFACE_2D && m_transport2DBox[c] &&
                cell.state != SWMM_TRANSPORT_UNAVAILABLE) {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                item->setCheckState(m_transport2DBox[c]->isChecked() ? Qt::Checked
                                                                     : Qt::Unchecked);
                if (!m_transport2DBox[c]->isChecked()) {
                    item->setText(tr("off"));
                    item->setToolTip(tr("Switched off by %1 (pending Apply)")
                                         .arg(QLatin1String(transport2DKey(c))));
                }
            }
#endif
        }
    }
    m_matrixSyncing = false;
}

QWidget *SimulationOptionsDialog::buildDatesTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // ── Simulation window ──────────────────────────────────────────────
    auto *winGroup = new QGroupBox(tr("Simulation window"), page);
    auto *winForm  = new QFormLayout(winGroup);

    m_startEdit = new QDateTimeEdit(winGroup);
    m_startEdit->setCalendarPopup(true);
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("Start:"), m_startEdit);

    m_endEdit = new QDateTimeEdit(winGroup);
    m_endEdit->setCalendarPopup(true);
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("End:"), m_endEdit);

    m_durationLabel = new QLabel(QStringLiteral("—"), winGroup);
    m_durationLabel->setToolTip(tr("Simulation timespan (End − Start)."));
    winForm->addRow(tr("Duration:"), m_durationLabel);

    m_reportStartEdit = new QDateTimeEdit(winGroup);
    m_reportStartEdit->setCalendarPopup(true);
    m_reportStartEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("Report start:"), m_reportStartEdit);

    vlay->addWidget(winGroup);

    // Live sync: clamp report-start ≥ start, refresh duration label.
    connect(m_startEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &s) {
                m_reportStartEdit->setMinimumDateTime(s);
                if (m_reportStartEdit->dateTime() < s)
                    m_reportStartEdit->setDateTime(s);
                updateDurationLabel();
            });
    connect(m_endEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &) { updateDurationLabel(); });

    // ── Time steps ─────────────────────────────────────────────────────
    auto *stepGroup = new QGroupBox(tr("Time steps"), page);
    auto *stepForm  = new QFormLayout(stepGroup);

    m_reportStepEdit = new QCustomTimespanEdit(stepGroup);
    m_reportStepEdit->setToolTip(tr("Reporting step (REPORT_STEP)."));
    stepForm->addRow(tr("Reporting step:"), m_reportStepEdit);

    m_dryStepEdit = new QCustomTimespanEdit(stepGroup);
    m_dryStepEdit->setToolTip(tr("Runoff dry-weather step (DRY_STEP)."));
    stepForm->addRow(tr("Dr&y-weather step:"), m_dryStepEdit);

    m_wetStepEdit = new QCustomTimespanEdit(stepGroup);
    m_wetStepEdit->setToolTip(tr("Runoff wet-weather step (WET_STEP)."));
    stepForm->addRow(tr("Wet-weat&her step:"), m_wetStepEdit);

    m_ruleStepEdit = new QCustomTimespanEdit(stepGroup);
    m_ruleStepEdit->setToolTip(tr("Control-rule evaluation step (RULE_STEP). "
                                   "0 means rules are evaluated every routing step."));
    stepForm->addRow(tr("Control rule step:"), m_ruleStepEdit);

    // Routing step — plain floating-point text box (no spin buttons),
    // displayed in seconds. Engine accepts decimal seconds via ROUTING_STEP.
    m_routingStepEdit = new QLineEdit(stepGroup);
    auto *routingValidator = new QDoubleValidator(0.001, 3600.0, 6, m_routingStepEdit);
    routingValidator->setNotation(QDoubleValidator::StandardNotation);
    m_routingStepEdit->setValidator(routingValidator);
    m_routingStepEdit->setPlaceholderText(QStringLiteral("seconds"));
    m_routingStepEdit->setToolTip(tr("Routing step in seconds (ROUTING_STEP)."));
    stepForm->addRow(tr("Routing step:"), m_routingStepEdit);

    vlay->addWidget(stepGroup);

    // ── Skip steady state ──────────────────────────────────────────────
    // LAT_FLOW_TOL / SYS_FLOW_TOL only matter when SKIP_STEADY_STATE is on
    // — engine treats them as the change thresholds for declaring a period
    // "steady". Grouping the three together makes the dependency clear.
    auto *skipGroup = new QGroupBox(tr("Skip steady state"), page);
    auto *skipForm  = new QFormLayout(skipGroup);

    m_skipSteadyBox = new QCheckBox(tr("Skip steady-periods (SKIP_STEADY_STATE)"),
                                     skipGroup);
    skipForm->addRow(QString(), m_skipSteadyBox);

    m_latFlowTolSpin = new QDoubleSpinBox(skipGroup);
    m_latFlowTolSpin->setRange(0.0, 100.0);
    m_latFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_latFlowTolSpin->setToolTip(tr("Lateral flow tolerance in percent (LAT_FLOW_TOL)."));
    skipForm->addRow(tr("Lateral flow tol:"), m_latFlowTolSpin);

    m_sysFlowTolSpin = new QDoubleSpinBox(skipGroup);
    m_sysFlowTolSpin->setRange(0.0, 100.0);
    m_sysFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_sysFlowTolSpin->setToolTip(tr("System flow tolerance (SYS_FLOW_TOL)."));
    skipForm->addRow(tr("System flow tol:"), m_sysFlowTolSpin);

    // Grey out the tolerance spins when skip-steady is off — they remain
    // serialised either way so toggling back on restores the prior values.
    auto syncSkipEnabled = [this]() {
        const bool on = m_skipSteadyBox->isChecked();
        m_latFlowTolSpin->setEnabled(on);
        m_sysFlowTolSpin->setEnabled(on);
    };
    connect(m_skipSteadyBox, &QCheckBox::toggled,
            this, [syncSkipEnabled](bool) { syncSkipEnabled(); });
    syncSkipEnabled();

    vlay->addWidget(skipGroup);

    // ── Sweep / antecedent ─────────────────────────────────────────────
    auto *sweepGroup = new QGroupBox(tr("Sweep / antecedent"), page);
    auto *sweepForm  = new QFormLayout(sweepGroup);

    // SWEEP_START / SWEEP_END are MM/DD only — use a fixed year (2000, a
    // leap year so 02/29 stays selectable) internally and strip it on
    // write. Matches the legacy SWMM-GUI Delphi convention.
    m_sweepStartEdit = new QDateEdit(QDate(2000, 1, 1), sweepGroup);
    m_sweepStartEdit->setDisplayFormat(QStringLiteral("MM/dd"));
    m_sweepStartEdit->setCalendarPopup(true);
    m_sweepStartEdit->setToolTip(tr("Street-sweeping season start (SWEEP_START, MM/DD)."));
    sweepForm->addRow(tr("Start sweeping on:"), m_sweepStartEdit);

    m_sweepEndEdit = new QDateEdit(QDate(2000, 12, 31), sweepGroup);
    m_sweepEndEdit->setDisplayFormat(QStringLiteral("MM/dd"));
    m_sweepEndEdit->setCalendarPopup(true);
    m_sweepEndEdit->setToolTip(tr("Street-sweeping season end (SWEEP_END, MM/DD)."));
    sweepForm->addRow(tr("End sweeping on:"), m_sweepEndEdit);

    m_dryDaysSpin = new QDoubleSpinBox(sweepGroup);
    m_dryDaysSpin->setRange(0.0, 3650.0);
    m_dryDaysSpin->setDecimals(2);
    m_dryDaysSpin->setSuffix(QStringLiteral(" d"));
    sweepForm->addRow(tr("Antecedent dry days:"), m_dryDaysSpin);

    vlay->addWidget(sweepGroup);

    // ── Events ([EVENTS] section editor, Slice CW) ─────────────────────
    // Mirrors SWMM 5.2's [EVENTS] block: a list of {start, end} windows the
    // engine treats as routing-active periods when SKIP_STEADY_STATE is YES
    // (see swmm_is_between_events in openswmm_engine.h:301).  Two columns:
    // Start and End.  Cells use a QDateTimeEdit inline editor.  HH:MM
    // precision (legacy SWMM 5 parity, decided 2026-05-21).
    auto *evGroup = new QGroupBox(tr("Events ([EVENTS])"), page);
    auto *evLay   = new QVBoxLayout(evGroup);
    evGroup->setToolTip(tr(
        "Optional list of routing-active time windows. When Skip Steady State "
        "is on the engine routes full dynamic-wave hydraulics only inside "
        "these windows."));

    m_eventsTable = new QTableWidget(0, 2, evGroup);
    m_eventsTable->setHorizontalHeaderLabels(
        {tr("Start (MM/DD/YYYY HH:MM)"), tr("End (MM/DD/YYYY HH:MM)")});
    m_eventsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_eventsTable->verticalHeader()->setVisible(false);
    m_eventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_eventsTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    evLay->addWidget(m_eventsTable);

    auto *evBtnRow = new QHBoxLayout();
    m_eventsAddBtn    = new QPushButton(tr("Add row"),         evGroup);
    m_eventsRemoveBtn = new QPushButton(tr("Remove selected"), evGroup);
    m_eventsRemoveBtn->setEnabled(false);
    evBtnRow->addWidget(m_eventsAddBtn);
    evBtnRow->addWidget(m_eventsRemoveBtn);
    evBtnRow->addStretch();
    evLay->addLayout(evBtnRow);

    connect(m_eventsAddBtn,    &QPushButton::clicked,
            this, &SimulationOptionsDialog::addEventRow);
    connect(m_eventsRemoveBtn, &QPushButton::clicked,
            this, &SimulationOptionsDialog::removeSelectedEventRows);
    // Gate on the selection MODEL, not selectedItems(): the cells hold only
    // setCellWidget() editors (no QTableWidgetItems), so selectedItems() is
    // always empty and an item-based gate leaves Remove permanently disabled.
    connect(m_eventsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_eventsRemoveBtn->setEnabled(
            !selectedRowsDescending(m_eventsTable).isEmpty());
    });

    vlay->addWidget(evGroup);
    vlay->addStretch();

    return page;
}

QWidget *SimulationOptionsDialog::buildHydraulicsTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // ── Surcharge group ────────────────────────────────────────────────
    auto *surGroup = new QGroupBox(tr("Surcharge handling"), page);
    auto *surForm  = new QFormLayout(surGroup);

    m_surchargeCombo = new QComboBox(surGroup);
    m_surchargeCombo->addItem(tr("EXTRAN (legacy)"),  QStringLiteral("EXTRAN"));
    m_surchargeCombo->addItem(tr("SLOT (Preissmann)"), QStringLiteral("SLOT"));
    m_surchargeCombo->addItem(tr("DYNAMIC_SLOT"),     QStringLiteral("DYNAMIC_SLOT"));
    m_surchargeCombo->addItem(tr("TPA (two-component pressure, experimental)"),
                              QStringLiteral("TPA"));
    m_surchargeCombo->setToolTip(
        tr("Method for handling surcharged conduits (option SURCHARGE_METHOD)."));
    surForm->addRow(tr("Method:"), m_surchargeCombo);

    // DPS_* parameters — only meaningful for DYNAMIC_SLOT.
    m_dpsCelerSpin = new QDoubleSpinBox(surGroup);
    m_dpsCelerSpin->setRange(0.1, 1000.0);
    m_dpsCelerSpin->setDecimals(2);
    m_dpsCelerSpin->setSuffix(QStringLiteral(" m/s"));
    m_dpsCelerSpin->setToolTip(tr("DYNAMIC_SLOT target wave celerity (DPS_CELERITY)."));
    surForm->addRow(tr("DPS celerity:"), m_dpsCelerSpin);

    m_dpsAlphaSpin = new QDoubleSpinBox(surGroup);
    m_dpsAlphaSpin->setRange(2.0, 100.0);
    m_dpsAlphaSpin->setDecimals(3);
    m_dpsAlphaSpin->setToolTip(tr("DYNAMIC_SLOT alpha exponent (DPS_ALPHA, ≥ 2)."));
    surForm->addRow(tr("DPS alpha:"), m_dpsAlphaSpin);

    m_dpsDecaySpin = new QDoubleSpinBox(surGroup);
    m_dpsDecaySpin->setRange(0.0, 60.0);
    m_dpsDecaySpin->setDecimals(3);
    m_dpsDecaySpin->setSuffix(QStringLiteral(" s"));
    m_dpsDecaySpin->setToolTip(tr("DYNAMIC_SLOT decay time (DPS_DECAY_TIME)."));
    surForm->addRow(tr("DPS decay:"), m_dpsDecaySpin);

    // TPA_CELERITY — only meaningful for the TPA surcharge method
    // (engine issue #156; GUI issue #10).
    m_tpaCeleritySpin = new QDoubleSpinBox(surGroup);
    m_tpaCeleritySpin->setRange(1.0, 5000.0);
    m_tpaCeleritySpin->setDecimals(1);
    m_tpaCeleritySpin->setToolTip(
        tr("Acoustic (pressure-wave) celerity a for the TPA surcharge "
           "method, in project length units per second (TPA_CELERITY). "
           "Sets the pressurized wall compliance w = g·A_full/a²."));
    surForm->addRow(tr("TPA celerity:"), m_tpaCeleritySpin);

    vlay->addWidget(surGroup);
    connect(m_surchargeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateSurchargeFieldsEnabled(); });

    // ── Solver group ───────────────────────────────────────────────────
    auto *solGroup = new QGroupBox(tr("Solver"), page);
    auto *solForm  = new QFormLayout(solGroup);

    m_nodeContinuityCombo = new QComboBox(solGroup);
    m_nodeContinuityCombo->addItem(tr("Explicit (legacy)"),       QStringLiteral("EXPLICIT"));
    m_nodeContinuityCombo->addItem(tr("Semi-implicit (new)"),     QStringLiteral("SEMI_IMPLICIT"));
    m_nodeContinuityCombo->setToolTip(
        tr("Node continuity scheme (option NODE_CONTINUITY)."));
    solForm->addRow(tr("Node continuity:"), m_nodeContinuityCombo);

    m_andersonAccelBox = new QCheckBox(tr("Anderson acceleration (ANDERSON_ACCEL)"), solGroup);
    m_andersonAccelBox->setToolTip(
        tr("Anderson acceleration of the iterative solver — typical 25–50% iteration reduction."));
    solForm->addRow(QString(), m_andersonAccelBox);

    m_maxTrialsSpin = new QSpinBox(solGroup);
    m_maxTrialsSpin->setRange(1, 100);
    m_maxTrialsSpin->setToolTip(tr("Max iterations per routing step (MAX_TRIALS)."));
    solForm->addRow(tr("Ma&x trials:"), m_maxTrialsSpin);

    m_headTolSpin = new QDoubleSpinBox(solGroup);
    m_headTolSpin->setRange(0.000001, 1.0);
    m_headTolSpin->setDecimals(6);
    m_headTolSpin->setToolTip(tr("Head convergence tolerance (HEAD_TOLERANCE)."));
    solForm->addRow(tr("Head tolerance:"), m_headTolSpin);

    m_lengtheningSpin = new QDoubleSpinBox(solGroup);
    m_lengtheningSpin->setRange(0.0, 3600.0);
    m_lengtheningSpin->setDecimals(2);
    m_lengtheningSpin->setSuffix(QStringLiteral(" s"));
    m_lengtheningSpin->setToolTip(tr("Conduit lengthening time step (LENGTHENING_STEP)."));
    solForm->addRow(tr("Lengthening step:"), m_lengtheningSpin);

    m_variableStepSpin = new QDoubleSpinBox(solGroup);
    m_variableStepSpin->setRange(0.0, 1.0);
    m_variableStepSpin->setSingleStep(0.05);
    m_variableStepSpin->setDecimals(3);
    m_variableStepSpin->setToolTip(
        tr("Variable timestep Courant safety fraction (VARIABLE_STEP, 0 disables)."));
    solForm->addRow(tr("Varia&ble step factor:"), m_variableStepSpin);

    m_minStepSpin = new QDoubleSpinBox(solGroup);
    m_minStepSpin->setRange(0.01, 60.0);
    m_minStepSpin->setSingleStep(0.1);
    m_minStepSpin->setDecimals(3);
    m_minStepSpin->setSuffix(QStringLiteral(" s"));
    m_minStepSpin->setToolTip(
        tr("Smallest routing step the adaptive solver may take (MINIMUM_STEP).\n"
           "In 1D/2D-coupled runs the coupling collapses the 1D step toward this "
           "floor; raising it (e.g. 1.0–1.5 s) recovers most of the runtime with a "
           "small accuracy trade. See the Performance tab's Fast preset."));
    solForm->addRow(tr("Minimum step:"), m_minStepSpin);

    vlay->addWidget(solGroup);

    // ── Finite-volume solver groups (FLOW_ROUTING FV) ──────────────────
    // Knobs for the explicit Godunov FV routing solver. Both groups are
    // enabled only while the Models / Processes tab's flow-routing combo
    // says FV; the engine accepts FV_* keys as inert under any other
    // routing model, so a greyed-out group never invalidates the project.
    m_fvGroup = new QGroupBox(tr("Finite volume solver"), page);
    auto *fvForm = new QFormLayout(m_fvGroup);

    m_fvCellLengthSpin = new QDoubleSpinBox(m_fvGroup);
    m_fvCellLengthSpin->setRange(0.0, 100000.0);
    m_fvCellLengthSpin->setDecimals(2);
    m_fvCellLengthSpin->setSpecialValueText(tr("whole conduit"));
    m_fvCellLengthSpin->setToolTip(
        tr("Target cell length for conduit discretisation, in project length "
           "units (FV_CELL_LENGTH). 0 = one cell per conduit."));
    fvForm->addRow(tr("Cell length:"), m_fvCellLengthSpin);

    m_fvMinCellsSpin = new QSpinBox(m_fvGroup);
    m_fvMinCellsSpin->setRange(1, 1000);
    m_fvMinCellsSpin->setToolTip(
        tr("Minimum number of cells per conduit (FV_MIN_CELLS)."));
    fvForm->addRow(tr("Min cells per conduit:"), m_fvMinCellsSpin);

    m_fvCflSpin = new QDoubleSpinBox(m_fvGroup);
    m_fvCflSpin->setRange(0.05, 1.0);
    m_fvCflSpin->setSingleStep(0.05);
    m_fvCflSpin->setDecimals(2);
    m_fvCflSpin->setToolTip(
        tr("Courant number the explicit substep targets (FV_CFL)."));
    fvForm->addRow(tr("CFL number:"), m_fvCflSpin);

    m_fvRiemannCombo = new QComboBox(m_fvGroup);
    m_fvRiemannCombo->addItem(tr("HLLC"), QStringLiteral("HLLC"));
    m_fvRiemannCombo->addItem(tr("HLL"),  QStringLiteral("HLL"));
    m_fvRiemannCombo->setToolTip(tr("Approximate Riemann solver for face fluxes (FV_RIEMANN)."));
    fvForm->addRow(tr("Riemann solver:"), m_fvRiemannCombo);

    m_fvOrderCombo = new QComboBox(m_fvGroup);
    m_fvOrderCombo->addItem(tr("1st order"),                  QStringLiteral("1"));
    m_fvOrderCombo->addItem(tr("2nd order (MUSCL-Hancock)"),  QStringLiteral("2"));
    m_fvOrderCombo->setToolTip(tr("Spatial reconstruction order (FV_ORDER)."));
    fvForm->addRow(tr("Spatial order:"), m_fvOrderCombo);

    m_fvLimiterCombo = new QComboBox(m_fvGroup);
    m_fvLimiterCombo->addItem(tr("Minmod"),   QStringLiteral("MINMOD"));
    m_fvLimiterCombo->addItem(tr("van Leer"), QStringLiteral("VANLEER"));
    m_fvLimiterCombo->addItem(tr("Superbee"), QStringLiteral("SUPERBEE"));
    m_fvLimiterCombo->setToolTip(
        tr("Slope limiter for 2nd-order reconstruction (FV_LIMITER)."));
    fvForm->addRow(tr("Slope limiter:"), m_fvLimiterCombo);

    m_fvTimeIntCombo = new QComboBox(m_fvGroup);
    m_fvTimeIntCombo->addItem(tr("Euler"), QStringLiteral("EULER"));
    m_fvTimeIntCombo->addItem(tr("RK2"),   QStringLiteral("RK2"));
    m_fvTimeIntCombo->setToolTip(tr("Substep time integrator (FV_TIME_INTEGRATION)."));
    fvForm->addRow(tr("Time integration:"), m_fvTimeIntCombo);

    m_fvSlotCeleritySpin = new QDoubleSpinBox(m_fvGroup);
    m_fvSlotCeleritySpin->setRange(1.0, 10000.0);
    m_fvSlotCeleritySpin->setDecimals(1);
    m_fvSlotCeleritySpin->setToolTip(
        tr("Preissmann-slot pressure-wave celerity, in project length units "
           "per second (FV_SLOT_CELERITY)."));
    fvForm->addRow(tr("Slot celerity:"), m_fvSlotCeleritySpin);

    // FV_PRESSURE_CLOSURE (engine issue #156; GUI issue #10). FV-only key;
    // the engine accepts it as inert under other routing, matching the FV_*
    // posture above.
    m_fvPressureClosureCombo = new QComboBox(m_fvGroup);
    m_fvPressureClosureCombo->addItem(tr("SLOT (Preissmann)"), QStringLiteral("SLOT"));
    m_fvPressureClosureCombo->addItem(tr("TPA (two-component pressure)"),
                                      QStringLiteral("TPA"));
    m_fvPressureClosureCombo->setToolTip(
        tr("Pressure closure for surcharged FV cells (FV_PRESSURE_CLOSURE). "
           "TPA carries a signed pressure head so sub-atmospheric "
           "full-pipe flow is representable; SLOT is the one-sided "
           "Preissmann slot."));
    fvForm->addRow(tr("Pressure closure:"), m_fvPressureClosureCombo);

    // Surfaced as experimental by explicit decision (2026-08-29): the solve
    // cannot yet compose with local time stepping (tiering stands down on
    // any substep where it engages) and slot program R2b is expected to
    // revise it, but it is fully functional and gated, and needed to
    // experiment with pressurized transmission mains from the GUI.
    m_fvPressImplicitBox = new QCheckBox(
        tr("Implicit pressurized head solve (FV_PRESSURIZED_IMPLICIT, experimental)"),
        m_fvGroup);
    m_fvPressImplicitBox->setToolTip(
        tr("Experimental. Solve surcharged-cell heads implicitly so "
           "pressurized reaches stop binding the CFL substep and full-bore "
           "head loss is Manning-exact regardless of slot celerity "
           "(FV_PRESSURIZED_IMPLICIT). CPU backend only; local time "
           "stepping stands down while the solve engages; free-to-"
           "pressurized transition faces stay explicit. Subject to change "
           "in slot program R2b."));
    fvForm->addRow(QString(), m_fvPressImplicitBox);

    // Not surfaced here on purpose: FV_DISPERSION (inert on every path — the
    // engine warns at open),
    // and the retired FV_NODE_COUPLING / FV_NODE_DT / FV_NODE_PICARD, which
    // the engine hardwires to their former defaults. FV_SCALAR_SCHEME lives
    // on the Quality & Transport page: its only live consumer is the
    // Eulerian ARD engine.

    m_fvStructCouplingCombo = new QComboBox(m_fvGroup);
    m_fvStructCouplingCombo->addItem(tr("Every substep"),      QStringLiteral("SUBSTEP"));
    m_fvStructCouplingCombo->addItem(tr("Every routing step"), QStringLiteral("ROUTING_STEP"));
    m_fvStructCouplingCombo->setToolTip(
        tr("How often weir/orifice/pump flows are re-evaluated "
           "(FV_STRUCTURE_COUPLING)."));
    fvForm->addRow(tr("Structure coupling:"), m_fvStructCouplingCombo);

    vlay->addWidget(m_fvGroup);

    m_fvPerfGroup = new QGroupBox(tr("Finite volume performance"), page);
    auto *fvPerfForm = new QFormLayout(m_fvPerfGroup);

    m_fvBackendCombo = new QComboBox(m_fvPerfGroup);
    m_fvBackendCombo->addItem(tr("Auto"),  QStringLiteral("AUTO"));
    m_fvBackendCombo->addItem(tr("CPU (serial)"), QStringLiteral("CPU"));
    m_fvBackendCombo->addItem(tr("OpenMP"), QStringLiteral("OMP"));
    m_fvBackendCombo->addItem(tr("CUDA"),  QStringLiteral("CUDA"));
    m_fvBackendCombo->addItem(tr("HIP"),   QStringLiteral("HIP"));
    m_fvBackendCombo->addItem(tr("SYCL"),  QStringLiteral("SYCL"));
    m_fvBackendCombo->setToolTip(
        tr("Compute backend for the FV kernels (FV_BACKEND). Auto picks "
           "based on mesh size and available plugins."));
    fvPerfForm->addRow(tr("Backend:"), m_fvBackendCombo);

    m_fvMinParallelSpin = new QSpinBox(m_fvPerfGroup);
    m_fvMinParallelSpin->setRange(0, 100000000);
    m_fvMinParallelSpin->setSingleStep(1000);
    m_fvMinParallelSpin->setToolTip(
        tr("Cell count below which the solver stays serial "
           "(FV_MIN_PARALLEL_CELLS)."));
    fvPerfForm->addRow(tr("Min parallel cells:"), m_fvMinParallelSpin);

    m_fvCompactionBox = new QCheckBox(tr("Compact dry-cell storage (FV_COMPACTION)"),
                                      m_fvPerfGroup);
    m_fvCompactionBox->setToolTip(
        tr("Skip fully dry reaches in the substep loop (FV_COMPACTION)."));
    fvPerfForm->addRow(QString(), m_fvCompactionBox);

    m_fvLtsBox = new QCheckBox(tr("Local time stepping (FV_LTS)"), m_fvPerfGroup);
    m_fvLtsBox->setToolTip(
        tr("Advance slow cells with larger substeps grouped in tiers "
           "(FV_LTS)."));
    fvPerfForm->addRow(QString(), m_fvLtsBox);

    m_fvLtsTiersSpin = new QSpinBox(m_fvPerfGroup);
    m_fvLtsTiersSpin->setRange(1, 8);
    m_fvLtsTiersSpin->setToolTip(
        tr("Maximum number of local-time-stepping tiers (FV_LTS_MAX_TIERS)."));
    fvPerfForm->addRow(tr("LTS max tiers:"), m_fvLtsTiersSpin);

    m_fvCflCensusSpin = new QSpinBox(m_fvPerfGroup);
    m_fvCflCensusSpin->setRange(1, 10000);
    m_fvCflCensusSpin->setToolTip(
        tr("Substeps between full CFL re-surveys of the mesh "
           "(FV_CFL_CENSUS_INTERVAL). 1 = every substep (exact)."));
    fvPerfForm->addRow(tr("CFL census interval:"), m_fvCflCensusSpin);

    vlay->addWidget(m_fvPerfGroup);

    // ── Unsteady friction (engine issue #156; GUI issue #10) ───────────
    // Consumed by BOTH the dynamic-wave and FV solvers, so it is its own
    // group gated on FLOW_ROUTING ∈ {DYNWAVE, FV} in updateFvFieldsEnabled().
    m_ufGroup = new QGroupBox(tr("Unsteady friction"), page);
    auto *ufForm = new QFormLayout(m_ufGroup);

    m_ufMethodCombo = new QComboBox(m_ufGroup);
    m_ufMethodCombo->addItem(tr("None"),      QStringLiteral("NONE"));
    m_ufMethodCombo->addItem(tr("Vitkovsky"), QStringLiteral("VITKOVSKY"));
    m_ufMethodCombo->setToolTip(
        tr("Unsteady (transient) friction model added to the steady friction "
           "slope during rapid transients (UNSTEADY_FRICTION). Applies to "
           "dynamic-wave and finite-volume routing. Vitkovsky "
           "instantaneous-acceleration model per Pinto, Vasconcelos & "
           "Soares (2025)."));
    ufForm->addRow(tr("Method:"), m_ufMethodCombo);

    m_ufK3Spin = new QDoubleSpinBox(m_ufGroup);
    m_ufK3Spin->setRange(0.0, 0.05);
    m_ufK3Spin->setDecimals(3);
    m_ufK3Spin->setSingleStep(0.005);
    m_ufK3Spin->setToolTip(
        tr("Vitkovsky (Brunone-type) coefficient k3 (UF_K3). Used only when "
           "an unsteady-friction method is selected; paper range "
           "0.005–0.020."));
    ufForm->addRow(tr("Coefficient k3:"), m_ufK3Spin);

    vlay->addWidget(m_ufGroup);

    connect(m_routingCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateFvFieldsEnabled(); });
    connect(m_fvOrderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateFvFieldsEnabled(); });
    connect(m_fvLtsBox, &QCheckBox::toggled,
            this, [this](bool){ updateFvFieldsEnabled(); });
    connect(m_ufMethodCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateFvFieldsEnabled(); });

    // ── Conduit / channel group ────────────────────────────────────────
    auto *condGroup = new QGroupBox(tr("Conduit / channel"), page);
    auto *condForm  = new QFormLayout(condGroup);

    m_forceMainCombo = new QComboBox(condGroup);
    m_forceMainCombo->addItem(tr("Hazen-Williams (H-W)"), QStringLiteral("H-W"));
    m_forceMainCombo->addItem(tr("Darcy-Weisbach (D-W)"), QStringLiteral("D-W"));
    m_forceMainCombo->setToolTip(tr("Force-main friction equation (FORCE_MAIN_EQUATION)."));
    condForm->addRow(tr("Force-main equation:"), m_forceMainCombo);

    m_normalFlowCombo = new QComboBox(condGroup);
    m_normalFlowCombo->addItem(tr("Slope"),   QStringLiteral("SLOPE"));
    m_normalFlowCombo->addItem(tr("Froude"),  QStringLiteral("FROUDE"));
    m_normalFlowCombo->addItem(tr("Both"),    QStringLiteral("BOTH"));
    m_normalFlowCombo->addItem(tr("Neither"), QStringLiteral("NEITHER"));
    m_normalFlowCombo->setToolTip(tr("Normal-flow limiter criterion (NORMAL_FLOW_LIMITED)."));
    condForm->addRow(tr("Normal-flow criterion:"), m_normalFlowCombo);

    m_inertialDampCombo = new QComboBox(condGroup);
    m_inertialDampCombo->addItem(tr("None"),    QStringLiteral("NONE"));
    m_inertialDampCombo->addItem(tr("Partial"), QStringLiteral("PARTIAL"));
    m_inertialDampCombo->addItem(tr("Full"),    QStringLiteral("FULL"));
    m_inertialDampCombo->setToolTip(tr("Inertial-term damping in dynamic-wave routing (INERTIAL_DAMPING)."));
    condForm->addRow(tr("Inertial damping:"), m_inertialDampCombo);

    m_minSurfAreaSpin = new QDoubleSpinBox(condGroup);
    m_minSurfAreaSpin->setRange(0.0, 1.0e6);
    m_minSurfAreaSpin->setDecimals(4);
    m_minSurfAreaSpin->setToolTip(
        tr("Minimum nodal surface area used in dynamic-wave routing (MIN_SURFAREA)."));
    condForm->addRow(tr("Min surface area:"), m_minSurfAreaSpin);

    m_minSlopeSpin = new QDoubleSpinBox(condGroup);
    m_minSlopeSpin->setRange(0.0, 100.0);
    m_minSlopeSpin->setDecimals(4);
    m_minSlopeSpin->setSuffix(QStringLiteral(" %"));
    m_minSlopeSpin->setToolTip(tr("Minimum conduit slope (MIN_SLOPE)."));
    condForm->addRow(tr("Min conduit slope:"), m_minSlopeSpin);

    vlay->addWidget(condGroup);
    vlay->addStretch();

    return page;
}

QWidget *SimulationOptionsDialog::buildPerformanceTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *threadsGroup = new QGroupBox(tr("Parallelisation"), page);
    auto *threadsForm  = new QFormLayout(threadsGroup);

    // Machine / OpenMP limits, queried once. The range stays 0–256 so the
    // user can deliberately oversubscribe; the suffix, tooltip and the
    // "Effective" label below show where the hardware limit is.
    swmm_get_thread_info(&m_threadInfo);

    m_threadsSpin = new QSpinBox(threadsGroup);
    m_threadsSpin->setRange(0, 256);
    m_threadsSpin->setSpecialValueText(tr("auto"));
    m_threadsSpin->setToolTip(
        tr("Number of OpenMP worker threads for the 1D and 2D solvers "
           "([OPTIONS] THREADS).\n"
           "0 = auto: the engine uses every logical processor the OpenMP "
           "runtime allows, then applies its own heuristics (model-size "
           "gates; on Apple Silicon the dynamic-wave team stays on the "
           "performance cores).\n"
           "N = exactly N threads. Values above the machine's logical "
           "processors are allowed but oversubscribe the CPU — the engine "
           "warns and the run is usually slower.\n\n%1")
            .arg(threadLimitsSummary(m_threadInfo)));
    threadsForm->addRow(tr("Wor&ker threads:"), m_threadsSpin);

    m_threadsEffective = new QLabel(threadsGroup);
    m_threadsEffective->setWordWrap(true);
    m_threadsEffective->setTextFormat(Qt::RichText);
    threadsForm->addRow(QString(), m_threadsEffective);
    connect(m_threadsSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &SimulationOptionsDialog::refreshThreadsEffectiveLabel);
    refreshThreadsEffectiveLabel();

    // ── Fast preset ────────────────────────────────────────────────────
    // One-click speed recipe for 1D/2D-coupled runs: use all worker threads
    // and floor the adaptive step so the coupling can't collapse it. On the
    // Bellinge benchmark this is ~2.6x faster with BETTER mass balance than the
    // as-shipped run.
    auto *fastBtn = new QPushButton(tr("Apply fast preset"), threadsGroup);
    fastBtn->setToolTip(
        tr("Sets THREADS = %1 (this machine's performance cores) and "
           "MINIMUM_STEP = 1.0 s — the conservative fast\n"
           "recipe for 1D/2D-coupled models (~2.6x faster, and mass balance as\n"
           "good as or better than the default). For an ~4x quick-screening run\n"
           "raise MINIMUM_STEP to 2.0 s, but note its continuity degrades.")
            .arg(fastPresetThreads()));
    QObject::connect(fastBtn, &QPushButton::clicked, this, [this]() {
        int    threads = 8;
        double minStep = 1.5;
        fastPresetValues(threads, minStep);
        if (m_threadsSpin) m_threadsSpin->setValue(threads);
        if (m_minStepSpin) m_minStepSpin->setValue(minStep);
        QMessageBox::information(
            this, tr("Fast preset applied"),
            tr("THREADS set to %1 and MINIMUM_STEP to 1.0 s.\n\n"
               "This is the conservative fast recipe for 1D/2D-coupled runs "
               "(~2.6x faster, with mass balance as good as or better than the "
               "default). Click OK / Apply to commit.").arg(threads));
    });
    threadsForm->addRow(QString(), fastBtn);

    auto *note = new QLabel(
        tr("<i>The IGNORE_* skip-process flags live on the Models / Processes tab. "
           "Future slices add more performance knobs here.</i>"),
        threadsGroup);
    note->setWordWrap(true);
    threadsForm->addRow(note);

    vlay->addWidget(threadsGroup);
    vlay->addStretch();

    return page;
}

QString SimulationOptionsDialog::threadLimitsSummary(const SWMM_ThreadInfo &ti)
{
    QStringList lines;
    lines << tr("This machine: %1 logical processors").arg(ti.logical_cpus);
    if (ti.perf_cores > 0)
        lines << tr("Performance cores: %1 (efficiency cores slow the solvers "
                    "and are avoided in auto mode)").arg(ti.perf_cores);
    if (!ti.omp_available)
        lines << tr("Engine built without OpenMP — every run is serial.");
    else if (ti.omp_max_threads < ti.logical_cpus)
        lines << tr("OpenMP limit in this process: %1 (OMP_NUM_THREADS, "
                    "OMP_THREAD_LIMIT or CPU affinity is limiting it).")
                     .arg(ti.omp_max_threads);
    if (ti.kokkos_omp_threads > 0)
        lines << tr("2D Kokkos backend already running with %1 threads "
                    "(fixed until restart).").arg(ti.kokkos_omp_threads);
    return lines.join(QLatin1Char('\n'));
}

void SimulationOptionsDialog::refreshThreadsEffectiveLabel()
{
    if (!m_threadsSpin || !m_threadsEffective) return;
    const int req = m_threadsSpin->value();

    // Suffix shows the hardware limit next to the value.
    const int logical = m_threadInfo.logical_cpus;
    const bool over = logical > 0 && req > logical;
    if (req == 0)
        m_threadsSpin->setSuffix(QString());
    else if (logical > 0)
        m_threadsSpin->setSuffix(over ? tr(" / %1 logical — oversubscribed").arg(logical)
                                      : tr(" / %1 logical").arg(logical));
    else
        m_threadsSpin->setSuffix(QString());

    // Effective counts come from the engine so this never re-implements
    // its heuristics; the engine reports 0 for a module the model lacks.
    int g = 0, dw = 0, td = 0;
    QString text;
    if (m_engine && swmm_get_effective_threads(m_engine, req, &g, &dw, &td) == SWMM_OK) {
        QStringList parts;
        parts << tr("general %1").arg(g);
        if (dw > 0) parts << tr("dynamic wave %1").arg(dw);
        if (td > 0) parts << tr("2D %1").arg(td);
        text = tr("Effective threads: %1.").arg(parts.join(QStringLiteral(" · ")));
    }
    if (over) {
        // Not-colour-alone: glyph + text carry the warning as well as colour.
        text += tr(" <span style=\"color:#D06F00\">&#9888; %1 threads exceed the "
                   "%2 logical processors — the run will be oversubscribed and "
                   "is usually slower.</span>").arg(req).arg(logical);
    } else if (m_threadInfo.perf_cores > 0 && req > m_threadInfo.perf_cores) {
        text += tr(" <span style=\"color:#D06F00\">&#9888; above the %1 "
                   "performance cores — efficiency cores slow the "
                   "barrier-synchronised solvers.</span>")
                    .arg(m_threadInfo.perf_cores);
    }
    m_threadsEffective->setText(text);
}

void SimulationOptionsDialog::updateSurchargeFieldsEnabled()
{
    if (!m_surchargeCombo) return;
    const bool dyn = m_surchargeCombo->currentData().toString()
                        == QStringLiteral("DYNAMIC_SLOT");
    const bool tpa = m_surchargeCombo->currentData().toString()
                        == QStringLiteral("TPA");
    if (m_dpsCelerSpin) m_dpsCelerSpin->setEnabled(dyn);
    if (m_dpsAlphaSpin) m_dpsAlphaSpin->setEnabled(dyn);
    if (m_dpsDecaySpin) m_dpsDecaySpin->setEnabled(dyn);
    if (m_tpaCeleritySpin) m_tpaCeleritySpin->setEnabled(tpa);
}

void SimulationOptionsDialog::updateFvFieldsEnabled()
{
    if (!m_routingCombo) return;
    const bool fv = m_routingCombo->currentData().toString()
                        == QStringLiteral("FV");
    if (m_fvGroup)     m_fvGroup->setEnabled(fv);
    if (m_fvPerfGroup) m_fvPerfGroup->setEnabled(fv);
    // The limiter only applies to 2nd-order reconstruction, and the tier
    // count only matters while local time stepping is on. Setting these on
    // a disabled group is harmless — Qt ANDs enabled state down the tree.
    if (m_fvLimiterCombo && m_fvOrderCombo)
        m_fvLimiterCombo->setEnabled(
            m_fvOrderCombo->currentData().toString() == QStringLiteral("2"));
    if (m_fvLtsTiersSpin && m_fvLtsBox)
        m_fvLtsTiersSpin->setEnabled(m_fvLtsBox->isChecked());
    // Unsteady friction applies to both dynamic-wave and FV routing;
    // m_ufSupported carries the applyEngineConstraints() capability probe
    // so a routing-combo change cannot re-enable the group on an engine
    // without the keys. k3 on a disabled group is harmless (Qt ANDs
    // enabled state down the tree, same as the limiter above).
    const bool ufRouting = fv || m_routingCombo->currentData().toString()
                                     == QStringLiteral("DYNWAVE");
    if (m_ufGroup) m_ufGroup->setEnabled(m_ufSupported && ufRouting);
    if (m_ufK3Spin && m_ufMethodCombo)
        m_ufK3Spin->setEnabled(m_ufMethodCombo->currentData().toString()
                                   != QStringLiteral("NONE"));
}

// ---------------------------------------------------------------------------
// Quality & Transport (Y1 / GUI plan G1g)
// ---------------------------------------------------------------------------
// Scope note (see the Y1 handoff §2): only keys the engine's C API actually
// exposes are edited here. The Eulerian ARD engine reads its scheme,
// dispersion and target-dx from its transport.ard COMPONENT file (engine
// D-UT8), not from [OPTIONS], so the ARD group carries a pointer to that
// binding rather than duplicating the FV page's FV_* widgets — two widgets
// writing one key is a defect, not a convenience.

QWidget *SimulationOptionsDialog::buildQualityTransportTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // ── Solver selection ───────────────────────────────────────────────
    auto *solGroup = new QGroupBox(tr("Water quality solver"), page);
    auto *solForm  = new QFormLayout(solGroup);

    m_qualitySolverCombo = new QComboBox(solGroup);
    m_qualitySolverCombo->addItem(tr("Legacy (complete mix)"),
                                  QStringLiteral("LEGACY"));
    m_qualitySolverCombo->addItem(tr("Eulerian ARD (advection–reaction–dispersion)"),
                                  QStringLiteral("EULERIAN_ARD"));
    m_qualitySolverCombo->addItem(tr("Lagrangian (LARD)"),
                                  QStringLiteral("LAGRANGIAN"));
    m_qualitySolverCombo->setToolTip(
        tr("Transport engine for pollutants and reserved species "
           "(option QUALITY_SOLVER)."));
    solForm->addRow(tr("Sol&ver:"), m_qualitySolverCombo);

    // Boundary re-entry quality at outfalls — solver-independent (both
    // engines honor it), so it lives with the solver selection rather
    // than in a per-solver group.
    m_outfallBackflowCombo = new QComboBox(solGroup);
    m_outfallBackflowCombo->addItem(tr("Hold last concentration (legacy)"),
                                    QStringLiteral("LAST"));
    m_outfallBackflowCombo->addItem(tr("Fresh (zero concentration and age)"),
                                    QStringLiteral("ZERO"));
    m_outfallBackflowCombo->setToolTip(
        tr("Quality carried by reverse flow at outfalls (option "
           "OUTFALL_BACKFLOW_QUALITY). \"Hold last\" re-injects the "
           "outfall's held state — under water age that water keeps aging "
           "with the clock, so a permanently supplying outfall grows old "
           "without bound. \"Fresh\" makes a supplying outfall an "
           "EPANET-style reservoir: backflow enters with zero "
           "concentration and zero age."));
    solForm->addRow(tr("Outfall &backflow:"), m_outfallBackflowCombo);
    vlay->addWidget(solGroup);

    // ── Eulerian ARD ───────────────────────────────────────────────────
    m_ardGroup = new QGroupBox(tr("Eulerian ARD"), page);
    auto *ardLay = new QVBoxLayout(m_ardGroup);
    auto *ardNote = new QLabel(
        tr("Dispersion and transport mesh spacing for the Eulerian ARD "
           "engine are configured in its component file (<i>model.ard</i>: "
           "[TRANSPORT_OPTIONS], [CONDUIT_DISPERSION]), bound on the Files / "
           "Output / Plugins page; a component file also overrides the "
           "scalar scheme below. The Lagrangian solver's dispersion is the "
           "DISPERSION option in its own group."),
        m_ardGroup);
    ardNote->setWordWrap(true);
    ardLay->addWidget(ardNote);

    // FV_SCALAR_SCHEME lives here, not on the Routing & Hydraulics page: its
    // only live consumer is the ARD engine, which reads it under any routing
    // model (FV routing itself transports no species). Gated with the group
    // on QUALITY_SOLVER == EULERIAN_ARD by updateQualitySolverFieldsEnabled.
    auto *ardForm = new QFormLayout();
    m_fvScalarSchemeCombo = new QComboBox(m_ardGroup);
    m_fvScalarSchemeCombo->addItem(tr("MUSCL"),              QStringLiteral("MUSCL"));
    m_fvScalarSchemeCombo->addItem(tr("Upwind"),             QStringLiteral("UPWIND"));
    m_fvScalarSchemeCombo->addItem(tr("QUICKEST-ULTIMATE"),  QStringLiteral("QUICKEST_ULTIMATE"));
    m_fvScalarSchemeCombo->setToolTip(
        tr("Advection scheme for water-quality scalars on the ARD transport "
           "mesh (FV_SCALAR_SCHEME). Read under any routing model; a bound "
           "component file's [TRANSPORT_OPTIONS] SCALAR_SCHEME overrides it."));
    ardForm->addRow(tr("Scalar scheme:"), m_fvScalarSchemeCombo);
    ardLay->addLayout(ardForm);
    vlay->addWidget(m_ardGroup);

    // ── Lagrangian (LARD) ──────────────────────────────────────────────
    m_lardGroup = new QGroupBox(tr("Lagrangian (LARD)"), page);
    auto *lardForm = new QFormLayout(m_lardGroup);

    m_qualityStepSpin = new QDoubleSpinBox(m_lardGroup);
    m_qualityStepSpin->setRange(0.0, 3600.0);
    m_qualityStepSpin->setDecimals(2);
    m_qualityStepSpin->setSuffix(QStringLiteral(" s"));
    m_qualityStepSpin->setToolTip(
        tr("Transport substep (QUALITY_STEP). 0 follows the routing step; "
           "smaller values refine transport without changing hydraulics."));
    lardForm->addRow(tr("Quality &step:"), m_qualityStepSpin);

    m_maxSegmentsSpin = new QSpinBox(m_lardGroup);
    m_maxSegmentsSpin->setRange(2, 10000);
    m_maxSegmentsSpin->setToolTip(
        tr("Segment slab capacity per link (MAX_SEGMENTS_PER_LINK)."));
    lardForm->addRow(tr("Max se&gments per link:"), m_maxSegmentsSpin);

    m_dispersionCombo = new QComboBox(m_lardGroup);
    m_dispersionCombo->addItem(tr("Off"),  QStringLiteral("OFF"));
    m_dispersionCombo->addItem(tr("RWPT (random-walk particle tracking)"),
                               QStringLiteral("RWPT"));
    m_dispersionCombo->setToolTip(
        tr("Longitudinal dispersion from resolved vertical shear "
           "(option DISPERSION)."));
    lardForm->addRow(tr("&Dispersion:"), m_dispersionCombo);

    m_rwptSeedSpin = new QSpinBox(m_lardGroup);
    m_rwptSeedSpin->setRange(-2147483647, 2147483647);
    m_rwptSeedSpin->setToolTip(
        tr("Deterministic RWPT seed (RWPT_SEED) — the same seed reproduces "
           "a run bit-for-bit at any thread count."));
    lardForm->addRow(tr("RWPT s&eed:"), m_rwptSeedSpin);
    vlay->addWidget(m_lardGroup);

    // ── Reserved species ───────────────────────────────────────────────
    auto *resGroup = new QGroupBox(tr("Reserved species"), page);
    auto *resLay   = new QVBoxLayout(resGroup);
    m_waterAgeBox = new QCheckBox(tr("Track water age"), resGroup);
    m_waterAgeBox->setToolTip(
        tr("Transported water age as the reserved species __WATER_AGE__, "
           "reported in hours (option WATER_AGE)."));
    m_heatTransportBox = new QCheckBox(tr("Simulate heat transport"), resGroup);
    m_heatTransportBox->setToolTip(
        tr("Transported temperature as the reserved species "
           "__TEMPERATURE__, reported in °C (option HEAT_TRANSPORT)."));
    resLay->addWidget(m_waterAgeBox);
    resLay->addWidget(m_heatTransportBox);
    auto *resNote = new QLabel(
        tr("Per-source initial ages are edited below; heat meteorology is "
           "configured in its component file until the heat page arrives."),
        resGroup);
    resNote->setWordWrap(true);
    resLay->addWidget(resNote);

    // Y3b: the Water Age Sources editor, reachable from the page that
    // enables the tracking it configures.
    auto *ageSrcBtn = new QPushButton(tr("Edit Source Ages..."), resGroup);
    ageSrcBtn->setObjectName(QStringLiteral("qt_editAgeSourcesBtn"));
    ageSrcBtn->setToolTip(
        tr("Initial age of water entering by each source pathway "
           "(WATER_AGE_SOURCES). Negative ages extract age-volume."));
    connect(ageSrcBtn, &QPushButton::clicked, this, [this]() {
        OpenSWMMVis::WaterAgeSourcesDialog dlg(m_engine, this);
        dlg.exec();   // writes straight to the engine; OK/Cancel is its own
    });
    resLay->addWidget(ageSrcBtn);

    // G-A1: the per-element Initial Quality editor, reachable from the
    // page that configures the quality run it seeds.
    auto *initQualBtn = new QPushButton(tr("Edit Initial Quality..."),
                                        resGroup);
    initQualBtn->setObjectName(QStringLiteral("qt_editInitialQualityBtn"));
    initQualBtn->setToolTip(
        tr("Per-node and per-link initial concentrations "
           "(INITIAL_QUALITY), overriding the global Cinit."));
    connect(initQualBtn, &QPushButton::clicked, this, [this]() {
        OpenSWMMVis::InitialQualityDialog dlg(m_engine, this);
        dlg.exec();   // writes straight to the engine; OK/Cancel is its own
    });
    resLay->addWidget(initQualBtn);
    vlay->addWidget(resGroup);

    vlay->addStretch();

    connect(m_qualitySolverCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateQualitySolverFieldsEnabled(); });
    connect(m_dispersionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateQualitySolverFieldsEnabled(); });

    return page;
}

void SimulationOptionsDialog::updateQualitySolverFieldsEnabled()
{
    if (!m_qualitySolverCombo) return;
    const QString solver = m_qualitySolverCombo->currentData().toString();
    if (m_ardGroup)
        m_ardGroup->setEnabled(solver == QLatin1String("EULERIAN_ARD"));
    if (m_lardGroup)
        m_lardGroup->setEnabled(solver == QLatin1String("LAGRANGIAN"));
    // The seed only matters while RWPT is on. Setting it on a disabled
    // group is harmless — Qt ANDs enabled state down the tree.
    if (m_rwptSeedSpin && m_dispersionCombo)
        m_rwptSeedSpin->setEnabled(
            m_dispersionCombo->currentData().toString()
                == QLatin1String("RWPT"));
}

void SimulationOptionsDialog::updateDurationLabel()
{
    if (!m_durationLabel || !m_startEdit || !m_endEdit) return;
    const qint64 secs = m_startEdit->dateTime().secsTo(m_endEdit->dateTime());
    if (secs <= 0) {
        m_durationLabel->setText(QStringLiteral("—"));
        return;
    }
    const qint64 days  = secs / 86400;
    const qint64 hours = (secs % 86400) / 3600;
    const qint64 mins  = (secs % 3600) / 60;
    const qint64 ss    = secs % 60;
    m_durationLabel->setText(
        QString::asprintf("%lldd %02lld:%02lld:%02lld",
                          static_cast<long long>(days),
                          static_cast<long long>(hours),
                          static_cast<long long>(mins),
                          static_cast<long long>(ss)));
}

// ---------------------------------------------------------------------------
// Tab 5 — Spatial & CRS
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::buildSpatialTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *crsGroup = new QGroupBox(tr("Coordinate reference system"), page);
    auto *crsForm  = new QFormLayout(crsGroup);

    auto *crsRow = new QWidget(crsGroup);
    auto *crsRowLay = new QHBoxLayout(crsRow);
    crsRowLay->setContentsMargins(0, 0, 0, 0);
    m_crsLabel = new QLabel(tr("(unknown)"), crsRow);
    m_crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_crsChangeButton = new QToolButton(crsRow);
    m_crsChangeButton->setText(tr("Change…"));
    m_crsChangeButton->setToolTip(tr("Open the CRS picker (writes to swmm_spatial_set_crs)."));
    m_crsDetectButton = new QToolButton(crsRow);
    m_crsDetectButton->setText(tr("Detect from coordinates"));
    m_crsDetectButton->setToolTip(tr(
        "Inspect the model's coordinate ranges and suggest EPSG:4326 if all\n"
        "coordinates fall within geographic bounds (±180° lon, ±85° lat)."));
    crsRowLay->addWidget(m_crsLabel, 1);
    crsRowLay->addWidget(m_crsChangeButton);
    crsRowLay->addWidget(m_crsDetectButton);
    crsForm->addRow(tr("Layer CRS:"), crsRow);

    m_extentLabel = new QLabel(tr("(extent unavailable)"), crsGroup);
    m_extentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_extentLabel->setWordWrap(true);
    crsForm->addRow(tr("Model extent:"), m_extentLabel);

    auto *note = new QLabel(
        tr("<i>Changing the CRS here updates the layer's stored CRS only. "
           "To permanently transform stored coordinates, use the canvas-CRS "
           "button on the status bar (Phase 0.7 reproject prompt).</i>"),
        crsGroup);
    note->setWordWrap(true);
    crsForm->addRow(note);

    vlay->addWidget(crsGroup);
    vlay->addStretch();

    return page;

    connect(m_crsChangeButton, &QToolButton::clicked,
            this, &SimulationOptionsDialog::onSpatialPickCRS);
    connect(m_crsDetectButton, &QToolButton::clicked,
            this, &SimulationOptionsDialog::onSpatialDetectCRS);

    if (!m_layer)
    {
        m_crsChangeButton->setEnabled(false);
        m_crsDetectButton->setEnabled(false);
    }
}

void SimulationOptionsDialog::refreshSpatialSummary()
{
    if (!m_crsLabel) return;

    if (m_layer)
    {
        if (auto *srs = m_layer->srs())
        {
            const QString auth = srs->toAuthority();
            m_crsLabel->setText(auth.isEmpty() ? tr("(local)") : auth);
        }
        else
        {
            m_crsLabel->setText(tr("(none)"));
        }

        const MapExtent ext = m_layer->extent();
        if (ext.isValid())
        {
            m_extentLabel->setText(
                tr("X: [%1, %2]   Y: [%3, %4]")
                    .arg(ext.xMin(), 0, 'g', 8)
                    .arg(ext.xMax(), 0, 'g', 8)
                    .arg(ext.yMin(), 0, 'g', 8)
                    .arg(ext.yMax(), 0, 'g', 8));
        }
        else
        {
            m_extentLabel->setText(tr("(extent invalid / not yet computed)"));
        }
    }
    else
    {
        m_crsLabel->setText(tr("(no layer)"));
        m_extentLabel->setText(tr("(no layer)"));
    }
}

void SimulationOptionsDialog::onSpatialPickCRS()
{
    if (!m_layer) return;
    CRSSelectionDialog dlg(this);
    dlg.setCurrentCRS(m_layer->srs());
    if (dlg.exec() != QDialog::Accepted) return;
    SpatialReferenceSystem *srs = dlg.selectedSRS();
    if (!srs) return;

    m_layer->setSRS(srs, true);
    // Also write the engine's CRS option so it round-trips through .inp save.
    setOption("CRS", srs->toAuthority().isEmpty() ? srs->toWkt() : srs->toAuthority());
    m_wroteChanges = true;
    refreshSpatialSummary();
}

void SimulationOptionsDialog::onSpatialDetectCRS()
{
    if (!m_layer) return;
    const MapExtent ext = m_layer->extent();
    if (!ext.isValid())
        return;

    const bool inGeographic =
        ext.xMin() >= -180.0 && ext.xMax() <=  180.0 &&
        ext.yMin() >=  -90.0 && ext.yMax() <=   90.0;

    if (inGeographic)
    {
        // Suggest EPSG:4326. Update the label as a hint; the user must press
        // Change… to actually apply (so detection is non-destructive).
        m_crsLabel->setText(tr("(suggested: EPSG:4326 — press Change… to apply)"));
    }
    else
    {
        m_crsLabel->setText(tr(
            "(coordinates exceed geographic bounds — pick a projected CRS)"));
    }
}

// ---------------------------------------------------------------------------
// Mesh tab — Slice AU file-management for 2D mesh configurations
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::buildMeshTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *header = new QLabel(tr(
        "Pick which 2D mesh configuration the engine reads: an external "
        "mesh file (.2dm, referenced via [2D_MESH_FILE]) or the inline mesh "
        "embedded in the project .inp. The list shows the .2dm files sitting "
        "next to the project — use Import… to bring one in from elsewhere on "
        "disk. New meshes are generated from the editing toolbar's Generate "
        "Mesh tool."), page);
    header->setWordWrap(true);
    vlay->addWidget(header);

    m_meshDirLabel = new QLabel(page);
    m_meshDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meshDirLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    // Both labels below carry a filesystem path. Unwrapped, their
    // minimumSizeHint is the full single-line width, which a deep project
    // directory pushes past the page — the whole Mesh page then scrolls
    // horizontally and clips the text above. Wrapping keeps the path fully
    // visible (and selectable) without dictating the page width.
    m_meshDirLabel->setWordWrap(true);
    vlay->addWidget(m_meshDirLabel);

    m_meshList = new QListWidget(page);
    m_meshList->setSelectionMode(QAbstractItemView::SingleSelection);
    vlay->addWidget(m_meshList, 1);

    // Two rows of two, not one row of four: four side-by-side push buttons are
    // wider than this page's viewport at the dialog's natural width, which
    // pushed the whole page into a horizontal scroll and clipped the text above.
    auto *btnRow = new QGridLayout;
    auto *btnSetActive = new QPushButton(tr("Set Active"), page);
    btnSetActive->setToolTip(tr("Patch [2D_MESH_FILE] to point at the "
                                 "selected configuration."));
    auto *btnRemove    = new QPushButton(tr("Remove"), page);
    btnRemove->setToolTip(tr("Delete the selected .2dm from disk."));
    auto *btnImport    = new QPushButton(tr("Import…"), page);
    btnImport->setToolTip(tr("Browse for an existing .2dm anywhere on disk, "
                              "copy it into the project folder and load it as "
                              "the active mesh."));
    auto *btnRefresh   = new QPushButton(tr("Refresh"), page);
    btnRow->addWidget(btnSetActive, 0, 0);
    btnRow->addWidget(btnRemove,    0, 1);
    btnRow->addWidget(btnImport,    1, 0);
    btnRow->addWidget(btnRefresh,   1, 1);
    btnRow->setColumnStretch(2, 1);   // keep the block left-aligned
    vlay->addLayout(btnRow);

    m_meshActiveLabel = new QLabel(page);
    m_meshActiveLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meshActiveLabel->setWordWrap(true);   // may hold an absolute mesh path
    vlay->addWidget(m_meshActiveLabel);

    connect(btnRefresh, &QPushButton::clicked, this,
            &SimulationOptionsDialog::refreshMeshList);
    connect(btnSetActive, &QPushButton::clicked, this,
            &SimulationOptionsDialog::onMeshSetActive);
    connect(btnRemove, &QPushButton::clicked, this,
            &SimulationOptionsDialog::onMeshRemove);
    connect(btnImport, &QPushButton::clicked, this,
            &SimulationOptionsDialog::onMeshImport);
    // Importing needs a project window to attach the mesh layer to.
    btnImport->setEnabled(m_projectWindow != nullptr);

    refreshMeshList();
    return page;
}

void SimulationOptionsDialog::refreshMeshList()
{
    if (!m_meshList || !m_meshDirLabel || !m_meshActiveLabel) return;
    m_meshList->clear();

    // Search the directory next to the active model (.inp). Without a
    // layer (e.g. dialog opened against a synthesized blank project)
    // we silently no-op — Generate New will create the first mesh.
    QString modelPath;
    if (m_layer) modelPath = m_layer->modelFilePath();
    const QFileInfo modelFi(modelPath);
    const QDir dir = modelPath.isEmpty() ? QDir() : modelFi.absoluteDir();

    m_meshDirLabel->setText(modelPath.isEmpty()
        ? tr("Search directory: <none — save the project first>")
        : tr("Search directory: %1").arg(dir.absolutePath()));

    // Read the .inp once: needed both to discover an inline mesh (embedded
    // [2D_*] sections, no sibling .2dm) and to read the current
    // [2D_MESH_FILE] reference.
    QString inpText;
    if (!modelPath.isEmpty())
    {
        QFile f(modelPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            inpText = QString::fromUtf8(f.readAll());
    }

    // External configurations: one row per sibling *.2dm file.
    if (!modelPath.isEmpty())
    {
        const QStringList meshes = dir.entryList(
            QStringList{QStringLiteral("*.2dm")},
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &name : meshes)
        {
            auto *item = new QListWidgetItem(name);
            item->setData(kMeshKindRole, kMeshExternal);
            m_meshList->addItem(item);
        }
    }

    // Inline configuration: the engine reads mesh geometry straight from the
    // .inp when [2D_VERTICES] + [2D_TRIANGLES] are present. Surface it as a
    // selectable row (pinned to the top) so a freshly-generated inline mesh
    // appears here and the user can switch back to it from an external file.
    const bool hasInline =
        inpText.indexOf(QStringLiteral("[2D_VERTICES]"),  0, Qt::CaseInsensitive) >= 0 &&
        inpText.indexOf(QStringLiteral("[2D_TRIANGLES]"), 0, Qt::CaseInsensitive) >= 0;
    if (hasInline)
    {
        auto *item = new QListWidgetItem(
            tr("(Inline mesh — embedded in project .inp)"));
        item->setData(kMeshKindRole, kMeshInline);
        m_meshList->insertItem(0, item);
    }

    // Probe the .inp for a current [2D_MESH_FILE] reference. Tolerant — the
    // section may be absent (engine reads the inline mesh, if any).
    QString active;
    {
        const int sectIdx = inpText.indexOf(QStringLiteral("[2D_MESH_FILE]"),
                                             0, Qt::CaseInsensitive);
        if (sectIdx >= 0)
        {
            // Walk forward to the first non-comment, non-blank line and pull
            // the FILE token.
            int p = inpText.indexOf(QChar('\n'), sectIdx);
            while (p > 0 && p < inpText.size())
            {
                const int nl = inpText.indexOf(QChar('\n'), p + 1);
                const QString line = inpText.mid(p + 1, (nl < 0 ? inpText.size() : nl) - p - 1).trimmed();
                if (!line.isEmpty() && !line.startsWith(QStringLiteral(";"))
                    && !line.startsWith(QChar('[')))
                {
                    // Format: "FILE  <path>".
                    const auto parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts.first().compare(
                            QStringLiteral("FILE"), Qt::CaseInsensitive) == 0)
                        active = parts.mid(1).join(QChar(' '));
                    break;
                }
                if (line.startsWith(QChar('['))) break;  // next section
                if (nl < 0) break;
                p = nl;
            }
        }
    }

    // The active configuration is the external file when [2D_MESH_FILE] is
    // present, otherwise the inline mesh (if any). Reflect that in the label
    // and pre-select the matching row.
    if (!active.isEmpty())
    {
        m_meshActiveLabel->setText(tr("Active mesh reference: %1").arg(active));
        const QString activeName = QFileInfo(active).fileName();
        for (int i = 0; i < m_meshList->count(); ++i)
            if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshExternal
                && m_meshList->item(i)->text() == activeName)
                m_meshList->setCurrentRow(i);
    }
    else if (hasInline)
    {
        m_meshActiveLabel->setText(
            tr("Active mesh: inline mesh embedded in project .inp"));
        for (int i = 0; i < m_meshList->count(); ++i)
            if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshInline)
                m_meshList->setCurrentRow(i);
    }
    else
    {
        m_meshActiveLabel->setText(
            tr("Active mesh reference: <none — generate a 2D mesh first>"));
    }
}

void SimulationOptionsDialog::onMeshSetActive()
{
    if (!m_meshList || !m_layer) return;

    QListWidgetItem *item = m_meshList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Set Active Mesh"),
            tr("Select a mesh (.2dm) from the list first."));
        return;
    }

    const QString modelPath = m_layer->modelFilePath();
    if (modelPath.isEmpty()) {
        QMessageBox::warning(this, tr("Set Active Mesh"),
            tr("Save the project first — the [2D_MESH_FILE] reference is "
               "written into the .inp on disk."));
        return;
    }

    QString err;
    if (item->data(kMeshKindRole).toInt() == kMeshInline)
    {
        // Inline mesh: drop any [2D_MESH_FILE] reference so the engine reads
        // the mesh sections embedded directly in the .inp.
        if (!mesh::InpMeshWriter::clearMeshFileRef(modelPath, &err)) {
            QMessageBox::critical(this, tr("Set Active Mesh"),
                tr("Could not switch to the inline mesh:\n%1").arg(err));
            return;
        }
        // Mirror into the engine's in-memory model so a save doesn't re-add a
        // stale reference. Empty clears it (engine reverts to inline mesh).
        if (m_engine)
            swmm_options_set_ext(m_engine, "MESH_FILE", "");
    }
    else
    {
        // External mesh: point [2D_MESH_FILE] at the selected .2dm.
        const QString meshPath =
            QFileInfo(modelPath).absoluteDir().absoluteFilePath(item->text());
        if (!mesh::InpMeshWriter::writeMeshFileRef(modelPath, meshPath, &err)) {
            QMessageBox::critical(this, tr("Set Active Mesh"),
                tr("Could not update [2D_MESH_FILE]:\n%1").arg(err));
            return;
        }
        // Mirror the reference into the engine's in-memory model. Without this
        // the engine re-serialises the .inp on the next save with mesh_file
        // empty and drops [2D_MESH_FILE] — the model silently reverts to 1D.
        if (m_engine)
            swmm_options_set_ext(m_engine, "MESH_FILE",
                                 item->text().toUtf8().constData());
    }

    // Selecting an active mesh implies the user wants 2D on. Flip the
    // module checkbox so the corresponding tab + persistence follow.
    if (m_module2DBox && !m_module2DBox->isChecked())
        m_module2DBox->setChecked(true);

    refreshMeshList();
}

void SimulationOptionsDialog::onMeshRemove()
{
    if (!m_meshList || !m_layer) return;

    QListWidgetItem *item = m_meshList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("Select a mesh (.2dm) from the list first."));
        return;
    }

    if (item->data(kMeshKindRole).toInt() == kMeshInline) {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("The inline mesh is embedded in the project .inp — it can't be "
               "deleted from here. Re-generate the mesh, or set an external "
               ".2dm active, to replace it."));
        return;
    }

    const QString modelPath = m_layer->modelFilePath();
    if (modelPath.isEmpty()) return;

    const QString name     = item->text();
    const QString meshPath =
        QFileInfo(modelPath).absoluteDir().absoluteFilePath(name);

    const bool isActive = m_meshActiveLabel &&
        m_meshActiveLabel->text().contains(name);
    const QString question = isActive
        ? tr("\"%1\" is the active [2D_MESH_FILE] reference. Deleting it will "
             "clear the active mesh and disable 2D Surface Routing for this "
             "model.\n\nDelete it anyway?").arg(name)
        : tr("Delete \"%1\" from disk? This cannot be undone.").arg(name);

    if (QMessageBox::question(this, tr("Remove Mesh"), question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    QFile f(meshPath);
    if (f.exists() && !f.remove()) {
        QMessageBox::critical(this, tr("Remove Mesh"),
            tr("Could not delete %1:\n%2").arg(meshPath, f.errorString()));
        return;
    }

    // Removing the ACTIVE mesh must not leave the model half-2D: null the
    // [2D_MESH_FILE] reference in the live engine and switch the 2D module
    // off (unchecking writes IGNORE_2D YES on OK, so the next run — which
    // auto-saves and re-opens the .inp — genuinely runs 1D-only).
    if (isActive) {
        if (m_engine)
            swmm_options_set_ext(m_engine, "MESH_FILE", "");
        if (m_module2DBox && m_module2DBox->isChecked())
            m_module2DBox->setChecked(false);
    }

    refreshMeshList();
}

void SimulationOptionsDialog::onMeshImport()
{
    if (!m_projectWindow) return;

    // Anywhere on disk — the whole point of this button is that the list above
    // can only ever show .2dm files already sitting next to the project.
    const QString modelPath = m_layer ? m_layer->modelFilePath() : QString();
    const QString startDir  = modelPath.isEmpty()
        ? QDir::homePath()
        : QFileInfo(modelPath).absolutePath();

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import 2D Mesh"), startDir,
        tr("2D Mesh — SWMMVis or SMS 2DM (*.2dm);;All Files (*)"));
    if (path.isEmpty()) return;

    // The project window owns the copy-into-project, parse and canvas
    // adoption; the outcome comes back once, asynchronously. The mesh becomes
    // the active layer, so [2D_MESH_FILE] follows it on the next save — the
    // list below just needs to re-read the folder.
    connect(m_projectWindow, &SWMMVisProjectWindow::meshImportFinished, this,
            [this](bool ok, const QString &message, const QString &meshPath) {
                if (!ok) {
                    if (!message.isEmpty())
                        QMessageBox::warning(this, tr("Import 2D Mesh"), message);
                    return;
                }
                refreshMeshList();
                // Select the imported file and run it through Set Active, so
                // the [2D_MESH_FILE] reference this tab reports (and the .inp
                // on disk) match the layer the import just activated.
                const QString name = QFileInfo(meshPath).fileName();
                for (int i = 0; m_meshList && i < m_meshList->count(); ++i) {
                    if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshExternal
                        && m_meshList->item(i)->text() == name) {
                        m_meshList->setCurrentRow(i);
                        onMeshSetActive();
                        break;
                    }
                }
            },
            static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));

    m_projectWindow->importMeshFileAsync(path);
}

void SimulationOptionsDialog::on2DModuleToggled(bool enabled)
{
    // Reached from the checkbox's toggled() signal, i.e. a real user action —
    // readFromEngine() blocks the signal and calls this directly, so seeding
    // the box does not count. From here on the box carries an intent worth
    // writing to IGNORE_2D.
    if (sender() == m_module2DBox) m_module2DIntentKnown = true;

    // Only the 2D Surface Routing solver-parameter page follows the module
    // toggle. The Mesh page is always interactive — mesh creation is what
    // flips the module on, so gating it here would be circular.
#ifdef OPENSWMM_HAS_2D
    set2DRowEnabled(enabled);
#else
    Q_UNUSED(enabled);
#endif
}

#ifdef OPENSWMM_HAS_2D

// ---------------------------------------------------------------------------
// Tab 6 — 2D Surface Routing  (only present when the engine ships the 2D module)
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::build2DTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // The explicit local-inertial marcher is the only 2D integrator (D2
    // retirement of the CVODE/ARKODE stack, 2026-07-29) — no selector, and
    // the marcher settings are always live.
    auto *stepGroup = new QGroupBox(tr("Time stepping"), page);
    auto *stepForm  = new QFormLayout(stepGroup);

    m_maxTimestepSpin = new QDoubleSpinBox(stepGroup);
    m_maxTimestepSpin->setRange(0.001, 3600.0);
    m_maxTimestepSpin->setDecimals(4);
    m_maxTimestepSpin->setSuffix(QStringLiteral(" s"));
    m_maxTimestepSpin->setToolTip(
        tr("Upper bound on the marcher's CFL substeps and on the 1D↔2D "
           "co-advance sync batch (default 10 s)."));
    stepForm->addRow(tr("Max timestep:"), m_maxTimestepSpin);

    vlay->addWidget(stepGroup);

    m_marcherGroup = new QGroupBox(tr("Explicit marcher"), page);
    auto *marchForm = new QFormLayout(m_marcherGroup);

    m_thetaSpin = new QDoubleSpinBox(m_marcherGroup);
    m_thetaSpin->setRange(0.05, 1.0);
    m_thetaSpin->setDecimals(2);
    m_thetaSpin->setSingleStep(0.05);
    m_thetaSpin->setToolTip(
        tr("θ-weighting of the face discharge in the momentum update. 1.0 is "
           "the classic Bates (2010) scheme; values below 1 blend in the "
           "neighbour discharges, damping thin-film checkerboarding on steep "
           "faces (default 0.8)."));
    marchForm->addRow(tr("Momentum &θ:"), m_thetaSpin);

    m_cflNumberSpin = new QDoubleSpinBox(m_marcherGroup);
    m_cflNumberSpin->setRange(0.05, 1.0);
    m_cflNumberSpin->setDecimals(2);
    m_cflNumberSpin->setSingleStep(0.05);
    m_cflNumberSpin->setToolTip(
        tr("Courant safety factor α on each cell's stable step "
           "dt = α·L/√(g·h) (default 0.7)."));
    marchForm->addRow(tr("CFL number:"), m_cflNumberSpin);

    m_ltsTiersSpin = new QSpinBox(m_marcherGroup);
    m_ltsTiersSpin->setRange(1, 8);
    m_ltsTiersSpin->setToolTip(
        tr("Local-timestepping tiers K: cells march at power-of-two multiples "
           "of the finest step (tier k fires every 2^k substeps). 1 = global "
           "timestep (debug/equivalence mode); raise toward 7–8 for meshes "
           "with extreme cell-size disparity (default 4)."));
    marchForm->addRow(tr("LTS tiers:"), m_ltsTiersSpin);

    m_hMoveSpin = new QDoubleSpinBox(m_marcherGroup);
    m_hMoveSpin->setRange(0.0, 1.0);
    m_hMoveSpin->setDecimals(4);
    m_hMoveSpin->setSuffix(QStringLiteral(" m"));
    m_hMoveSpin->setToolTip(
        tr("Movement threshold: cells shallower than this stay in the lazy "
           "(source-only) set — rain over thin films costs nothing until "
           "water must move. Raising it shrinks the active set and speeds "
           "up drainage tails (default 0.003 m)."));
    marchForm->addRow(tr("Movement threshold:"), m_hMoveSpin);

    // MOMENTUM_EQUATION (engine 2026-09-06): the marcher's momentum closure.
    m_momentum2DCombo = new QComboBox(m_marcherGroup);
    m_momentum2DCombo->addItem(tr("Local inertial (default)"),
                               QStringLiteral("LOCAL_INERTIAL"));
    m_momentum2DCombo->addItem(tr("Full shallow-water (HLLC, shock capturing)"),
                               QStringLiteral("FULL_SWE"));
    m_momentum2DCombo->addItem(tr("Diffusive wave"),
                               QStringLiteral("DIFFUSIVE_WAVE"));
    m_momentum2DCombo->setToolTip(
        tr("Momentum closure of the explicit 2D marcher. Local inertial: "
           "de Almeida–Bates face update (valid Fr < ~0.5). Full shallow-water: "
           "conservative Godunov/HLLC with the convective term — transcritical "
           "flow, hydraulic jumps and dam breaks; 2–3× the cost. Diffusive "
           "wave: Manning quasi-steady flux, no inertia (Δx²-bound steps)."));
    marchForm->addRow(tr("Momentum equation:"), m_momentum2DCombo);

    m_reconOrder2DSpin = new QSpinBox(m_marcherGroup);
    m_reconOrder2DSpin->setRange(1, 2);
    m_reconOrder2DSpin->setToolTip(
        tr("Full shallow-water only: 1 = first-order Godunov, 2 = MUSCL "
           "(Barth–Jespersen) + SSP-RK2 (runs in global-dt mode)."));
    marchForm->addRow(tr("Reconstruction order:"), m_reconOrder2DSpin);

    m_froudeMaxSpin = new QDoubleSpinBox(m_marcherGroup);
    m_froudeMaxSpin->setRange(0.1, 5.0);
    m_froudeMaxSpin->setDecimals(2);
    m_froudeMaxSpin->setSingleStep(0.1);
    m_froudeMaxSpin->setToolTip(
        tr("Froude-number cap on face discharge — the supercritical guard for "
           "the local-inertial scheme (default 1.5)."));
    marchForm->addRow(tr("Max Froude number:"), m_froudeMaxSpin);

    m_advection2DBox = new QCheckBox(
        tr("Convective momentum flux (ADVECTION)"), m_marcherGroup);
    m_advection2DBox->setToolTip(
        tr("Include the convective momentum flux at interior faces "
           "(Stelling–Duinmeijer staggered upwind form). Restores velocity "
           "head on transcritical reaches and correct bore states; off "
           "reproduces the established pure local-inertial results."));
    marchForm->addRow(QString(), m_advection2DBox);

    vlay->addWidget(m_marcherGroup);

    // Same shape as the FV tab's "Finite volume performance" group: the
    // model's own backend request, persisted as [2D_OPTIONS] BACKEND.
    auto *perfGroup = new QGroupBox(tr("Performance"), page);
    auto *perfForm  = new QFormLayout(perfGroup);

    m_backend2DCombo = new QComboBox(perfGroup);
    m_backend2DCombo->addItem(tr("Auto"),                   QStringLiteral("AUTO"));
    m_backend2DCombo->addItem(tr("CPU (built-in marcher)"), QStringLiteral("CPU"));
    m_backend2DCombo->addItem(tr("OpenMP (Kokkos)"),        QStringLiteral("OMP"));
    m_backend2DCombo->addItem(tr("CUDA"),                   QStringLiteral("CUDA"));
    m_backend2DCombo->addItem(tr("HIP"),                    QStringLiteral("HIP"));
    m_backend2DCombo->addItem(tr("SYCL"),                   QStringLiteral("SYCL"));
    m_backend2DCombo->setToolTip(
        tr("Which marcher implementation runs the 2D mesh (BACKEND). Auto "
           "prefers an installed GPU plugin above the device mesh-size floor, "
           "then the OpenMP plugin above its own floor, else the built-in "
           "CPU marcher. A named backend loads that plugin outright, "
           "regardless of mesh size; if it is not installed or has no usable "
           "device the run falls back to CPU with a notice. Pin CPU when a "
           "model measures slower on the accelerator. The OPENSWMM_2D_BACKEND "
           "environment variable overrides this setting when set."));
    perfForm->addRow(tr("Backend:"), m_backend2DCombo);

    vlay->addWidget(perfGroup);

    auto *meshGroup = new QGroupBox(tr("Mesh"), page);
    auto *meshForm  = new QFormLayout(meshGroup);

    m_dryDepthSpin = new QDoubleSpinBox(meshGroup);
    m_dryDepthSpin->setRange(0.0, 1.0);
    m_dryDepthSpin->setDecimals(6);
    m_dryDepthSpin->setSuffix(QStringLiteral(" m"));
    meshForm->addRow(tr("Dry depth threshold:"), m_dryDepthSpin);

    m_limiterEpsSpin = new QDoubleSpinBox(meshGroup);
    m_limiterEpsSpin->setRange(0.0, 1.0);
    m_limiterEpsSpin->setDecimals(9);
    meshForm->addRow(tr("Limiter epsilon:"), m_limiterEpsSpin);

    m_fluxDhEpsSpin = new QDoubleSpinBox(meshGroup);
    m_fluxDhEpsSpin->setRange(0.0, 1.0);
    m_fluxDhEpsSpin->setDecimals(6);
    m_fluxDhEpsSpin->setSuffix(QStringLiteral(" m"));
    m_fluxDhEpsSpin->setToolTip(
        tr("Head-difference regularization for the 2D diffusive-wave flux. "
           "0.004 m is the current recommended default from the road/weir "
           "performance trials."));
    meshForm->addRow(tr("Flux head epsilon:"), m_fluxDhEpsSpin);

    vlay->addWidget(meshGroup);

    auto *closureGroup = new QGroupBox(tr("Cell closure (wetting / drying)"), page);
    auto *closureForm  = new QFormLayout(closureGroup);

    m_cellClosureCombo = new QComboBox(closureGroup);
    m_cellClosureCombo->addItem(tr("Flat (η = z̄ + V/A, legacy)"), QStringLiteral("FLAT"));
    m_cellClosureCombo->addItem(tr("VFR (planar-bed volume/free-surface)"),
                                QStringLiteral("VFR"));
    m_cellClosureCombo->setToolTip(
        tr("How the free-surface elevation of a partially wet cell is reconstructed "
           "from its stored volume. Flat overstates the surface on slope/step cells "
           "(water can climb uphill and strand on slopes); VFR (Begnudelli & Sanders) "
           "uses the exact planar-bed relation so a lake at rest stays at rest. "
           "Supported on every backend (serial and Kokkos)."));
    closureForm->addRow(tr("Cell closure:"), m_cellClosureCombo);

    m_faceReconCombo = new QComboBox(closureGroup);
    m_faceReconCombo->addItem(tr("Mean (upwind cell depth, legacy)"),
                              QStringLiteral("MEAN"));
    m_faceReconCombo->addItem(tr("VFR face (edge depth + wetting gate)"),
                              QStringLiteral("VFR_FACE"));
    m_faceReconCombo->setToolTip(
        tr("Effective conveyance depth at a shared edge. Mean uses the upwind cell's "
           "mean depth; VFR face reconstructs the depth at the edge from the upwind "
           "surface and the edge's bed elevations, blocking flow across an edge whose "
           "bed is above the water (kills uphill creep and slope stranding). "
           "Best paired with the VFR cell closure."));
    closureForm->addRow(tr("Face reconstruction:"), m_faceReconCombo);

    m_vfrMinWetFracSpin = new QDoubleSpinBox(closureGroup);
    m_vfrMinWetFracSpin->setRange(0.0001, 0.5);
    m_vfrMinWetFracSpin->setDecimals(4);
    m_vfrMinWetFracSpin->setSingleStep(0.01);
    m_vfrMinWetFracSpin->setToolTip(
        tr("Wetted-area-fraction floor ε that regularizes the VFR closure as a "
           "cell dries (bounds dη/dV). Only used when Cell closure = VFR "
           "(default 0.01)."));
    closureForm->addRow(tr("VFR min wet fraction:"), m_vfrMinWetFracSpin);

    vlay->addWidget(closureGroup);

    auto *coupGroup = new QGroupBox(tr("1D ↔ 2D coupling"), page);
    auto *coupForm  = new QFormLayout(coupGroup);

    m_couplingCdSpin = new QDoubleSpinBox(coupGroup);
    m_couplingCdSpin->setRange(0.0, 1.0);
    m_couplingCdSpin->setDecimals(4);
    coupForm->addRow(tr("Coupling Cd:"), m_couplingCdSpin);

    m_couplingSyncSpin = new QDoubleSpinBox(coupGroup);
    m_couplingSyncSpin->setRange(0.0, 60.0);
    m_couplingSyncSpin->setDecimals(1);
    m_couplingSyncSpin->setSingleStep(5.0);
    m_couplingSyncSpin->setSuffix(QStringLiteral(" s"));
    m_couplingSyncSpin->setSpecialValueText(tr("Every routing step"));
    m_couplingSyncSpin->setToolTip(
        tr("How often 1D↔2D exchange volumes are settled (COUPLING_SYNC). "
           "0 (default) couples every routing step — tightest feedback, "
           "needed for fast fill-and-spill ponds behind weirs/culverts. "
           "A batching interval (clamped to [routing step, 60 s]) advances "
           "the 2D in longer spans and delivers exchange to the 1D spread "
           "over the following span — much faster on large meshes, but the "
           "one-span feedback delay can ring on rapidly filling ponds."));
    coupForm->addRow(tr("Exchange interval:"), m_couplingSyncSpin);

    m_couplingAreaAutoBox = new QCheckBox(
        tr("Derive exchange areas automatically (COUPLING_AREA AUTO)"),
        coupGroup);
    m_couplingAreaAutoBox->setToolTip(
        tr("Override every coupling point's exchange area with "
           "1.25 × the largest connected conduit area (clamped 0.05–2 m²). "
           "Recommended when the mesh authored default areas much larger than "
           "the pipes they feed — oversized areas drive fill-and-spill churn. "
           "Explicit AREA values in the input are replaced while this is on."));
    coupForm->addRow(QString(), m_couplingAreaAutoBox);

    vlay->addWidget(coupGroup);

    // U1 (2026-09-07) — Processes group: the 2D process enables of E2
    // ([2D_OPTIONS] INFILTRATION / INFIL_STEP / INFIL_DEFAULT_METHOD /
    // INFIL_DESTINATION, EVAPORATION, TRANSPORT_*) beside RAINFALL_MODE.
    // TABS T5 later moves the whole group into the 2D › Processes tab.
    auto *rainfallGroup = new QGroupBox(tr("Processes"), page);
    auto *rainfallForm  = new QFormLayout(rainfallGroup);

    m_rainfall2DModeCombo = new QComboBox(rainfallGroup);
    m_rainfall2DModeCombo->addItem(tr("Natural neighbour (all gages)"),
                                   QStringLiteral("NATURAL_NEIGHBOUR"));
    m_rainfall2DModeCombo->addItem(tr("System (uniform gage mean)"),
                                   QStringLiteral("SYSTEM"));
    m_rainfall2DModeCombo->addItem(tr("None (no direct rainfall)"),
                                   QStringLiteral("NONE"));
    m_rainfall2DModeCombo->setToolTip(
        tr("How raingage rainfall drives the 2D mesh. Natural neighbour spatially "
           "interpolates all located gages onto each cell (inverse-distance "
           "outside the gage hull); System applies one uniform value — the mean "
           "of all gages; None applies no direct rainfall to the mesh."));
    rainfallForm->addRow(tr("Rainfall mode:"), m_rainfall2DModeCombo);

    // ── Infiltration ────────────────────────────────────────────────────
    m_infil2DModeCombo = new QComboBox(rainfallGroup);
    m_infil2DModeCombo->setObjectName(QStringLiteral("infil2DModeCombo"));
    m_infil2DModeCombo->addItem(tr("Automatic (on when per-cell rows exist)"),
                                QStringLiteral("AUTO"));
    m_infil2DModeCombo->addItem(tr("On"),  QStringLiteral("YES"));
    m_infil2DModeCombo->addItem(tr("Off (keep rows, run without infiltration)"),
                                QStringLiteral("NO"));
    m_infil2DModeCombo->setToolTip(
        tr("[2D_OPTIONS] INFILTRATION. Automatic is the pre-existing rule — "
           "cells infiltrate when [2D_INFILTRATION_DEFAULTS] / "
           "[2D_INFILTRATION] rows resolve. Off keeps the rows in the model "
           "but books no infiltration this run; On warns when no row "
           "resolves."));
    rainfallForm->addRow(tr("Infiltration:"), m_infil2DModeCombo);

    auto *istepRow = new QWidget(rainfallGroup);
    auto *istepLay = new QHBoxLayout(istepRow);
    istepLay->setContentsMargins(0, 0, 0, 0);
    m_infil2DStepSameBox = new QCheckBox(tr("Same as wet-weather step"), istepRow);
    m_infil2DStepSameBox->setObjectName(QStringLiteral("infil2DStepSameBox"));
    m_infil2DStepSameBox->setToolTip(
        tr("Infiltration rates are recomputed on their own cadence "
           "([2D_OPTIONS] INFIL_STEP, alias of [2D_INFILTRATION_OPTIONS]) "
           "and held between updates. Ticked: the project WET_STEP."));
    m_infil2DStepEdit = new QCustomTimespanEdit(istepRow);
    m_infil2DStepEdit->setObjectName(QStringLiteral("infil2DStepEdit"));
    istepLay->addWidget(m_infil2DStepSameBox);
    istepLay->addWidget(m_infil2DStepEdit, 1);
    rainfallForm->addRow(tr("Infiltration step:"), istepRow);
    connect(m_infil2DStepSameBox, &QCheckBox::toggled, this, [this](bool same) {
        m_infil2DStepEdit->setEnabled(!same);
        if (same) m_infil2DStepEdit->setTotalSeconds(m_wetStepEdit->totalSeconds());
    });

    m_infil2DMethodCombo = new QComboBox(rainfallGroup);
    m_infil2DMethodCombo->setObjectName(QStringLiteral("infil2DMethodCombo"));
    m_infil2DMethodCombo->addItem(tr("None (no mesh-wide default)"), QStringLiteral("NONE"));
    m_infil2DMethodCombo->addItem(tr("Horton"),                   QStringLiteral("HORTON"));
    m_infil2DMethodCombo->addItem(tr("Modified Horton"),          QStringLiteral("MOD_HORTON"));
    m_infil2DMethodCombo->addItem(tr("Green-Ampt"),               QStringLiteral("GREEN_AMPT"));
    m_infil2DMethodCombo->addItem(tr("Modified Green-Ampt"),      QStringLiteral("MOD_GREEN_AMPT"));
    m_infil2DMethodCombo->addItem(tr("Curve Number"),             QStringLiteral("CURVE_NUMBER"));
    m_infil2DMethodCombo->addItem(tr("Constant rate"),            QStringLiteral("CONSTANT"));
    m_infil2DMethodCombo->setToolTip(
        tr("[2D_OPTIONS] INFIL_DEFAULT_METHOD — the method of the '*' row of "
           "[2D_INFILTRATION_DEFAULTS]. The row carries the parameters: pick "
           "None to drop the mesh-wide default for a run; a method must "
           "match the row (edit the row's parameters with the per-cell "
           "editor)."));
    rainfallForm->addRow(tr("Default method:"), m_infil2DMethodCombo);

    m_infil2DDestCombo = new QComboBox(rainfallGroup);
    m_infil2DDestCombo->setObjectName(QStringLiteral("infil2DDestCombo"));
    m_infil2DDestCombo->addItem(tr("Lost (leaves the model)"), QStringLiteral("LOST"));
    m_infil2DDestCombo->addItem(tr("Subcatchment aquifer (containing subcatchment)"),
                                QStringLiteral("SUBCATCH_AQUIFER"));
    m_infil2DDestCombo->addItem(tr("2D aquifer (authoring only until the groundwater kernel)"),
                                QStringLiteral("AQUIFER_2D"));
    m_infil2DDestCombo->setToolTip(
        tr("[2D_OPTIONS] INFIL_DESTINATION — where infiltrated water goes "
           "for every row that does not spell its own DEST column. "
           "Subcatchment aquifer recharges the legacy aquifer of the "
           "subcatchment containing each cell; 2D aquifer is accepted in "
           "the file but a run refuses it until the integrated groundwater "
           "kernel lands."));
    rainfallForm->addRow(tr("Destination:"), m_infil2DDestCombo);

    m_editInfilCellsBtn = new QPushButton(tr("Edit per-cell infiltration…"), rainfallGroup);
    m_editInfilCellsBtn->setObjectName(QStringLiteral("editInfilCellsBtn"));
    m_editInfilCellsBtn->setToolTip(
        tr("Opens Model ▸ Mesh ▸ Assign Infiltration to Selection for the "
           "cells selected on the map (methods, parameters and destination "
           "per cell or per region tag)."));
    connect(m_editInfilCellsBtn, &QPushButton::clicked, this, [this]() {
        QAction *act = nullptr;
        if (QWidget *top = m_projectWindow ? m_projectWindow->window() : nullptr)
            act = top->findChild<QAction *>(QStringLiteral("actionMeshAssignInfilToSelection"));
        if (!act)
            for (QWidget *w : QApplication::topLevelWidgets())
                if ((act = w->findChild<QAction *>(
                         QStringLiteral("actionMeshAssignInfilToSelection"))))
                    break;
        if (act) {
            act->trigger();
        } else {
            QMessageBox::information(
                this, tr("Per-cell infiltration"),
                tr("Select cells on the map, then use Model ▸ Mesh ▸ Assign "
                   "Infiltration to Selection…"));
        }
    });
    rainfallForm->addRow(QString(), m_editInfilCellsBtn);

    // ── Evaporation ─────────────────────────────────────────────────────
    m_evap2DCombo = new QComboBox(rainfallGroup);
    m_evap2DCombo->setObjectName(QStringLiteral("evap2DCombo"));
    m_evap2DCombo->addItem(tr("Forced only (API / control forcing)"), QStringLiteral("YES"));
    m_evap2DCombo->addItem(tr("Project climate rate ([EVAPORATION]) on unforced cells"),
                           QStringLiteral("CLIMATE"));
    m_evap2DCombo->addItem(tr("Off"), QStringLiteral("NO"));
    m_evap2DCombo->setToolTip(
        tr("[2D_OPTIONS] EVAPORATION. Forced only is the pre-existing sink: "
           "cells evaporate only where swmm_2d_force_evap* prescribed a "
           "rate. Project climate rate applies the [EVAPORATION] rate the "
           "1D side uses (Climatology dialog) to every unforced cell. Off "
           "books no evaporation."));
    rainfallForm->addRow(tr("Evaporation:"), m_evap2DCombo);

    // ── Transport (the 2D column of the Models page matrix) ─────────────
    auto *trBox = new QWidget(rainfallGroup);
    auto *trLay = new QHBoxLayout(trBox);
    trLay->setContentsMargins(0, 0, 0, 0);
    const char *trLabels[4] = {"Pollutants", "MSX species", "Water age", "Temperature"};
    for (int c = 0; c < 4; ++c) {
        m_transport2DBox[c] = new QCheckBox(tr(trLabels[c]), trBox);
        m_transport2DBox[c]->setObjectName(QStringLiteral("transport2DBox_") +
                                           QLatin1String(transport2DKey(c)));
        m_transport2DBox[c]->setToolTip(
            tr("[2D_OPTIONS] %1 — carry this class on the 2D mesh when the "
               "project enables it. Unticking is a real performance lever "
               "(each row costs per-cell memory and a reaction stage); the "
               "1D side is unaffected.").arg(QLatin1String(transport2DKey(c))));
        trLay->addWidget(m_transport2DBox[c]);
        connect(m_transport2DBox[c], &QCheckBox::toggled, this, [this](bool) {
            if (!m_matrixSyncing) refreshTransportMatrix();
        });
    }
    trLay->addStretch();
    rainfallForm->addRow(tr("Transport on the mesh:"), trBox);

    vlay->addWidget(rainfallGroup);

    // U5 (2026-09-07, rewired the same day once the G1 two-zone kernel
    // landed) — Groundwater group. This page holds the PROCESS ENABLES; the
    // parameters live in Mesh 2D > Groundwater (2D) > Aquifer Parameters,
    // which edits the same [2D_AQUIFER_OPTIONS] values (CLAUDE.md §5.1 — one
    // model, two views).
    m_gw2DGroup = new QGroupBox(tr("Groundwater (subsurface)"), page);
    auto *gwForm = new QFormLayout(m_gw2DGroup);

    // Tri-state, exactly like Infiltration above: Automatic is the default and
    // is never written, so opening this dialog and clicking Apply on a deck
    // that never mentioned the key stays the identity.
    m_gw2DEnableCombo = new QComboBox(m_gw2DGroup);
    m_gw2DEnableCombo->setObjectName(QStringLiteral("gw2DEnableCombo"));
    m_gw2DEnableCombo->addItem(tr("Automatic (on when aquifer rows exist)"),
                               QStringLiteral("AUTO"));
    m_gw2DEnableCombo->addItem(tr("On"), QStringLiteral("YES"));
    m_gw2DEnableCombo->addItem(tr("Off (keep rows, run without groundwater)"),
                               QStringLiteral("NO"));
    m_gw2DEnableCombo->setToolTip(
        tr("[2D_OPTIONS] GROUNDWATER. Turns on the two-zone subsurface column "
           "under every mesh cell: unsaturated + saturated storage, lateral "
           "flow and (with the sections below) subsurface transport. "
           "Automatic is the pre-existing rule — the subsurface runs when "
           "[2D_AQUIFER_OPTIONS] / [2D_AQUIFER] rows are authored. Off keeps "
           "those rows in the model but runs without groundwater; On warns "
           "when no row was authored."));
    gwForm->addRow(tr("Subsurface:"), m_gw2DEnableCombo);

    m_gw2DEtCombo = new QComboBox(m_gw2DGroup);
    m_gw2DEtCombo->setObjectName(QStringLiteral("gw2DEtCombo"));
    m_gw2DEtCombo->addItem(tr("None"), QStringLiteral("NONE"));
    m_gw2DEtCombo->addItem(tr("Capillary rise (from the water table)"),
                           QStringLiteral("CAPILLARY_RISE"));
    m_gw2DEtCombo->addItem(tr("Boundary ET (from the unsaturated column)"),
                           QStringLiteral("BOUNDARY_ET"));
    m_gw2DEtCombo->addItem(tr("Both"), QStringLiteral("BOTH"));
    m_gw2DEtCombo->setToolTip(
        tr("GW_ET: evapotranspiration from the subsurface, at the same "
           "climate PET the 1D side uses. Separate from the surface "
           "Evaporation setting above: that removes ponded water, this removes "
           "soil water, and each books its own ledger row.\n\n"
           "This is the same setting as Groundwater ET in Mesh 2D > "
           "Groundwater (2D) > Aquifer Parameters — it is stored once, in "
           "[2D_AQUIFER_OPTIONS], and editing it in either place changes the "
           "same value."));
    gwForm->addRow(tr("Evapotranspiration:"), m_gw2DEtCombo);

    m_gw2DStatusLabel = new QLabel(m_gw2DGroup);
    m_gw2DStatusLabel->setObjectName(QStringLiteral("gw2DStatusLabel"));
    m_gw2DStatusLabel->setWordWrap(true);
    gwForm->addRow(tr("Transport:"), m_gw2DStatusLabel);

    m_gw2DEditBtn = new QPushButton(tr("Subsurface initial quality..."), m_gw2DGroup);
    m_gw2DEditBtn->setObjectName(QStringLiteral("gw2DEditBtn"));
    m_gw2DEditBtn->setToolTip(
        tr("Open the Initial Quality editor: per-element initial "
           "concentrations, temperatures and ages. Subsurface rows "
           "([GW_INITIAL_QUALITY]) are authored there and saved with the "
           "model."));
    connect(m_gw2DEditBtn, &QPushButton::clicked, this, [this]() {
        OpenSWMMVis::InitialQualityDialog dlg(m_engine, this);
        if (dlg.exec() == QDialog::Accepted && dlg.wroteAnyChanges())
            m_wroteChanges = true;
        update2DGroundwaterEnabled();
    });
    gwForm->addRow(QString(), m_gw2DEditBtn);

    connect(m_gw2DEnableCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { update2DGroundwaterEnabled(); });
    vlay->addWidget(m_gw2DGroup);

    auto *outGroup = new QGroupBox(tr("Output"), page);
    auto *outForm  = new QFormLayout(outGroup);

    m_report2DBox = new QCheckBox(tr("Write 2D results to output (REPORT_2D)"),
                                  outGroup);
    outForm->addRow(QString(), m_report2DBox);

    auto *outRow = new QWidget(outGroup);
    auto *outLay = new QHBoxLayout(outRow);
    outLay->setContentsMargins(0, 0, 0, 0);
    m_output2DFileEdit = new QLineEdit(outRow);
    m_output2DFileEdit->setPlaceholderText(tr("<model>.2d.h5 (automatic)"));
    m_output2DFileEdit->setToolTip(
        tr("HDF5 file receiving 2D results ([2D_OPTIONS] OUTPUT_FILE). "
           "Relative paths resolve against the input file's folder. Leave "
           "blank to use <model>.2d.h5 next to the input file."));
    auto *outBrowse = new QPushButton(tr("Browse…"), outRow);
    connect(outBrowse, &QPushButton::clicked, this, [this]() {
        QString start = m_output2DFileEdit->text().trimmed();
        if (start.isEmpty() && m_layer)
            start = QFileInfo(m_layer->modelFilePath()).absolutePath();
        const QString f = QFileDialog::getSaveFileName(
            this, tr("2D results file"), start,
            tr("HDF5 results (*.h5);;All files (*)"));
        if (!f.isEmpty()) m_output2DFileEdit->setText(f);
    });
    outLay->addWidget(m_output2DFileEdit, 1);
    outLay->addWidget(outBrowse);
    outForm->addRow(tr("2D results file:"), outRow);

    // ── E1 — storage precision / compression / cadence / variable selection ──
    m_output2DPrecisionCombo = new QComboBox(outGroup);
    m_output2DPrecisionCombo->setObjectName(QStringLiteral("output2DPrecisionCombo"));
    m_output2DPrecisionCombo->addItem(tr("Single (float32, half the size)"),
                                      QStringLiteral("FLOAT32"));
    m_output2DPrecisionCombo->addItem(tr("Double (float64, bit-exact)"),
                                      QStringLiteral("FLOAT64"));
    m_output2DPrecisionCombo->setToolTip(
        tr("Storage type of every time-varying dataset in the 2D results file "
           "([2D_OPTIONS] OUTPUT_PRECISION). The solver always runs in double; "
           "float32 storage keeps ~7 significant digits, ample for depth, head "
           "and velocity rendering, and halves the file. Choose float64 for "
           "bit-exact regression comparisons."));
    outForm->addRow(tr("Precision:"), m_output2DPrecisionCombo);

    m_output2DCompressionSpin = new QSpinBox(outGroup);
    m_output2DCompressionSpin->setObjectName(QStringLiteral("output2DCompressionSpin"));
    m_output2DCompressionSpin->setRange(0, 9);
    m_output2DCompressionSpin->setSpecialValueText(tr("0 (off)"));
    m_output2DCompressionSpin->setToolTip(
        tr("HDF5 deflate level for the results datasets ([2D_OPTIONS] "
           "OUTPUT_COMPRESSION, 0–9). Levels above 0 also enable the shuffle "
           "filter, which typically halves float32 depth fields again on "
           "mostly-dry meshes. 4 is the default; 0 writes uncompressed chunks."));
    outForm->addRow(tr("Compression:"), m_output2DCompressionSpin);

    auto *stepRow = new QWidget(outGroup);
    auto *stepLay = new QHBoxLayout(stepRow);
    stepLay->setContentsMargins(0, 0, 0, 0);
    m_report2DStepSameBox = new QCheckBox(tr("Same as reporting step"), stepRow);
    m_report2DStepSameBox->setObjectName(QStringLiteral("report2DStepSameBox"));
    m_report2DStepSameBox->setToolTip(
        tr("Write a 2D frame every REPORT_STEP (the 1D reporting step). "
           "Untick to thin the 2D time axis to a longer multiple of it "
           "([2D_OPTIONS] REPORT_2D_STEP) — the 1D output keeps its own step."));
    m_report2DStepEdit = new QCustomTimespanEdit(stepRow);
    m_report2DStepEdit->setObjectName(QStringLiteral("report2DStepEdit"));
    m_report2DStepEdit->setToolTip(
        tr("2D frame interval ([2D_OPTIONS] REPORT_2D_STEP). Must be a whole "
           "multiple of REPORT_STEP; the engine refuses other values."));
    stepLay->addWidget(m_report2DStepSameBox);
    stepLay->addWidget(m_report2DStepEdit, 1);
    outForm->addRow(tr("2D report step:"), stepRow);
    connect(m_report2DStepSameBox, &QCheckBox::toggled, this, [this](bool same) {
        m_report2DStepEdit->setEnabled(!same);
        if (same) m_report2DStepEdit->setTotalSeconds(m_reportStepEdit->totalSeconds());
        update2DOutputSizeEstimate();
    });
    connect(m_reportStepEdit, &QCustomTimespanEdit::totalSecondsChanged, this,
            [this](qint64 secs) {
                if (m_report2DStepSameBox->isChecked())
                    m_report2DStepEdit->setTotalSeconds(secs);
                update2DOutputSizeEstimate();
            });
    connect(m_report2DStepEdit, &QCustomTimespanEdit::totalSecondsChanged, this,
            [this](qint64) { update2DOutputSizeEstimate(); });

    // Variables checklist — one row per engine dataset group, in bit order.
    // The tooltip names the GUI consumers so a user knows what greys out.
    auto *varsBox = new QWidget(outGroup);
    auto *varsLay = new QVBoxLayout(varsBox);
    varsLay->setContentsMargins(0, 0, 0, 0);
    m_report2DVarsList = new QListWidget(varsBox);
    m_report2DVarsList->setObjectName(QStringLiteral("report2DVarsList"));
    m_report2DVarsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_report2DVarsList->setToolTip(
        tr("Dataset groups written to the 2D results file ([2D_OPTIONS] "
           "REPORT_2D_VARIABLES). Unticked groups are absent from the file; "
           "plots and symbology that need them are greyed out when the run "
           "is loaded. Depth is always written."));
    struct VarDoc { const char *token; const char *label; const char *tip; };
    static const VarDoc kVarDocs[] = {
        {"DEPTH",        "Depth & head (per cell)",
         "Mesh2_face_depth / Mesh2_face_head — depth fill, contour bands and "
         "lines, cell depth / HGL plots. Always written."},
        {"VELOCITY",     "Velocity (per cell)",
         "Mesh2_face_vx / Mesh2_face_vy — cell-centred velocity components "
         "(exported for external post-processing; the GUI reconstructs velocity "
         "from edge fluxes)."},
        {"EDGE_FLUX",    "Edge fluxes (per edge)",
         "Mesh2_edge_flux — velocity vectors and velocity magnitude symbology, "
         "edge flow / flux plots, velocity plots."},
        {"NODE_HEAD",    "Vertex head & depth (per vertex)",
         "Mesh2_node_head / Mesh2_node_depth — smooth (vertex-interpolated) "
         "depth rendering and flood-extent isolines; without them the GUI "
         "falls back to a coarser cell-average reconstruction."},
        {"SPECIES",      "Water quality (per cell × species)",
         "Mesh2_face_species_conc — concentration rendering and plots for "
         "pollutants, MSX species, water age and temperature. Use the species "
         "list below to keep only some rows."},
        {"RAINFALL",     "Rainfall (per cell)",
         "Mesh2_face_rainfall / Mesh2_face_rain_cum — cell rainfall intensity "
         "and cumulative rain-volume plots."},
        {"INFILTRATION", "Infiltration (per cell)",
         "Mesh2_face_infil_rate / Mesh2_face_infil_cum — infiltration-rate "
         "and cumulative-loss plots and colour ramps."},
        {"COUPLING",     "1D↔2D exchange (per cell)",
         "Mesh2_face_coupling_flux / Mesh2_face_net_source — diagnostic "
         "exchange and net-source fields (no GUI consumer today)."},
        {"GRADIENTS",    "Reconstruction gradients (diagnostic)",
         "Mesh2_face_grad_* (4 fields) — solver diagnostics only."},
        {"CONTINUITY",   "Continuity error (diagnostic)",
         "Mesh2_face_continuity_err (+ its envelope) — solver diagnostics only."},
        {"ENVELOPES",    "Run maxima (once per run)",
         "Mesh2_face_max_depth / Mesh2_face_max_velocity — maximum-depth and "
         "maximum-velocity symbology and the flood-extent envelope; written "
         "once at the end of the run, negligible size."},
    };
    const int nVars = swmm_2d_output_variable_count();
    for (int i = 0; i < nVars; ++i) {
        const QString token = QString::fromLatin1(swmm_2d_output_variable_name(i));
        const VarDoc *doc = nullptr;
        for (const VarDoc &d : kVarDocs)
            if (token.compare(QLatin1String(d.token), Qt::CaseInsensitive) == 0) { doc = &d; break; }
        auto *item = new QListWidgetItem(
            doc ? tr("%1  [%2]").arg(tr(doc->label), token) : token, m_report2DVarsList);
        item->setData(Qt::UserRole, i);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Unchecked);
        if (doc) item->setToolTip(tr(doc->tip));
        if (token.compare(QLatin1String("DEPTH"), Qt::CaseInsensitive) == 0) {
            item->setCheckState(Qt::Checked);
            item->setFlags(Qt::ItemIsUserCheckable);   // always on, not editable
        }
    }
    m_report2DVarsList->setFixedHeight(
        m_report2DVarsList->sizeHintForRow(0) * std::max(1, nVars) + 6);

    auto *presetRow = new QWidget(varsBox);
    auto *presetLay = new QHBoxLayout(presetRow);
    presetLay->setContentsMargins(0, 0, 0, 0);
    auto addPreset = [&](const QString &label, const char *preset, const QString &tip) {
        auto *b = new QPushButton(label, presetRow);
        b->setObjectName(QStringLiteral("report2DPreset_") + QLatin1String(preset));
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [this, preset]() {
            setReport2DVarsMask(swmm_2d_output_variable_mask(preset));
        });
        presetLay->addWidget(b);
    };
    addPreset(tr("Default"), "DEFAULT",
              tr("Everything the GUI renders or plots; drops the solver "
                 "diagnostics (exchange, gradients, continuity)."));
    addPreset(tr("Minimal"), "MINIMAL",
              tr("Depth, vertex head and run maxima only — smallest file that "
                 "still animates depth."));
    addPreset(tr("All"), "ALL", tr("Every dataset group, including diagnostics."));
    presetLay->addStretch();
    varsLay->addWidget(m_report2DVarsList);
    varsLay->addWidget(presetRow);
    outForm->addRow(tr("Variables:"), varsBox);
    connect(m_report2DVarsList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { update2DOutputSizeEstimate(); });

    // Species sub-list — pollutants + MSX species; "All" keeps every row the
    // 2D transport solver carries (including age / temperature when enabled).
    auto *spBox = new QWidget(outGroup);
    auto *spLay = new QVBoxLayout(spBox);
    spLay->setContentsMargins(0, 0, 0, 0);
    m_report2DAllSpeciesBox = new QCheckBox(tr("All transported species"), spBox);
    m_report2DAllSpeciesBox->setObjectName(QStringLiteral("report2DAllSpeciesBox"));
    m_report2DAllSpeciesBox->setToolTip(
        tr("Write every species row the 2D transport solver carries "
           "([2D_OPTIONS] REPORT_2D_SPECIES ALL). Untick to keep only the "
           "names ticked below."));
    m_report2DSpeciesList = new QListWidget(spBox);
    m_report2DSpeciesList->setObjectName(QStringLiteral("report2DSpeciesList"));
    m_report2DSpeciesList->setSelectionMode(QAbstractItemView::NoSelection);
    m_report2DSpeciesList->setToolTip(
        tr("Species rows kept in Mesh2_face_species_conc "
           "([2D_OPTIONS] REPORT_2D_SPECIES). Names not in the model are "
           "ignored by the engine."));
    if (m_engine) {
        const int nPol = swmm_pollutant_count(m_engine);
        for (int i = 0; i < nPol; ++i) {
            const char *id = swmm_pollutant_id(m_engine, i);
            if (!id || !*id) continue;
            auto *it = new QListWidgetItem(QString::fromUtf8(id), m_report2DSpeciesList);
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(Qt::Unchecked);
            it->setToolTip(tr("[POLLUTANTS] %1").arg(QString::fromUtf8(id)));
        }
        const int nSp = swmm_reaction_species_count(m_engine);
        for (int i = 0; i < nSp; ++i) {
            char name[128] = {}; char units[32] = {};
            int isWall = 0; double atol = 0.0, rtol = 0.0;
            if (swmm_reaction_species_get(m_engine, i, name, sizeof(name), &isWall,
                                          units, sizeof(units), &atol, &rtol) != 0)
                continue;
            if (!name[0]) continue;
            const QString nm = QString::fromUtf8(name);
            if (!m_report2DSpeciesList->findItems(nm, Qt::MatchFixedString).isEmpty())
                continue;
            auto *it = new QListWidgetItem(nm, m_report2DSpeciesList);
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(Qt::Unchecked);
            it->setToolTip(tr("[REACTION_SPECIES] %1").arg(nm));
        }
    }
    m_report2DSpeciesList->setFixedHeight(
        m_report2DSpeciesList->sizeHintForRow(0) *
            std::clamp(m_report2DSpeciesList->count(), 1, 6) + 6);
    spLay->addWidget(m_report2DAllSpeciesBox);
    spLay->addWidget(m_report2DSpeciesList);
    outForm->addRow(tr("Species:"), spBox);
    connect(m_report2DAllSpeciesBox, &QCheckBox::toggled, this, [this](bool all) {
        m_report2DSpeciesList->setEnabled(!all);
        update2DOutputSizeEstimate();
    });
    connect(m_report2DSpeciesList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { update2DOutputSizeEstimate(); });

    m_output2DSizeLabel = new QLabel(QStringLiteral("—"), outGroup);
    m_output2DSizeLabel->setObjectName(QStringLiteral("output2DSizeLabel"));
    m_output2DSizeLabel->setWordWrap(true);
    m_output2DSizeLabel->setToolTip(
        tr("Rough uncompressed size of the time-varying datasets: frames × "
           "(selected fields × cells / vertices / edges) × bytes per value. "
           "Compression typically shrinks it 2–5×; mesh geometry is not counted."));
    outForm->addRow(tr("Estimated size:"), m_output2DSizeLabel);
    connect(m_output2DPrecisionCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { update2DOutputSizeEstimate(); });
    connect(m_output2DCompressionSpin, &QSpinBox::valueChanged, this,
            [this](int) { update2DOutputSizeEstimate(); });
    connect(m_startEdit, &QDateTimeEdit::dateTimeChanged, this,
            [this](const QDateTime &) { update2DOutputSizeEstimate(); });
    connect(m_endEdit, &QDateTimeEdit::dateTimeChanged, this,
            [this](const QDateTime &) { update2DOutputSizeEstimate(); });

    vlay->addWidget(outGroup);

    vlay->addStretch();

    return page;
}

void SimulationOptionsDialog::read2DFromEngine()
{
    auto getExt = [&](const char *key, const QString &fallback) -> QString {
        if (!m_engine) return fallback;
        char buf[256] = {};
        if (swmm_options_get_ext(m_engine, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
        return fallback;
    };
    // Same fallback-on-garbage semantics as optDouble/optInt in
    // readFromEngine: never seed 0 into a spin box from an unparseable
    // engine string (write2DToEngine would persist it as a real edit).
    auto extDouble = [&](const char *key, double fallback) {
        bool okNum = false;
        const double v = getExt(key, QString::number(fallback, 'g', 8))
                             .toDouble(&okNum);
        return okNum ? v : fallback;
    };
    auto extInt = [&](const char *key, int fallback) {
        bool okNum = false;
        const int v = getExt(key, QString::number(fallback)).toInt(&okNum);
        return okNum ? v : fallback;
    };

    // Iteration 4 — source every missing-key fallback from the 2D Defaults
    // preferences (same lockstep idiom as the 1D tabs, see readFromEngine):
    // the dialog shows the user-preferred default whenever the project has
    // no value for a key, matching what File→New would synthesize.
    const auto t = PreferencesManager::instance()->twoDDefaults();

    m_maxTimestepSpin  ->setValue(extDouble("MAX_TIMESTEP",     t.maxTimestepSec));
    m_dryDepthSpin     ->setValue(extDouble("DRY_DEPTH",        t.dryDepth));
    m_limiterEpsSpin   ->setValue(extDouble("LIMITER_EPSILON",  t.limiterEpsilon));
    m_fluxDhEpsSpin    ->setValue(extDouble("FLUX_DH_EPS",      t.fluxDhEps));
    m_vfrMinWetFracSpin->setValue(extDouble("VFR_MIN_WET_FRAC", t.vfrMinWetFrac));
    m_couplingCdSpin   ->setValue(extDouble("COUPLING_CD",      t.couplingCd));
    m_couplingSyncSpin ->setValue(extDouble("COUPLING_SYNC",    t.couplingSync));

    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    selectComboByData(m_cellClosureCombo,    getExt("CELL_CLOSURE",        t.cellClosure));
    selectComboByData(m_faceReconCombo,      getExt("FACE_RECONSTRUCTION", t.faceReconstruction));
    selectComboByData(m_rainfall2DModeCombo, getExt("RAINFALL_MODE",       t.rainfallMode));
    m_report2DBox->setChecked(parseEngineBool(
        getExt("REPORT_2D", t.report2D ? "YES" : "NO")) == Qt::Checked);

    // Explicit-marcher configuration (the only 2D integrator; no INTEGRATOR
    // read/write — the engine default is EXPLICIT).
    m_thetaSpin    ->setValue(extDouble("THETA",      t.theta));
    m_cflNumberSpin->setValue(extDouble("CFL_NUMBER", t.cflNumber));
    m_ltsTiersSpin ->setValue(extInt("LTS_TIERS",     t.ltsTiers));
    m_hMoveSpin    ->setValue(extDouble("H_MOVE",     t.hMove));
    m_froudeMaxSpin->setValue(extDouble("FROUDE_MAX", t.froudeMax));
    selectComboByData(m_momentum2DCombo,
                      getExt("MOMENTUM_EQUATION", QStringLiteral("LOCAL_INERTIAL")));
    m_reconOrder2DSpin->setValue(extInt("RECONSTRUCTION_ORDER", 1));
    m_advection2DBox->setChecked(parseEngineBool(
        getExt("ADVECTION", t.advection ? "YES" : "NO")) == Qt::Checked);
    // No preferences default: the engine's AUTO is the only sensible seed.
    selectComboByData(m_backend2DCombo, getExt("BACKEND", QStringLiteral("AUTO")));
    m_couplingAreaAutoBox->setChecked(
        getExt("COUPLING_AREA", t.couplingAreaAuto ? "AUTO" : "DEFAULT")
            .compare(QStringLiteral("AUTO"), Qt::CaseInsensitive) == 0);

    // OUTPUT_FILE has no preferences default — blank means "auto-derive
    // <model>.2d.h5 at run time" (see SimulationRunner / run wiring).
    m_output2DFileEdit->setText(getExt("OUTPUT_FILE", QString()));

    // E1 — output precision / compression / cadence / variables / species.
    // A species list can exceed the 256-byte getExt buffer; read it wide.
    auto getExtWide = [&](const char *key) -> QString {
        if (!m_engine) return {};
        std::vector<char> buf(4096, '\0');
        if (swmm_options_get_ext(m_engine, key, buf.data(), static_cast<int>(buf.size())) == 0)
            return QString::fromUtf8(buf.data()).trimmed();
        return {};
    };
    selectComboByData(m_output2DPrecisionCombo,
                      getExt("OUTPUT_PRECISION", QStringLiteral("FLOAT32")));
    m_output2DCompressionSpin->setValue(std::clamp(extInt("OUTPUT_COMPRESSION", 4), 0, 9));
    {
        const qint64 step2d = parseStepSeconds(getExt("REPORT_2D_STEP", QStringLiteral("0")), 0);
        QSignalBlocker b1(m_report2DStepSameBox);
        QSignalBlocker b2(m_report2DStepEdit);
        m_report2DStepSameBox->setChecked(step2d <= 0);
        m_report2DStepEdit->setEnabled(step2d > 0);
        m_report2DStepEdit->setTotalSeconds(step2d > 0 ? step2d
                                                       : m_reportStepEdit->totalSeconds());
    }
    {
        const QString varsText = getExtWide("REPORT_2D_VARIABLES");
        unsigned mask = varsText.isEmpty() ? 0u
                      : swmm_2d_output_variable_mask(varsText.toUtf8().constData());
        if (mask == 0u) mask = swmm_2d_output_variable_mask("DEFAULT");
        setReport2DVarsMask(mask);
    }
    setReport2DSpeciesText(getExtWide("REPORT_2D_SPECIES"));
    update2DOutputSizeEstimate();

    // U1 — Processes group (E2 keys). INFILTRATION is AUTO | YES | NO as
    // stored; the Automatic item's text carries the effective state derived
    // from the rows the engine holds.
    selectComboByData(m_infil2DModeCombo, getExt("INFILTRATION", QStringLiteral("AUTO")));
    {
        int nDefaults = 0;
        swmm_infil2d_defaults_count(m_engine, &nDefaults);
        // No per-cell override count in the C API: scan a bounded prefix
        // when there is no default row (a large mesh with only overrides
        // keeps the plain label).
        int nCells = 0;
        swmm_2d_cell_count(m_engine, &nCells);
        const int scanLimit = 50000;
        bool anyCellRow = false;
        for (int t = 0; nDefaults == 0 && t < nCells && t < scanLimit && !anyCellRow; ++t) {
            SWMM_Infil2DRow row{};
            int isOverride = 0;
            if (swmm_infil2d_get_cell(m_engine, t, &row, &isOverride) == SWMM_OK &&
                isOverride && row.has_method)
                anyCellRow = true;
        }
        const bool rows = nDefaults > 0 || anyCellRow;
        m_infil2DModeCombo->setItemText(
            m_infil2DModeCombo->findData(QStringLiteral("AUTO")),
            (nDefaults == 0 && nCells > scanLimit && !anyCellRow)
                ? tr("Automatic (on when per-cell rows exist)")
                : (rows ? tr("Automatic — on (per-cell rows exist)")
                        : tr("Automatic — off (no per-cell rows)")));
    }
    {
        const qint64 step = parseStepSeconds(getExt("INFIL_STEP", QStringLiteral("0")), 0);
        QSignalBlocker b1(m_infil2DStepSameBox);
        QSignalBlocker b2(m_infil2DStepEdit);
        m_infil2DStepSameBox->setChecked(step <= 0);
        m_infil2DStepEdit->setEnabled(step > 0);
        m_infil2DStepEdit->setTotalSeconds(step > 0 ? step : m_wetStepEdit->totalSeconds());
    }
    selectComboByData(m_infil2DMethodCombo, getExt("INFIL_DEFAULT_METHOD", QStringLiteral("NONE")));
    selectComboByData(m_infil2DDestCombo,   getExt("INFIL_DESTINATION",    QStringLiteral("LOST")));
    selectComboByData(m_evap2DCombo,        getExt("EVAPORATION",          QStringLiteral("YES")));
    for (int c = 0; c < 4; ++c) {
        QSignalBlocker b(m_transport2DBox[c]);
        m_transport2DBox[c]->setChecked(
            parseEngineBool(getExt(transport2DKey(c), QStringLiteral("YES"))) != Qt::Unchecked);
    }
    refreshTransportMatrix();

    // U5 — groundwater process enables. GROUNDWATER reports AS STORED, so
    // AUTO seeds AUTO and an unedited Apply writes nothing.
    selectComboByData(m_gw2DEnableCombo,
                      getExt("GROUNDWATER", QStringLiteral("AUTO")));
    selectComboByData(m_gw2DEtCombo, getExt("GW_ET", QStringLiteral("NONE")));
    update2DGroundwaterEnabled();
}

void SimulationOptionsDialog::update2DGroundwaterEnabled()
{
    if (!m_gw2DGroup) return;
    // Tri-state: only an explicit "On" is a definite yes. Under Automatic the
    // answer depends on whether aquifer rows exist, which this dialog does not
    // count — so it leaves the destination free and lets the engine validate,
    // rather than guessing and locking a control the user cannot then change.
    const QString mode = m_gw2DEnableCombo->currentData().toString();
    const bool on  = (mode == QLatin1String("YES"));
    const bool off = (mode == QLatin1String("NO"));
    m_gw2DEtCombo->setEnabled(!off);
    // The surface Infiltration destination and the subsurface are the two
    // ends of the same water: with the 2D aquifer on, infiltration belongs
    // to it, and the mutual exclusion the engine validates shows here as a
    // state change rather than a hidden group (OPT plan section 7).
    if (m_infil2DDestCombo) {
        const int aq = m_infil2DDestCombo->findData(QStringLiteral("AQUIFER_2D"));
        if (aq >= 0 && on && m_infil2DDestCombo->currentIndex() != aq)
            m_infil2DDestCombo->setCurrentIndex(aq);
        m_infil2DDestCombo->setEnabled(!on);
        m_infil2DDestCombo->setToolTip(
            on ? tr("Locked to the 2D aquifer while Groundwater is enabled: "
                    "infiltrated water enters the subsurface column under the "
                    "cell. Turn Groundwater off to route it elsewhere.")
               : tr("[2D_OPTIONS] INFIL_DESTINATION: where infiltrated water "
                    "goes for every row that does not spell its own DEST "
                    "column. Subcatchment aquifer recharges the legacy aquifer "
                    "of the subcatchment containing each cell; 2D aquifer is "
                    "accepted in the file but a run refuses it until the "
                    "integrated groundwater kernel lands."));
        if (m_infil2DMethodCombo)
            m_infil2DMethodCombo->setEnabled(!on);
    }

    // Name the [GW_*] authoring surface and what a run will do with it.
    int rows = 0;
    if (m_engine) {
        rows = std::max(0, swmm_gw_params_count(m_engine)) +
               std::max(0, swmm_gw_sorption_count(m_engine)) +
               std::max(0, swmm_gw_init_quality_count(m_engine)) +
               std::max(0, swmm_gw_boundary_quality_count(m_engine)) +
               std::max(0, swmm_gw_source_count(m_engine));
    }
    m_gw2DStatusLabel->setText(
        rows > 0
            ? tr("%1 subsurface transport row(s) authored "
                 "([GW_TRANSPORT_PARAMS], [GW_SORPTION], [GW_INITIAL_QUALITY], "
                 "[GW_BOUNDARY_QUALITY], [GW_SOURCES]). They are saved with "
                 "the model; a run reports them as authored-but-inert until "
                 "the integrated groundwater component ships.").arg(rows)
            : tr("No subsurface transport rows authored yet. They are saved "
                 "with the model and run when the integrated groundwater "
                 "component ships."));
}

const char *SimulationOptionsDialog::transport2DKey(int speciesClass)
{
    switch (speciesClass) {
    case SWMM_TRANSPORT_CLASS_POLLUTANTS:  return "TRANSPORT_POLLUTANTS";
    case SWMM_TRANSPORT_CLASS_MSX:         return "TRANSPORT_MSX";
    case SWMM_TRANSPORT_CLASS_AGE:         return "TRANSPORT_AGE";
    case SWMM_TRANSPORT_CLASS_TEMPERATURE: return "TRANSPORT_TEMPERATURE";
    default:                               return "";
    }
}

unsigned SimulationOptionsDialog::report2DVarsMask() const
{
    unsigned mask = 0u;
    for (int r = 0; r < m_report2DVarsList->count(); ++r) {
        const QListWidgetItem *it = m_report2DVarsList->item(r);
        if (it->checkState() == Qt::Checked)
            mask |= 1u << it->data(Qt::UserRole).toInt();
    }
    return mask;
}

void SimulationOptionsDialog::setReport2DVarsMask(unsigned mask)
{
    QSignalBlocker b(m_report2DVarsList);
    for (int r = 0; r < m_report2DVarsList->count(); ++r) {
        QListWidgetItem *it = m_report2DVarsList->item(r);
        const int bit = it->data(Qt::UserRole).toInt();
        const bool isDepth = bit == 0;   // DEPTH is bit 0 and always on
        it->setCheckState((isDepth || (mask & (1u << bit))) ? Qt::Checked : Qt::Unchecked);
    }
    update2DOutputSizeEstimate();
}

QString SimulationOptionsDialog::report2DSpeciesText() const
{
    if (m_report2DAllSpeciesBox->isChecked()) return QStringLiteral("ALL");
    QStringList names;
    for (int r = 0; r < m_report2DSpeciesList->count(); ++r) {
        const QListWidgetItem *it = m_report2DSpeciesList->item(r);
        if (it->checkState() == Qt::Checked) names << it->text();
    }
    // Nothing ticked means "all" to the engine (empty list = unfiltered);
    // spell it out so the .inp reads as intended.
    return names.isEmpty() ? QStringLiteral("ALL") : names.join(QLatin1Char(' '));
}

void SimulationOptionsDialog::setReport2DSpeciesText(const QString &text)
{
    QSignalBlocker b1(m_report2DAllSpeciesBox);
    QSignalBlocker b2(m_report2DSpeciesList);
    const QStringList names = text.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                         Qt::SkipEmptyParts);
    const bool all = names.isEmpty() ||
        (names.size() == 1 && names.first().compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0);
    for (int r = 0; r < m_report2DSpeciesList->count(); ++r)
        m_report2DSpeciesList->item(r)->setCheckState(Qt::Unchecked);
    if (!all) {
        for (const QString &nm : names) {
            auto hits = m_report2DSpeciesList->findItems(nm, Qt::MatchFixedString);
            if (hits.isEmpty()) {
                // Name from the file that the model no longer declares — keep it
                // so the round trip does not silently drop it.
                auto *it = new QListWidgetItem(nm, m_report2DSpeciesList);
                it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
                it->setToolTip(tr("Not declared in this model"));
                hits << it;
            }
            hits.first()->setCheckState(Qt::Checked);
        }
    }
    m_report2DAllSpeciesBox->setChecked(all);
    m_report2DSpeciesList->setEnabled(!all);
}

void SimulationOptionsDialog::update2DOutputSizeEstimate()
{
    if (!m_output2DSizeLabel) return;
    if (!m_engine) { m_output2DSizeLabel->setText(QStringLiteral("—")); return; }

    int nCells = 0, nVerts = 0, nQuads = 0;
    swmm_2d_cell_count(m_engine, &nCells);
    swmm_2d_vertex_count(m_engine, &nVerts);
    swmm_2d_quad_count(m_engine, &nQuads);
    if (nCells <= 0) {
        m_output2DSizeLabel->setText(tr("No mesh loaded — nothing to estimate."));
        return;
    }
    const int edgeStride = nQuads > 0 ? 4 : 3;

    const qint64 durationSec = std::max<qint64>(
        0, m_startEdit->dateTime().secsTo(m_endEdit->dateTime()));
    const qint64 stepSec = m_report2DStepSameBox->isChecked()
        ? m_reportStepEdit->totalSeconds() : m_report2DStepEdit->totalSeconds();
    if (stepSec <= 0 || durationSec <= 0) {
        m_output2DSizeLabel->setText(tr("Set a positive duration and report step."));
        return;
    }
    const double frames = static_cast<double>(durationSec) / stepSec + 1.0;

    int nSpecies = 0;
    if (m_report2DAllSpeciesBox->isChecked()) {
        nSpecies = swmm_pollutant_count(m_engine);
        for (int r = 0; r < m_report2DSpeciesList->count(); ++r)
            if (!m_report2DSpeciesList->item(r)->toolTip().startsWith(QLatin1String("[POLLUTANTS]")))
                ++nSpecies;   // MSX species (the list de-duplicates by name)
    } else {
        for (int r = 0; r < m_report2DSpeciesList->count(); ++r)
            if (m_report2DSpeciesList->item(r)->checkState() == Qt::Checked) ++nSpecies;
    }

    // Values per frame by group; bit order matches swmm_2d_output_variable_name.
    const unsigned mask = report2DVarsMask();
    auto on = [&](const char *token) {
        return (mask & swmm_2d_output_variable_mask(token)) != 0u;
    };
    double perFrame = 1.0;                                        // time
    if (on("DEPTH"))        perFrame += 2.0 * nCells;
    if (on("VELOCITY"))     perFrame += 2.0 * nCells;
    if (on("EDGE_FLUX"))    perFrame += static_cast<double>(nCells) * edgeStride;
    if (on("NODE_HEAD"))    perFrame += 2.0 * nVerts;
    if (on("SPECIES"))      perFrame += static_cast<double>(nSpecies) * nCells;
    if (on("RAINFALL"))     perFrame += 2.0 * nCells;
    if (on("INFILTRATION")) perFrame += 2.0 * nCells;
    if (on("COUPLING"))     perFrame += 2.0 * nCells;
    if (on("GRADIENTS"))    perFrame += 4.0 * nCells;
    if (on("CONTINUITY"))   perFrame += 1.0 * nCells;
    const double bytesPer = m_output2DPrecisionCombo->currentData().toString()
                                .compare(QLatin1String("FLOAT64"), Qt::CaseInsensitive) == 0
                            ? 8.0 : 4.0;
    const double bytes = frames * perFrame * bytesPer;

    auto human = [](double b) {
        const char *units[] = {"B", "KB", "MB", "GB", "TB"};
        int u = 0;
        while (b >= 1024.0 && u < 4) { b /= 1024.0; ++u; }
        return QStringLiteral("%1 %2").arg(b, 0, 'f', u == 0 ? 0 : 1)
                                      .arg(QLatin1String(units[u]));
    };
    m_output2DSizeLabel->setText(
        tr("≈ %1 uncompressed (%2 frames × %3 values × %4 B)%5")
            .arg(human(bytes))
            .arg(static_cast<qint64>(frames))
            .arg(QLocale().toString(static_cast<qint64>(perFrame)))
            .arg(static_cast<int>(bytesPer))
            .arg(m_output2DCompressionSpin->value() > 0
                     ? tr("; deflate level %1 typically 2–5× smaller")
                           .arg(m_output2DCompressionSpin->value())
                     : QString()));
}

int SimulationOptionsDialog::write2DToEngine(int &n)
{
    auto getExt = [&](const char *key) -> QString {
        if (!m_engine) return {};
        char buf[256] = {};
        if (swmm_options_get_ext(m_engine, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
        return {};
    };
    auto setExt = [&](const char *key, const QString &v) -> bool {
        if (!m_engine) return false;
        return swmm_options_set_ext(m_engine, key, v.toUtf8().constData()) == 0;
    };
    auto writeIfChanged = [&](const char *key, const QString &cur, const QString &nv) {
        // Same record as writeToEngine's pass: [2D_OPTIONS] keys are written
        // through swmm_options_set_ext, but they are still option editors and
        // must appear in the reachability comparison.
        m_lastWriteKeys << QString::fromLatin1(key);
        // Numeric-aware compare, same rationale as writeToEngine().
        if (optionValueEquals(cur, nv)) return;
        if (setExt(key, nv)) ++n;
    };

    writeIfChanged("MAX_TIMESTEP",      getExt("MAX_TIMESTEP"),
                   QString::number(m_maxTimestepSpin->value(), 'g', 8));
    writeIfChanged("DRY_DEPTH",         getExt("DRY_DEPTH"),
                   QString::number(m_dryDepthSpin->value(), 'g', 8));
    writeIfChanged("LIMITER_EPSILON",   getExt("LIMITER_EPSILON"),
                   QString::number(m_limiterEpsSpin->value(), 'g', 8));
    writeIfChanged("FLUX_DH_EPS",       getExt("FLUX_DH_EPS"),
                   QString::number(m_fluxDhEpsSpin->value(), 'g', 8));
    writeIfChanged("CELL_CLOSURE",        getExt("CELL_CLOSURE"),
                   m_cellClosureCombo->currentData().toString());
    writeIfChanged("FACE_RECONSTRUCTION", getExt("FACE_RECONSTRUCTION"),
                   m_faceReconCombo->currentData().toString());
    writeIfChanged("VFR_MIN_WET_FRAC",  getExt("VFR_MIN_WET_FRAC"),
                   QString::number(m_vfrMinWetFracSpin->value(), 'g', 6));
    writeIfChanged("COUPLING_CD",       getExt("COUPLING_CD"),
                   QString::number(m_couplingCdSpin->value(), 'f', 4));
    writeIfChanged("COUPLING_SYNC",     getExt("COUPLING_SYNC"),
                   QString::number(m_couplingSyncSpin->value(), 'g', 6));
    writeIfChanged("RAINFALL_MODE",     getExt("RAINFALL_MODE"),
                   m_rainfall2DModeCombo->currentData().toString());
    writeIfChanged("REPORT_2D",         getExt("REPORT_2D"),
                   engineBoolString(m_report2DBox->isChecked()));

    // Explicit-marcher configuration (the only 2D integrator).
    writeIfChanged("THETA",         getExt("THETA"),
                   QString::number(m_thetaSpin->value(), 'g', 6));
    writeIfChanged("CFL_NUMBER",    getExt("CFL_NUMBER"),
                   QString::number(m_cflNumberSpin->value(), 'g', 6));
    writeIfChanged("LTS_TIERS",     getExt("LTS_TIERS"),
                   QString::number(m_ltsTiersSpin->value()));
    writeIfChanged("H_MOVE",        getExt("H_MOVE"),
                   QString::number(m_hMoveSpin->value(), 'g', 6));
    writeIfChanged("FROUDE_MAX",    getExt("FROUDE_MAX"),
                   QString::number(m_froudeMaxSpin->value(), 'g', 6));
    writeIfChanged("MOMENTUM_EQUATION", getExt("MOMENTUM_EQUATION"),
                   m_momentum2DCombo->currentData().toString());
    writeIfChanged("RECONSTRUCTION_ORDER", getExt("RECONSTRUCTION_ORDER"),
                   QString::number(m_reconOrder2DSpin->value()));
    writeIfChanged("ADVECTION",     getExt("ADVECTION"),
                   engineBoolString(m_advection2DBox->isChecked()));
    writeIfChanged("BACKEND",       getExt("BACKEND"),
                   m_backend2DCombo->currentData().toString());
    writeIfChanged("COUPLING_AREA", getExt("COUPLING_AREA"),
                   m_couplingAreaAutoBox->isChecked()
                       ? QStringLiteral("AUTO")
                       : QStringLiteral("DEFAULT"));
    // Empty clears the key — InpWriter omits OUTPUT_FILE when unset and the
    // run wiring falls back to <model>.2d.h5.
    writeIfChanged("OUTPUT_FILE",   getExt("OUTPUT_FILE"),
                   m_output2DFileEdit->text().trimmed());

    // E1 — precision / compression / cadence / variables / species.
    auto getExtWide = [&](const char *key) -> QString {
        if (!m_engine) return {};
        std::vector<char> buf(4096, '\0');
        if (swmm_options_get_ext(m_engine, key, buf.data(), static_cast<int>(buf.size())) == 0)
            return QString::fromUtf8(buf.data()).trimmed();
        return {};
    };
    writeIfChanged("OUTPUT_PRECISION", getExt("OUTPUT_PRECISION"),
                   m_output2DPrecisionCombo->currentData().toString());
    writeIfChanged("OUTPUT_COMPRESSION", getExt("OUTPUT_COMPRESSION"),
                   QString::number(m_output2DCompressionSpin->value()));
    {
        // Engine formats the key as HH:MM:SS; compare in seconds so an
        // untouched value is not re-written.
        const qint64 cur = parseStepSeconds(getExt("REPORT_2D_STEP"), 0);
        const qint64 nv  = m_report2DStepSameBox->isChecked()
                               ? 0 : m_report2DStepEdit->totalSeconds();
        if (cur != nv) {
            const qint64 h = nv / 3600, m = (nv % 3600) / 60, s = nv % 60;
            if (setExt("REPORT_2D_STEP",
                       QStringLiteral("%1:%2:%3")
                           .arg(h, 2, 10, QLatin1Char('0'))
                           .arg(m, 2, 10, QLatin1Char('0'))
                           .arg(s, 2, 10, QLatin1Char('0'))))
                ++n;
        }
    }
    {
        // Compare as masks: "DEFAULT" and its expanded token list are equal.
        const QString curText = getExtWide("REPORT_2D_VARIABLES");
        const unsigned cur = curText.isEmpty()
            ? swmm_2d_output_variable_mask("DEFAULT")
            : swmm_2d_output_variable_mask(curText.toUtf8().constData());
        const unsigned nv = report2DVarsMask();
        if (cur != nv && setExt("REPORT_2D_VARIABLES",
                                QString::fromLatin1(swmm_2d_output_variable_text(nv))))
            ++n;
    }
    {
        const QString cur = getExtWide("REPORT_2D_SPECIES");
        const QString nv  = report2DSpeciesText();
        const bool curAll = cur.isEmpty() || cur.compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0;
        const bool nvAll  = nv.compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0;
        if (!(curAll && nvAll) && cur.compare(nv, Qt::CaseInsensitive) != 0 &&
            setExt("REPORT_2D_SPECIES", nv))
            ++n;
    }

    // U1 — Processes group (E2 keys).
    writeIfChanged("INFILTRATION", getExt("INFILTRATION"),
                   m_infil2DModeCombo->currentData().toString());
    {
        // INFIL_STEP is the alias of [2D_INFILTRATION_OPTIONS] INFIL_STEP;
        // compare in seconds (engine spells HH:MM:SS).
        const qint64 cur = parseStepSeconds(getExt("INFIL_STEP"), 0);
        const qint64 nv  = m_infil2DStepSameBox->isChecked() ? 0
                                                              : m_infil2DStepEdit->totalSeconds();
        if (cur != nv) {
            const qint64 h = nv / 3600, m = (nv % 3600) / 60, sec = nv % 60;
            if (setExt("INFIL_STEP", QStringLiteral("%1:%2:%3")
                                         .arg(h, 2, 10, QLatin1Char('0'))
                                         .arg(m, 2, 10, QLatin1Char('0'))
                                         .arg(sec, 2, 10, QLatin1Char('0'))))
                ++n;
        }
    }
    writeIfChanged("INFIL_DEFAULT_METHOD", getExt("INFIL_DEFAULT_METHOD"),
                   m_infil2DMethodCombo->currentData().toString());
    writeIfChanged("INFIL_DESTINATION", getExt("INFIL_DESTINATION"),
                   m_infil2DDestCombo->currentData().toString());
    writeIfChanged("EVAPORATION", getExt("EVAPORATION"),
                   m_evap2DCombo->currentData().toString());
    for (int c = 0; c < 4; ++c)
        writeIfChanged(transport2DKey(c), getExt(transport2DKey(c)),
                       engineBoolString(m_transport2DBox[c]->isChecked()));

    // U5 — groundwater process enables. GW_ET writes through the alias into
    // [2D_AQUIFER_OPTIONS], so this and the Mesh 2D aquifer editor set the
    // same value.
    writeIfChanged("GROUNDWATER", getExt("GROUNDWATER"),
                   m_gw2DEnableCombo->currentData().toString());
    writeIfChanged("GW_ET", getExt("GW_ET"),
                   m_gw2DEtCombo->currentData().toString());
    return n;
}

#endif // OPENSWMM_HAS_2D

// ---------------------------------------------------------------------------
// Engine helpers
// ---------------------------------------------------------------------------

QString SimulationOptionsDialog::getOption(const char *key,
                                           const QString &fallback) const
{
    if (!m_engine) return fallback;
    char buf[256] = {};
    if (swmm_options_get(m_engine, key, buf, sizeof(buf)) == 0)
        return QString::fromUtf8(buf).trimmed();
    return fallback;
}

// ---------------------------------------------------------------------------
// [EVENTS] section helpers (Slice CW — 2026-05-21)
// ---------------------------------------------------------------------------
// oaDateFromQDateTime / qDateTimeFromOaDate are static methods on
// SimulationOptionsDialog defined in simulationoptionshelpers.cpp so the
// leaf QtTest can link them without dragging the spatial-tab OGR cascade.

namespace {

// Clicking a cell WIDGET never reaches the table's mousePressEvent, so the
// row under the click is not selected and row-based actions (Remove) see no
// selection.  Mirror the editor's focus into a row selection instead: on
// FocusIn, find the row owning this editor and select it.
class EventEditorFocusFilter : public QObject
{
public:
    EventEditorFocusFilter(QTableWidget *table, QObject *parent)
        : QObject(parent), m_table(table) {}

protected:
    bool eventFilter(QObject *watched, QEvent *ev) override
    {
        if (ev->type() == QEvent::FocusIn && m_table) {
            auto *w = qobject_cast<QWidget *>(watched);
            for (int r = 0; w && r < m_table->rowCount(); ++r) {
                for (int c = 0; c < m_table->columnCount(); ++c) {
                    if (m_table->cellWidget(r, c) == w) {
                        m_table->selectRow(r);
                        return false;
                    }
                }
            }
        }
        return false;
    }

private:
    QTableWidget *m_table;
};

// Wrap a QDateTimeEdit inside a QTableWidget cell.  Centralised so every
// row uses the same display format / calendar policy.  HH:MM precision
// (legacy SWMM 5 parity, decided 2026-05-21).
QDateTimeEdit *makeEventCellEditor(const QDateTime &dt, QWidget *parent)
{
    auto *edit = new QDateTimeEdit(dt, parent);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("MM/dd/yyyy HH:mm"));
    edit->setFrame(false);
    if (auto *table = qobject_cast<QTableWidget *>(parent))
        edit->installEventFilter(new EventEditorFocusFilter(table, edit));
    return edit;
}

} // namespace

void SimulationOptionsDialog::addEventRow()
{
    if (!m_eventsTable) return;
    const int row = m_eventsTable->rowCount();
    m_eventsTable->insertRow(row);
    // Default both columns to (project start, project end) so the user only
    // edits the deltas.  Fall back to "now" when the Dates tab edits haven't
    // been populated yet (shouldn't happen — buildDatesTab seeds them).
    const QDateTime defStart = m_startEdit ? m_startEdit->dateTime()
                                           : QDateTime::currentDateTime();
    const QDateTime defEnd   = m_endEdit   ? m_endEdit->dateTime()
                                           : defStart.addDays(1);
    m_eventsTable->setCellWidget(row, 0, makeEventCellEditor(defStart, m_eventsTable));
    m_eventsTable->setCellWidget(row, 1, makeEventCellEditor(defEnd,   m_eventsTable));
}

void SimulationOptionsDialog::removeSelectedEventRows()
{
    if (!m_eventsTable) return;
    // Distinct rows, descending, so removeRow() doesn't shift the indices
    // we still need to delete. Shares the query with the Remove-button
    // enable gate so the two can't disagree about what counts as selected.
    const QList<int> rows = selectedRowsDescending(m_eventsTable);
    for (int r : rows)
        m_eventsTable->removeRow(r);
}

void SimulationOptionsDialog::readEventsFromEngine()
{
    if (!m_eventsTable) return;

    // Wipe before refilling — readFromEngine is also called after writeApply
    // to surface engine-normalised values.
    m_eventsTable->setRowCount(0);
    m_eventsSnapshot.clear();

    if (!m_engine) return;

    int count = 0;
    if (swmm_events_count(m_engine, &count) != 0) return;

    for (int i = 0; i < count; ++i) {
        double start = 0.0, end = 0.0;
        if (swmm_events_get(m_engine, i, &start, &end) != 0) continue;
        const QDateTime qs = qDateTimeFromOaDate(start);   // static helper
        const QDateTime qe = qDateTimeFromOaDate(end);     // static helper

        const int row = m_eventsTable->rowCount();
        m_eventsTable->insertRow(row);
        m_eventsTable->setCellWidget(row, 0, makeEventCellEditor(qs, m_eventsTable));
        m_eventsTable->setCellWidget(row, 1, makeEventCellEditor(qe, m_eventsTable));
        m_eventsSnapshot.append(qMakePair(qs, qe));
    }
}

bool SimulationOptionsDialog::validateEvents(QString *warn)
{
    if (!m_eventsTable) return true;

    bool anyInvalid = false;
    QList<QPair<QDateTime, QDateTime>> rows;
    const int n = m_eventsTable->rowCount();
    rows.reserve(n);
    // Tokenized error fill (D5) — the old hardcoded #ffc8c8 was
    // illegible on the dark theme.
    const QString badStyle = QStringLiteral("QDateTimeEdit { %1 }")
                                 .arg(openswmmvis::ui::theme::errorFillStyle());
    for (int r = 0; r < n; ++r) {
        auto *startEdit = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 0));
        auto *endEdit   = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 1));
        if (!startEdit || !endEdit) { anyInvalid = true; continue; }
        const QDateTime s = startEdit->dateTime();
        const QDateTime e = endEdit->dateTime();
        rows.append(qMakePair(s, e));

        const bool bad = !(s < e);
        startEdit->setStyleSheet(bad ? badStyle : QString());
        endEdit  ->setStyleSheet(bad ? badStyle : QString());
        const QString tip = bad ? tr("Start must be earlier than End.")
                                : QString();
        startEdit->setToolTip(tip);
        endEdit  ->setToolTip(tip);
        if (bad) anyInvalid = true;
    }

    if (warn) {
        // Out-of-range check against the simulation window.
        const QDateTime simStart = m_startEdit ? m_startEdit->dateTime() : QDateTime();
        const QDateTime simEnd   = m_endEdit   ? m_endEdit->dateTime()   : QDateTime();
        for (int r = 0; r < rows.size(); ++r) {
            const auto &p = rows[r];
            if (simStart.isValid() && simEnd.isValid()
                && (p.second <= simStart || p.first >= simEnd))
            {
                *warn += tr("Row %1 lies entirely outside the simulation window.\n")
                            .arg(r + 1);
            }
        }
        // Overlap detection: O(n^2) — n is small (typically << 20).
        for (int i = 0; i < rows.size(); ++i)
            for (int j = i + 1; j < rows.size(); ++j)
                if (rows[i].first < rows[j].second &&
                    rows[j].first < rows[i].second)
                {
                    *warn += tr("Rows %1 and %2 overlap.\n")
                                .arg(i + 1).arg(j + 1);
                }
    }

    return !anyInvalid;
}

int SimulationOptionsDialog::writeEventsToEngine()
{
    if (!m_eventsTable || !m_engine) return 0;

    // Snapshot the table into a flat list for diffing against m_eventsSnapshot.
    QList<QPair<QDateTime, QDateTime>> current;
    const int n = m_eventsTable->rowCount();
    current.reserve(n);
    for (int r = 0; r < n; ++r) {
        auto *startEdit = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 0));
        auto *endEdit   = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 1));
        if (!startEdit || !endEdit) continue;
        current.append(qMakePair(startEdit->dateTime(), endEdit->dateTime()));
    }

    if (current == m_eventsSnapshot)
        return 0;   // no change → no write, no dirty flag

    if (swmm_events_clear(m_engine) != 0)
        return 0;

    int written = 0;
    for (const auto &p : current) {
        const double start = oaDateFromQDateTime(p.first);
        const double end   = oaDateFromQDateTime(p.second);
        if (!(start < end)) continue;   // skip invalid rows defensively
        if (swmm_events_add(m_engine, start, end, nullptr) == 0)
            ++written;
    }

    m_eventsSnapshot = current;
    return written;
}

// ---------------------------------------------------------------------------
// Files / Output / Plugins sub-tab validation (Phase 3.10.4 — 2026-05-22)
// ---------------------------------------------------------------------------
//
// Blocking errors:
//   • [PLUGINS] row with empty plugin id (column 0).
//   • Scheduled hot-start save row with empty path (column 0).
//
// Non-blocking warnings (appended to @p warn, surfaced as Yes/No to user):
//   • Report selector with "Selected" radio + empty list → silently
//     collapses to NONE on write; warn the user to make it explicit.
//   • .rpt / .out parent directory missing (typo in path, etc).
//
bool SimulationOptionsDialog::validateFilesTab(QString *warn)
{
    bool anyInvalid = false;

    // ── [PLUGINS] table: empty id rejected ─────────────────────────────
    //
    // Phase 3.10.6 — model-backed.  The blocking error is surfaced via
    // the @p warn channel (previously a per-cell red background, which
    // QAbstractTableModel doesn't expose without an extra role round-
    // trip); anyInvalid still stops Apply.
    if (m_pluginsModel) {
        const int n = m_pluginsModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QString id = m_pluginsModel->pathAt(r).trimmed();
            if (id.isEmpty()) {
                anyInvalid = true;
                if (warn)
                    warn->append(tr("[PLUGINS] row %1: plugin id / path is required.\n")
                                     .arg(r + 1));
            }
        }
    }

    // ── Hot-start saves table: empty path rejected ─────────────────────
    //
    // Phase 3.10.5 — table is now MVC-backed.  We can't paint per-cell
    // background through the model without an extra role round-trip, so
    // the inline error affordance is the row's row-header text and a
    // tool-tipped warning surfaced via the @p warn channel; the blocking
    // anyInvalid flag still stops Apply.
    if (m_hotstartSavesModel) {
        const int n = m_hotstartSavesModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QString p = m_hotstartSavesModel->pathAt(r).trimmed();
            if (p.isEmpty()) {
                anyInvalid = true;
                if (warn)
                    warn->append(tr("Hot-start save row %1: path is required.\n")
                                     .arg(r + 1));
            }
        }
    }

    // ── Non-blocking warnings ──────────────────────────────────────────
    if (warn) {
        auto checkSelector = [warn](QRadioButton *some, QLineEdit *list,
                                    const QString &label) {
            if (!some || !list) return;
            if (some->isChecked() && list->text().trimmed().isEmpty()) {
                *warn += SimulationOptionsDialog::tr(
                    "%1: \"Selected\" is chosen but the name list is empty "
                    "(will be written as NONE).\n").arg(label);
            }
        };
        checkSelector(m_rptSubcatchSomeRadio, m_rptSubcatchListEdit,
                      tr("Subcatchments"));
        checkSelector(m_rptNodeSomeRadio,     m_rptNodeListEdit,
                      tr("Nodes"));
        checkSelector(m_rptLinkSomeRadio,     m_rptLinkListEdit,
                      tr("Links"));

        auto checkParentDir = [warn](QLineEdit *edit, const QString &label) {
            if (!edit) return;
            const QString path = edit->text().trimmed();
            if (path.isEmpty()) return;
            const QFileInfo fi(path);
            const QDir parent = fi.absoluteDir();
            if (!parent.exists()) {
                *warn += SimulationOptionsDialog::tr(
                    "%1: parent directory does not exist (%2).\n")
                    .arg(label, QDir::toNativeSeparators(parent.absolutePath()));
            }
        };
        checkParentDir(m_reportFilePathEdit, tr("Report file"));
        checkParentDir(m_outputFilePathEdit, tr("Output file"));
    }

    return !anyInvalid;
}

// ---------------------------------------------------------------------------
// [REPORT] contents editor (Slice BV.1 — 2026-05-22)
// ---------------------------------------------------------------------------
//
// Engine surface: RPT_DISABLED / RPT_INPUT / RPT_CONTINUITY / RPT_FLOWSTATS /
// RPT_CONTROLS / RPT_AVERAGES (booleans, YES/NO) plus RPT_SUBCATCHMENTS /
// RPT_NODES / RPT_LINKS (selectors, "ALL" / "NONE" / "name1,name2,...").

void SimulationOptionsDialog::buildReportContentsGroup(QVBoxLayout *parentLayout,
                                                       QWidget *page)
{
    auto *grp = new QGroupBox(tr("Report contents ([REPORT])"), page);
    auto *vlay = new QVBoxLayout(grp);
    grp->setToolTip(tr(
        "Controls which summary sections and which objects appear in the "
        "simulation report (.rpt) and binary output (.out) files."));

    // ---- bool flags row ------------------------------------------------
    auto *flagsGroup = new QGroupBox(tr("Summary sections"), grp);
    auto *flagsForm  = new QFormLayout(flagsGroup);

    m_rptDisabledBox   = new QCheckBox(tr("Disable all reporting (DISABLED)"),  flagsGroup);
    m_rptInputBox      = new QCheckBox(tr("Echo input summary (INPUT)"),         flagsGroup);
    m_rptContinuityBox = new QCheckBox(tr("Continuity errors (CONTINUITY)"),     flagsGroup);
    m_rptFlowstatsBox  = new QCheckBox(tr("Flow statistics (FLOWSTATS)"),        flagsGroup);
    m_rptControlsBox   = new QCheckBox(tr("Control rule actions (CONTROLS)"),    flagsGroup);
    m_rptAveragesBox   = new QCheckBox(tr("Time-averaged results (AVERAGES)"),   flagsGroup);

    flagsForm->addRow(QString(), m_rptDisabledBox);
    flagsForm->addRow(QString(), m_rptInputBox);
    flagsForm->addRow(QString(), m_rptContinuityBox);
    flagsForm->addRow(QString(), m_rptFlowstatsBox);
    flagsForm->addRow(QString(), m_rptControlsBox);
    flagsForm->addRow(QString(), m_rptAveragesBox);

    // DISABLED short-circuits everything else — grey out the dependent
    // controls when it's on so the user knows nothing else matters.
    auto syncDisabledShortCircuit = [this, flagsGroup]() {
        const bool disabled = m_rptDisabledBox && m_rptDisabledBox->isChecked();
        for (QCheckBox *cb : { m_rptInputBox, m_rptContinuityBox,
                               m_rptFlowstatsBox, m_rptControlsBox,
                               m_rptAveragesBox })
            if (cb) cb->setEnabled(!disabled);
        Q_UNUSED(flagsGroup);
    };
    connect(m_rptDisabledBox, &QCheckBox::toggled,
            this, [syncDisabledShortCircuit](bool) { syncDisabledShortCircuit(); });

    vlay->addWidget(flagsGroup);

    // REPORT_SIGNED_HEADS ([OPTIONS], engine issue #156 O-6; GUI issue #10).
    // Output option for any routing model; deliberately outside the
    // RPT_DISABLED short-circuit — it shapes the .out, not the .rpt.
    m_signedHeadsCheck = new QCheckBox(
        tr("Report signed piezometric heads (sub-atmospheric)"), grp);
    m_signedHeadsCheck->setToolTip(
        tr("When on, the binary output's HEAD variable carries the signed "
           "piezometric head, so sub-atmospheric full-pipe pressure (e.g. "
           "under the TPA closure) is visible; DEPTH stays floored at zero "
           "(REPORT_SIGNED_HEADS). Off (the default) keeps legacy "
           "bit-parity."));
    vlay->addWidget(m_signedHeadsCheck);

    // ---- selectors -----------------------------------------------------
    // Per-kind: a row with [○ None] [○ All] [○ Selected] [name list edit].
    // The line-edit greys out unless Selected is chosen.
    auto buildSelector = [this, grp](const QString &label,
                                      QRadioButton **noneR,
                                      QRadioButton **allR,
                                      QRadioButton **someR,
                                      QLineEdit    **listE)
    {
        auto *row = new QGroupBox(label, grp);
        auto *h = new QHBoxLayout(row);

        *noneR = new QRadioButton(tr("None"),     row);
        *allR  = new QRadioButton(tr("All"),      row);
        *someR = new QRadioButton(tr("Selected:"), row);

        auto *bg = new QButtonGroup(row);
        bg->addButton(*noneR, 0);
        bg->addButton(*allR,  1);
        bg->addButton(*someR, 2);

        *listE = new QLineEdit(row);
        (*listE)->setPlaceholderText(
            tr("comma- or space-separated object names"));
        (*listE)->setEnabled(false);

        h->addWidget(*noneR);
        h->addWidget(*allR);
        h->addWidget(*someR);
        h->addWidget(*listE, 1);

        // Edit field follows the Selected radio.
        connect(*someR, &QRadioButton::toggled, *listE, &QLineEdit::setEnabled);

        return row;
    };

    vlay->addWidget(buildSelector(tr("Subcatchments"),
        &m_rptSubcatchNoneRadio, &m_rptSubcatchAllRadio,
        &m_rptSubcatchSomeRadio, &m_rptSubcatchListEdit));
    vlay->addWidget(buildSelector(tr("Nodes"),
        &m_rptNodeNoneRadio, &m_rptNodeAllRadio,
        &m_rptNodeSomeRadio, &m_rptNodeListEdit));
    vlay->addWidget(buildSelector(tr("Links"),
        &m_rptLinkNoneRadio, &m_rptLinkAllRadio,
        &m_rptLinkSomeRadio, &m_rptLinkListEdit));

    parentLayout->addWidget(grp);
}

void SimulationOptionsDialog::readReportContentsFromEngine()
{
    if (!m_engine) return;

    // Bool keys ---------------------------------------------------------
    auto setBox = [this](QCheckBox *box, const char *key, bool fallback) {
        if (!box) return;
        const QString v = getOption(key, fallback ? QStringLiteral("YES")
                                                  : QStringLiteral("NO"));
        QSignalBlocker blk(box);
        box->setChecked(parseEngineBool(v) == Qt::Checked);
    };
    setBox(m_rptDisabledBox,   "RPT_DISABLED",   false);
    setBox(m_rptInputBox,      "RPT_INPUT",      false);
    setBox(m_rptContinuityBox, "RPT_CONTINUITY", true);
    setBox(m_rptFlowstatsBox,  "RPT_FLOWSTATS",  true);
    setBox(m_rptControlsBox,   "RPT_CONTROLS",   false);
    setBox(m_rptAveragesBox,   "RPT_AVERAGES",   false);
    setBox(m_signedHeadsCheck, "REPORT_SIGNED_HEADS", false);

    // Sync the disabled-short-circuit state once after the initial read.
    if (m_rptDisabledBox) {
        const bool disabled = m_rptDisabledBox->isChecked();
        for (QCheckBox *cb : { m_rptInputBox, m_rptContinuityBox,
                               m_rptFlowstatsBox, m_rptControlsBox,
                               m_rptAveragesBox })
            if (cb) cb->setEnabled(!disabled);
    }

    // Selector keys -----------------------------------------------------
    auto setSelector = [this](QRadioButton *noneR, QRadioButton *allR,
                              QRadioButton *someR, QLineEdit *listE,
                              const char *key) {
        if (!noneR || !allR || !someR || !listE) return;
        const QString v = getOption(key, QStringLiteral("ALL")).trimmed();
        QSignalBlocker b1(noneR), b2(allR), b3(someR), b4(listE);
        if (v.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) == 0) {
            noneR->setChecked(true);
            listE->clear();
            listE->setEnabled(false);
        } else if (v.isEmpty()
                || v.compare(QStringLiteral("ALL"), Qt::CaseInsensitive) == 0) {
            allR->setChecked(true);
            listE->clear();
            listE->setEnabled(false);
        } else {
            someR->setChecked(true);
            listE->setText(v);
            listE->setEnabled(true);
        }
    };
    setSelector(m_rptSubcatchNoneRadio, m_rptSubcatchAllRadio,
                m_rptSubcatchSomeRadio, m_rptSubcatchListEdit,
                "RPT_SUBCATCHMENTS");
    setSelector(m_rptNodeNoneRadio, m_rptNodeAllRadio,
                m_rptNodeSomeRadio, m_rptNodeListEdit,
                "RPT_NODES");
    setSelector(m_rptLinkNoneRadio, m_rptLinkAllRadio,
                m_rptLinkSomeRadio, m_rptLinkListEdit,
                "RPT_LINKS");
}

int SimulationOptionsDialog::writeReportContentsToEngine()
{
    if (!m_engine) return 0;
    int n = 0;
    auto writeIfChanged = [this, &n](const char *key, const QString &newVal) {
        if (getOption(key) == newVal) return;
        if (setOption(key, newVal)) ++n;
    };

    auto boolStr = [](QCheckBox *box, bool def) {
        return box ? engineBoolString(box->isChecked()) : engineBoolString(def);
    };
    writeIfChanged("RPT_DISABLED",   boolStr(m_rptDisabledBox,   false));
    writeIfChanged("RPT_INPUT",      boolStr(m_rptInputBox,      false));
    writeIfChanged("RPT_CONTINUITY", boolStr(m_rptContinuityBox, true));
    writeIfChanged("RPT_FLOWSTATS",  boolStr(m_rptFlowstatsBox,  true));
    writeIfChanged("RPT_CONTROLS",   boolStr(m_rptControlsBox,   false));
    writeIfChanged("RPT_AVERAGES",   boolStr(m_rptAveragesBox,   false));
    writeIfChanged("REPORT_SIGNED_HEADS", boolStr(m_signedHeadsCheck, false));

    auto selectorStr = [](QRadioButton *noneR, QRadioButton *allR,
                          QRadioButton *someR, QLineEdit *listE) {
        if (!noneR || !allR || !someR || !listE) return QStringLiteral("ALL");
        if (noneR->isChecked()) return QStringLiteral("NONE");
        if (allR->isChecked())  return QStringLiteral("ALL");
        // Selected — but empty list collapses to NONE so we don't push
        // an invalid empty SOME state through the engine.
        const QString text = listE->text().trimmed();
        return text.isEmpty() ? QStringLiteral("NONE") : text;
    };
    writeIfChanged("RPT_SUBCATCHMENTS",
        selectorStr(m_rptSubcatchNoneRadio, m_rptSubcatchAllRadio,
                    m_rptSubcatchSomeRadio, m_rptSubcatchListEdit));
    writeIfChanged("RPT_NODES",
        selectorStr(m_rptNodeNoneRadio, m_rptNodeAllRadio,
                    m_rptNodeSomeRadio, m_rptNodeListEdit));
    writeIfChanged("RPT_LINKS",
        selectorStr(m_rptLinkNoneRadio, m_rptLinkAllRadio,
                    m_rptLinkSomeRadio, m_rptLinkListEdit));

    return n;
}

bool SimulationOptionsDialog::setOption(const char *key, const QString &value)
{
    // Prefer the layer's setOption when available — it emits
    // optionsChanged() which the main window + status bar listen to, so
    // per-key writes do the live-sync automatically instead of
    // depending on a post-hoc refresh from the caller. Fall back to the
    // raw engine API when no layer is bound (e.g. dialog used in
    // engine-only tests).
    if (m_layer) {
        return m_layer->setOption(QByteArray(key), value);
    }
    if (!m_engine) return false;
    const QByteArray v = value.toUtf8();
    return swmm_options_set(m_engine, key, v.constData()) == 0;
}

// ---------------------------------------------------------------------------
// Engine ↔ widgets
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::readFromEngine()
{
    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };

    // Source every getOption() fallback from PreferencesManager so the dialog
    // shows the user-preferred default whenever the engine has no value for
    // a key — keeps the new-project synthesis path and the missing-key path
    // in lockstep and avoids hardcoded magic-number drift.
    const auto sim = PreferencesManager::instance()->simulationDefaults();
    const auto ynStr = [](bool v) {
        return v ? QStringLiteral("YES") : QStringLiteral("NO");
    };

    // ---- Tab 0 — Title / Notes ----------------------------------------
    if (m_titleNotesEdit) {
        QSignalBlocker blk(m_titleNotesEdit);
        // Prefer the .oswp-persisted rich HTML when available — it preserves
        // formatting that the engine's plain-text [TITLE] cannot.
        const QString persistedHtml = m_projectWindow ? m_projectWindow->notesHtml()
                                                      : QString();
        if (!persistedHtml.isEmpty()) {
            m_titleNotesEdit->setHtml(persistedHtml);
        } else if (m_engine) {
            int count = 0;
            QStringList lines;
            if (swmm_title_get_count(m_engine, &count) == 0 && count > 0) {
                lines.reserve(count);
                for (int i = 0; i < count; ++i) {
                    char buf[1024] = {0};
                    if (swmm_title_get_line(m_engine, i, buf, sizeof(buf)) == 0)
                        lines << QString::fromUtf8(buf);
                }
            }
            m_titleNotesEdit->setPlainText(lines.join(QChar('\n')));
        } else {
            m_titleNotesEdit->clear();
        }
        m_initialNotesHtml = m_titleNotesEdit->toHtml();
    }

    // ---- Tab 1 ---------------------------------------------------------
    selectComboByData(m_infiltrationCombo, getOption("INFILTRATION", sim.infiltrationModel));
    selectComboByData(m_routingCombo,      getOption("FLOW_ROUTING", sim.flowRouting));

    m_allowPondingBox->setChecked(parseEngineBool(getOption("ALLOW_PONDING",     ynStr(sim.allowPonding)))    == Qt::Checked);
    m_skipSteadyBox->setChecked(  parseEngineBool(getOption("SKIP_STEADY_STATE", ynStr(sim.skipSteadyState))) == Qt::Checked);
    // Inverted UI: checked = process active = engine IGNORE_X is NO.
    m_ignoreRainfallBox->setChecked(   parseEngineBool(getOption("IGNORE_RAINFALL",    ynStr(sim.ignoreRainfall)))    != Qt::Checked);
    m_ignoreSnowmeltBox->setChecked(   parseEngineBool(getOption("IGNORE_SNOWMELT",    ynStr(sim.ignoreSnowmelt)))    != Qt::Checked);
    m_ignoreGroundwaterBox->setChecked(parseEngineBool(getOption("IGNORE_GROUNDWATER", ynStr(sim.ignoreGroundwater))) != Qt::Checked);
    m_ignoreRDIIBox->setChecked(       parseEngineBool(getOption("IGNORE_RDII",        ynStr(sim.ignoreRdii)))        != Qt::Checked);
    m_ignoreQualityBox->setChecked(    parseEngineBool(getOption("IGNORE_QUALITY",     ynStr(sim.ignoreQuality)))     != Qt::Checked);
    m_ignoreRoutingBox->setChecked(    parseEngineBool(getOption("IGNORE_ROUTING",     ynStr(sim.ignoreRouting)))     != Qt::Checked);

    // Context-sensitive availability (mirrors the legacy Delphi Analysis Options
    // form, Doptions.pas:314-332): a process toggle is disabled when the model
    // has no objects of the class it controls. The box still shows its stored
    // value; it just cannot be edited. Counts come from the engine C API.
    if (m_engine) {
        const int nGages     = swmm_gage_count(m_engine);
        const int nSnowpacks = swmm_snowpack_count(m_engine);
        const int nAquifers  = swmm_aquifer_count(m_engine);
        const int nLinks     = swmm_link_count(m_engine);
        const int nPolluts   = swmm_pollutant_count(m_engine);
        const int nHydros    = swmm_hydrograph_count(m_engine);
        m_ignoreRainfallBox   ->setEnabled(nGages > 0);
        m_ignoreSnowmeltBox   ->setEnabled(nSnowpacks > 0);
        m_ignoreGroundwaterBox->setEnabled(nAquifers > 0);
        m_ignoreRDIIBox       ->setEnabled(nGages > 0 && nHydros > 0);
        m_ignoreQualityBox    ->setEnabled(nPolluts > 0);
        m_ignoreRoutingBox    ->setEnabled(nLinks > 0);
    }

    // ---- Tab 2 ---------------------------------------------------------
    // Block signals on Start/Report-start during seeding so the clamp
    // connection doesn't bump report-start prematurely between the two
    // reads. Seed the minimum + duration label explicitly at the end.
    {
        QSignalBlocker bs(m_startEdit);
        QSignalBlocker br(m_reportStartEdit);

        QDateTime start = parseEngineDateTime(
            getOption("START_DATE"), getOption("START_TIME", "00:00:00"));
        if (start.isValid()) m_startEdit->setDateTime(start);

        QDateTime end = parseEngineDateTime(
            getOption("END_DATE"),   getOption("END_TIME",   "00:00:00"));
        if (end.isValid()) m_endEdit->setDateTime(end);

        QDateTime rpt = parseEngineDateTime(
            getOption("REPORT_START_DATE"),
            getOption("REPORT_START_TIME", "00:00:00"));
        if (rpt.isValid()) m_reportStartEdit->setDateTime(rpt);
    }
    m_reportStartEdit->setMinimumDateTime(m_startEdit->dateTime());
    updateDurationLabel();

    bool ok = false;

    // Numeric option reads: keep the fallback when the engine string fails
    // to parse instead of silently seeding 0 into the spin box (which
    // writeToEngine would then persist as a real edit).
    auto optDouble = [this](const char *key, double fallback) {
        bool okNum = false;
        const double v = getOption(key, QString::number(fallback, 'g', 6))
                             .toDouble(&okNum);
        return okNum ? v : fallback;
    };
    auto optInt = [this](const char *key, int fallback) {
        bool okNum = false;
        const int v = getOption(key, QString::number(fallback)).toInt(&okNum);
        return okNum ? v : fallback;
    };

    // Engine round-trip for step values is loose: a step may come back as
    // plain seconds ("900"), decimal seconds ("900.000000") or as HH:MM:SS
    // ("00:15:00", "48:00:00"). The static parseStepSeconds() helper
    // (simulationoptionshelpers.cpp, unit-tested) accepts all three.

    m_reportStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("REPORT_STEP", QString::number(sim.reportStepSec)),
                         sim.reportStepSec));
    m_dryStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("DRY_STEP", QString::number(sim.dryStepSec)),
                         sim.dryStepSec));
    m_wetStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("WET_STEP", QString::number(sim.wetStepSec)),
                         sim.wetStepSec));

    m_ruleStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("RULE_STEP", QString::number(sim.ruleStepSec)),
                         sim.ruleStepSec));

    const double routeStep = getOption("ROUTING_STEP",
                                       QString::number(sim.routingStepSec, 'g', 6))
                                .toDouble(&ok);
    m_routingStepEdit->setText(
        QString::number(ok ? routeStep : sim.routingStepSec, 'g', 6));

    const double dryDays = getOption("DRY_DAYS",
                                     QString::number(sim.dryDays, 'g', 6))
                              .toDouble(&ok);
    m_dryDaysSpin->setValue(ok ? dryDays : sim.dryDays);

    // Sweep window — engine stores "MM/DD"; map into a fixed-year QDate
    // (2000 is a leap year so 02/29 stays selectable).
    auto parseSweep = [](const QString &s, QDate fallback) {
        const QStringList parts = s.split(QLatin1Char('/'));
        if (parts.size() != 2) return fallback;
        bool okM = false, okD = false;
        const int m = parts[0].toInt(&okM);
        const int d = parts[1].toInt(&okD);
        if (!okM || !okD) return fallback;
        const QDate q(2000, m, d);
        return q.isValid() ? q : fallback;
    };
    const QDate sweepStartPref = parseSweep(sim.sweepStart, QDate(2000, 1, 1));
    const QDate sweepEndPref   = parseSweep(sim.sweepEnd,   QDate(2000, 12, 31));
    m_sweepStartEdit->setDate(parseSweep(getOption("SWEEP_START", sim.sweepStart),
                                         sweepStartPref));
    m_sweepEndEdit->setDate(parseSweep(getOption("SWEEP_END", sim.sweepEnd),
                                       sweepEndPref));

    // ---- Tab 2 — [EVENTS] (Slice CW) -----------------------------------
    readEventsFromEngine();

    // ---- Tab 3 ---------------------------------------------------------
    selectComboByData(m_surchargeCombo,      getOption("SURCHARGE_METHOD",    sim.surchargeMethod));
    selectComboByData(m_nodeContinuityCombo, getOption("NODE_CONTINUITY",     sim.nodeContinuity));
    selectComboByData(m_forceMainCombo,      getOption("FORCE_MAIN_EQUATION", sim.forceMainEquation));
    selectComboByData(m_normalFlowCombo,     getOption("NORMAL_FLOW_LIMITED", sim.normalFlowLimited));
    selectComboByData(m_inertialDampCombo,   getOption("INERTIAL_DAMPING",    sim.inertialDamping));
    m_andersonAccelBox->setChecked(parseEngineBool(getOption("ANDERSON_ACCEL",
                                                              ynStr(sim.andersonAccel))) == Qt::Checked);

    // DPS_* knobs are dynamic-slot specific and not surfaced in
    // PreferencesManager — keep engine-side defaults.
    m_dpsCelerSpin->setValue(optDouble("DPS_CELERITY",   25.0));
    m_dpsAlphaSpin->setValue(optDouble("DPS_ALPHA",      3.0));
    m_dpsDecaySpin->setValue(optDouble("DPS_DECAY_TIME", 0.5));
    // TPA_CELERITY follows the DPS_* rule: method-specific, not surfaced in
    // PreferencesManager — fallback is the engine-side default.
    m_tpaCeleritySpin->setValue(optDouble("TPA_CELERITY", 100.0));

    // Unsteady friction (engine issue #156) — prefs-backed like the other
    // method combos on this tab.
    selectComboByData(m_ufMethodCombo,
                      getOption("UNSTEADY_FRICTION", sim.unsteadyFriction));
    m_ufK3Spin->setValue(optDouble("UF_K3", sim.ufK3));

    m_lengtheningSpin->setValue(
        optDouble("LENGTHENING_STEP", sim.lengtheningStepSec));
    // VARIABLE_STEP toggle in prefs zeroes the Courant factor when off.
    const double variablePref = sim.variableStepOn ? sim.variableStepFactor : 0.0;
    m_variableStepSpin->setValue(optDouble("VARIABLE_STEP", variablePref));
    m_minStepSpin->setValue(optDouble("MINIMUM_STEP", sim.minRoutingStepSec));

    m_maxTrialsSpin->setValue(optInt("MAX_TRIALS", sim.maxTrials));
    m_headTolSpin->setValue(optDouble("HEAD_TOLERANCE", sim.headTolerance));
    // LAT_FLOW_TOL / SYS_FLOW_TOL speak percent through the options API on
    // both get and set, mirroring the .inp surface; prefs hold percent too.
    m_latFlowTolSpin->setValue(optDouble("LAT_FLOW_TOL", sim.latFlowTolPct));
    m_sysFlowTolSpin->setValue(optDouble("SYS_FLOW_TOL", sim.sysFlowTolPct));
    // MIN_SURFAREA isn't in prefs; engine default is 0.
    m_minSurfAreaSpin->setValue(optDouble("MIN_SURFAREA", 0.0));
    m_minSlopeSpin->setValue(optDouble("MIN_SLOPE", sim.minSlopePct));

    // FV_* knobs are FV-routing specific and not surfaced in
    // PreferencesManager — fallbacks are the engine-side defaults.
    m_fvCellLengthSpin->setValue(optDouble("FV_CELL_LENGTH", 0.0));
    m_fvMinCellsSpin->setValue(optInt("FV_MIN_CELLS", 4));
    m_fvCflSpin->setValue(optDouble("FV_CFL", 0.5));
    selectComboByData(m_fvRiemannCombo,  getOption("FV_RIEMANN",  QStringLiteral("HLLC")));
    selectComboByData(m_fvOrderCombo,    getOption("FV_ORDER",    QStringLiteral("1")));
    selectComboByData(m_fvLimiterCombo,  getOption("FV_LIMITER",  QStringLiteral("MINMOD")));
    selectComboByData(m_fvTimeIntCombo,  getOption("FV_TIME_INTEGRATION", QStringLiteral("EULER")));
    m_fvSlotCeleritySpin->setValue(optDouble("FV_SLOT_CELERITY", 100.0));
    selectComboByData(m_fvPressureClosureCombo,
                      getOption("FV_PRESSURE_CLOSURE", QStringLiteral("SLOT")));
    m_fvPressImplicitBox->setChecked(
        parseEngineBool(getOption("FV_PRESSURIZED_IMPLICIT",
                                  QStringLiteral("NO"))) == Qt::Checked);
    selectComboByData(m_fvScalarSchemeCombo,
                      getOption("FV_SCALAR_SCHEME", QStringLiteral("MUSCL")));
    selectComboByData(m_fvStructCouplingCombo,
                      getOption("FV_STRUCTURE_COUPLING", QStringLiteral("SUBSTEP")));
    m_fvCompactionBox->setChecked(
        parseEngineBool(getOption("FV_COMPACTION", QStringLiteral("YES"))) == Qt::Checked);
    selectComboByData(m_fvBackendCombo,  getOption("FV_BACKEND",  QStringLiteral("AUTO")));
    m_fvMinParallelSpin->setValue(optInt("FV_MIN_PARALLEL_CELLS", 20000));
    m_fvLtsBox->setChecked(
        parseEngineBool(getOption("FV_LTS", QStringLiteral("YES"))) == Qt::Checked);
    m_fvLtsTiersSpin->setValue(optInt("FV_LTS_MAX_TIERS", 6));
    m_fvCflCensusSpin->setValue(optInt("FV_CFL_CENSUS_INTERVAL", 1));

    updateSurchargeFieldsEnabled();
    updateFvFieldsEnabled();

    // ---- Quality & Transport (Y1) --------------------------------------
    // Fallbacks are the ENGINE's documented defaults (Y0's gate 1 pins
    // them); if the engine's defaults drift, that gate flags this block
    // for a resync — the same contract the FV group above carries.
    selectComboByData(m_qualitySolverCombo,
                      getOption("QUALITY_SOLVER", QStringLiteral("LEGACY")));
    selectComboByData(m_outfallBackflowCombo,
                      getOption("OUTFALL_BACKFLOW_QUALITY",
                                QStringLiteral("LAST")));
    m_qualityStepSpin->setValue(optDouble("QUALITY_STEP", 0.0));
    m_maxSegmentsSpin->setValue(optInt("MAX_SEGMENTS_PER_LINK", 100));
    selectComboByData(m_dispersionCombo,
                      getOption("DISPERSION", QStringLiteral("OFF")));
    m_rwptSeedSpin->setValue(optInt("RWPT_SEED", 0));
    m_waterAgeBox->setChecked(
        parseEngineBool(getOption("WATER_AGE", QStringLiteral("NO")))
            == Qt::Checked);
    m_heatTransportBox->setChecked(
        parseEngineBool(getOption("HEAT_TRANSPORT", QStringLiteral("NO")))
            == Qt::Checked);
    updateQualitySolverFieldsEnabled();

    // ---- Tab 4 ---------------------------------------------------------
    m_threadsSpin->setValue(optInt("THREADS", sim.threads));

    // ---- 2D module toggle (Tab 1 → Modules group) ----------------------
    // Persisted per-.inp under QSettings since the engine has no native
    // option for "module enabled" — module activation is implicit in the
    // presence of [2D_VERTICES]/[2D_TRIANGLES] sections. When there's no
    // stored preference the default is inferred from the .inp itself: a
    // file that already carries 2D sections (pre-built demos, externally
    // authored models) shows the module ON; a fresh blank .inp shows OFF.
    if (m_module2DBox && m_layer)
    {
        QSettings s;
        const QString key = QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                                .arg(m_layer->modelFilePath());
        // The engine's IGNORE_2D flag is authoritative when set — it is what
        // actually keeps the 2D solver from running. QSettings/.inp sections
        // only seed the intent when the model has never been toggled.
        //
        // Distinguish the two, because only the first is a user *preference*:
        // the engine reports IGNORE_2D = "NO" by default and does not
        // serialise it, so on a 1D deck the key is absent, the box is
        // unchecked purely because the .inp carries no 2D sections, and
        // writing that back as IGNORE_2D YES would turn "this model has no
        // mesh" into "the user asked to ignore 2D" — dirtying the project on
        // an Apply with no edits.
        // The engine cannot tell us whether IGNORE_2D was *set*: it reports the
        // default "NO" for a deck that never mentions the key, so a non-empty
        // read proves nothing. Intent therefore comes only from a stored
        // per-project preference, or from the user toggling the box (see
        // on2DModuleToggled). Everything else is inference from the .inp.
        m_module2DIntentKnown = s.contains(key);
        const bool ignored2d =
            parseEngineBool(getOption("IGNORE_2D", QStringLiteral("NO")))
                == Qt::Checked;
        const bool enabled = !ignored2d &&
            (s.contains(key)
                 ? s.value(key).toBool()
                 : inpCarries2DSections(m_layer->modelFilePath()));
        QSignalBlocker blk(m_module2DBox);  // avoid wiring through on2DModuleToggled twice
        m_module2DBox->setChecked(enabled);
        on2DModuleToggled(enabled);  // explicit: sync tab-enabled state.
    }

    // ---- Tab 6 (2D) — only present when compiled in --------------------
#ifdef OPENSWMM_HAS_2D
    read2DFromEngine();
#endif

    // ---- Tab 7 (Files / Plugins) ----------------------------------------
    readPluginsFromEngine();
    readProcessComponentsFromEngine();   // U1
    readFilesSectionFromEngine();
    readWriterCombosFromEngine();
    readReportContentsFromEngine();
    readOutputPathsFromSettings();

    // U1 — the Models page matrix reflects the engine as last applied (and
    // the 2D page's TRANSPORT_* boxes for its 2D column).
    refreshTransportMatrix();
}

// ---------------------------------------------------------------------------
// Tab 7 — Files / Plugins (Slice AA-3.5)
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::buildFilesTab()
{
    // Phase 3.10 (2026-05-22) — the legacy single "Files" page is split
    // into three sub-tabs (Files / Output / Plugins) via a nested
    // QTabWidget.  Existing widgets keep their member identities; this
    // method just regroups them by concern:
    //   • Files   — [FILES] secondary refs + scheduled hot-start saves
    //   • Output  — writer combos + [REPORT] flags + .rpt / .out paths
    //   • Plugins — [PLUGINS] table editor (Phase 3.10.3)
    auto *page = new QWidget(this);
    auto *outerLay = new QVBoxLayout(page);
    outerLay->setContentsMargins(0, 0, 0, 0);

    auto *subTabs = new QTabWidget(page);
    subTabs->setDocumentMode(true);
    outerLay->addWidget(subTabs);

    // =====================================================================
    // Sub-tab "Files" — [FILES] secondary refs + scheduled hot-start saves
    // =====================================================================
    auto *filesPage = new QWidget(subTabs);
    auto *vlay = new QVBoxLayout(filesPage);

    // ── [FILES] secondary references group ─────────────────────────────
    auto *secondary = new QGroupBox(
        tr("Secondary file references (.inp [FILES] section)"), filesPage);
    auto *secForm = new QFormLayout(secondary);

    auto makeModeCombo = [secondary] {
        auto *c = new QComboBox(secondary);
        c->addItem(tr("(off)"), QString());
        c->addItem(tr("USE"),   QStringLiteral("USE"));
        c->addItem(tr("SAVE"),  QStringLiteral("SAVE"));
        return c;
    };
    // Slice IO-11a — every [FILES] row is a RelativePathPicker so the
    // line edit shows the path relative to the project anchor (defaulting
    // to the .inp directory) and the embedded "…" button opens the file
    // dialog. The picker re-routes the browse outcome through its own
    // resolveAgainst-anchor logic; we no longer build a separate
    // QPushButton + QFileDialog branch here.
    //
    // The mode combo (USE/SAVE) drives the picker's acceptMode so SAVE
    // rows pick the "Save as" dialog flavour. Filters are coarse — most
    // legacy SWMM secondary refs are plain text/CSV; the hot-start USE
    // row gets its own *.hsf filter.
    const QString projectAnchor =
        (m_layer && !m_layer->modelFilePath().isEmpty())
            ? QFileInfo(m_layer->modelFilePath()).absolutePath()
            : QString();

    auto makePathRow = [secondary, secForm, this, projectAnchor](
                            const QString &label,
                            openswmmvis::ui::RelativePathPicker **edit,
                            QComboBox *modeCombo,
                            bool defaultSave,
                            const QString &dialogTitle,
                            const QString &filter) {
        auto *picker = new openswmmvis::ui::RelativePathPicker(secondary);
        picker->setProjectAnchor(projectAnchor);
        picker->setFileFilter(filter);
        picker->setDialogCaption(dialogTitle);
        picker->setAcceptMode(defaultSave ? QFileDialog::AcceptSave
                                          : QFileDialog::AcceptOpen);
        *edit = picker;

        auto *row = new QHBoxLayout();
        row->addWidget(picker, 1);
        if (modeCombo) {
            row->addWidget(new QLabel(QObject::tr("Mode:"), secondary));
            row->addWidget(modeCombo);

            // Mode change → pick the right file-dialog flavour on next browse.
            connect(modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                    picker, [picker, modeCombo, defaultSave] {
                bool save = defaultSave;
                const QString m = modeCombo->currentData().toString();
                if (!m.isEmpty())
                    save = (m.compare(QLatin1String("SAVE"),
                                       Qt::CaseInsensitive) == 0);
                picker->setAcceptMode(save ? QFileDialog::AcceptSave
                                            : QFileDialog::AcceptOpen);
            });
        }
        secForm->addRow(label, row);
    };

    const QString textFilter = tr("Text files (*.txt *.dat);;All Files (*)");
    const QString csvFilter  = tr("Text / CSV (*.txt *.dat *.csv);;All Files (*)");
    const QString hsfFilter  = tr("Hot-start files (*.hsf);;All Files (*)");

    m_rainfallModeCombo = makeModeCombo();
    makePathRow(tr("Rainfall:"), &m_rainfallPathEdit, m_rainfallModeCombo,
                false, tr("Choose Rainfall File"), textFilter);

    m_runoffModeCombo = makeModeCombo();
    makePathRow(tr("Runoff:"),   &m_runoffPathEdit,   m_runoffModeCombo,
                false, tr("Choose Runoff File"), textFilter);

    m_rdiiModeCombo = makeModeCombo();
    makePathRow(tr("RDII:"),     &m_rdiiPathEdit,     m_rdiiModeCombo,
                false, tr("Choose RDII File"), textFilter);

    makePathRow(tr("Inflows (USE only):"),   &m_inflowsPathEdit,  nullptr,
                false, tr("Choose Inflows File"),  csvFilter);
    makePathRow(tr("Outflows (SAVE only):"), &m_outflowsPathEdit, nullptr,
                true,  tr("Choose Outflows File"), csvFilter);
    makePathRow(tr("Hot-start file (USE):"), &m_hotstartUseEdit,  nullptr,
                false, tr("Choose Hot-Start File"), hsfFilter);

    vlay->addWidget(secondary);

    // ── Scheduled hot-start saves (Slice BV-01, 2026-05-21) ─────────────
    // Multi-row uncapped table replaces the legacy single SAVE field.
    // Backed by the new swmm_hotstart_saves_* engine C API.
    //
    // Phase 3.10.5 (2026-05-22): true MVC — a QTableView bound to a
    // HotstartSavesModel.  HotstartSavesPathDelegate renders each path
    // cell as a [QLineEdit][…] composite so the user can browse for the
    // save path inline; HotstartSavesDateTimeDelegate renders each
    // datetime cell as a QDateTimeEdit with the "(end of run)" sentinel.
    // Persistent editors keep both widgets visible without click-to-edit.
    auto *hsSaves = new QGroupBox(
        tr("Scheduled hot-start saves (.inp [FILES] SAVE HOTSTART)"), filesPage);
    auto *hsSavesLay = new QVBoxLayout(hsSaves);

    m_hotstartSavesModel   = new HotstartSavesModel(this);
    m_hotstartSavesPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::SaveFile,
        tr("Choose Hot-Start Save File"),
        tr("Hot-start files (*.hsf);;All Files (*)"),
        tr("path relative to the .inp directory"),
        this);
    // Slice IO-11c — feed the same project anchor into the delegate so
    // file picks under the .inp directory commit as relative tokens,
    // matching the [FILES] picker behaviour set up immediately above.
    m_hotstartSavesPathDel->setProjectAnchor(projectAnchor);
    m_hotstartSavesDtDel   = new HotstartSavesDateTimeDelegate(this);

    m_hotstartSavesView = new QTableView(hsSaves);
    m_hotstartSavesView->setModel(m_hotstartSavesModel);
    m_hotstartSavesView->setItemDelegateForColumn(
        HotstartSavesModel::ColPath, m_hotstartSavesPathDel);
    m_hotstartSavesView->setItemDelegateForColumn(
        HotstartSavesModel::ColDateTime, m_hotstartSavesDtDel);
    // Interactive resize: both columns are user-draggable.  Set sensible
    // defaults — path column takes the leftover space initially, datetime
    // is sized to fit the picker — but neither is locked.
    {
        auto *hdr = m_hotstartSavesView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(false);
        hdr->setSectionsMovable(false);
        hdr->setMinimumSectionSize(60);
        // Width hint: ~2/3 path, ~1/3 datetime — refined once the view is
        // shown (Qt will clamp later if the dialog is resized).
        hdr->resizeSection(HotstartSavesModel::ColPath,     360);
        hdr->resizeSection(HotstartSavesModel::ColDateTime, 200);
    }
    m_hotstartSavesView->verticalHeader()->setVisible(false);
    m_hotstartSavesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hotstartSavesView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hotstartSavesView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_hotstartSavesView->setToolTip(tr(
        "Each row schedules one hot-start save during the run.\n"
        "Leave Datetime empty (\"(end of run)\") to save at the end of the\n"
        "simulation; otherwise the engine writes the file when the sim\n"
        "clock crosses the chosen datetime."));
    hsSavesLay->addWidget(m_hotstartSavesView);

    auto openPersistentForRow = [this](int row) {
        if (!m_hotstartSavesView || !m_hotstartSavesModel) return;
        m_hotstartSavesView->openPersistentEditor(
            m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath));
        m_hotstartSavesView->openPersistentEditor(
            m_hotstartSavesModel->index(row, HotstartSavesModel::ColDateTime));
    };
    // Open persistent editors on every newly-inserted row so the path
    // line-edit + browse button and the QDateTimeEdit are visible by
    // default without click-to-edit.
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsInserted,
            this, [openPersistentForRow](const QModelIndex &, int first, int last) {
        for (int r = first; r <= last; ++r) openPersistentForRow(r);
    });
    connect(m_hotstartSavesModel, &QAbstractItemModel::modelReset, this,
            [this, openPersistentForRow] {
        if (!m_hotstartSavesModel) return;
        for (int r = 0; r < m_hotstartSavesModel->rowCount(); ++r)
            openPersistentForRow(r);
    });

    auto *hsBtnRow = new QHBoxLayout();
    m_hotstartSavesAddBtn    = new QPushButton(tr("Add…"),     hsSaves);
    m_hotstartSavesBrowseBtn = new QPushButton(tr("Browse…"),  hsSaves);
    m_hotstartSavesRemoveBtn = new QPushButton(tr("Remove"),   hsSaves);
    m_hotstartSavesUpBtn     = new QPushButton(tr("Move up"),  hsSaves);
    m_hotstartSavesDownBtn   = new QPushButton(tr("Move down"),hsSaves);
    m_hotstartSavesBrowseBtn->setToolTip(
        tr("Choose a save-as path for the selected row"));
    hsBtnRow->addWidget(m_hotstartSavesAddBtn);
    hsBtnRow->addWidget(m_hotstartSavesBrowseBtn);
    hsBtnRow->addWidget(m_hotstartSavesRemoveBtn);
    hsBtnRow->addWidget(m_hotstartSavesUpBtn);
    hsBtnRow->addWidget(m_hotstartSavesDownBtn);
    hsBtnRow->addStretch(1);
    hsSavesLay->addLayout(hsBtnRow);
    vlay->addWidget(hsSaves);

    auto updateHsButtons = [this] {
        const int row = (m_hotstartSavesView && m_hotstartSavesView->currentIndex().isValid())
                            ? m_hotstartSavesView->currentIndex().row() : -1;
        const int n   = m_hotstartSavesModel ? m_hotstartSavesModel->rowCount() : 0;
        const bool hasSel = row >= 0;
        if (m_hotstartSavesBrowseBtn) m_hotstartSavesBrowseBtn->setEnabled(hasSel);
        if (m_hotstartSavesRemoveBtn) m_hotstartSavesRemoveBtn->setEnabled(hasSel);
        if (m_hotstartSavesUpBtn)     m_hotstartSavesUpBtn->setEnabled(hasSel && row > 0);
        if (m_hotstartSavesDownBtn)   m_hotstartSavesDownBtn->setEnabled(hasSel && row < n - 1);
    };
    updateHsButtons();
    connect(m_hotstartSavesView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [updateHsButtons](const QModelIndex &, const QModelIndex &) {
        updateHsButtons();
    });
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsInserted,
            this, updateHsButtons);
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsRemoved,
            this, updateHsButtons);
    connect(m_hotstartSavesModel, &QAbstractItemModel::modelReset,
            this, updateHsButtons);

    connect(m_hotstartSavesAddBtn,    &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveAddRow);
    connect(m_hotstartSavesBrowseBtn, &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveBrowseRow);
    connect(m_hotstartSavesRemoveBtn, &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveRemoveRow);
    connect(m_hotstartSavesUpBtn,     &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveMoveRowUp);
    connect(m_hotstartSavesDownBtn,   &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveMoveRowDown);

    vlay->addStretch(1);
    subTabs->addTab(filesPage, tr("Files"));

    // =====================================================================
    // Sub-tab "Output" — writer combos + [REPORT] flags + .rpt / .out paths
    // (Phase 3.10.2, 2026-05-22)
    // =====================================================================
    auto *outputPage = new QWidget(subTabs);
    auto *outVlay = new QVBoxLayout(outputPage);

    // ── Writer / Container group ────────────────────────────────────────
    // Three combos let the user pick the plugin driving each role.  The
    // combo's hidden `data()` is the plugin id (empty string for the
    // built-in `.inp` / `.out` / `.rpt` writer).  Picking a non-default
    // entry adds the corresponding [PLUGINS] row on Apply.
    m_writersGroup = new QGroupBox(tr("Writer / Container"), outputPage);
    auto *writerGroup = m_writersGroup;
    auto *writerForm  = new QFormLayout(writerGroup);

    auto populateWriterCombo = [](QComboBox *combo, openswmmvis::FilterKind k) {
        // First entry: built-in (empty plugin id = engine default).
        const char *defaultLabel =
            (k == openswmmvis::FilterKind::InputRead)
                ? "(built-in: .inp writer)"
                : (k == openswmmvis::FilterKind::ResultsWrite)
                      ? "(built-in: .out writer)"
                      : "(built-in: .rpt writer)";
        combo->addItem(QObject::tr(defaultLabel), QString());
        auto *registry = openswmmvis::FileFilterRegistry::instance();
        QStringList seen;  // dedupe by pluginId
        // Phase 3.10.6 (2026-05-22): the engine SDK has no INPUT_WRITE
        // role — plugins that handle the model input file advertise
        // INPUT_READ only, and the GUI registry sets `canWrite=false`
        // on those entries.  The previous gate `if (!canWrite) continue;`
        // therefore silently dropped every engine-provided input plugin
        // from the Input writer combo.  All three combos now show every
        // plugin id advertising the matching role, deduped.
        for (const auto &entry : registry->entriesFor(k)) {
            if (entry.pluginId.isEmpty()) continue;
            if (seen.contains(entry.pluginId)) continue;
            seen << entry.pluginId;
            const QString label = entry.description.isEmpty()
                ? entry.pluginId
                : QStringLiteral("%1 (%2)").arg(entry.description, entry.pluginId);
            combo->addItem(label, entry.pluginId);
        }
    };

    m_inputWriterCombo  = new QComboBox(writerGroup);
    populateWriterCombo(m_inputWriterCombo, openswmmvis::FilterKind::InputRead);
    writerForm->addRow(tr("Input writer:"),  m_inputWriterCombo);

    m_outputWriterCombo = new QComboBox(writerGroup);
    populateWriterCombo(m_outputWriterCombo, openswmmvis::FilterKind::ResultsWrite);
    writerForm->addRow(tr("Output writer:"), m_outputWriterCombo);

    m_reportWriterCombo = new QComboBox(writerGroup);
    populateWriterCombo(m_reportWriterCombo, openswmmvis::FilterKind::ReportWrite);
    writerForm->addRow(tr("Report writer:"), m_reportWriterCombo);

    m_singleContainerBox = new QCheckBox(
        tr("Single container (write input, output, and report to one file)"),
        writerGroup);
    m_singleContainerBox->setToolTip(
        tr("Enabled when the chosen Input writer plugin advertises input, "
           "output, and report roles for the same extension (e.g., GeoPackage). "
           "When checked, the Output and Report writer combos lock to the "
           "Input writer's plugin id."));
    m_singleContainerBox->setEnabled(false);
    writerForm->addRow(QString(), m_singleContainerBox);

    connect(m_inputWriterCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSingleContainerEnabled(); });
    connect(m_singleContainerBox, &QCheckBox::toggled,
            this, &SimulationOptionsDialog::onSingleContainerToggled);

    outVlay->addWidget(writerGroup);

    // ── Report contents ([REPORT] section — Slice BV.1, 2026-05-22) ───
    buildReportContentsGroup(outVlay, outputPage);

    // ── Report file path (Slice AA-4) ────────────────────────────────────
    auto *rptGroup = new QGroupBox(tr("Report file"), outputPage);
    auto *rptForm  = new QFormLayout(rptGroup);

    auto *rptPathRow = new QHBoxLayout();
    m_reportFilePathEdit = new QLineEdit(rptGroup);
    m_reportFilePathEdit->setPlaceholderText(tr("(auto — sibling of input file with .rpt extension)"));
    m_reportFilePathEdit->setToolTip(tr(
        "Override path for the simulation report file. Leave blank to "
        "derive the path automatically from the input file location. "
        "The format is determined by the Report writer combo above."));
    rptPathRow->addWidget(m_reportFilePathEdit, 1);
    auto *rptBrowse = new QPushButton(tr("Browse…"), rptGroup);
    connect(rptBrowse, &QPushButton::clicked,
            this, &SimulationOptionsDialog::browseForReportFile);
    rptPathRow->addWidget(rptBrowse);
    rptForm->addRow(tr("Report file path:"), rptPathRow);
    outVlay->addWidget(rptGroup);

    // ── Output (results) file path (Slice AA-4) ──────────────────────────
    auto *outGroup = new QGroupBox(tr("Results output file"), outputPage);
    auto *outForm  = new QFormLayout(outGroup);

    auto *outPathRow = new QHBoxLayout();
    m_outputFilePathEdit = new QLineEdit(outGroup);
    m_outputFilePathEdit->setPlaceholderText(tr("(auto — sibling of input file with .out extension)"));
    m_outputFilePathEdit->setToolTip(tr(
        "Override path for the binary results output file. Leave blank to "
        "derive the path automatically from the input file location. "
        "The format is determined by the Output writer combo above."));
    outPathRow->addWidget(m_outputFilePathEdit, 1);
    auto *outBrowse = new QPushButton(tr("Browse…"), outGroup);
    connect(outBrowse, &QPushButton::clicked,
            this, &SimulationOptionsDialog::browseForOutputFile);
    outPathRow->addWidget(outBrowse);
    outForm->addRow(tr("Output file path:"), outPathRow);
    outVlay->addWidget(outGroup);

    outVlay->addStretch(1);
    subTabs->addTab(outputPage, tr("O&utput"));

    // =====================================================================
    // Sub-tab "Plugins" — [PLUGINS] table (Phase 3.10.3, 2026-05-22)
    // =====================================================================
    auto *pluginsPage = new QWidget(subTabs);
    auto *plVlay = new QVBoxLayout(pluginsPage);

    auto *intro = new QLabel(
        tr("Plugins listed in the model's <b>[PLUGINS]</b> section.  Each row "
           "names a writer / output / report plugin (by id, <i>id:version</i>, "
           "or shared-library path) and any free-form arguments to pass to "
           "its initialize() call.  The first input-capable row is also used "
           "by File → Save As when picking a non-<code>.inp</code> "
           "extension."),
        pluginsPage);
    intro->setWordWrap(true);
    plVlay->addWidget(intro);

    // Phase 3.10.6 (2026-05-22) — MVC: model + QTableView with the
    // reusable PathBrowseDelegate on column 0 so each row's plugin
    // path field carries an inline "…" browse button targeting the
    // platform's shared-library extensions.  Column 1 (arguments)
    // uses the default QLineEdit delegate (free-form text).
    m_pluginsModel   = new PluginsTableModel(this);
    m_pluginsPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::OpenFile,
        tr("Choose Plugin Library"),
#if defined(Q_OS_WIN)
        tr("Plugin libraries (*.dll);;All Files (*)"),
#elif defined(Q_OS_MACOS)
        tr("Plugin libraries (*.dylib *.so *.bundle);;All Files (*)"),
#else
        tr("Plugin libraries (*.so);;All Files (*)"),
#endif
        tr("plugin id, id:version, or library path"),
        this);

    m_pluginsView = new QTableView(pluginsPage);
    m_pluginsView->setModel(m_pluginsModel);
    m_pluginsView->setItemDelegateForColumn(
        PluginsTableModel::ColPath, m_pluginsPathDel);
    m_pluginsView->verticalHeader()->setVisible(false);
    m_pluginsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pluginsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pluginsView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    {
        auto *hdr = m_pluginsView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(true);
        hdr->setMinimumSectionSize(60);
        hdr->resizeSection(PluginsTableModel::ColPath, 360);
        hdr->resizeSection(PluginsTableModel::ColArgs, 220);
    }
    plVlay->addWidget(m_pluginsView, 1);

    // Open the path-cell persistent editor for every row so the browse
    // "…" button is visible without click-to-edit.  Args column keeps
    // the default click-to-edit behaviour.
    auto openPersistentPluginPath = [this](int row) {
        if (!m_pluginsView || !m_pluginsModel) return;
        m_pluginsView->openPersistentEditor(
            m_pluginsModel->index(row, PluginsTableModel::ColPath));
    };
    connect(m_pluginsModel, &QAbstractItemModel::rowsInserted, this,
            [openPersistentPluginPath](const QModelIndex &, int first, int last) {
        for (int r = first; r <= last; ++r) openPersistentPluginPath(r);
    });
    connect(m_pluginsModel, &QAbstractItemModel::modelReset, this,
            [this, openPersistentPluginPath] {
        if (!m_pluginsModel) return;
        for (int r = 0; r < m_pluginsModel->rowCount(); ++r)
            openPersistentPluginPath(r);
    });

    auto *btnRow = new QHBoxLayout();
    m_pluginsAddBtn    = new QPushButton(tr("Add"), pluginsPage);
    m_pluginsRemoveBtn = new QPushButton(tr("Remove"), pluginsPage);
    m_pluginsRemoveBtn->setEnabled(false);
    btnRow->addWidget(m_pluginsAddBtn);
    btnRow->addWidget(m_pluginsRemoveBtn);
    btnRow->addStretch();
    plVlay->addLayout(btnRow);

    connect(m_pluginsAddBtn, &QPushButton::clicked, this, [this] {
        if (!m_pluginsModel || !m_pluginsView) return;
        const int row = m_pluginsModel->appendRow(QString(), QString());
        m_pluginsView->setCurrentIndex(
            m_pluginsModel->index(row, PluginsTableModel::ColPath));
    });
    connect(m_pluginsRemoveBtn, &QPushButton::clicked, this, [this] {
        if (!m_pluginsModel || !m_pluginsView) return;
        const QModelIndex cur = m_pluginsView->currentIndex();
        if (cur.isValid()) m_pluginsModel->removeRows(cur.row(), 1);
    });
    // Slice RC.4 — Plugins tab honours `is_builtin` from the engine's
    // plugin discovery API. Built-ins (today: Default Input / Output /
    // Report / StateIO + GeoPackage when OPENSWMM_HAS_GEOPACKAGE) are
    // statically linked into the engine, so a user attempting to
    // "Remove" the row from the .inp's [PLUGINS] section cannot
    // actually unload them — the singleton stays alive in-process.
    // Greying the Remove button when the selected row's path matches a
    // built-in plugin_id makes that constraint visible.
    auto builtInPluginIds = [] {
        QSet<QString> ids;
        for (const auto &p : openswmm::discover_plugins_by_id()) {
            if (p.is_builtin)
                ids.insert(QString::fromStdString(p.plugin_id));
        }
        return ids;
    }();
    connect(m_pluginsView->selectionModel(),
            &QItemSelectionModel::currentChanged, this,
            [this, builtInPluginIds](const QModelIndex &cur, const QModelIndex &) {
        if (!cur.isValid()) {
            m_pluginsRemoveBtn->setEnabled(false);
            m_pluginsRemoveBtn->setToolTip(QString());
            return;
        }
        const QString rowPath = m_pluginsModel
            ? m_pluginsModel->pathAt(cur.row()).trimmed()
            : QString();
        const bool isBuiltIn = builtInPluginIds.contains(rowPath);
        m_pluginsRemoveBtn->setEnabled(!isBuiltIn);
        m_pluginsRemoveBtn->setToolTip(
            isBuiltIn
              ? tr("\"%1\" is a built-in plugin statically linked into "
                   "the engine — removing the row would have no effect "
                   "(the plugin stays loaded in-process).").arg(rowPath)
              : QString());
    });

    // U1 (2026-09-07) — [PROCESS_COMPONENTS] table. Same MVC shape as the
    // plugins table above: model over the engine registrations, combo
    // delegate for the id (engine's built-in catalogue, free text allowed),
    // PathBrowseDelegate for the config file. Reaction editor auto-registers
    // its component straight into the engine; this table re-reads on open.
    auto *compGroup = new QGroupBox(tr("Process components ([PROCESS_COMPONENTS])"),
                                    pluginsPage);
    auto *compLay = new QVBoxLayout(compGroup);
    auto *compIntro = new QLabel(
        tr("Transport / reaction components bound to this model and the "
           "config file each reads (relative to the model file). The "
           "reactions component is registered automatically by the Reaction "
           "System editor's first save; other ids are listed from the "
           "engine's catalogue."),
        compGroup);
    compIntro->setWordWrap(true);
    compLay->addWidget(compIntro);

    m_componentsModel   = new ProcessComponentsModel(this);
    m_componentsIdDel   = new ProcessComponentIdDelegate(this);
    m_componentsPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::OpenFile, tr("Choose Component Config File"),
        tr("Component config (*.rxn *.ard *.lard *.heat *.age *.i2d *.txt);;All Files (*)"),
        tr("config file, relative to the model"), this);
    m_componentsView = new QTableView(compGroup);
    m_componentsView->setObjectName(QStringLiteral("processComponentsView"));
    m_componentsView->setModel(m_componentsModel);
    m_componentsView->setItemDelegateForColumn(ProcessComponentsModel::ColId,
                                               m_componentsIdDel);
    m_componentsView->setItemDelegateForColumn(ProcessComponentsModel::ColConfig,
                                               m_componentsPathDel);
    m_componentsView->verticalHeader()->setVisible(false);
    m_componentsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_componentsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_componentsView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    {
        auto *hdr = m_componentsView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(true);
        hdr->resizeSection(ProcessComponentsModel::ColId, 300);
        hdr->resizeSection(ProcessComponentsModel::ColConfig, 240);
    }
    compLay->addWidget(m_componentsView, 1);

    auto *compBtnRow = new QHBoxLayout();
    m_componentsAddBtn    = new QPushButton(tr("Add"), compGroup);
    m_componentsAddBtn->setObjectName(QStringLiteral("processComponentsAddBtn"));
    m_componentsRemoveBtn = new QPushButton(tr("Remove"), compGroup);
    m_componentsRemoveBtn->setObjectName(QStringLiteral("processComponentsRemoveBtn"));
    m_componentsRemoveBtn->setEnabled(false);
    compBtnRow->addWidget(m_componentsAddBtn);
    compBtnRow->addWidget(m_componentsRemoveBtn);
    compBtnRow->addStretch();
    compLay->addLayout(compBtnRow);
    connect(m_componentsAddBtn, &QPushButton::clicked, this, [this] {
        const int row = m_componentsModel->appendRow(QString(), QString());
        const QModelIndex idx = m_componentsModel->index(row, ProcessComponentsModel::ColId);
        m_componentsView->setCurrentIndex(idx);
        m_componentsView->edit(idx);
    });
    connect(m_componentsRemoveBtn, &QPushButton::clicked, this, [this] {
        const QModelIndex cur = m_componentsView->currentIndex();
        if (cur.isValid()) m_componentsModel->removeRows(cur.row(), 1);
    });
    connect(m_componentsView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
                m_componentsRemoveBtn->setEnabled(cur.isValid());
            });
    plVlay->addWidget(compGroup);

    subTabs->addTab(pluginsPage, tr("Plu&gins"));

    return page;
}

void SimulationOptionsDialog::readProcessComponentsFromEngine()
{
    if (!m_componentsModel) return;
    const QString dir = m_layer ? QFileInfo(m_layer->modelFilePath()).absolutePath()
                                : QString();
    m_componentsModel->load(m_engine, dir);
}

int SimulationOptionsDialog::writeProcessComponentsToEngine()
{
    if (!m_componentsModel || !m_engine) return 0;
    return m_componentsModel->commit(m_engine);
}

void SimulationOptionsDialog::readPluginsFromEngine()
{
    if (!m_pluginsModel) return;
    m_pluginsModel->clearRows();
    if (!m_engine) return;

    int count = 0;
    if (swmm_plugins_count(m_engine, &count) != 0) return;

    char path_buf[1024];
    char args_buf[2048];
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(m_engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        m_pluginsModel->appendRow(QString::fromUtf8(path_buf),
                                  QString::fromUtf8(args_buf));
    }
}

// ---------------------------------------------------------------------------
// Writer / Container combos (Slice AA-3.5 full design)
//
// The combos drive a derived view onto the [PLUGINS] section: the user
// picks which plugin handles each role (input writer, results output,
// report).  Apply collects the selected plugin ids and ensures each
// non-empty id has a matching [PLUGINS] row — without disturbing any
// rows the user may have added manually via the table below.
// ---------------------------------------------------------------------------

namespace {

// Find the first plugin id in the current [PLUGINS] section that advertises
// @p role per the engine's grouped discovery.  Returns empty string when
// no such plugin is loaded (caller treats empty as "built-in default").
QString findActivePluginForRole(SWMM_Engine engine, openswmm::PluginRole role)
{
    if (!engine) return {};
    int count = 0;
    if (swmm_plugins_count(engine, &count) != 0 || count == 0) return {};

    // Build the role index once: plugin_id → roles-bitset.
    auto plugins = openswmm::discover_plugins_by_id();

    char path_buf[1024];
    char args_buf[512];  // ignored
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        const QString rowId = QString::fromUtf8(path_buf);
        for (const auto &p : plugins) {
            if (QString::fromStdString(p.plugin_id) != rowId) continue;
            for (auto r : p.roles) {
                if (r == role) return rowId;
            }
        }
    }
    return {};
}

// True when the plugin advertises all three writer roles (INPUT_READ,
// OUTPUT_WRITE, REPORT_WRITE) — the "Single container" precondition.
bool isTriRolePlugin(const QString &pluginId)
{
    if (pluginId.isEmpty()) return false;
    for (const auto &p : openswmm::discover_plugins_by_id()) {
        if (QString::fromStdString(p.plugin_id) != pluginId) continue;
        bool hasIn = false, hasOut = false, hasRpt = false;
        for (auto r : p.roles) {
            if      (r == openswmm::PluginRole::INPUT_READ)   hasIn  = true;
            else if (r == openswmm::PluginRole::OUTPUT_WRITE) hasOut = true;
            else if (r == openswmm::PluginRole::REPORT_WRITE) hasRpt = true;
        }
        return hasIn && hasOut && hasRpt;
    }
    return false;
}

void selectComboByPluginId(QComboBox *c, const QString &pluginId)
{
    if (!c) return;
    const int idx = c->findData(pluginId);
    c->setCurrentIndex(idx >= 0 ? idx : 0);
}

} // anonymous

void SimulationOptionsDialog::readWriterCombosFromEngine()
{
    selectComboByPluginId(m_inputWriterCombo,
        findActivePluginForRole(m_engine, openswmm::PluginRole::INPUT_READ));
    selectComboByPluginId(m_outputWriterCombo,
        findActivePluginForRole(m_engine, openswmm::PluginRole::OUTPUT_WRITE));
    selectComboByPluginId(m_reportWriterCombo,
        findActivePluginForRole(m_engine, openswmm::PluginRole::REPORT_WRITE));

    updateSingleContainerEnabled();
    if (m_singleContainerBox && m_singleContainerBox->isEnabled()) {
        // Auto-check when all three combos already resolve to the same id
        // — the simulation was previously set up as a single-container.
        const QString in  = m_inputWriterCombo  ? m_inputWriterCombo ->currentData().toString() : QString();
        const QString out = m_outputWriterCombo ? m_outputWriterCombo->currentData().toString() : QString();
        const QString rpt = m_reportWriterCombo ? m_reportWriterCombo->currentData().toString() : QString();
        if (!in.isEmpty() && in == out && in == rpt) {
            QSignalBlocker block(m_singleContainerBox);
            m_singleContainerBox->setChecked(true);
            // Also disable the locked combos so the UI is consistent.
            if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(false);
            if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(false);
        }
    }
}

int SimulationOptionsDialog::writeWriterCombosToEngine()
{
    if (!m_engine) return 0;
    QStringList wanted;
    auto addWanted = [&](QComboBox *c) {
        if (!c) return;
        const QString id = c->currentData().toString();
        if (!id.isEmpty() && !wanted.contains(id)) wanted << id;
    };
    addWanted(m_inputWriterCombo);
    addWanted(m_outputWriterCombo);
    addWanted(m_reportWriterCombo);

    // Collect existing [PLUGINS] row keys to check before inserting.  We
    // never auto-remove rows — the user manages those via the table so
    // any args they added stay intact.
    QSet<QString> existing;
    int count = 0;
    swmm_plugins_count(m_engine, &count);
    char path_buf[1024];
    char args_buf[512];
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(m_engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        existing.insert(QString::fromUtf8(path_buf));
    }

    int added = 0;
    for (const QString &id : wanted) {
        if (existing.contains(id)) continue;
        const QByteArray utf = id.toUtf8();
        if (swmm_plugin_set(m_engine, utf.constData(), nullptr) == SWMM_OK)
            ++added;
    }
    return added;
}

// ---------------------------------------------------------------------------
// Output / Report file path helpers (Slice AA-4)
// Paths are per-project, stored in QSettings keyed by model file path.
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::readOutputPathsFromSettings()
{
    if (!m_layer || !m_reportFilePathEdit || !m_outputFilePathEdit) return;
    QSettings s;
    const QString base = QStringLiteral("SWMMVis/Project/%1/")
                             .arg(m_layer->modelFilePath());
    m_reportFilePathEdit->setText(s.value(base + QStringLiteral("ReportFilePath")).toString());
    m_outputFilePathEdit->setText(s.value(base + QStringLiteral("OutputFilePath")).toString());
}

void SimulationOptionsDialog::writeOutputPathsToSettings()
{
    if (!m_layer || !m_reportFilePathEdit || !m_outputFilePathEdit) return;
    QSettings s;
    const QString base = QStringLiteral("SWMMVis/Project/%1/")
                             .arg(m_layer->modelFilePath());
    s.setValue(base + QStringLiteral("ReportFilePath"),
               m_reportFilePathEdit->text().trimmed());
    s.setValue(base + QStringLiteral("OutputFilePath"),
               m_outputFilePathEdit->text().trimmed());
}

void SimulationOptionsDialog::browseForReportFile()
{
    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;
    auto *reg = FileFilterRegistry::instance();
    const QString filter = reg->filterFor(FilterKind::ReportWrite);
    const QString current = m_reportFilePathEdit ? m_reportFilePathEdit->text().trimmed() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Report File"),
        current.isEmpty() ? QString() : current,
        filter.isEmpty() ? tr("All Files (*)") : filter);
    if (!path.isEmpty() && m_reportFilePathEdit)
        m_reportFilePathEdit->setText(path);
}

void SimulationOptionsDialog::browseForOutputFile()
{
    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;
    auto *reg = FileFilterRegistry::instance();
    const QString filter = reg->filterFor(FilterKind::ResultsWrite);
    const QString current = m_outputFilePathEdit ? m_outputFilePathEdit->text().trimmed() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Output File"),
        current.isEmpty() ? QString() : current,
        filter.isEmpty() ? tr("All Files (*)") : filter);
    if (!path.isEmpty() && m_outputFilePathEdit)
        m_outputFilePathEdit->setText(path);
}

// ---------------------------------------------------------------------------
// Multi-row SAVE HOTSTART slots (Slice BV-01, 2026-05-21).
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::onHotstartSaveAddRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const int row = m_hotstartSavesModel->appendRow(QString{}, 0.0);
    m_hotstartSavesView->setCurrentIndex(
        m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath));
    onHotstartSaveBrowseRow();   // prompt for the save-as path immediately
}

void SimulationOptionsDialog::onHotstartSaveBrowseRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0 || row >= m_hotstartSavesModel->rowCount()) return;

    const QString current = m_hotstartSavesModel->pathAt(row).trimmed();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Hot-Start Save File"),
        current.isEmpty() ? QString() : current,
        tr("Hot-start files (*.hsf);;All Files (*)"));
    if (path.isEmpty()) return;
    m_hotstartSavesModel->setData(
        m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath),
        path, Qt::EditRole);
}

void SimulationOptionsDialog::onHotstartSaveRemoveRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0) return;
    m_hotstartSavesModel->removeRows(row, 1);
}

void SimulationOptionsDialog::onHotstartSaveMoveRowUp()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row <= 0) return;
    moveHotstartSaveRow(row, row - 1);
}

void SimulationOptionsDialog::onHotstartSaveMoveRowDown()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0 || row >= m_hotstartSavesModel->rowCount() - 1) return;
    moveHotstartSaveRow(row, row + 1);
}

void SimulationOptionsDialog::moveHotstartSaveRow(int from, int to)
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    if (!m_hotstartSavesModel->swapRows(from, to)) return;
    m_hotstartSavesView->setCurrentIndex(
        m_hotstartSavesModel->index(to, HotstartSavesModel::ColPath));
}

void SimulationOptionsDialog::updateSingleContainerEnabled()
{
    if (!m_singleContainerBox || !m_inputWriterCombo) return;
    const QString id = m_inputWriterCombo->currentData().toString();
    const bool eligible = isTriRolePlugin(id);
    m_singleContainerBox->setEnabled(eligible);
    if (!eligible && m_singleContainerBox->isChecked()) {
        QSignalBlocker b(m_singleContainerBox);
        m_singleContainerBox->setChecked(false);
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(true);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(true);
    }
}

void SimulationOptionsDialog::onSingleContainerToggled(bool on)
{
    if (!m_inputWriterCombo) return;
    const QString id = m_inputWriterCombo->currentData().toString();

    if (on) {
        selectComboByPluginId(m_outputWriterCombo, id);
        selectComboByPluginId(m_reportWriterCombo, id);
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(false);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(false);
    } else {
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(true);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(true);
    }
}

void SimulationOptionsDialog::readFilesSectionFromEngine()
{
    if (!m_engine) return;
    char buf[1024];

    auto getStr = [&](const char *key) -> QString {
        buf[0] = '\0';
        if (swmm_files_get(m_engine, key, buf, sizeof(buf)) != SWMM_OK) return {};
        return QString::fromUtf8(buf);
    };
    auto setMode = [&](QComboBox *c, const QString &mode) {
        if (!c) return;
        const int idx = c->findData(mode, Qt::UserRole, Qt::MatchFixedString);
        c->setCurrentIndex(idx >= 0 ? idx : 0);
    };

    if (m_rainfallPathEdit) m_rainfallPathEdit->setPath(getStr("RAINFALL_PATH"));
    setMode(m_rainfallModeCombo, getStr("RAINFALL_MODE"));
    if (m_runoffPathEdit)   m_runoffPathEdit->setPath(getStr("RUNOFF_PATH"));
    setMode(m_runoffModeCombo,   getStr("RUNOFF_MODE"));
    if (m_rdiiPathEdit)     m_rdiiPathEdit->setPath(getStr("RDII_PATH"));
    setMode(m_rdiiModeCombo,     getStr("RDII_MODE"));
    if (m_inflowsPathEdit)  m_inflowsPathEdit->setPath(getStr("INFLOWS_PATH"));
    if (m_outflowsPathEdit) m_outflowsPathEdit->setPath(getStr("OUTFLOWS_PATH"));
    if (m_hotstartUseEdit)  m_hotstartUseEdit->setPath(getStr("HOTSTART_USE_PATH"));

    // Multi-row SAVE HOTSTART table (Slice BV-01).  Phase 3.10.5 — pull
    // each entry from the engine's hotstart_saves vector into the
    // HotstartSavesModel.  Persistent editors get re-opened by the
    // rowsInserted hook installed in buildFilesTab().
    if (m_hotstartSavesModel) {
        m_hotstartSavesModel->clearRows();
        int count = 0;
        if (swmm_hotstart_saves_count(m_engine, &count) != SWMM_OK) count = 0;
        for (int i = 0; i < count; ++i) {
            char pbuf[1024] = {0};
            double dt = 0.0;
            swmm_hotstart_saves_get_path(m_engine, i, pbuf, sizeof(pbuf));
            swmm_hotstart_saves_get_datetime(m_engine, i, &dt);
            m_hotstartSavesModel->appendRow(QString::fromUtf8(pbuf),
                                            dt > 0.0 ? dt : 0.0);
        }
    }
}

int SimulationOptionsDialog::writeFilesSectionToEngine()
{
    if (!m_engine) return 0;
    int written = 0;
    char buf[1024];

    auto getCurrent = [&](const char *key) -> QString {
        buf[0] = '\0';
        swmm_files_get(m_engine, key, buf, sizeof(buf));
        return QString::fromUtf8(buf);
    };

    // Convert absolute paths to paths relative to the .inp directory
    // so the project folder stays portable.  Relative paths pass
    // through unchanged — the engine resolves them against the .inp
    // directory at run time (legacy SWMM5 behaviour).
    const QString inpPath = m_layer ? m_layer->modelFilePath() : QString();
    auto toRelative = [&](const QString &raw) -> QString {
        if (raw.isEmpty() || inpPath.isEmpty()) return raw;
        if (!QDir::isAbsolutePath(raw)) return raw;
        return ProjectSerializer::toRelativePath(raw, inpPath);
    };

    auto writeIfChanged = [&](const char *key, const QString &newVal) {
        if (getCurrent(key) == newVal) return;
        const QByteArray utf = newVal.toUtf8();
        if (swmm_files_set(m_engine, key, utf.constData()) == SWMM_OK)
            ++written;
    };
    auto writePathIfChanged = [&](const char *key, const QString &rawText) {
        writeIfChanged(key, toRelative(rawText.trimmed()));
    };

    if (m_rainfallPathEdit)
        writePathIfChanged("RAINFALL_PATH", m_rainfallPathEdit->absolutePath());
    if (m_rainfallModeCombo)
        writeIfChanged("RAINFALL_MODE",
                       m_rainfallModeCombo->currentData().toString());
    if (m_runoffPathEdit)
        writePathIfChanged("RUNOFF_PATH",   m_runoffPathEdit->absolutePath());
    if (m_runoffModeCombo)
        writeIfChanged("RUNOFF_MODE",
                       m_runoffModeCombo->currentData().toString());
    if (m_rdiiPathEdit)
        writePathIfChanged("RDII_PATH",     m_rdiiPathEdit->absolutePath());
    if (m_rdiiModeCombo)
        writeIfChanged("RDII_MODE",
                       m_rdiiModeCombo->currentData().toString());
    if (m_inflowsPathEdit)
        writePathIfChanged("INFLOWS_PATH",  m_inflowsPathEdit->absolutePath());
    if (m_outflowsPathEdit)
        writePathIfChanged("OUTFLOWS_PATH", m_outflowsPathEdit->absolutePath());
    if (m_hotstartUseEdit)
        writePathIfChanged("HOTSTART_USE_PATH",  m_hotstartUseEdit->absolutePath());

    // Multi-row SAVE HOTSTART (Slice BV-01).  The vector API is simpler
    // to drive than per-slot diffing: snapshot the engine's current
    // entries, compare against the model contents (path + datetime),
    // and only rebuild the vector when something actually changed.
    if (m_hotstartSavesModel) {
        struct HsRow { QString path; double dt; };
        QList<HsRow> desired;
        const int nRows = m_hotstartSavesModel->rowCount();
        for (int row = 0; row < nRows; ++row) {
            const QString rawPath = m_hotstartSavesModel->pathAt(row).trimmed();
            const QString relPath = toRelative(rawPath);
            const double dt       = m_hotstartSavesModel->oaDateAt(row);

            // Skip empty rows entirely — an empty path with no datetime
            // is a stub the user never filled in.
            if (relPath.isEmpty() && dt == 0.0) continue;
            desired.push_back({relPath, dt});
        }

        // Compare against current engine state.
        int curCount = 0;
        swmm_hotstart_saves_count(m_engine, &curCount);
        bool changed = (curCount != desired.size());
        for (int i = 0; !changed && i < curCount; ++i) {
            char pbuf[1024] = {0};
            double curDt = 0.0;
            swmm_hotstart_saves_get_path(m_engine, i, pbuf, sizeof(pbuf));
            swmm_hotstart_saves_get_datetime(m_engine, i, &curDt);
            if (QString::fromUtf8(pbuf) != desired[i].path) changed = true;
            else if (curDt != desired[i].dt)                changed = true;
        }
        if (changed) {
            swmm_hotstart_saves_clear(m_engine);
            for (const auto &r : desired) {
                const QByteArray utf = r.path.toUtf8();
                swmm_hotstart_saves_add(m_engine, utf.constData(), r.dt);
            }
            ++written;
        }
    }
    return written;
}

int SimulationOptionsDialog::writePluginsToEngine()
{
    if (!m_pluginsModel || !m_engine) return 0;

    // Snapshot existing engine rows by key so we can compute the
    // additions, replacements, and removals that the table represents.
    int existingCount = 0;
    swmm_plugins_count(m_engine, &existingCount);

    QHash<QString, QString> existing;
    char path_buf[1024];
    char args_buf[2048];
    for (int i = 0; i < existingCount; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(m_engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        existing.insert(QString::fromUtf8(path_buf),
                        QString::fromUtf8(args_buf));
    }

    int written = 0;
    QSet<QString> seen;
    const int nRows = m_pluginsModel->rowCount();
    for (int row = 0; row < nRows; ++row) {
        const QString key  = m_pluginsModel->pathAt(row).trimmed();
        const QString args = m_pluginsModel->argsAt(row).trimmed();
        if (key.isEmpty()) continue;
        seen.insert(key);

        // Only call set when the row is new or its args changed —
        // avoids spurious dirty-marker bumps in the engine.
        const auto it = existing.constFind(key);
        if (it == existing.constEnd() || it.value() != args) {
            const QByteArray k = key.toUtf8();
            const QByteArray a = args.toUtf8();
            if (swmm_plugin_set(m_engine, k.constData(),
                                args.isEmpty() ? nullptr : a.constData()) == 0)
                ++written;
        }
    }

    // Remove engine rows whose keys are no longer in the table.
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        if (!seen.contains(it.key())) {
            const QByteArray k = it.key().toUtf8();
            if (swmm_plugin_remove(m_engine, k.constData()) == 0)
                ++written;
        }
    }
    return written;
}

int SimulationOptionsDialog::writeToEngine()
{
    int n = 0;
    // One write pass: the key record starts empty and collects every key this
    // pass considered, so lastWriteKeys() can be compared against the tagged
    // editors (reachability seam).
    m_lastWriteKeys.clear();
    auto writeIfChanged = [&](const char *key, const QString &current,
                              const QString &newVal) {
        m_lastWriteKeys << QString::fromLatin1(key);
        // Numeric-aware compare: the engine renders numerics with six
        // decimals ("0.000000") while this dialog formats 'f',2/'f',3/etc.,
        // so a plain string compare would rewrite every key (and dirty the
        // project) on each OK even with no edits.
        if (optionValueEquals(current, newVal)) return;
        if (setOption(key, newVal)) ++n;
    };

    // Tab 0 — Title / Notes
    if (m_titleNotesEdit) {
        const QString currentHtml = m_titleNotesEdit->toHtml();
        if (currentHtml != m_initialNotesHtml) {
            if (m_engine) {
                const QString plain = m_titleNotesEdit->toPlainText();
                if (swmm_title_clear(m_engine) == 0) {
                    const QByteArray utf8 = plain.toUtf8();
                    if (swmm_title_set(m_engine, utf8.constData()) == 0)
                        ++n;
                }
            }
            if (m_projectWindow) {
                const QString plain = m_titleNotesEdit->toPlainText();
                // Drop the HTML if the document only carries plain text — keeps
                // .oswp tidy and matches the "no notes" empty case.
                m_projectWindow->setNotesHtml(plain.isEmpty() ? QString()
                                                              : currentHtml);
            }
            m_initialNotesHtml = currentHtml;
        }
    }

    // Tab 1
    writeIfChanged("INFILTRATION",       getOption("INFILTRATION"),
                   m_infiltrationCombo->currentData().toString());
    writeIfChanged("FLOW_ROUTING",       getOption("FLOW_ROUTING"),
                   m_routingCombo->currentData().toString());
    writeIfChanged("ALLOW_PONDING",      getOption("ALLOW_PONDING"),
                   engineBoolString(m_allowPondingBox->isChecked()));
    writeIfChanged("SKIP_STEADY_STATE",  getOption("SKIP_STEADY_STATE"),
                   engineBoolString(m_skipSteadyBox->isChecked()));
    // Inverted UI: checked = active = IGNORE_X NO. Unchecked = ignore.
    writeIfChanged("IGNORE_RAINFALL",    getOption("IGNORE_RAINFALL"),
                   engineBoolString(!m_ignoreRainfallBox->isChecked()));
    writeIfChanged("IGNORE_SNOWMELT",    getOption("IGNORE_SNOWMELT"),
                   engineBoolString(!m_ignoreSnowmeltBox->isChecked()));
    writeIfChanged("IGNORE_GROUNDWATER", getOption("IGNORE_GROUNDWATER"),
                   engineBoolString(!m_ignoreGroundwaterBox->isChecked()));
    writeIfChanged("IGNORE_RDII",        getOption("IGNORE_RDII"),
                   engineBoolString(!m_ignoreRDIIBox->isChecked()));
    writeIfChanged("IGNORE_QUALITY",     getOption("IGNORE_QUALITY"),
                   engineBoolString(!m_ignoreQualityBox->isChecked()));
    writeIfChanged("IGNORE_ROUTING",     getOption("IGNORE_ROUTING"),
                   engineBoolString(!m_ignoreRoutingBox->isChecked()));

    // Tab 2 — dates & times
    QString d, t;
    formatEngineDateTime(m_startEdit->dateTime(),       d, t);
    writeIfChanged("START_DATE", getOption("START_DATE"), d);
    writeIfChanged("START_TIME", getOption("START_TIME"), t);

    formatEngineDateTime(m_endEdit->dateTime(),         d, t);
    writeIfChanged("END_DATE",   getOption("END_DATE"),   d);
    writeIfChanged("END_TIME",   getOption("END_TIME"),   t);

    formatEngineDateTime(m_reportStartEdit->dateTime(), d, t);
    writeIfChanged("REPORT_START_DATE", getOption("REPORT_START_DATE"), d);
    writeIfChanged("REPORT_START_TIME", getOption("REPORT_START_TIME"), t);

    writeIfChanged("REPORT_STEP",  getOption("REPORT_STEP"),
                   QString::number(m_reportStepEdit->totalSeconds()));
    writeIfChanged("DRY_STEP",     getOption("DRY_STEP"),
                   QString::number(m_dryStepEdit->totalSeconds()));
    writeIfChanged("WET_STEP",     getOption("WET_STEP"),
                   QString::number(m_wetStepEdit->totalSeconds()));
    writeIfChanged("RULE_STEP",    getOption("RULE_STEP"),
                   QString::number(m_ruleStepEdit->totalSeconds()));
    {
        // Routing step is a plain text box; preserve the user's typed
        // decimal precision but normalise to a canonical %g rendering.
        bool ok = false;
        const double v = m_routingStepEdit->text().trimmed().toDouble(&ok);
        if (ok)
            writeIfChanged("ROUTING_STEP", getOption("ROUTING_STEP"),
                           QString::number(v, 'g', 6));
    }
    writeIfChanged("DRY_DAYS",     getOption("DRY_DAYS"),
                   QString::number(m_dryDaysSpin->value(), 'f', 2));
    writeIfChanged("SWEEP_START",  getOption("SWEEP_START"),
                   m_sweepStartEdit->date().toString(QStringLiteral("MM/dd")));
    writeIfChanged("SWEEP_END",    getOption("SWEEP_END"),
                   m_sweepEndEdit->date().toString(QStringLiteral("MM/dd")));

    // Tab 2 — [EVENTS] (Slice CW). writeEventsToEngine() returns the number
    // of rows it actually pushed; folded into n so wroteChanges flips.
    n += writeEventsToEngine();

    // Tab 3 — Routing & Hydraulics
    writeIfChanged("SURCHARGE_METHOD",    getOption("SURCHARGE_METHOD"),
                   m_surchargeCombo->currentData().toString());
    writeIfChanged("DPS_CELERITY",        getOption("DPS_CELERITY"),
                   QString::number(m_dpsCelerSpin->value(), 'f', 4));
    writeIfChanged("DPS_ALPHA",           getOption("DPS_ALPHA"),
                   QString::number(m_dpsAlphaSpin->value(), 'f', 4));
    writeIfChanged("DPS_DECAY_TIME",      getOption("DPS_DECAY_TIME"),
                   QString::number(m_dpsDecaySpin->value(), 'f', 4));
    writeIfChanged("TPA_CELERITY",        getOption("TPA_CELERITY"),
                   QString::number(m_tpaCeleritySpin->value(), 'f', 1));
    // Unsteady friction (engine issue #156) — consumed by DW and FV; the
    // engine accepts the keys under any routing model.
    writeIfChanged("UNSTEADY_FRICTION",   getOption("UNSTEADY_FRICTION"),
                   m_ufMethodCombo->currentData().toString());
    writeIfChanged("UF_K3",               getOption("UF_K3"),
                   QString::number(m_ufK3Spin->value(), 'f', 3));
    writeIfChanged("NODE_CONTINUITY",     getOption("NODE_CONTINUITY"),
                   m_nodeContinuityCombo->currentData().toString());
    writeIfChanged("ANDERSON_ACCEL",      getOption("ANDERSON_ACCEL"),
                   engineBoolString(m_andersonAccelBox->isChecked()));
    writeIfChanged("FORCE_MAIN_EQUATION", getOption("FORCE_MAIN_EQUATION"),
                   m_forceMainCombo->currentData().toString());
    writeIfChanged("NORMAL_FLOW_LIMITED", getOption("NORMAL_FLOW_LIMITED"),
                   m_normalFlowCombo->currentData().toString());
    writeIfChanged("INERTIAL_DAMPING",    getOption("INERTIAL_DAMPING"),
                   m_inertialDampCombo->currentData().toString());
    writeIfChanged("LENGTHENING_STEP",    getOption("LENGTHENING_STEP"),
                   QString::number(m_lengtheningSpin->value(), 'f', 2));
    writeIfChanged("VARIABLE_STEP",       getOption("VARIABLE_STEP"),
                   QString::number(m_variableStepSpin->value(), 'f', 3));
    writeIfChanged("MINIMUM_STEP",        getOption("MINIMUM_STEP"),
                   QString::number(m_minStepSpin->value(), 'f', 3));
    writeIfChanged("MAX_TRIALS",          getOption("MAX_TRIALS"),
                   QString::number(m_maxTrialsSpin->value()));
    writeIfChanged("HEAD_TOLERANCE",      getOption("HEAD_TOLERANCE"),
                   QString::number(m_headTolSpin->value(), 'f', 6));
    // LAT/SYS_FLOW_TOL speak percent through the options API, matching the
    // spin display and the .inp surface.
    writeIfChanged("LAT_FLOW_TOL",        getOption("LAT_FLOW_TOL"),
                   QString::number(m_latFlowTolSpin->value(), 'f', 2));
    writeIfChanged("SYS_FLOW_TOL",        getOption("SYS_FLOW_TOL"),
                   QString::number(m_sysFlowTolSpin->value(), 'f', 2));
    writeIfChanged("MIN_SURFAREA",        getOption("MIN_SURFAREA"),
                   QString::number(m_minSurfAreaSpin->value(), 'f', 4));
    writeIfChanged("MIN_SLOPE",           getOption("MIN_SLOPE"),
                   QString::number(m_minSlopeSpin->value(), 'f', 4));

    // Tab 3 — Finite volume solver. Written regardless of the routing
    // selection: the engine accepts FV_* keys as inert under non-FV routing
    // and its InpWriter only persists them when FLOW_ROUTING is FV.
    writeIfChanged("FV_CELL_LENGTH",      getOption("FV_CELL_LENGTH"),
                   QString::number(m_fvCellLengthSpin->value(), 'f', 2));
    writeIfChanged("FV_MIN_CELLS",        getOption("FV_MIN_CELLS"),
                   QString::number(m_fvMinCellsSpin->value()));
    writeIfChanged("FV_CFL",              getOption("FV_CFL"),
                   QString::number(m_fvCflSpin->value(), 'f', 2));
    writeIfChanged("FV_RIEMANN",          getOption("FV_RIEMANN"),
                   m_fvRiemannCombo->currentData().toString());
    writeIfChanged("FV_ORDER",            getOption("FV_ORDER"),
                   m_fvOrderCombo->currentData().toString());
    writeIfChanged("FV_LIMITER",          getOption("FV_LIMITER"),
                   m_fvLimiterCombo->currentData().toString());
    writeIfChanged("FV_TIME_INTEGRATION", getOption("FV_TIME_INTEGRATION"),
                   m_fvTimeIntCombo->currentData().toString());
    writeIfChanged("FV_SLOT_CELERITY",    getOption("FV_SLOT_CELERITY"),
                   QString::number(m_fvSlotCeleritySpin->value(), 'f', 1));
    writeIfChanged("FV_PRESSURE_CLOSURE", getOption("FV_PRESSURE_CLOSURE"),
                   m_fvPressureClosureCombo->currentData().toString());
    writeIfChanged("FV_PRESSURIZED_IMPLICIT", getOption("FV_PRESSURIZED_IMPLICIT"),
                   engineBoolString(m_fvPressImplicitBox->isChecked()));
    // FV_SCALAR_SCHEME is edited on the Quality & Transport page (ARD group)
    // but is an FV_* key; written with its siblings so the engine sees one
    // coherent FV block.
    writeIfChanged("FV_SCALAR_SCHEME",    getOption("FV_SCALAR_SCHEME"),
                   m_fvScalarSchemeCombo->currentData().toString());
    writeIfChanged("FV_STRUCTURE_COUPLING", getOption("FV_STRUCTURE_COUPLING"),
                   m_fvStructCouplingCombo->currentData().toString());
    writeIfChanged("FV_COMPACTION",       getOption("FV_COMPACTION"),
                   engineBoolString(m_fvCompactionBox->isChecked()));
    writeIfChanged("FV_BACKEND",          getOption("FV_BACKEND"),
                   m_fvBackendCombo->currentData().toString());
    writeIfChanged("FV_MIN_PARALLEL_CELLS", getOption("FV_MIN_PARALLEL_CELLS"),
                   QString::number(m_fvMinParallelSpin->value()));
    writeIfChanged("FV_LTS",              getOption("FV_LTS"),
                   engineBoolString(m_fvLtsBox->isChecked()));
    writeIfChanged("FV_LTS_MAX_TIERS",    getOption("FV_LTS_MAX_TIERS"),
                   QString::number(m_fvLtsTiersSpin->value()));
    writeIfChanged("FV_CFL_CENSUS_INTERVAL", getOption("FV_CFL_CENSUS_INTERVAL"),
                   QString::number(m_fvCflCensusSpin->value()));

    // Quality & Transport (Y1). Every key is written unconditionally of the
    // solver selection — the engine accepts them under any solver (Y0 §2.1),
    // so a user can configure LARD before switching to it and the settings
    // survive. writeIfChanged's numeric-aware compare keeps QUALITY_STEP
    // from churning against the engine's "0.000000" rendering.
    writeIfChanged("QUALITY_SOLVER",      getOption("QUALITY_SOLVER"),
                   m_qualitySolverCombo->currentData().toString());
    writeIfChanged("OUTFALL_BACKFLOW_QUALITY",
                   getOption("OUTFALL_BACKFLOW_QUALITY"),
                   m_outfallBackflowCombo->currentData().toString());
    writeIfChanged("QUALITY_STEP",        getOption("QUALITY_STEP"),
                   QString::number(m_qualityStepSpin->value(), 'f', 2));
    writeIfChanged("MAX_SEGMENTS_PER_LINK", getOption("MAX_SEGMENTS_PER_LINK"),
                   QString::number(m_maxSegmentsSpin->value()));
    writeIfChanged("DISPERSION",          getOption("DISPERSION"),
                   m_dispersionCombo->currentData().toString());
    writeIfChanged("RWPT_SEED",           getOption("RWPT_SEED"),
                   QString::number(m_rwptSeedSpin->value()));
    writeIfChanged("WATER_AGE",           getOption("WATER_AGE"),
                   engineBoolString(m_waterAgeBox->isChecked()));
    writeIfChanged("HEAT_TRANSPORT",      getOption("HEAT_TRANSPORT"),
                   engineBoolString(m_heatTransportBox->isChecked()));

    // Tab 4 — System / Performance
    writeIfChanged("THREADS",             getOption("THREADS"),
                   QString::number(m_threadsSpin->value()));

    // 2D module toggle — IGNORE_2D is the engine-honored gate (unchecked ⇒
    // IGNORE_2D YES ⇒ the solver never activates, mesh or no mesh); QSettings
    // keeps the per-project UI intent for models without the key.
    //
    // Written only once the box carries a real intent (see
    // m_module2DIntentKnown): on a 1D deck that has never been toggled the
    // unchecked box merely describes the .inp, and materialising IGNORE_2D
    // from it made every no-edit Apply dirty the project.
    if (m_module2DBox && m_module2DIntentKnown)
        writeIfChanged("IGNORE_2D", getOption("IGNORE_2D"),
                       engineBoolString(!m_module2DBox->isChecked()));
    if (m_module2DBox && m_layer && m_module2DIntentKnown)
    {
        // Same condition as the IGNORE_2D write above: persisting an inferred
        // state would make s.contains(key) true on the next open, promoting
        // the inference to an "intent" and writing IGNORE_2D after all — the
        // bug would simply reappear on the second Apply. A deck whose state
        // was inferred re-infers it next time, to the same answer.
        QSettings s;
        const QString key = QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                                .arg(m_layer->modelFilePath());
        s.setValue(key, m_module2DBox->isChecked());
    }

    // Tab 6 — 2D (only when compiled in)
#ifdef OPENSWMM_HAS_2D
    write2DToEngine(n);
#endif

    // Tab 7 — Files / Plugins (Slice AA-3.5)
    // Order matters: the table editor reconciles existing rows first
    // (its diff pass can REMOVE rows the user deleted), then the combos
    // run as an additive-only "ensure these are present" pass.  If a
    // combo's pluginId was just removed by the table edit, the combo
    // re-adds it — combos win on conflict, by design.
    n += writePluginsToEngine();
    n += writeProcessComponentsToEngine();   // U1
    n += writeFilesSectionToEngine();
    n += writeWriterCombosToEngine();
    n += writeReportContentsToEngine();
    writeOutputPathsToSettings();

    if (n > 0)
        m_wroteChanges = true;
    return n;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::onApply()
{
    // [EVENTS] validation gate (Slice CW).  Block Apply when any row has
    // Start >= End; warn (non-blocking) on overlap / out-of-range so users
    // can still proceed when they know what they're doing.
    QString warn;
    if (!validateEvents(&warn)) {
        QMessageBox::warning(this, tr("Invalid event row"),
            tr("One or more events have Start ≥ End.  Fix the highlighted "
               "rows before applying."));
        return;
    }
    // Files / Output / Plugins validation gate (Phase 3.10.4).  Block on
    // empty plugin id or empty hot-start save path; warn (non-blocking)
    // on softer checks (empty Selected list, missing parent dirs).
    if (!validateFilesTab(&warn)) {
        QMessageBox::warning(this, tr("Invalid Files / Plugins row"),
            tr("One or more rows on the Files or Plugins sub-tab are "
               "missing a required value.  Fix the highlighted rows "
               "before applying."));
        return;
    }
    if (!warn.isEmpty()) {
        const auto choice = QMessageBox::warning(this, tr("Warnings"),
            warn + tr("\nApply anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes) return;
    }

    writeToEngine();
    // Re-read after write so the controls reflect whatever the engine
    // actually accepted (some keys may be clamped or normalised).
    readFromEngine();
}

void SimulationOptionsDialog::onAccept()
{
    QString warn;
    if (!validateEvents(&warn)) {
        QMessageBox::warning(this, tr("Invalid event row"),
            tr("One or more events have Start ≥ End.  Fix the highlighted "
               "rows before clicking OK."));
        return;
    }
    if (!validateFilesTab(&warn)) {
        QMessageBox::warning(this, tr("Invalid Files / Plugins row"),
            tr("One or more rows on the Files or Plugins sub-tab are "
               "missing a required value.  Fix the highlighted rows "
               "before clicking OK."));
        return;
    }
    if (!warn.isEmpty()) {
        const auto choice = QMessageBox::warning(this, tr("Warnings"),
            warn + tr("\nApply anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes) return;
    }

    writeToEngine();
    accept();
}
