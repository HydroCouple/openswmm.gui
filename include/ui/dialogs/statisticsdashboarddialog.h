/*!
 * \file   statisticsdashboarddialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BD — Statistics / frequency analysis report dashboard.
 *
 * Reads cumulative statistics from openswmm_statistics.h: per-node
 * max-depth / flood-volume / flood-time, per-link max-flow / max-velocity /
 * max-capacity, per-subcatch peak-runoff / total-runoff. Renders three
 * sortable tables (Node / Link / Subcatchment) + a frequency-analysis
 * histogram chart for the selected variable.
 *
 * Engine pre-reqs:
 *   - openswmm_stat_node_max_depth / max_overflow / vol_flooded / time_flooded
 *   - openswmm_stat_link_max_flow / max_velocity
 *   - openswmm_stat_subcatch_peak_runoff / total_runoff
 *   - For time-of-occurrence: stat_*_time_of_occurrence (gap-fill via Slice BA-01).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_STATISTICSDASHBOARDDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STATISTICSDASHBOARDDIALOG_H

#include <QDialog>
#include <QPointer>

class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTabWidget;
class QTableView;
class QStandardItemModel;
class QChartView;
class SWMMResultsLayer;

namespace openswmmvis::ui {

class StatisticsDashboardDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StatisticsDashboardDialog(SWMMResultsLayer *layer,
                                        QWidget *parent = nullptr);
    ~StatisticsDashboardDialog() override;

    void setResultsLayer(SWMMResultsLayer *layer);

private slots:
    void onTableSelectionChanged();
    void onExportClicked();
    void onQueryApplyClicked();
    void onQueryClearClicked();
    void onCurrentTabChanged(int index);

private:
    void buildUi();
    void populateNodeStats();
    void populateLinkStats();
    void populateSubcatchStats();
    void rebuildHistogramFor(int column);
    bool applyQueryToAllTables();
    void updateQueryStatus();
    QTableView *currentTable() const;
    QSortFilterProxyModel *currentProxy() const;

    QPointer<SWMMResultsLayer> m_layer;

    QTabWidget          *m_tabs = nullptr;
    QLineEdit           *m_queryEdit = nullptr;
    QPushButton         *m_queryApply = nullptr;
    QPushButton         *m_queryClear = nullptr;
    QLabel              *m_queryStatus = nullptr;
    QTableView          *m_nodeTable = nullptr;
    QTableView          *m_linkTable = nullptr;
    QTableView          *m_subTable  = nullptr;
    QStandardItemModel  *m_nodeModel = nullptr;
    QStandardItemModel  *m_linkModel = nullptr;
    QStandardItemModel  *m_subModel  = nullptr;
    QSortFilterProxyModel *m_nodeProxy = nullptr;
    QSortFilterProxyModel *m_linkProxy = nullptr;
    QSortFilterProxyModel *m_subProxy  = nullptr;
    QChartView          *m_histView  = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STATISTICSDASHBOARDDIALOG_H
