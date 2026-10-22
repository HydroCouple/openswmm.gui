/**
 * @file      comparisonplotdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BL — Multi-pane Comparison Plot Dialog.
 *
 * Replaces the single-series TimeSeriesPlotDialog with a comparison /
 * analysis surface. Slice AT spec §4819-5093 of GUI_IMPLEMENTATION_PLAN.md.
 *
 * Time-series rows optionally include a 1v1 scatter column when matching
 * baseline/comparison or configured pairs produce overlapping samples.
 * FitMetrics supplies title statistics for the best finite NSE pair.
 *
 * CF.3-ready: any IRunLayer (1D SwmmOutRunLayer or 2D SWMM2DResultsLayer)
 * can be added as a RunSource, and the dialog auto-spawns chart rows
 * keyed by PlotAttribute — including the five Mesh2D* enumerators.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_COMPARISONPLOTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_COMPARISONPLOTDIALOG_H

#include "plot/comparisonplotmodel.h"

#include <QDateTime>
#include "ui/panels/plotpanelview.h"
#include <QHash>
#include <QPointer>
#include <QString>

#include <memory>
#include <thread>

class SWMMVisProjectWindow;
class QAction;
class QProgressBar;
class QTimer;
class QActionGroup;
class QChartView;
class QLabel;
class QChart;
class QLineSeries;
class QScatterSeries;
namespace openswmmvis::plot
{
class UtcTimeAxis;
}
class QValueAxis;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QGridLayout;
class QScrollArea;
class QToolBar;
class QWidget;
class QComboBox;
class QPushButton;

// Forward declarations — these classes live in the global namespace, so they
// must be declared here at file scope (not inline inside the openswmmvis::ui
// namespace below, which would create phantom openswmmvis::ui::* types).
class SWMMResultsLayer;
class SWMM2DResultsLayer;

namespace openswmmvis::ui
{

class InteractiveChartView;
class RangeSliderWidget;
class StatsSummaryPanel;

class ComparisonPlotDialog : public openswmmvis::ui::PlotPanelView
{
    Q_OBJECT
public:
    /**
     * @brief Build a GUI-thread comparison plot panel with an owned source/series model.
     * @param[in] parent Optional Qt owner of this panel and controls.
     * Owns charts/views and an optional saved-results extraction thread; layer adapters share run
     * objects while borrowing guarded project layers. Model changes update charts/tree; no project
     * model data is changed by plotting.
     */
    explicit ComparisonPlotDialog(QWidget *parent = nullptr);
    /**
     * @brief Request cancellation and join any saved-2D extraction thread before destroying
     * model/widgets.
     * Worker checks cancellation between frame reads and never captures this dialog; destruction
     * may wait for an in-progress frame I/O, without a strict time bound.
     */
    ~ComparisonPlotDialog() override;
    /**
     * @brief Delegate panel show/layout restoration, then resize the initial series pane to its
     * labels.
     * @param[in,out] event Required borrowed Qt show event, forwarded to PlotPanelView; not
     * retained.
     */
    void showEvent(QShowEvent *event) override;
    /**
     * @brief Remove run adapters belonging to the closing project, then rebuild once.
     * @param[in] project Nullable borrowed project; requires an existing canvas, otherwise no-op.
     * Matches canvas layers and 1D results bound to its model, cancels a matching extraction, and
     * suppresses intermediate model signals during removal. Does not delete project layers.
     */
    void removeProjectSources(SWMMVisProjectWindow *project);

    /*! \brief Underlying model — exposed so callers (Object Browser wiring,
     *  CF.3's openComparisonPlotForCells) can add runs/series directly. */
    /**
     * @brief Borrow the owned comparison model for source/series configuration.
     * @return Non-null pointer valid until panel destruction; caller must not delete it and uses
     * GUI-thread model mutations.
     */
    openswmmvis::plot::ComparisonPlotModel *model() noexcept { return m_model.get(); }

    /*! \brief Convenience helper: add a 1D RunSource for the given results
     *  layer (or update the existing one if already added) and return its
     *  run index. */
    /**
     * @brief Find or add a shared 1D adapter around a borrowed result layer.
     * @param[in,out] layer Nullable borrowed layer; lifetime observers remove its run on
     * destruction/project close. Deduplicates by resultsFilePath/persistence key.
     * @param[in] makeBaseline Sets baseline only when a new run is created; an existing matching
     * run returns immediately without changing it.
     * @return Existing/new zero-based run index, or -1 for null. First run seeds cursor time;
     * periodsAppended triggers incremental tails. Does not update an existing adapter despite older
     * shorthand.
     */
    int ensureRunSourceForLayer(::SWMMResultsLayer *layer, bool makeBaseline = false);

    /*! \brief CF.3 — add a 2D RunSource for the given mesh results layer
     *  (or return the existing one) and return its run index. */
    /**
     * @brief Find or add a shared 2D adapter by exact borrowed layer identity.
     * @param[in,out] layer Nullable borrowed results layer, observed for destruction/project close;
     * its ownership stays external.
     * @return Zero-based run index or -1 for null. New runs defer saved-file reads, subscribe live
     * range changes and seed first-run time when a source exists.
     */
    int ensureRunSourceForMeshLayer(::SWMM2DResultsLayer *layer);

    /*! \brief Convenience helper: add a series in one call. Returns the new
     *  series index, or -1 on failure. */
    /**
     * @brief Add a fixed-attribute series through the descriptor overload.
     * @param[in] runIndex Zero-based run index.
     * @param[in] ref Borrowed object identity copied into the model.
     * @param[in] attr Fixed attribute converted to a descriptor.
     * @return Model series index or -1 for invalid run/object/descriptor/model rejection; normal
     * model signals refresh views.
     */
    int addSeries(int runIndex, const openswmmvis::plot::ObjectRef &ref,
                  openswmmvis::plot::PlotAttribute attr);

    /*! \brief Y2b-2 (amendment D-Y4): descriptor-shaped overload — a
     *  fixed attribute or a species BY NAME. The enum overload above
     *  forwards here. */
    /**
     * @brief Validate identities and append a series specification to the model.
     * @param[in] runIndex Index in [0,runSourceCount).
     * @param[in] ref Borrowed valid object reference copied into the specification.
     * @param[in] descriptor Borrowed valid fixed/species/catalog descriptor copied into the
     * specification.
     * @return Model series index, or -1 for invalid inputs/model rejection. Data availability is
     * resolved separately; an accepted spec may display an error later.
     */
    int addSeries(int runIndex, const openswmmvis::plot::ObjectRef &ref,
                  const openswmmvis::plot::ResultDescriptor &descriptor);

    /*! \brief Add several (object, descriptor) series to one run with a
     *  single tree/chart rebuild. Invalid pairs are skipped. Returns the
     *  count of series added. */
    /**
     * @brief Append valid object/descriptor pairs using one model batch notification.
     * @param[in] runIndex Zero-based existing run index; invalid yields zero.
     * @param[in] items Borrowed identities copied into specs; invalid object/descriptor pairs are
     * skipped.
     * @return Count accepted by the model, including zero; does not require all pairs to succeed.
     */
    int addSeriesBatch(
        int runIndex,
        const QVector<QPair<openswmmvis::plot::ObjectRef, openswmmvis::plot::ResultDescriptor>>
            &items);

    /*! \brief CF.3 — add per-cell series for the supplied attribute list.
     *  Returns the count of series successfully added. */
    /**
     * @brief Add the cross-product of cell identities and fixed attributes in one batch.
     * @param[in] runIndex Existing run index, invalid yields zero.
     * @param[in] triIdxList Source mesh cell/triangle identities used by ObjectRef::forMesh2DCell;
     * descriptor/model validation decides usability.
     * @param[in] attributes Fixed attributes converted to descriptors.
     * @return Number added, zero for invalid run/empty input or rejection; no geometric lookup is
     * done here.
     */
    int addCellSeries(int runIndex, const QVector<int> &triIdxList,
                      const QVector<openswmmvis::plot::PlotAttribute> &attributes);

    /*! \brief Per-cell series for any descriptor — fixed attributes and the
     *  2D source's catalog variables (groundwater, infiltration, species). */
    /**
     * @brief Add the cross-product of cell identities and result descriptors.
     * @param[in] runIndex Existing run index validated by addSeriesBatch.
     * @param[in] triIdxList Borrowed cell identities copied into ObjectRef values.
     * @param[in] descriptors Borrowed fixed/species/catalog descriptors copied into batch specs.
     * @return Number accepted, zero for no valid pairs; source availability is resolved later.
     */
    int addCellDescriptorSeries(int runIndex, const QVector<int> &triIdxList,
                                const QVector<openswmmvis::plot::ResultDescriptor> &descriptors);

    /*! \brief Slice AT.2 — programmatically set the Add-from-Map toolbar
     *  action's checked state. Used by SWMMVis when the user cancels the
     *  pick tool via Escape (so the toolbar button un-toggles). */
    /**
     * @brief Set the map-picking toolbar action state, emitting its normal toggle signal when
     * changed.
     * @param[in] checked Requested state; no-op before action construction. External owner controls
     * the canvas tool lifecycle.
     */
    void setAddFromMapChecked(bool checked);

    /*! \brief True while saved 2D results are read on a worker thread. */
    /**
     * @brief Read whether a saved-2D job state is currently held.
     * @return True from worker start until GUI completion polling removes it, including a completed
     * but not-yet-polled job; pending scheduled work alone is false.
     */
    bool isLoadingResults() const { return static_cast<bool>(m_load); }

signals:
    /*! \brief Slice AT.2 — emitted when the user toggles the
     *  "Add from Map…" toolbar action. The dialog itself doesn't know
     *  about the canvas; SWMMVis owns the MapToolPlotPick lifecycle. */
    /**
     * @brief Request external activation/deactivation of map-based series picking.
     * @param[in] active New action state; the panel itself does not own or install the canvas pick
     * tool.
     */
    void addFromMapToggled(bool active);

protected:
    /**
     * @brief Turn off the external map-pick action when this panel deactivates.
     * @param[in] active New plot activation; activation itself does not restart picking.
     */
    void plotActivationChanged(bool active) override;

private slots:
    /**
     * @brief Enable deferred file reads on a new mesh adapter and rebuild the series tree.
     * @param[in] runIndex Valid newly added model run index, accessed without a local bounds guard.
     */
    void onRunSourceAdded(int runIndex);
    /**
     * @brief Rebuild tree and all charts after complete run removal.
     * @param[in] runIndex Ignored notification index (the project cleanup path may pass -1).
     */
    void onRunSourceRemoved(int runIndex);
    /**
     * @brief Rebuild tree/charts after series insertion.
     * @param[in] seriesIndex Ignored notification index; full current model state is used.
     */
    void onSeriesAdded(int seriesIndex);
    /**
     * @brief Rebuild tree/charts after series removal.
     * @param[in] seriesIndex Ignored removed index, which must not be dereferenced.
     */
    void onSeriesRemoved(int seriesIndex);
    /**
     * @brief Restyle the first matching plotted line and its tree label/check/swatch without chart
     * reconstruction.
     * @param[in] seriesIndex Valid current model series index expected from styleChanged; absent
     * row/line ends without work. Model rows and widget arrays must correspond.
     */
    void onStyleChanged(int seriesIndex);
    /**
     * @brief Receive row-layout changes without immediate work; series/run/batch handlers perform
     * the chart rebuild after the complete mutation.
     */
    void onRowsChanged();
    /**
     * @brief Cache the animation timestamp and update cursor-line endpoints.
     * @param[in] t Timestamp copied/moved into panel state; invalid time falls back to each row
     * minimum when placing the cursor.
     */
    void onAnimationTimeChanged(QDateTime t);

    /*! Live results: append the points a growing run source gained since
     *  the last build/append to every affected series, extending axes
     *  outward only. Keeps every chart widget alive (no rebuildCharts).
     *  Falls back to rebuildCharts() when the row layout changed. */
    /**
     * @brief Batch-resolve live tails using consumed period counts and append strictly newer finite
     * points without replacing widgets.
     * UTC epoch milliseconds form X; axes extend outward with padding, replacing placeholders on
     * first data. Thinned-history period shifts trigger a full series reread with timestamp
     * deduplication. Layout/parallel-array mismatch falls back to rebuildCharts; same-time
     * corrections are not replaced by this append path.
     */
    void appendChartTails();

    /**
     * @brief Show supported quick-add run/attribute menus, then prompt for a 1D object ID when
     * required.
     * System pseudo-objects skip the ID prompt; canceled/empty choices are ignored. Does not
     * validate object existence against source data before appending the spec.
     */
    void onAddSeriesClicked();
    /**
     * @brief Prompt for an observed text file and its fixed attribute, then synchronously parse it
     * as SI data.
     * Reports parse/empty-column failures; shares the loaded adapter with the model and batches one
     * series per labeled column. Dialog cancellation adds nothing.
     */
    void onLoadObservedClicked();
    /**
     * @brief Remove selected series in descending index order to preserve earlier identities; run
     * rows are ignored.
     */
    void onRemoveSelectedClicked();
    /**
     * @brief Enable Remove when any selected tree item denotes a series; does not mutate data.
     */
    void onSeriesItemSelectionChanged();
    /**
     * @brief Open a modal full series-style/legend editor with live preview.
     * @param[in,out] item Nullable borrowed current tree item; run/invalid-index rows are ignored.
     * @param[in] column Ignored double-click column.
     * Cancel restores original style/legend override; OK reapplies final editor values, then tree
     * refreshes. Model owns all specification values.
     */
    void onSeriesItemDoubleClicked(QTreeWidgetItem *item, int column);

    // Slice AT.3 — series-tree visibility + context menu.
    /**
     * @brief Map a series checkbox to showLine only when the value differs.
     * @param[in] item Nullable borrowed tree item carrying model series identity.
     * @param[in] column Must be zero; invalid columns/indices are ignored. Exactly Checked shows
     * the line; updates model style through normal notifications.
     */
    void onSeriesItemChanged(QTreeWidgetItem *item, int column);
    /**
     * @brief Offer row-specific visibility/style/export/remove or run add/remove actions.
     * @param[in] pos Tree-viewport pixel coordinate, mapped globally for the popup. Missing items
     * cancel immediately. Plot This Only hides siblings sharing its descriptor; no undo command is
     * created.
     */
    void onSeriesTreeContextMenu(const QPoint &pos);

    // Slice AT.2 — toolbar slots.
    /**
     * @brief Propagate the current mutually exclusive navigation mode to existing time/scatter
     * views.
     */
    void onModeActionTriggered();
    /**
     * @brief Fit axes to the cached valid X selection, or fully rebuild without one.
     * For a selection, resolves row data and fits finite values with 5 percent Y padding when
     * nonconstant, retaining prior Y range otherwise; X follows the selection.
     */
    void onFitClicked();
    /**
     * @brief Prompt for PNG and stitch current row-frame grabs vertically on white.
     * No rows/empty destination/zero image height is ignored; writer failure displays a warning.
     * Exports rendered chart frames, without a vector or dataset export.
     */
    void onExportPngClicked();
    /**
     * @brief Export every current series through exportSeriesData, including hidden series when
     * resolvable.
     */
    void onExportDataClicked();
    /**
     * @brief Set master cursor/legend visibility on all current time charts.
     * @param[in] checked Visibility flag; endpoint placement is handled separately and scatter
     * charts have no animation cursor.
     */
    void onAnimationCursorToggled(bool checked);
    /**
     * @brief Choose a canonical system attribute from the first run supporting SystemRunoff and add
     * its pseudo-object series.
     * No suitable run displays guidance; menu cancellation adds nothing.
     */
    void onAddSystemSeriesClicked();

    // CP.1 — view-pane visibility toggles. Each persists splitter sizes
    // before hiding so the previous user-set width restores on re-show.
    /**
     * @brief Show/hide the series pane and preserve its prior splitter width.
     * @param[in] show True restores valid saved sizes or a 280-pixel default; false snapshots sizes
     * then hides. Missing controls are ignored.
     */
    void onShowSeriesToggled(bool show);
    /**
     * @brief Show/hide statistics, preserving outer chart/slider/stats sizes.
     * @param[in] show True restores saved positive stats size or a 4:1 chart/stats fallback with
     * 24-pixel slider; false snapshots then hides.
     */
    void onShowStatsToggled(bool show);
    /**
     * @brief Change only range-slider-host visibility.
     * @param[in] show Requested visibility; no-op without the host.
     */
    void onShowSliderToggled(bool show);
    /**
     * @brief Drive series/slider/stats action states together.
     * @param[in] chartsOnly True unchecks/hides all three, false checks/shows all three via normal
     * toggle signals. Does not remember their previous independent checked combination.
     */
    void onChartsOnlyToggled(bool chartsOnly);

    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 4 — optional 1v1 column.
    /**
     * @brief Rebuild charts, using the current action state to gate scatter pairing/construction.
     * @param[in] show Ignored notification value; rebuild reads the action itself.
     */
    void onShow1v1Toggled(bool show);

    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — pair configuration.
    /**
     * @brief Show a modal editor over the borrowed owned-model pair list; pair changes rebuild
     * scatter columns live.
     */
    void onConfigure1v1Clicked();

private:
    /**
     * @brief Remove the matching run after layer destruction or its ancestor project closes.
     * @param[in] layer Required borrowed QObject, dereferenced to inspect ancestors; context-bound
     * connections do not own it.
     * @param[in] source Shared adapter observed through a weak reference in callbacks, without
     * extending lifetime there.
     * Cancels a matching extraction, suppresses intermediate removal notifications and rebuilds
     * after complete removal.
     */
    void observeSourceLifetime(QObject *layer,
                               const std::shared_ptr<openswmmvis::plot::IRunLayer> &source);
    /**
     * @brief Create Qt-owned toolbar, load strip/timer, tree, chart splitters, range slider and
     * model-bound statistics panel once.
     * Slider maps normalized fractions to full UTC data range and routes row-zero axis changes
     * through cross-row synchronization.
     */
    void buildUi();
    /**
     * @brief Create owned navigation, fit/export/cursor/system/pick and pane/scatter actions once.
     * Connects checkable action states and shortcuts; restored layout can later alter persistent
     * view toggles.
     */
    void buildToolBar();
    /**
     * @brief Replace tree-owned run/series items under signal blocking, retaining model indices in
     * item data.
     * Displays baseline marker, descriptor names, visibility checks and color swatches; expands
     * rows and fits initial pane width. Previous item pointers are invalidated.
     */
    void rebuildSeriesTree();
    /**
     * @brief Batch-resolve series and replace all row frames/charts with deferred deletion of old
     * widgets.
     * Filters invalid times/nonfinite values, uses UTC milliseconds and descriptor units, applies
     * model styles/errors in legends and numeric preferences. Builds optional nearest-time paired
     * scatter only when nonempty pairs exist; best finite NSE supplies title metrics. Retains
     * per-descriptor legend alignment, rewires axes/navigation/cursors, resets full-range slider
     * and schedules deferred saved-file reads. Plot widget pointers must not be retained across
     * rebuilds.
     */
    void rebuildCharts();
    /*! \brief Resolve a series' chart legend name: spec.legendOverride
     *  if non-empty, else the auto label "<run> — <object> (<attr>)". */
    /**
     * @brief Resolve a time-series legend label without modifying the spec.
     * @param[in] spec Borrowed spec with valid runIndex; run lookup is unchecked here.
     * @return Explicit override when nonempty, otherwise run/object/descriptor label; mesh IDs are
     * printed as stored.
     */
    QString legendNameFor(const openswmmvis::plot::SeriesSpec &spec) const;
    /**
     * @brief Request a full chart rebuild for an existing widget row.
     * @param[in] rowIndex Index in [0,rowWidgets.size); invalid indices are ignored. No incremental
     * per-row update is implemented.
     */
    void updateChartForRow(int rowIndex);
    /**
     * @brief Replace cursor endpoints at cached animation time or each row minimum when invalid,
     * spanning its current Y bounds.
     * Leaves lines empty if no valid time exists; visibility remains controlled by the master
     * toggle.
     */
    void applyAnimationCursorToCharts();
    /*! \brief Push the current toolbar Mode into every row's
     *  InteractiveChartView. Called whenever the mode action group changes. */
    /**
     * @brief Apply selected Pan/ZoomIn/ZoomOut or default Select mode to every time and scatter
     * chart view.
     */
    void propagateModeToRows();
    /*! \brief Equalise splitter row sizes (called after add/remove). */
    /**
     * @brief Assign equal row heights using at least 220 pixels per row; no-op without
     * splitter/rows.
     */
    void equaliseChartSplitterSizes();
    /**
     * @brief Fit tree labels and grow the initial pane between 60 pixels and 60 percent of
     * available width.
     * Manual splitter movement marks its width authoritative, preventing later additions from
     * resizing it.
     */
    void fitSeriesPanelToContents();
    /*! \brief Hook up X-axis rangeChanged on a row's xAxis to mirror across rows. */
    /**
     * @brief Connect one row UTC range to every other time row under signal/reentry guards.
     * @param[in] rowIndex Current widget-row index; invalid/missing-axis input is ignored. Called
     * after insertion so captured index remains valid until chart replacement.
     */
    void wireXAxisSync(int rowIndex);
    /*! \brief Prompt for a CSV / SWMM .dat path and write the given series
     *  (model indices). Shared by the toolbar Export Data action, the chart
     *  row context menu, and the series-tree context menu. */
    /**
     * @brief Resolve and export selected model series to CSV or one SWMM DAT file per series.
     * @param[in] seriesIndices Borrowed model indices; invalid/unresolvable/empty series are
     * skipped. Hidden series can be included.
     * Temporarily disables mesh-file deferral for synchronous reads, then reenables it before
     * prompting. Captures values before the save dialog, honors DAT filter/suffix and reports
     * empty/error/multi-file results. Does not export only the current zoom window.
     */
    void exportSeriesData(const QVector<int> &seriesIndices);

    std::unique_ptr<openswmmvis::plot::ComparisonPlotModel> m_model;

    // UI
    QSplitter *m_splitter = nullptr; ///< outer: series panel | charts pane
    QTreeWidget *m_seriesTree = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_loadObsBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QScrollArea *m_chartsScroll = nullptr; ///< wraps the vertical splitter for >6 rows
    QSplitter *m_chartsSplitter = nullptr; ///< Slice AT.2: rows stack vertically here
    QSplitter *m_chartsOuter =
        nullptr; ///< Slice AT.3: outer splitter (charts | range slider | stats panel)
    RangeSliderWidget *m_rangeSlider = nullptr; ///< AT.3 — X-range slider under the chart rows
    QLabel *m_rangeLabel = nullptr;             ///< AT.3 polish — datetime label for slider window
    QWidget *m_sliderHost = nullptr;            ///< CP.1 — slider row container (toggle target)
    QWidget *m_leftHost = nullptr;              ///< CP.1 — series tree container (toggle target)
    StatsSummaryPanel *m_statsPanel = nullptr;  ///< AT.3 — stats tabs docked under range slider

    // Slice AT.2 — top toolbar
    QToolBar *m_toolBar = nullptr;
    QActionGroup *m_modeActions = nullptr;
    QAction *m_actSelect = nullptr;
    QAction *m_actPan = nullptr;
    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actFit = nullptr;
    QAction *m_actExport = nullptr;
    QAction *m_actExportData = nullptr;
    QAction *m_actAnimCursor = nullptr;
    QAction *m_actAddSystem = nullptr;
    QAction *m_actAddFromMap = nullptr;

    // CP.1 — View toggles. Each is checkable + persistent across the
    // session. Toggling "Charts Only" off snapshots the current visible
    // state so the previous user-set splitter sizes restore on toggle-on.
    QAction *m_actShowSeries = nullptr; ///< Show/hide left series panel
    QAction *m_actShowStats = nullptr;  ///< Show/hide bottom stats panel
    QAction *m_actShowSlider = nullptr; ///< Show/hide X-range slider row
    QAction *m_actChartsOnly = nullptr; ///< Convenience: all three off
    QAction *m_actShow1v1 = nullptr;    ///< Show/hide 1v1 scatter column
    QAction *m_actConfig1v1 = nullptr;  ///< Open the 1v1 pairs editor

    // Cached splitter sizes for restore when a pane is re-shown.
    QList<int> m_savedSplitterSizes;    ///< outer (series | charts)
    QList<int> m_savedChartsOuterSizes; ///< charts | slider | stats
    bool m_seriesPanelResized = false;

    // Per-row chart bookkeeping. Key = row index in model->rows().
    struct RowWidgets {
        openswmmvis::plot::ResultDescriptor descriptor;
        // Outer per-row frame placed into m_chartsSplitter.
        QWidget *rowFrame = nullptr;

        // Column 0 — time series.
        InteractiveChartView *view = nullptr;
        QChart *chart = nullptr;
        openswmmvis::plot::UtcTimeAxis *xAxis = nullptr;
        QValueAxis *yAxis = nullptr;
        QLineSeries *cursorLine = nullptr;
        QVector<QLineSeries *> series; ///< Parallel to model row's seriesIndices.
        QVector<int> consumed; ///< Parallel to series: periods already plotted (live tail cursor).

        // Column 1 — 1v1 scatter (visible only when ≥2 runs produce
        // baseline↔comparison pairs). InteractiveChartView so toolbar
        // modes + wheel zoom apply (COMPARISON_PLOT_1V1_AND_TREE_PLAN).
        InteractiveChartView *scatterView = nullptr;
        QChart *scatterChart = nullptr;
        QValueAxis *scatterXAxis = nullptr;
        QValueAxis *scatterYAxis = nullptr;
        QLineSeries *identityLine = nullptr; ///< 45° dashed.
        QVector<QScatterSeries *> scatterSeries;
    };
    QVector<RowWidgets> m_rowWidgets;

    QDateTime m_curAnimTime;  ///< Cached for cursor updates on row rebuilds.
    bool m_syncingX = false;  ///< Re-entry guard for linked X-axis sync.
    bool m_showCursor = true; ///< Animation-cursor master toggle.

    // Slice AT.3 — full X-range of all loaded data, captured at rebuildCharts;
    // used to map [0..1] from the range slider to absolute QDateTime bounds.
    QDateTime m_xFullMin;
    QDateTime m_xFullMax;
    bool m_syncingSlider = false; ///< Guard so axis→slider updates don't recurse.

    // AT.3 polish — cached Shift-drag X selection so Fit can refit Y to the
    // windowed data when a selection is active.
    QDateTime m_xSelLo;
    QDateTime m_xSelHi;

    // Saved 2D results load on a worker thread, one job at a time. The
    // adapter queues uncached series (Mesh2DRunLayer::setDeferFileReads);
    // the job owns its reader; completion is polled on the GUI thread and
    // rejected if the source changed. Charts stay on the GUI thread.
    struct ResultLoad;
    /**
     * @brief Coalesce saved-file job startup to one zero-delay GUI callback after chart/stat
     * requests have queued.
     */
    void scheduleResultLoading_();
    /**
     * @brief Start the first queued saved-2D extraction job when none is active.
     * Worker captures only shared job/cancel/progress state, writes results then release-publishes
     * completion. GUI starts a 100 ms poll timer; only one worker runs at a time.
     */
    void startResultLoading_();
    /**
     * @brief Update progress and acquire-check worker completion on the GUI thread.
     * Joins a completed thread, accepts extraction only when successful and not canceled (adapter
     * rejects stale source jobs), otherwise marks canceled. Rebuilds charts/stats to queue later
     * work; stops timer without active job.
     */
    void pollResultLoading_();
    /**
     * @brief Request cancellation of the active extraction and disable its button, or retry
     * canceled adapters when idle.
     * Cancellation is observed between frame reads; retry rebuilds charts/stats and queues another
     * job.
     */
    void onLoadButtonClicked_();
    /**
     * @brief Display active job request/frame progress, idle-canceled Retry state, or hide the load
     * strip.
     * Reads progress atomically; updates UI only on the GUI thread.
     */
    void updateLoadStrip_();
    /*! \brief Read saved files synchronously while off (explicit exports). */
    /**
     * @brief Set saved-file read deferral on every current mesh adapter.
     * @param[in] on True queues uncached series for workers, false allows synchronous reads used by
     * explicit export. Other adapters are untouched.
     */
    void setMeshFileDeferral_(bool on);
    std::shared_ptr<ResultLoad> m_load;
    std::thread m_loadThread;
    bool m_loadScheduled = false;
    QWidget *m_loadStrip = nullptr;
    QLabel *m_loadLabel = nullptr;
    QProgressBar *m_loadProgress = nullptr;
    QPushButton *m_loadButton = nullptr; ///< Cancel while loading; Retry after a cancel
    QTimer *m_loadTimer = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COMPARISONPLOTDIALOG_H
