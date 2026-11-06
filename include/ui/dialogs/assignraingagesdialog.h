/**
 * @file      assignraingagesdialog.h
 * @brief     Bind rain gages to subcatchments and RDII inflows spatially, in one undoable step.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Bind rain gages to subcatchments and RDII inflows spatially, in one undoable
 * step.
 *
 * Two methods:
 *
 *   Proximity — assign the gage whose Thiessen (Voronoi) cell covers the
 *     largest share of the subcatchment's area (an RDII node, being a point,
 *     takes its nearest gage). Uses the real watershed boundary rather than a
 *     representative point.
 *
 *   Interpolated — area-average natural-neighbour weights over the gage
 *     network, group objects whose weight vectors agree, and materialise each
 *     group as a generated gage backed by a generated series. SWMM binds
 *     exactly one gage per subcatchment / unit-hydrograph group, so an
 *     interpolated field cannot be expressed any other way.
 *
 * The dialog is a front end over assignment/raingageassignment.h: inputs are
 * snapshotted on the GUI thread, the plan is computed on a worker (progress +
 * Cancel), and a computed plan is reused by Apply until the model or an option
 * changes. Everything is computed before any mutation, so the volume gate can
 * abort with nothing written; the whole assignment is one undo step.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H

#include "assignment/raingageassignment.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>

#include <memory>

class MapCanvas;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTimer;
class SelectionManager;
class SWMMModelLayer;

namespace openswmmvis::ui
{

class AssignRainGagesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] layer Optional borrowed guarded target model; engine access on GUI thread, owner
     *   coordinates handle lifecycle.
     * @param[in] canvas Optional borrowed guarded canvas; supplies undo stack when present,
     *   otherwise commands run once without undo retention.
     * @param[in] selection Optional borrowed guarded selection manager; current selection sampled
     *   when options gathered, not subscribed for cache invalidation.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog and pending
     *   destruction wait.
     * @brief Build controls/default target scope, subscribe model edit signals, initialize worker
     *   watcher and 100ms progress polling.
     * @note Initial RDII count and selection count govern checkbox/radio enabled states once at
     *   construction; not refreshed when those counts change.
     *   ModelEdited/geometry/attribute/hydrograph/data-object signals invalidate cached plan, but
     *   selection changes alone do not. No computation/application until requested. GUI-thread UI;
     *   apply window-stacking policy at end.
     */
    AssignRainGagesDialog(SWMMModelLayer *layer, MapCanvas *canvas, SelectionManager *selection,
                          QWidget *parent = nullptr);
    /**
     * @brief Request cooperative cancellation and synchronously wait for pending worker before
     *   destroying members.
     * @note Can block GUI until compute acknowledges/finishes; does not apply unfinished plan or
     *   undo previous assignments. Ordinary QDialog reject/hide alone does not run this destructor
     *   or cancel job if owner retains dialog.
     */
    ~AssignRainGagesDialog() override;

    /*! \brief True while a plan is being computed. Tests and the perf harness
     *         wait on this after clicking Preview / Apply.
     * @return Stored m_computing flag true from startCompute through GUI onComputeFinished,
     *   including interval after worker finishes but before queued completion handled. No direct
     *   future/engine query.
 */
    [[nodiscard]] bool busy() const;

private slots:
    /**
     * @brief Enable weighting/tolerance for interpolation only, synchronously count eligible gages
     *   and report unlocated/coincident exclusions.
     * @note Calls eligibleGages on GUI thread, then refreshes action buttons. Does not itself
     *   invalidate cached plan (separate proximity-toggle connection does), refresh RDII/selection
     *   counts, compute weights or mutate assignments. Model notifications only invalidate, so
     *   label can lag later model changes until this helper runs.
     */
    void onMethodChanged();
    /**
     * @brief Start asynchronous plan computation without auto-apply; busy call no-op. GUI snapshot
     *   gathering still occurs synchronously and refreshes external rain files.
     */
    void onPreview();
    /**
     * @brief Reuse valid cached plan immediately, otherwise start computation with auto-apply
     *   intent.
     * @note Cached eligibility relies on subscribed generation changes, not independent
     *   model/selection fingerprint validation. Does not accept/close dialog. Busy normally
     *   prevented by disabled Apply; direct start path is guarded by startCompute.
     */
    void onApply();
    /**
     * @brief Stop/hide progress, store worker plan and mark cache reusable only for empty error and
     *   unchanged generation.
     * @note Clear busy before applying/presenting. Auto-apply stale result receives model-changed
     *   error and is not applied; stale Preview still displays plan without added stale error but
     *   cache remains invalid. Worker error/cancellation is shown without mutation. Completion can
     *   still apply while dialog merely hidden/rejected and retained because no reject override
     *   cancels intent.
     */
    void onComputeFinished();
    /**
     * @brief Set shared cooperative compute cancellation flag when present, without closing dialog,
     *   waiting or clearing busy/cache immediately.
     * @note Worker completion supplies cancellation error; no rollback of earlier applied
     *   assignment or immediate source mutation. Safe shared atomic state outlives worker.
     */
    void onCancelClicked();
    /**
     * @brief Clear reusable-cache flag and increment generation so in-flight result cannot
     *   auto-apply as current.
     * @note Does not cancel worker, clear preview/error, refresh counts/buttons or inspect actual
     *   changed field. Selection changes require caller-triggered invalidation if cached
     *   selected-only scope must track them.
     */
    void invalidatePlan();

private:
    /**
     * @brief Create owned method/targets/scope controls, readonly preview, hidden progress and
     *   Preview/Apply/Cancel/Close actions.
     * @note Proximity, subcatchments and all-objects default; RDII defaults enabled/checked only if
     *   existing count positive; selected scope enabled only if initial selected node/subcatch
     *   count positive. Tolerance editor 0.1..10 percent, default1; weighting Sibson/Laplace.
     * @note Option signals invalidate plan but controls remain editable during compute. Close
     *   rejects without explicit worker cancellation; Cancel is an action flag, not QDialog
     *   rejection. No engine assignments or worker dispatch inside UI builder.
     */
    void buildUi();
    /**
     * @brief Enable Preview/Apply for guarded layer with nonnull engine and no active computation,
     *   Cancel only while busy.
     * @note Does not check engine editable lifecycle, eligible-gage/target/selection count, plan
     *   validity or canvas stack; other option controls unchanged. No engine mutation.
     */
    void updateButtons();

    /**
     * @return Owned current widget options: interpolation versus nearest, Laplace flag, displayed
     *   percent divided by100 (0.001..0.1 fraction), target booleans and current selected names
     *   when selected scope/manager exists.
     * @note RDII requires checked AND enabled; selectedOnly checks radio/manager rather than radio
     *   enabled state. Node/subcatch selections copied by name only without layer
     *   filtering/deduplication; canvas/native coordinates read later by gatherInput. No engine
     *   writes or cache invalidation here.
     */
    [[nodiscard]] assignment::raingage::Options currentOptions() const;
    /**
     * @param[in] applyWhenDone True requests immediate apply only when completed successful plan
     *   matches captured generation, false preview only.
     * @brief If idle, snapshot model/options on GUI thread, then dispatch pure plan worker using
     *   independent shared Input/Progress values.
     * @note gatherInput can refresh external rain-file cache and may invalidate model generation
     *   while gathering; generation captured BEFORE gather, conservatively making result stale if
     *   notifications arrive. Null/unavailable model is represented by input/plan error, not local
     *   preflight refusal.
     * @note Set busy/cache/intent and start 100ms atomic progress polling; worker owns no GUI
     *   pointers. No assignments until GUI completion/apply command; option edits increment
     *   generation while worker runs, selection-only changes are not automatically observed.
     */
    void startCompute(bool applyWhenDone);
    /**
     * @param[in] plan Borrowed successful computed plan whose IDs/data still describe target; maker
     *   rejects unavailable model/engine, error or no edits.
     * @brief Build assignment bulk command and push to canvas undo stack, or redo once/delete when
     *   no stack available.
     * @note Transfers ownership to stack; direct fallback leaves no undo record. On null command
     *   show preview then Nothing to assign or plan error. On created command, invalidate cache and
     *   display plan as applied without inspecting individual engine write success (commands have
     *   no returned status).
     * @note Caller ensures GUI thread and current plan; no independent generation check here, no
     *   acceptance/close. Planned generated gages/series/group changes share one bulk undo command
     *   when stacked, but this dialog does not guarantee atomic runtime rollback on rejected child
     *   write.
     */
    void applyPlan(const assignment::raingage::Plan &plan);
    /**
     * @param[in] plan Borrowed immutable plan read for rows/errors/counts/warnings.
     * @param[in] applied True uses past-tense planned summary, false would-change; no engine
     *   verification.
     * @brief Replace preview rows and render summary, without mutating model or cache.
     * @note Error clears rows/shows error only. Otherwise unchanged rows use disabled foreground,
     *   columns resize, and generated/group/removal counts reflect plan flags (including preview).
     *   Worst volume relative error displayed as raw dimensionless value, not percentage
     *   conversion. No stage/cancel/command status query.
     */
    void showPlan(const assignment::raingage::Plan &plan, bool applied);

    QPointer<SWMMModelLayer> m_layer;
    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;

    QRadioButton *m_methodProximity = nullptr;
    QRadioButton *m_methodInterp = nullptr;
    QComboBox *m_variantCombo = nullptr;
    QDoubleSpinBox *m_tolSpin = nullptr;
    QCheckBox *m_targetSubcatch = nullptr;
    QCheckBox *m_targetRdii = nullptr;
    QRadioButton *m_scopeAll = nullptr;
    QRadioButton *m_scopeSelected = nullptr;
    QLabel *m_gageCountLbl = nullptr;
    QTableWidget *m_preview = nullptr;
    QLabel *m_statusLbl = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_previewBtn = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    // ── Background compute + plan cache ─────────────────────────────────
    QFutureWatcher<assignment::raingage::Plan> m_watcher;
    std::shared_ptr<assignment::raingage::Progress> m_progress;
    QTimer *m_progressTimer = nullptr;
    bool m_applyWhenDone = false;
    bool m_computing = false; ///< Until onComputeFinished() has run.
    bool m_havePlan = false;  ///< m_plan matches the current model + options.
    quint64 m_generation = 0; ///< Bumped by every invalidating change.
    quint64 m_computeGeneration = 0;
    assignment::raingage::Plan m_plan;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H
