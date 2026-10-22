/**
 * @file      statisticsdashboarddialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BD — Statistics / frequency analysis report dashboard.
 *
 * Computes per-object extrema and sample sums over the full saved-output period,
 * displaying three sortable, queryable read-only tables. Native output values are
 * rounded for display/sorting; sample sums are not report-step integrals. A hidden
 * off-layout chart helper can build a 20-bin histogram of visible rows.
 * No cumulative-statistics engine API or time-of-occurrence lookup is used here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_STATISTICSDASHBOARDDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STATISTICSDASHBOARDDIALOG_H

#include "selection/selectionmanager.h"

#include <QDialog>
#include <QPointer>
#include <QSet>

class QLabel;
class QLineEdit;
class QPoint;
class QPushButton;
class QSortFilterProxyModel;
class QTabWidget;
class QTableView;
class QStandardItemModel;
class QChartView;
class MapCanvas;
class SWMMModelLayer;
class SWMMResultsLayer;

namespace openswmmvis::ui
{

class StatisticsDashboardDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] layer Optional borrowed QObject results layer, guarded by QPointer; output handle
     *   accessed only synchronously.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build read-only tables and load whole saved-output period into snapshot statistics;
     *   null/deleted layer leaves empty data. No live output-change subscription. GUI thread only.
     */
    explicit StatisticsDashboardDialog(SWMMResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Release owned table/query/chart controls; does not close results or project resources.
     */
    ~StatisticsDashboardDialog() override;

    /**
     * @param[in] layer Optional borrowed guarded results source; must remain accessible throughout
     *   synchronous output calls.
     * @brief Replace source, clear/refill all tables and reapply query text if valid; no engine
     *   cumulative-statistics calls.
     * @note Source deletion clears QPointer but leaves already copied rows. Does not explicitly
     *   refresh histogram or restore selection after rebuild; invalid query retains prior
     *   predicates.
     */
    void setResultsLayer(SWMMResultsLayer *layer);

    /*! \brief Bind the dialog to the project's cross-view selection bus so
     *         table rows highlight on the map, and to the canvas + model layer
     *         so the context menu can zoom to the selected features.
     * @param[in] layer Optional guarded model for resolving result names to geometry; should match
     *   results project.
     * @param[in] selMgr Optional guarded selection bus; disconnect all prior bus-to-dialog
     *   connections before replacement.
     * @param[in] canvas Optional guarded map for zoom and coordinate transform; no ownership
     *   transfer.
     * @note Immediately mirrors new bus selection into all visible table rows. Null bus leaves
     *   current table selection. Does not validate model/result identity or subscribe geometry
     *   changes.
 */
    void setProject(SWMMModelLayer *layer, SelectionManager *selMgr, MapCanvas *canvas);

private slots:
    /**
     * @brief Refresh active histogram, then replace selection bus from active sender table selected
     *   row names and active object type.
     * @note Ignores background-table sender, missing bus and guarded echoes; even empty table
     *   selection replaces whole bus with empty set. Names matched literally, no engine resolution.
     */
    void onTableSelectionChanged();
    /**
     * @param[in] current Borrowed complete bus selection; matching object type/name selects rows in
     *   each proxy.
     * @param[in] added Ignored incremental additions; whole current set is authoritative.
     * @param[in] removed Ignored incremental removals.
     * @brief Clear/mirror selection across three tables under echo guard; only currently visible
     *   filtered names can be selected. Does not switch tab, select filtered-out rows or query
     *   geometry.
     */
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    /**
     * @param[in] pos Viewport-local menu position of sender QTableView.
     * @brief Offer Zoom to Selected when sender selection and guarded project/canvas exist. Zoom
     *   handler uses active table, not separately retained sender.
     */
    void onTableContextMenuRequested(const QPoint &pos);
    /**
     * @brief Resolve active selected names to model extents, union and transform to canvas CRS,
     *   then set padded view.
     * @note Skip empty names/nonfinite x bounds; no valid extent means no-op. Point buffer is
     *   max(25, 0.5 percent of project span), or 100 without valid project extent, in canvas units;
     *   nonpoint adds 10 percent per axis with 1e-6 floor. No source edits.
     */
    void onZoomToSelected();
    /**
     * @brief Ask destination then truncate/write visible sorted rows of ALL three proxies as
     *   sectioned CSV.
     * @note Uses displayed rounded values and translated headers, no quoting/escaping, unit
     *   conversion or atomic save; cancel/open failure silently returns and later write errors are
     *   not checked. No accepted-result flag or source writes.
     */
    void onExportClicked();
    /**
     * @brief Parse current shared WHERE-style expression, apply only if valid and update active
     *   matched-row count. Invalid input retains prior filters.
     */
    void onQueryApplyClicked();
    /**
     * @brief Clear query/style, install invalid-empty predicate (accept all rows), update active
     *   count; absent query control no-op.
     */
    void onQueryClearClicked();
    /**
     * @param[in] index Ignored signal index; helpers read current widget directly.
     * @brief Update active count/histogram without changing global bus selection. Can replace a
     *   displayed query error with row-count text.
     */
    void onCurrentTabChanged(int index);

private:
    /**
     * @brief Create owned sorted numeric read-only tables, one shared query, export and cross-view
     *   selection wiring.
     * @note Histogram chart exists as hidden off-layout helper; no visible frequency panel despite
     *   class overview. UI headers omit units; numbers retain output-file native units.
     */
    void buildUi();
    /*! \brief Object type carried by the rows of the active tab.
     * @return Node/Link/Subcatchment for active tab 0/1/2; Unknown for absent/unrecognized tab.
 */
    SWMMObjectRef::ObjectType currentObjectType() const;
    /**
     * @brief Clear table and compute node maxima of depth/head/overflow and SUM of stored volume
     *   samples (not flooded volume or time integral). Failed series leaves -1e30 maximum or zero
     *   sum; maxima quantized to three decimals, volume sum to one.
     * @note Uses every saved period of current output handle, native file units; absent
     *   handle/nonpositive counts yields no rows. No finite-sample filtering, cumulative-statistics
     *   query or failure dialog. Numeric EditRole is parsed back from formatted text, so
     *   sorting/query/export use rounded values.
     */
    void populateNodeStats();
    /**
     * @brief Clear table and compute link maxima of signed flow, depth, velocity and capacity ratio
     *   (not absolute magnitude). Failed series leaves -1e30; values quantized to three decimals.
     * @note Uses every saved period of current output handle, native file units; absent
     *   handle/nonpositive counts yields no rows. No finite-sample filtering, cumulative-statistics
     *   query or failure dialog. Numeric EditRole is parsed back from formatted text, so
     *   sorting/query/export use rounded values.
     */
    void populateLinkStats();
    /**
     * @brief Clear table and compute subcatchment peak runoff and sample SUMS of
     *   runoff/infiltration/evaporation (no report-step integration or depth/volume conversion).
     *   Failed runoff leaves -1e30 peak and zero sum; other failed sums zero. Values quantized to
     *   three decimals.
     * @note Uses every saved period of current output handle, native file units; absent
     *   handle/nonpositive counts yields no rows. No finite-sample filtering, cumulative-statistics
     *   query or failure dialog. Numeric EditRole is parsed back from formatted text, so
     *   sorting/query/export use rounded values.
     */
    void populateSubcatchStats();
    /**
     * @param[in] column Active proxy column, intended numeric column greater than zero; caller must
     *   supply valid index.
     * @brief Count finite values from ALL visible filtered rows (not only selected rows) into 20
     *   bins using floor(normalized*19), clamped to 0..19.
     * @note Fewer than two values/equal range installs empty chart. Last bin generally holds
     *   maximum only, not 20 equal-width intervals. Chart view takes new chart; this method does
     *   not explicitly dispose previously released chart. Hidden helper remains hidden.
     */
    void rebuildHistogramFor(int column);
    /**
     * @brief Rebuild hidden histogram for first selected cell of active table when its column is
     *   nonzero; absent selection or ID column leaves prior chart unchanged. Row-selection usually
     *   makes first selected cell ID.
     */
    void refreshHistogramForSelection();
    /**
     * @return True when current trimmed query is empty or parses validly and predicate is copied to
     *   all proxies; false for missing edit or invalid nonempty query.
     * @note Invalid query sets error style/column/message and preserves all prior predicates. Field
     *   lookup uses translated column headers plus Name alias for column zero; no per-table field
     *   validation here, no source edits.
     */
    bool applyQueryToAllTables();
    /**
     * @brief Display active source total or matched/total when edit text nonempty, regardless
     *   whether that text is last successfully applied predicate. Null status no-op.
     */
    void updateQueryStatus();
    /**
     * @return Borrowed active QTableView, null for missing tabs/non-table widget; caller must not
     *   delete.
     */
    QTableView *currentTable() const;
    /**
     * @return Borrowed owned proxy for tab 0/1/2, null for missing/unrecognized tab; no model
     *   rebuild.
     */
    QSortFilterProxyModel *currentProxy() const;

    QPointer<SWMMResultsLayer> m_layer;
    QPointer<SWMMModelLayer> m_modelLayer;
    QPointer<SelectionManager> m_selMgr;
    QPointer<MapCanvas> m_canvas;
    bool m_applyingFromBus = false; ///< Reentrancy guard, mirrors AttributeTablePanel.

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_queryEdit = nullptr;
    QPushButton *m_queryApply = nullptr;
    QPushButton *m_queryClear = nullptr;
    QLabel *m_queryStatus = nullptr;
    QTableView *m_nodeTable = nullptr;
    QTableView *m_linkTable = nullptr;
    QTableView *m_subTable = nullptr;
    QStandardItemModel *m_nodeModel = nullptr;
    QStandardItemModel *m_linkModel = nullptr;
    QStandardItemModel *m_subModel = nullptr;
    QSortFilterProxyModel *m_nodeProxy = nullptr;
    QSortFilterProxyModel *m_linkProxy = nullptr;
    QSortFilterProxyModel *m_subProxy = nullptr;
    QChartView *m_histView = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STATISTICSDASHBOARDDIALOG_H
