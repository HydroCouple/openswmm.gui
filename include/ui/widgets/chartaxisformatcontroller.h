/*!
 * \file   chartaxisformatcontroller.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-chart axis number-format state + "Chart Properties…" launcher.
 *
 * The editor charts (curve / pattern / timeseries / scatter / hydrograph /
 * transect) each own one of these. It holds the persistent X/Y axis
 * `NumberFormat` (decimals vs. significant figures, digit count, optional
 * custom printf override), seeded from the global PreferencesManager
 * defaults, and:
 *   - `apply()` pushes the current formats onto every QValueAxis of the
 *     bound chart (call it after a (re)build that recreates axes);
 *   - `setChart()` rebinds to a freshly-rebuilt chart and re-applies;
 *   - `openDialog()` opens the shared ChartPropertiesDialog seeded from the
 *     stored formats, writing any edits back here so they survive replots.
 *
 * Persisting the format on the controller (not on the transient
 * ChartProperties the dialog owns) is what lets a per-chart choice stick
 * across the editors' replot cycles.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_CHARTAXISFORMATCONTROLLER_H
#define OPENSWMMVIS_UI_WIDGETS_CHARTAXISFORMATCONTROLLER_H

#include "plot/numberformat.h"

#include <QObject>
#include <QPointer>

class QChart;
class QWidget;

namespace openswmmvis::ui {

class ChartAxisFormatController : public QObject
{
    Q_OBJECT
public:
    /*! \brief Construct bound to \p chart (may be null; bind later via
     *  setChart). X/Y formats are seeded from the global Preferences. */
    explicit ChartAxisFormatController(QChart *chart, QObject *parent = nullptr);

    /*! \brief Rebind to \p chart and re-apply the stored formats. Use when an
     *  editor rebuilds the whole chart/axes (e.g. the scatter plot). */
    void setChart(QChart *chart);

    /*! \brief Push the stored X/Y formats onto every QValueAxis of the bound
     *  chart. Time/category axes are left untouched. */
    void apply();

    /*! \brief Open the modeless ChartPropertiesDialog for the bound chart,
     *  seeded from the stored formats; format edits are written back here. */
    void openDialog(QWidget *parent);

private:
    QPointer<QChart>              m_chart;
    openswmmvis::plot::NumberFormat m_x;   ///< X axis format (persistent).
    openswmmvis::plot::NumberFormat m_y;   ///< Y axis format (persistent).
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CHARTAXISFORMATCONTROLLER_H
