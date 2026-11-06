/**
 * @file      rainfallvisualizationdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Modeless dialog visualizing and comparing every rain gage's
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
namespace openswmmvis::plot
{
class UtcTimeAxis;
}
class QLabel;
class QSplitter;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;
class QTimer;
class QToolBar;
class QValueAxis;

class SWMMModelLayer;

namespace openswmmvis::ui
{

class InteractiveChartView;
class ChartAxisFormatController;

class RainfallVisualizationDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Build GUI-thread rainfall comparison views and synchronously load the selected model
     * engine.
     * @param[in,out] layer Nullable borrowed model layer, QPointer-guarded; its engine remains
     * borrowed by the owned RainfallSeriesModel and must remain valid during reload.
     * @param[in] parent Optional Qt owner.
     * Uses floating-panel/always-on-top policy and a 300 ms single-shot edit debounce. The opener
     * determines WA_DeleteOnClose; this constructor does not set it.
     */
    explicit RainfallVisualizationDialog(SWMMModelLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Destroy Qt-owned views, timer and the value-owned rainfall model without owning the
     * model layer/engine.
     */
    ~RainfallVisualizationDialog() override;

    /*! \brief Re-bind to a different project's model layer (project-window
     *  switch on the singleton instance) and reload. */
    /**
     * @brief Rebind the borrowed engine and reload all gages/views synchronously.
     * @param[in,out] layer Nullable borrowed model, tracked with QPointer. Disconnects the old
     * layer only when it differs; repeated binding to the same layer adds edit connections.
     * Model/options edits restart the 300 ms debounce. Layer ownership never transfers.
     */
    void setLayer(SWMMModelLayer *layer);

    /*! \brief Make \a gageId the only visible gage (context-menu / property
     *  panel launches). The rest stay listed in the stats table and can be
     *  toggled back on via its checkboxes. Unknown / empty ids are ignored. */
    /**
     * @brief Make a known gage the sole visible series and rebuild charts/table.
     * @param[in] gageId Exact nonempty gage ID; unknown/empty input leaves visibility unchanged,
     * including failed known gages.
     */
    void setFocusGage(const QString &gageId);

    /*! \brief Gage ids currently plotted (exposed for tests). */
    /**
     * @brief Copy the IDs selected for plotting, which can include known gages without data.
     * @return Owned set; modifications do not alter dialog visibility.
     */
    QSet<QString> visibleGages() const { return m_visibleGages; }

    /*! \brief Current display basis (exposed for tests). */
    /**
     * @brief Read the display basis stored in combo item data.
     * @return Current basis or Intensity before the combo exists.
     */
    openswmmvis::plot::RainfallSeriesModel::Basis basis() const;

    /*! \brief The data model (exposed for tests). */
    /**
     * @brief Borrow the dialog-owned rainfall model.
     * @return Non-null address valid until dialog destruction; callers must not delete it.
     */
    openswmmvis::plot::RainfallSeriesModel *model() { return &m_model; }

    /*! \brief Overlay chart (exposed for tests — charts live in the
     *  QGraphicsScene, outside the widget findChild tree). */
    /**
     * @brief Borrow the overlay chart hosted by its chart view/graphics scene.
     * @return Chart pointer, initially null before UI assembly; lifetime ends with the view/dialog.
     */
    QChart *overlayChart() const { return m_overlay.chart; }

    /*! \brief Per-gage panel charts, in gage order (tests). */
    /**
     * @brief Copy the current per-gage chart handles in visible data-bearing gage order.
     * @return Borrowed chart pointers; rebuildCharts_ replaces/defer-deletes their views, so
     * callers must not retain across refresh or delete them.
     */
    QVector<QChart *> panelCharts() const
    {
        QVector<QChart *> out;
        for (const auto &p : m_panels)
            out.push_back(p.chart);
        return out;
    }

    /*! \brief Per-gage panel X axes, in gage order (tests). */
    /**
     * @brief Copy the current per-gage UTC axis handles.
     * @return Borrowed chart-owned axes in panel order; invalid after panel rebuild/destruction.
     */
    QVector<openswmmvis::plot::UtcTimeAxis *> panelTimeAxes() const
    {
        QVector<openswmmvis::plot::UtcTimeAxis *> out;
        for (const auto &p : m_panels)
            out.push_back(p.axisX);
        return out;
    }

public slots:
    /*! \brief Reload gage data from the engine (re-reading FILE gages when
     *  the engine state allows) and rebuild every view. */
    /**
     * @brief Reload engine rainfall data requesting file reload where allowed, then rebuild
     * charts/table and empty state.
     * Retains known visible IDs; if none remain chooses the first gage with data, otherwise the
     * first listed gage. Updates inch/mm basis labels from FLOW_UNITS; may perform synchronous
     * file/engine work.
     */
    void refresh();

private slots:
    /**
     * @brief Rebuild plotted points/ranges using the current combo basis without reloading data.
     * @param[in] index Ignored notification index; basis() reads current item data.
     */
    void onBasisChanged_(int index);
    /**
     * @brief Reload views after the edit debounce fires, using the same synchronous refresh path.
     */
    void onAutoRefresh_();
    /**
     * @brief Apply a gage checkbox change and rebuild plots only when visibility differs.
     * @param[in] item Nullable borrowed stats item; ignores null/non-gage columns and unchanged
     * states. Exactly Checked means visible; gage ID comes from item text.
     */
    void onStatsItemChanged_(QTableWidgetItem *item);

private:
    /**
     * @brief Create owned toolbar, overlay/per-gage hosts, sortable stats table and Close controls
     * once.
     * Wires navigation modes, fit/reload and basis changes. Close is not a default button and
     * routes through close(); no layer mutation.
     */
    void buildUi_();
    /**
     * @brief Replace overlay series and per-gage views for visible gages with data.
     * Builds step points in the current basis with UTC epoch-millisecond X coordinates and
     * gage-index-stable styles; shared X extent spans visible data. Per-gage Y starts at zero with
     * 5 percent headroom or [0,1]. Old panel views deleteLater; panel X ranges mirror other panels
     * under a reentry guard (overlay is not wired into that mirroring). Applies current navigation
     * mode.
     */
    void rebuildCharts_();
    /**
     * @brief Recreate model-owned statistics cells/checks under blocked item signals and
     * temporarily disabled sorting.
     * Includes failed/empty gages, unit-labeled totals/peaks and numeric sort keys for
     * durations/times. Enables sorting afterward and resizes columns; does not change rainfall
     * data.
     */
    void rebuildStatsTable_();
    /**
     * @brief Apply the selected navigation action to overlay and all current panel views,
     * defaulting to Select.
     */
    void applyMode_();
    /**
     * @brief Resolve rainfall depth units from the model FLOW_UNITS option.
     * @return Translated mm for CMS/LPS/MLD, otherwise in, including null/unknown/default CFS.
     */
    QString rainUnitLabel_() const; ///< "in" or "mm" from FLOW_UNITS system.
    /**
     * @brief Format the Y title for the current basis and rainfall units.
     * @return Intensity in depth/hour, or interval/cumulative depth in in/mm.
     */
    QString basisAxisTitle_() const;

    struct GagePanel {
        QChart *chart = nullptr;
        openswmmvis::plot::UtcTimeAxis *axisX = nullptr;
        QValueAxis *axisY = nullptr;
        InteractiveChartView *view = nullptr;
        ChartAxisFormatController *fmt = nullptr;
    };

    QPointer<SWMMModelLayer> m_layer;
    openswmmvis::plot::RainfallSeriesModel m_model;

    QToolBar *m_toolbar = nullptr;
    QComboBox *m_basisCombo = nullptr;
    QTabWidget *m_tabs = nullptr;
    QSplitter *m_mainSplit = nullptr;
    QTableWidget *m_statsTable = nullptr;
    QLabel *m_emptyLabel = nullptr;

    // Overlay tab.
    GagePanel m_overlay;

    // Per-gage tab (rebuilt per refresh).
    QWidget *m_panelsHost = nullptr; ///< Scroll-area content widget.
    QVector<GagePanel> m_panels;
    bool m_syncingX = false; ///< Re-entry guard for X mirroring.

    /*! Gages currently plotted. Defaults to the focused / first gage only;
     *  the stats table's checkboxes toggle the rest on. */
    QSet<QString> m_visibleGages;

    QTimer *m_refreshDebounce = nullptr;

    QAction *m_actSelect = nullptr, *m_actPan = nullptr, *m_actZoomIn = nullptr,
            *m_actZoomOut = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_RAINFALLVISUALIZATIONDIALOG_H
