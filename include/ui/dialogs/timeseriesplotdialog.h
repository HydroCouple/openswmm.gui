/*!
 * \file   timeseriesplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Phase 5.2 (first cut) — single-object single-variable time series plot
 * for a SWMM .out file. Subsequent slices add multi-series, derived
 * difference series for two-session comparison, and the per-session
 * column from the broader Phase 5.2 plan.
 */
#ifndef TIMESERIESPLOTDIALOG_H
#define TIMESERIESPLOTDIALOG_H

#include "selection/selectionmanager.h"

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QChartView;

/*!
 * \class TimeSeriesPlotDialog
 * \brief Plots one variable for one object from an open .out file.
 *
 * The variable combo is populated based on the object's class:
 *  - Node:         depth / head / volume / inflow / overflow
 *  - Link:         flow / depth / velocity / volume / capacity
 *  - Subcatchment: rainfall / runoff / infiltration / evaporation
 *  - RainGage:     not applicable; dialog disabled
 */
class TimeSeriesPlotDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param outPath  Absolute path to the .out file to read.
     * \param obj      Object whose time series to plot. Must be valid.
     */
    explicit TimeSeriesPlotDialog(const QString &outPath,
                                  const SWMMObjectRef &obj,
                                  QWidget *parent = nullptr);
    ~TimeSeriesPlotDialog() override;

private slots:
    void onVariableChanged(int index);

private:
    void buildUi();
    void populateVariables();
    void plotSeries();

    QString               m_outPath;
    SWMMObjectRef         m_object;

    QLabel       *m_titleLabel = nullptr;
    QComboBox    *m_varCombo   = nullptr;
    QChartView   *m_chartView  = nullptr;
};

#endif // TIMESERIESPLOTDIALOG_H
