/*!
 * \file   statssummarypanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.3 — Statistics Summary Panel docked under the
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

namespace openswmmvis::plot { class ComparisonPlotModel; }

namespace openswmmvis::ui {

class StatsSummaryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StatsSummaryPanel(QWidget *parent = nullptr);
    ~StatsSummaryPanel() override = default;

    /*! \brief Bind the panel to a model. Refreshes immediately. The
     *  panel listens for the model's structural signals so the user
     *  doesn't have to call refresh() after add/remove. */
    void setModel(openswmmvis::plot::ComparisonPlotModel *model);

    /*! \brief Current number format used for all statistic value cells. */
    [[nodiscard]] openswmmvis::plot::NumberFormat statisticNumberFormat() const
    { return m_valueFormat; }

    /*! \brief Set the number format used for statistic value cells. */
    void setStatisticNumberFormat(const openswmmvis::plot::NumberFormat &format);

public slots:
    /*! \brief Narrow the stats to samples whose timestamp falls in
     *  `[lo, hi]`. Pass invalid datetimes to clear the selection. */
    void setSelectionRange(QDateTime lo, QDateTime hi);

    /*! \brief Recompute all tabs from the current model + selection.
     *  Called automatically on model signals; also exposed for manual
     *  refresh after the user changes a series style etc. */
    void refresh();

private:
    void rebuildTabs();
    void populateTab(QTableWidget *table, int rowIndex);
    /*! \brief Apply current column-visibility map to one table. */
    void applyColumnVisibility(QTableWidget *table);
    /*! \brief Show the per-column visibility context menu at \p globalPos
     *  for a specific table. Persists toggle to QSettings. */
    void showHeaderContextMenu(QTableWidget *table, const QPoint &globalPos);
    /*! \brief Load column-visibility map from QSettings (one entry per
     *  column name; default: all visible). */
    void loadColumnVisibility();
    /*! \brief Persist column-visibility map to QSettings. */
    void saveColumnVisibility();
    void loadNumberFormat();
    void saveNumberFormat() const;
    [[nodiscard]] QString formatValue(double value) const;

    QTabWidget *m_tabs = nullptr;
    QPointer<openswmmvis::plot::ComparisonPlotModel> m_model;
    QDateTime   m_selLo;          ///< Invalid → use full series.
    QDateTime   m_selHi;
    openswmmvis::plot::NumberFormat m_valueFormat;

    /*! \brief Column-name → visible? Map keyed by column header text
     *  ("count", "mean", "NSE", …). The "Series" column is always
     *  visible and not stored in the map. Persisted in QSettings
     *  under group `ComparisonPlotDialog/StatsColumns`. */
    QHash<QString, bool> m_columnVisible;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_STATSSUMMARYPANEL_H
