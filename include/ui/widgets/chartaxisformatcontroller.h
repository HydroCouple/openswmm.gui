/**
 * @file      chartaxisformatcontroller.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Per-chart axis number-format state + "Chart Properties…" launcher.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_CHARTAXISFORMATCONTROLLER_H
#define OPENSWMMVIS_UI_WIDGETS_CHARTAXISFORMATCONTROLLER_H

#include "plot/numberformat.h"

#include <QObject>
#include <QPointer>

class QChart;
class QWidget;

namespace openswmmvis::ui
{

class ChartAxisFormatController : public QObject
{
    Q_OBJECT
public:
    /*! \brief Construct bound to \p chart (may be null; bind later via
     *  setChart). X/Y formats are seeded from the global Preferences.
     * @param[in] chart Borrowed QChart tracked by QPointer; null permits later binding.
     * @param[in] parent Optional QObject owner. Initial preference formats are immediately applied on the GUI thread.
     */
    explicit ChartAxisFormatController(QChart *chart, QObject *parent = nullptr);

    /*! \brief Rebind to \p chart and re-apply the stored formats. Use when an
     *  editor rebuilds the whole chart/axes (e.g. the scatter plot).
     * @param[in] chart Borrowed new chart, including null; stored formats survive rebinding and are applied immediately.
     */
    void setChart(QChart *chart);

    /*! \brief Push the stored X/Y formats onto every QValueAxis of the bound
     *  chart. UtcTimeAxis is explicitly skipped; QDateTimeAxis is not a QValueAxis. Other QCategoryAxis subclasses still receive a numeric label format.
     * @note Null/destroyed chart is a no-op. Only axis label formats are changed; axis bounds/titles and global preferences are untouched.
     */
    void apply();

    /*! \brief Open the modeless ChartPropertiesDialog for the bound chart,
     *  seeded from the stored formats; format edits are written back here.
     * @param[in] parent Optional QWidget owner for the modeless properties dialog.
     * @note Missing chart is a no-op. The dialog owns the temporary ChartProperties adapter; format signals update this controller's state, and the dialog controls chart edits/acceptance semantics.
     */
    void openDialog(QWidget *parent);

private:
    QPointer<QChart> m_chart;
    openswmmvis::plot::NumberFormat m_x; ///< X axis format (persistent).
    openswmmvis::plot::NumberFormat m_y; ///< Y axis format (persistent).
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CHARTAXISFORMATCONTROLLER_H
