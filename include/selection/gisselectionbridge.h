/*!
 * \file   gisselectionbridge.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-way bridge between GIS vector layers' fid selections and the
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
    GisSelectionBridge(SelectionManager *selection, MapCanvas *canvas,
                       QObject *parent = nullptr);

private slots:
    void onLayerAdded(OpenSWMMVisLayer *layer);
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    void onLayerSelectionChanged();
    void onBusSelectionChanged();

private:
    void hook(GISVectorLayer *gis);

    QPointer<SelectionManager> m_selection;
    QPointer<MapCanvas>        m_canvas;
    bool                       m_busy = false;
};

#endif // OPENSWMMVIS_SELECTION_GISSELECTIONBRIDGE_H
