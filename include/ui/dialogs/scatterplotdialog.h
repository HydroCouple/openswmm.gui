/**
 * @file      scatterplotdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BG — Variable-correlation scatter plot.
 *
 * Pick an X and Y variable from any two (object, attribute) pairs in the
 * open .out; plot a scatter; overlay a linear regression line with R²
 * and slope printed in the title. Samples pair by report index within
 * one output file. Useful for inspecting attribute correlations.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SCATTERPLOTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SCATTERPLOTDIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QChartView;
class SWMMResultsLayer;

namespace openswmmvis::ui
{

class ChartAxisFormatController;

class ScatterPlotDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] layer Optional borrowed guarded source whose readable output supplies BOTH axes;
     *   caller coordinates output lifetime on GUI thread.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build independent X/Y kind/object/variable selectors, populate them and attempt
     *   initial scatter/regression chart.
     * @note Reads whole output synchronously, no source mutation or live source-change
     *   subscription. Dependent variable combo signals are not blocked during repopulation and can
     *   reenter onAnyChange; no recursion guard is present. Does not retain two different run
     *   sources or align distinct time grids.
     */
    explicit ScatterPlotDialog(SWMMResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned widgets/current chart/controller without closing borrowed output or
     *   exporting/saving data.
     */
    ~ScatterPlotDialog() override;

private slots:
    /**
     * @brief Repopulate BOTH axis object/variable choices from current kind then replot.
     * @note This resets choices to first items on ANY picker edit, not only kind changes.
     *   Object-combo signals blocked temporarily, variable-combo changes remain connected and may
     *   reenter this method during clear/add; no guard/coalescing. No output mutation or
     *   user-selection preservation.
     */
    void onAnyChange();

private:
    /**
     * @brief Build owned three-kind X/Y selectors and antialiased chart view with per-chart
     *   axis-properties context menu and Close action.
     * @note Every picker currentIndexChanged connects to same full-reset handler. Context menu
     *   changes axis formatting only, Close accepts; no source-data reads until population/replot,
     *   save/undo or staged model edits.
     */
    void buildUi();
    /**
     * @param[in] kindCombo Required borrowed kind combo, zero Nodes/one Links/other Subcatchments.
     * @param[in] objCombo Required mutable object choice populated from current output
     *   IDs/zero-based indices; signals blocked then unconditionally unblocked.
     * @param[in] varCombo Required mutable variable choice reset by varsForKind WITHOUT signal
     *   blocking.
     * @brief For live source/output, replace object and fixed variable choices, discarding prior
     *   selection.
     * @note Null source/output leaves existing choices. No object existence checks beyond output
     *   enumeration, species variables, unit conversion or source writes; connected variable
     *   changes can trigger nested onAnyChange.
     */
    void populateObjectCombos(QComboBox *kindCombo, QComboBox *objCombo, QComboBox *varCombo);
    /**
     * @brief Read matching report-index X/Y series from one output and build scatter plus ordinary
     *   least-squares y=a+b*x.
     * @note Null source/handle, <=1 period or failed axis read leaves previous chart unchanged. Raw
     *   native output units, no timestamp query/interpolation/unit conversion or
     *   finite/missing-value filter. Slope uses zero fallback when centered X denominator
     *   magnitude<=1e-12; intercept=meanY-b*meanX, R2 zero when Y total variance nonpositive,
     *   otherwise 1-SSE/SST without clamping.
     * @note Title displays R2 and slope (not intercept); axis titles object.variable with no unit
     *   suffix. Scatter marker 4.5 logical pixels, range pads 5 percent only for finite varying
     *   data, dashed red regression line only with finite distinct X endpoints. Replace view chart
     *   and rebind persistent axis-format controller so user precision survives replots. No
     *   output/engine mutation or automatic export.
     */
    void replot();

    QPointer<SWMMResultsLayer> m_layer;

    QComboBox *m_xKind = nullptr, *m_xObj = nullptr, *m_xVar = nullptr;
    QComboBox *m_yKind = nullptr, *m_yObj = nullptr, *m_yVar = nullptr;
    QChartView *m_chart = nullptr;
    ChartAxisFormatController *m_axisFmt = nullptr; ///< Per-chart axis number format.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SCATTERPLOTDIALOG_H
