/*!
 * \file   scatterplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BG — Variable-correlation scatter plot.
 *
 * Pick an X and Y variable from any two (object, attribute) pairs in the
 * open .out; plot a scatter; overlay a linear regression line with R²
 * and slope/intercept printed in the title. Per-period pair samples,
 * timestamp-aligned. Useful for inspecting attribute correlations.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SCATTERPLOTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SCATTERPLOTDIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QChartView;
class SWMMResultsLayer;

namespace openswmmvis::ui {

class ChartAxisFormatController;

class ScatterPlotDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScatterPlotDialog(SWMMResultsLayer *layer, QWidget *parent = nullptr);
    ~ScatterPlotDialog() override;

private slots:
    void onAnyChange();

private:
    void buildUi();
    void populateObjectCombos(QComboBox *kindCombo, QComboBox *objCombo, QComboBox *varCombo);
    void replot();

    QPointer<SWMMResultsLayer> m_layer;

    QComboBox  *m_xKind = nullptr, *m_xObj = nullptr, *m_xVar = nullptr;
    QComboBox  *m_yKind = nullptr, *m_yObj = nullptr, *m_yVar = nullptr;
    QChartView *m_chart = nullptr;
    ChartAxisFormatController *m_axisFmt = nullptr;  ///< Per-chart axis number format.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SCATTERPLOTDIALOG_H
