/*!
 * \file   timeseriesplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Multi-series time-series plot over one SWMM .out file. Each row in the
 * left-hand series list pairs an object + variable; selecting a row binds
 * its `SeriesStyleObject` to the right-hand QPropertyModel-backed editor
 * for per-series styling (pen / marker / labels / area / opacity / name)
 * that live-updates the chart on every edit.
 *
 * The original (single-object, single-variable) constructor is preserved
 * for back-compatibility: it auto-seeds one series from the constructor
 * arguments. Hosts can call `addSeries(...)` afterwards to stack more.
 */
#ifndef TIMESERIESPLOTDIALOG_H
#define TIMESERIESPLOTDIALOG_H

#include "plot/seriesstyle.h"
#include "selection/selectionmanager.h"

#include <QDialog>
#include <QString>
#include <QVector>

#include <memory>

class QChart;
class QChartView;
class QComboBox;
class QLabel;
class QLineSeries;
class QListView;
class QPushButton;
class QStandardItemModel;
class QValueAxis;
class QSplitter;

namespace openswmmvis::plot { class SeriesStyleObject; }
namespace openswmmvis::ui   { class SeriesStyleEditor; }

/*!
 * \class TimeSeriesPlotDialog
 * \brief Plots one or more (object, variable) pairs read from a .out file.
 *
 * Variable codes per object class:
 *  - Node:         depth / head / volume / lateral inflow / total inflow / overflow
 *  - Link:         flow / depth / velocity / volume / capacity
 *  - Subcatchment: rainfall / snow depth / evaporation / infiltration / runoff
 *  - RainGage:     not applicable; "Add Series" filters those out
 */
class TimeSeriesPlotDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Seed the dialog with a single (object, variable=default) series. */
    explicit TimeSeriesPlotDialog(const QString &outPath,
                                  const SWMMObjectRef &obj,
                                  QWidget *parent = nullptr);
    ~TimeSeriesPlotDialog() override;

    /*! \brief Append a series. Variable code is the SWMM_OUT_*_* index for the
     *  object's class. Returns the new series index, or -1 on failure (e.g.
     *  object not found in the .out, or variable code out of range). */
    int addSeries(const SWMMObjectRef &obj, int variableCode);

    int  seriesCount() const noexcept { return m_entries.size(); }
    void removeSeries(int index);

private slots:
    void onAddSeriesClicked();
    void onRemoveSeriesClicked();
    void onSeriesSelectionChanged();
    void onSeriesStyleChanged(const openswmmvis::plot::SeriesStyle& style);

private:
    struct Entry {
        SWMMObjectRef                                       object;
        int                                                 variableCode = 0;
        QString                                             variableLabel;
        openswmmvis::plot::SeriesStyleObject               *styleObject  = nullptr;
        QLineSeries                                        *line         = nullptr;
    };

    void buildUi();
    void rebuildChart();
    void rebuildSeriesListModel();
    void bindEditorToSelection();
    QString defaultLegendFor(const Entry& e) const;

    QString               m_outPath;

    QVector<Entry>        m_entries;
    int                   m_selectedIndex = -1;

    // Left pane (series list)
    QListView            *m_seriesList    = nullptr;
    QStandardItemModel   *m_seriesModel   = nullptr;
    QPushButton          *m_addBtn        = nullptr;
    QPushButton          *m_removeBtn     = nullptr;

    // Centre pane (chart)
    QLabel               *m_titleLabel    = nullptr;
    QChartView           *m_chartView     = nullptr;
    QChart               *m_chart         = nullptr;
    QValueAxis           *m_xAxis         = nullptr;
    QValueAxis           *m_yAxis         = nullptr;

    // Right pane (style editor bound to selection)
    openswmmvis::ui::SeriesStyleEditor *m_styleEditor = nullptr;

    QSplitter            *m_splitter      = nullptr;
};

#endif // TIMESERIESPLOTDIALOG_H
