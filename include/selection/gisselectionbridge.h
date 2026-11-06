/**
 * @file      gisselectionbridge.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Two-way bridge between GIS vector layers' fid selections and the
 *         SelectionManager bus.
 *
 * SVBC round B. GISVectorLayer has carried a selectionChanged(QSet<qint64>)
 * signal since day one — with zero subscribers, which is why picking a
 * feature on the map never reached the Attribute Table. This is the GIS
 * analogue of SWMMVisProjectWindow's inline SWMM-layer bridge, as its own
 * small QObject so it is testable with a light MapCanvas fixture (the SWMM
 * bridge is reachable only through a full project window).
 *
 * Wiring, not abstraction: no virtual interface, GIS layers only.
 *  - layer → bus: any layer's selectionChanged rebuilds Feature refs from
 *    EVERY GIS layer on the canvas (a point click does not stop at the
 *    first layer, so per-layer Replace would drop another layer's picks)
 *    and Replace-selects them on the bus.
 *  - bus → layers: Feature refs are grouped by layerId and pushed with
 *    setSelectedFeatureIds per layer — an empty set clears, and the
 *    layer's own needs-rebuild + repaint plumbing lights the highlight.
 * Both directions run under one busy flag so nothing bounces.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SELECTION_GISSELECTIONBRIDGE_H
#define OPENSWMMVIS_SELECTION_GISSELECTIONBRIDGE_H

#include <QObject>
#include <QPointer>

class MapCanvas;
class GISVectorLayer;
class OpenSWMMVisLayer;
class SelectionManager;

class GisSelectionBridge : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Wire the GIS feature portion of project selection in both directions.
     * @param[in] selection Borrowed QPointer-guarded bus; null leaves the bridge inert.
     * @param[in] canvas Borrowed QPointer-guarded canvas; null leaves the bridge inert.
     * @param[in] parent Optional QObject owner.
     * @details Hooks already loaded GIS layers and future additions. Use on the GUI thread; no initial selection synchronization is performed until a change signal arrives.
     */
    GisSelectionBridge(SelectionManager *selection, MapCanvas *canvas, QObject *parent = nullptr);

private slots:
    /**
     * @brief Hook selection notifications on a newly added GIS vector layer.
     * @param[in] layer Borrowed layer; non-GIS/null input is ignored.
     */
    void onLayerAdded(OpenSWMMVisLayer *layer);
    /**
     * @brief Disconnect a removed GIS layer and rebuild surviving feature references.
     * @param[in] layer Removed layer, still alive during signal delivery; expected absent from the canvas layer list.
     * @details Ignores non-GIS/missing-bus/reentrant calls.
     */
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    /**
     * @brief Replace only bus Feature references from all remaining GIS layer selections.
     * @details Preserves every non-Feature reference; suppresses reciprocal handling with the shared busy guard.
     */
    void onLayerSelectionChanged();
    /**
     * @brief Apply decoded Feature refs to every GIS layer, clearing layers with no refs.
     * @details Malformed feature keys are ignored; non-Feature selections are untouched. Busy/missing-owner calls are no-ops.
     */
    void onBusSelectionChanged();

private:
    /**
     * @brief Connect one layer selection signal without duplicate connections.
     * @param[in] gis Non-null borrowed GIS vector layer on the canvas.
     */
    void hook(GISVectorLayer *gis);

    QPointer<SelectionManager> m_selection;
    QPointer<MapCanvas> m_canvas;
    bool m_busy = false;
};

#endif // OPENSWMMVIS_SELECTION_GISSELECTIONBRIDGE_H
