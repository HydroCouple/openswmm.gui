/*!
 * \file   rainfallvisualizationdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Modeless dialog visualizing and comparing every rain gage's
 *         rainfall series — inline [TIMESERIES], standard SWMM rain files
 *         and multi-column CSV/TSF files alike.
 *
 * Data comes from RainfallSeriesModel (engine-resolved, run-exact values).
 * Views:
 *   - "Overlay"  — the visible gages on one chart with a legend (with the
 *     Cumulative depth basis this IS the cumulative-curve comparison view);
 *   - "Per Gage" — stacked charts sharing a synced time axis, splitting the
 *     available height equally (scrolling only past the per-chart minimum);
 *   - a per-gage summary stats table (total depth, peak intensity, record
 *     interval, date range, gaps) that also surfaces failed/empty gages and
 *     carries the per-gage visibility checkboxes: only the focused / first
 *     gage plots by default, the rest opt in.
 * A basis selector converts each gage's series to Intensity /
 * Depth-per-interval / Cumulative depth for comparison.
 *
 * Launched (as a raise-or-create singleton parented to the main window)
 * from the Analysis menu, the object browser's Rain Gages context menu,
 * and the rain gage property editor's "Plot Rainfall…" button.
 */
#ifndef OPENSWMMVIS_UI_RAINFALLVISUALIZATIONDIALOG_H
#define OPENSWMMVIS_UI_RAINFALLVISUALIZATIONDIALOG_H

#include "plot/rainfallseriesmodel.h"

#include <QDialog>
#include <QPointer>
#include <QSet>
#include <QVector>

class QChart;
class QComboBox;
class QDateTimeAxis;
class QLabel;
class QSplitter;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;
class QTimer;
class QToolBar;
class QValueAxis;

class SWMMModelLayer;

namespace openswmmvis::ui {

class InteractiveChartView;
class ChartAxisFormatController;

class RainfallVisualizationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RainfallVisualizationDialog(SWMMModelLayer *layer,
                                         QWidget *parent = nullptr);
    ~RainfallVisualizationDialog() override;

    /*! \brief Re-bind to a different project's model layer (project-window
     *  switch on the singleton instance) and reload. */
    void setLayer(SWMMModelLayer *layer);

    /*! \brief Make \a gageId the only visible gage (context-menu / property
     *  panel launches). The rest stay listed in the stats table and can be
     *  toggled back on via its checkboxes. Unknown / empty ids are ignored. */
    void setFocusGage(const QString &gageId);

    /*! \brief Gage ids currently plotted (exposed for tests). */
    QSet<QString> visibleGages() const { return m_visibleGages; }

    /*! \brief Current display basis (exposed for tests). */
    openswmmvis::plot::RainfallSeriesModel::Basis basis() const;

    /*! \brief The data model (exposed for tests). */
    openswmmvis::plot::RainfallSeriesModel *model() { return &m_model; }

    /*! \brief Overlay chart (exposed for tests — charts live in the
     *  QGraphicsScene, outside the widget findChild tree). */
    QChart *overlayChart() const { return m_overlay.chart; }

    /*! \brief Per-gage panel charts, in gage order (tests). */
    QVector<QChart *> panelCharts() const
    {
        QVector<QChart *> out;
        for (const auto &p : m_panels) out.push_back(p.chart);
        return out;
    }

    /*! \brief Per-gage panel X axes, in gage order (tests). */
    QVector<QDateTimeAxis *> panelTimeAxes() const
    {
        QVector<QDateTimeAxis *> out;
        for (const auto &p : m_panels) out.push_back(p.axisX);
        return out;
    }

public slots:
    /*! \brief Reload gage data from the engine (re-reading FILE gages when
     *  the engine state allows) and rebuild every view. */
    void refresh();

private slots:
    void onBasisChanged_(int index);
    void onAutoRefresh_();
    void onStatsItemChanged_(QTableWidgetItem *item);

private:
    void buildUi_();
    void rebuildCharts_();
    void rebuildStatsTable_();
    void applyMode_();
    QString rainUnitLabel_() const;   ///< "in" or "mm" from FLOW_UNITS system.
    QString basisAxisTitle_() const;

    struct GagePanel {
        QChart                    *chart = nullptr;
        QDateTimeAxis             *axisX = nullptr;
        QValueAxis                *axisY = nullptr;
        InteractiveChartView      *view  = nullptr;
        ChartAxisFormatController *fmt   = nullptr;
    };

    QPointer<SWMMModelLayer>            m_layer;
    openswmmvis::plot::RainfallSeriesModel m_model;

    QToolBar     *m_toolbar    = nullptr;
    QComboBox    *m_basisCombo = nullptr;
    QTabWidget   *m_tabs       = nullptr;
    QSplitter    *m_mainSplit  = nullptr;
    QTableWidget *m_statsTable = nullptr;
    QLabel       *m_emptyLabel = nullptr;

    // Overlay tab.
    GagePanel     m_overlay;

    // Per-gage tab (rebuilt per refresh).
    QWidget      *m_panelsHost = nullptr;   ///< Scroll-area content widget.
    QVector<GagePanel> m_panels;
    bool          m_syncingX = false;       ///< Re-entry guard for X mirroring.

    /*! Gages currently plotted. Defaults to the focused / first gage only;
     *  the stats table's checkboxes toggle the rest on. */
    QSet<QString> m_visibleGages;

    QTimer       *m_refreshDebounce = nullptr;

    QAction *m_actSelect = nullptr, *m_actPan = nullptr,
            *m_actZoomIn = nullptr, *m_actZoomOut = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_RAINFALLVISUALIZATIONDIALOG_H
