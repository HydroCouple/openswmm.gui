/*!
 * \file   comparisonplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — Multi-pane Comparison Plot Dialog.
 *
 * Replaces the single-series TimeSeriesPlotDialog with a comparison /
 * analysis surface. Slice AT spec §4819-5093 of GUI_IMPLEMENTATION_PLAN.md.
 *
 * First cut (BL.4): Nx1 layout (column 0 = time series). The 1v1 scatter
 * column 1 from the full AT spec is deferred to a follow-up — the
 * `FitMetrics` infrastructure is already wired so its title bar lights
 * up automatically when the column ships.
 *
 * CF.3-ready: any IRunLayer (1D SwmmOutRunLayer or 2D SWMM2DResultsLayer)
 * can be added as a RunSource, and the dialog auto-spawns chart rows
 * keyed by PlotAttribute — including the five Mesh2D* enumerators.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_COMPARISONPLOTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_COMPARISONPLOTDIALOG_H

#include "plot/comparisonplotmodel.h"

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QString>

#include <memory>

class QAction;
class QActionGroup;
class QChartView;
class QLabel;
class QChart;
class QLineSeries;
class QScatterSeries;
class QDateTimeAxis;
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

namespace openswmmvis::ui {

class InteractiveChartView;
class RangeSliderWidget;
class StatsSummaryPanel;

class ComparisonPlotDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ComparisonPlotDialog(QWidget *parent = nullptr);
    ~ComparisonPlotDialog() override;

    /*! \brief Underlying model — exposed so callers (Object Browser wiring,
     *  CF.3's openComparisonPlotForCells) can add runs/series directly. */
    openswmmvis::plot::ComparisonPlotModel* model() noexcept { return m_model.get(); }

    /*! \brief Convenience helper: add a 1D RunSource for the given results
     *  layer (or update the existing one if already added) and return its
     *  run index. */
    int ensureRunSourceForLayer(::SWMMResultsLayer *layer,
                                bool makeBaseline = false);

    /*! \brief CF.3 — add a 2D RunSource for the given mesh results layer
     *  (or return the existing one) and return its run index. */
    int ensureRunSourceForMeshLayer(::SWMM2DResultsLayer *layer);

    /*! \brief Convenience helper: add a series in one call. Returns the new
     *  series index, or -1 on failure. */
    int addSeries(int runIndex,
                  const openswmmvis::plot::ObjectRef& ref,
                  openswmmvis::plot::PlotAttribute attr);

    /*! \brief CF.3 — add per-cell series for the supplied attribute list.
     *  Returns the count of series successfully added. */
    int addCellSeries(int runIndex,
                       const QVector<int>& triIdxList,
                       const QVector<openswmmvis::plot::PlotAttribute>& attributes);

    /*! \brief Slice AT.2 — programmatically set the Add-from-Map toolbar
     *  action's checked state. Used by SWMMVis when the user cancels the
     *  pick tool via Escape (so the toolbar button un-toggles). */
    void setAddFromMapChecked(bool checked);

signals:
    /*! \brief Slice AT.2 — emitted when the user toggles the
     *  "Add from Map…" toolbar action. The dialog itself doesn't know
     *  about the canvas; SWMMVis owns the MapToolPlotPick lifecycle. */
    void addFromMapToggled(bool active);

private slots:
    void onRunSourceAdded(int runIndex);
    void onRunSourceRemoved(int runIndex);
    void onSeriesAdded(int seriesIndex);
    void onSeriesRemoved(int seriesIndex);
    void onStyleChanged(int seriesIndex);
    void onRowsChanged();
    void onAnimationTimeChanged(QDateTime t);

    void onAddSeriesClicked();
    void onLoadObservedClicked();
    void onRemoveSelectedClicked();
    void onSeriesItemSelectionChanged();
    void onSeriesItemDoubleClicked(QTreeWidgetItem *item, int column);

    // Slice AT.3 — series-tree visibility + context menu.
    void onSeriesItemChanged(QTreeWidgetItem *item, int column);
    void onSeriesTreeContextMenu(const QPoint &pos);

    // Slice AT.2 — toolbar slots.
    void onModeActionTriggered();
    void onFitClicked();
    void onExportPngClicked();
    void onAnimationCursorToggled(bool checked);
    void onAddSystemSeriesClicked();

    // CP.1 — view-pane visibility toggles. Each persists splitter sizes
    // before hiding so the previous user-set width restores on re-show.
    void onShowSeriesToggled(bool show);
    void onShowStatsToggled(bool show);
    void onShowSliderToggled(bool show);
    void onChartsOnlyToggled(bool chartsOnly);

    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 4 — optional 1v1 column.
    void onShow1v1Toggled(bool show);

    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — pair configuration.
    void onConfigure1v1Clicked();

private:
    void buildUi();
    void buildToolBar();
    void rebuildSeriesTree();
    void rebuildCharts();
    /*! \brief Resolve a series' chart legend name: spec.legendOverride
     *  if non-empty, else the auto label "<run> — <object> (<attr>)". */
    QString legendNameFor(const openswmmvis::plot::SeriesSpec& spec) const;
    void updateChartForRow(int rowIndex);
    void applyAnimationCursorToCharts();
    /*! \brief Push the current toolbar Mode into every row's
     *  InteractiveChartView. Called whenever the mode action group changes. */
    void propagateModeToRows();
    /*! \brief Equalise splitter row sizes (called after add/remove). */
    void equaliseChartSplitterSizes();
    /*! \brief Hook up X-axis rangeChanged on a row's xAxis to mirror across rows. */
    void wireXAxisSync(int rowIndex);

    std::unique_ptr<openswmmvis::plot::ComparisonPlotModel> m_model;

    // UI
    QSplitter   *m_splitter         = nullptr;   ///< outer: series panel | charts pane
    QTreeWidget *m_seriesTree       = nullptr;
    QPushButton *m_addBtn           = nullptr;
    QPushButton *m_loadObsBtn       = nullptr;
    QPushButton *m_removeBtn        = nullptr;
    QScrollArea *m_chartsScroll     = nullptr;   ///< wraps the vertical splitter for >6 rows
    QSplitter   *m_chartsSplitter   = nullptr;   ///< Slice AT.2: rows stack vertically here
    QSplitter   *m_chartsOuter      = nullptr;   ///< Slice AT.3: outer splitter (charts | range slider | stats panel)
    RangeSliderWidget *m_rangeSlider = nullptr;  ///< AT.3 — X-range slider under the chart rows
    QLabel      *m_rangeLabel       = nullptr;   ///< AT.3 polish — datetime label for slider window
    QWidget     *m_sliderHost       = nullptr;   ///< CP.1 — slider row container (toggle target)
    QWidget     *m_leftHost         = nullptr;   ///< CP.1 — series tree container (toggle target)
    StatsSummaryPanel *m_statsPanel  = nullptr;  ///< AT.3 — stats tabs docked under range slider

    // Slice AT.2 — top toolbar
    QToolBar     *m_toolBar         = nullptr;
    QActionGroup *m_modeActions     = nullptr;
    QAction      *m_actSelect       = nullptr;
    QAction      *m_actPan          = nullptr;
    QAction      *m_actZoomIn       = nullptr;
    QAction      *m_actZoomOut      = nullptr;
    QAction      *m_actFit          = nullptr;
    QAction      *m_actExport       = nullptr;
    QAction      *m_actAnimCursor   = nullptr;
    QAction      *m_actAddSystem    = nullptr;
    QAction      *m_actAddFromMap   = nullptr;

    // CP.1 — View toggles. Each is checkable + persistent across the
    // session. Toggling "Charts Only" off snapshots the current visible
    // state so the previous user-set splitter sizes restore on toggle-on.
    QAction      *m_actShowSeries   = nullptr;  ///< Show/hide left series panel
    QAction      *m_actShowStats    = nullptr;  ///< Show/hide bottom stats panel
    QAction      *m_actShowSlider   = nullptr;  ///< Show/hide X-range slider row
    QAction      *m_actChartsOnly   = nullptr;  ///< Convenience: all three off
    QAction      *m_actShow1v1      = nullptr;  ///< Show/hide 1v1 scatter column
    QAction      *m_actConfig1v1    = nullptr;  ///< Open the 1v1 pairs editor

    // Cached splitter sizes for restore when a pane is re-shown.
    QList<int>    m_savedSplitterSizes;       ///< outer (series | charts)
    QList<int>    m_savedChartsOuterSizes;    ///< charts | slider | stats

    // Per-row chart bookkeeping. Key = row index in model->rows().
    struct RowWidgets {
        // Outer per-row frame placed into m_chartsSplitter.
        QWidget            *rowFrame  = nullptr;

        // Column 0 — time series.
        InteractiveChartView *view       = nullptr;
        QChart               *chart      = nullptr;
        QDateTimeAxis        *xAxis      = nullptr;
        QValueAxis           *yAxis      = nullptr;
        QLineSeries          *cursorLine = nullptr;
        QVector<QLineSeries*> series;     ///< Parallel to model row's seriesIndices.

        // Column 1 — 1v1 scatter (visible only when ≥2 runs produce
        // baseline↔comparison pairs). InteractiveChartView so toolbar
        // modes + wheel zoom apply (COMPARISON_PLOT_1V1_AND_TREE_PLAN).
        InteractiveChartView *scatterView  = nullptr;
        QChart             *scatterChart  = nullptr;
        QValueAxis         *scatterXAxis  = nullptr;
        QValueAxis         *scatterYAxis  = nullptr;
        QLineSeries        *identityLine  = nullptr;   ///< 45° dashed.
        QVector<QScatterSeries*> scatterSeries;
    };
    QVector<RowWidgets> m_rowWidgets;

    QDateTime m_curAnimTime;          ///< Cached for cursor updates on row rebuilds.
    bool      m_syncingX     = false; ///< Re-entry guard for linked X-axis sync.
    bool      m_showCursor   = true;  ///< Animation-cursor master toggle.

    // Slice AT.3 — full X-range of all loaded data, captured at rebuildCharts;
    // used to map [0..1] from the range slider to absolute QDateTime bounds.
    QDateTime m_xFullMin;
    QDateTime m_xFullMax;
    bool      m_syncingSlider = false; ///< Guard so axis→slider updates don't recurse.

    // AT.3 polish — cached Shift-drag X selection so Fit can refit Y to the
    // windowed data when a selection is active.
    QDateTime m_xSelLo;
    QDateTime m_xSelHi;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COMPARISONPLOTDIALOG_H
