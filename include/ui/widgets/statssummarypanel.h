/**
 * @file      statssummarypanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.3 — Statistics Summary Panel docked under the
 *         ComparisonPlotDialog chart splitter.
 *
 * QTabWidget with one tab per PlotAttribute row (matches the chart
 * rows). Each tab is a QTableWidget where rows are individual series
 * and columns are: count, mean, median, stddev, min, max, p05, p25,
 * p50, p75, p95, sum. When a baseline run is set, additional columns
 * appear: NSE / R² / RMSE / PBIAS against the per-series baseline.
 *
 * Default scope is the full series. Calling `setSelectionRange(lo, hi)`
 * narrows the stats to samples within that time window (inclusive).
 * Calling `setSelectionRange(invalid, invalid)` clears the selection.
 *
 * Column visibility is configurable via the per-tab table header's
 * context menu; preferences persist in QSettings under
 * `ComparisonPlotDialog/StatsColumns`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_STATSSUMMARYPANEL_H
#define OPENSWMMVIS_UI_WIDGETS_STATSSUMMARYPANEL_H

#include "plot/numberformat.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QWidget>

class QTabWidget;
class QTableWidget;

namespace openswmmvis::plot
{
class ComparisonPlotModel;
}

namespace openswmmvis::ui
{

class StatsSummaryPanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty statistics panel seeded from plot number-format preferences and persisted panel settings.
     * @param[in] parent Optional QWidget owner; statistics resolve synchronously on the GUI thread.
     */
    explicit StatsSummaryPanel(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned tab/table widgets without deleting the borrowed comparison model.
     */
    ~StatsSummaryPanel() override = default;

    /*! \brief Bind the panel to a model. Refreshes immediately. The
     *  panel listens for the model's structural signals so the user
     *  doesn't have to call refresh() after add/remove.
     * @param[in] model Borrowed comparison model tracked by QPointer; null unbinds and clears tabs. Repeated pointer is a no-op.
     * @note Replaces structural/baseline subscriptions. Changes to underlying sample data outside these signals require an explicit refresh().
     */
    void setModel(openswmmvis::plot::ComparisonPlotModel *model);

    /*! \brief Current number format used for all statistic value cells.
     * @return Current format value copied from this panel, independent of global changes after construction.
     */
    [[nodiscard]] openswmmvis::plot::NumberFormat statisticNumberFormat() const
    {
        return m_valueFormat;
    }

    /*! \brief Set the number format used for statistic value cells.
     * @param[in] format New value used without additional range validation. Immediately persists panel settings and rebuilds every statistics tab, even if unchanged.
     */
    void setStatisticNumberFormat(const openswmmvis::plot::NumberFormat &format);

public slots:
    /*! \brief Narrow the stats to samples whose timestamp falls in
     *  `[lo, hi]`. Pass invalid datetimes to clear the selection.
     * @param[in] lo Optional valid lower timestamp, inclusive; invalid removes only the lower bound.
     * @param[in] hi Optional valid upper timestamp, inclusive; invalid removes only the upper bound.
     * @note Bounds are stored as supplied without reordering; reversed valid bounds can yield empty statistics. Both invalid select the full series. Equal stored bounds are a no-op.
     */
    void setSelectionRange(QDateTime lo, QDateTime hi);

    /*! \brief Recompute all tabs from the current model + selection.
     *  Called automatically on model signals; also exposed for manual
     *  refresh after the user changes a series style etc.
     * @note Recreates all attribute/species tabs and synchronously resolves every plotted series. This is a display computation; no model edits occur.
     */
    void refresh();

private:
    /**
     * @brief Recreate per-descriptor tables from model rows and apply current formatting/column visibility.
     * @note Adds fit-metric columns only when a baseline exists and at least two run sources are present. Missing model clears visible tabs.
     */
    void rebuildTabs();
    /**
     * @brief Resolve one descriptor row, compute window-filtered summary statistics and match comparison samples to baseline times.
     * @param[in,out] table Required target table with columns preconfigured by rebuildTabs().
     * @param[in] rowIndex Valid zero-based model row.
     * @note Baseline identities combine object kind/name/mesh index. Fit pairing is ordered, within half the first baseline step, and requires finite paired values; one-sided windows test baseline timestamps. Unresolved series get an explanatory row, and unavailable fit values show an em dash.
     */
    void populateTab(QTableWidget *table, int rowIndex);
    /*! \brief Apply current column-visibility map to one table.
     * @param[in,out] table Optional table; null is ignored. Series stays visible and unrecognized column names default to visible.
     */
    void applyColumnVisibility(QTableWidget *table);
    /*! \brief Show the per-column visibility context menu at \p globalPos
     *  for a specific table. Persists toggle to QSettings.
     * @param[in,out] table Optional target table; null is a no-op.
     * @param[in] globalPos Global position for a blocking menu. Each accepted toggle persists immediately and updates every visible tab; Series is excluded.
     */
    void showHeaderContextMenu(QTableWidget *table, const QPoint &globalPos);
    /*! \brief Load column-visibility map from QSettings (one entry per
     *  column name; default: all visible). */
    void loadColumnVisibility();
    /*! \brief Persist column-visibility map to QSettings. */
    void saveColumnVisibility();
    /**
     * @brief Read panel-specific mode/precision/custom format from ComparisonPlotDialog/StatsFormat settings.
     * @note Invalid mode enums become Decimals; precision/custom strings are loaded without local range validation.
     */
    void loadNumberFormat();
    /**
     * @brief Persist this panel's current number-format fields to QSettings without explicitly checking synchronization errors.
     */
    void saveNumberFormat() const;
    /**
     * @brief Format one summary/fit value using the panel's configured statistic formatter.
     * @param[in] value Value in the statistic's source units (or dimensionless/percent metric); may be nonfinite.
     * @return formatStatisticValue() display text, including its unavailable/nonfinite convention.
     */
    [[nodiscard]] QString formatValue(double value) const;

    QTabWidget *m_tabs = nullptr;
    QPointer<openswmmvis::plot::ComparisonPlotModel> m_model;
    QDateTime m_selLo; ///< Invalid → use full series.
    QDateTime m_selHi;
    openswmmvis::plot::NumberFormat m_valueFormat;

    /*! \brief Column-name → visible? Map keyed by column header text
     *  ("count", "mean", "NSE", …). The "Series" column is always
     *  visible and not stored in the map. Persisted in QSettings
     *  under group `ComparisonPlotDialog/StatsColumns`. */
    QHash<QString, bool> m_columnVisible;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_STATSSUMMARYPANEL_H
