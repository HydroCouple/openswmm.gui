/*!
 * \file   gisselectionbridge.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "selection/gisselectionbridge.h"

#include "layers/gisobjectref.h"
#include "layers/gisvectorlayer.h"
#include "map/mapcanvas.h"
#include "selection/selectionmanager.h"

#include <QHash>
#include <QSet>

GisSelectionBridge::GisSelectionBridge(SelectionManager *selection,
                                       MapCanvas *canvas, QObject *parent)
    : QObject(parent), m_selection(selection), m_canvas(canvas)
{
    if (!m_selection || !m_canvas) return;

    connect(m_canvas, &MapCanvas::layerAdded,
            this, &GisSelectionBridge::onLayerAdded);
    connect(m_canvas, &MapCanvas::layerRemoved,
            this, &GisSelectionBridge::onLayerRemoved);
    connect(m_selection, &SelectionManager::selectionChanged,
            this, &GisSelectionBridge::onBusSelectionChanged);

    // Layers loaded before the bridge existed (project restore order).
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *gis = qobject_cast<GISVectorLayer *>(l))
            hook(gis);
}

void GisSelectionBridge::hook(GISVectorLayer *gis)
{
    connect(gis, &GISVectorLayer::selectionChanged,
            this, &GisSelectionBridge::onLayerSelectionChanged,
            Qt::UniqueConnection);
}

void GisSelectionBridge::onLayerAdded(OpenSWMMVisLayer *layer)
{
    if (auto *gis = qobject_cast<GISVectorLayer *>(layer))
        hook(gis);
}

void GisSelectionBridge::onLayerRemoved(OpenSWMMVisLayer *layer)
{
    // A removed layer's refs must not linger on the bus, or the table's
    // show-selected-only filter and other views keep phantom features.
    auto *gis = qobject_cast<GISVectorLayer *>(layer);
    if (!gis || !m_selection || m_busy) return;
    disconnect(gis, nullptr, this, nullptr);
    onLayerSelectionChanged();   // rebuild from the remaining layers
}

void GisSelectionBridge::onLayerSelectionChanged()
{
    if (m_busy || !m_selection || !m_canvas) return;
    m_busy = true;

    QSet<SWMMObjectRef> refs;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *gis = qobject_cast<GISVectorLayer *>(l);
        if (!gis) continue;
        const QString id = gis->layerId();
        for (long long fid : gis->selectedFeatureIds())
            refs.insert(GisObjectRef::feature(id, fid));
    }

    // Replace ONLY the Feature portion of the bus: keep every non-Feature
    // ref so a feature pick coexists with, rather than clobbers, whatever
    // SWMM/mesh selection another view holds — and vice versa the SWMM
    // bridge's Replace happens at the layer level, so this mirrors it.
    QSet<SWMMObjectRef> keep;
    for (const SWMMObjectRef &r : m_selection->selection())
        if (r.objectType != SWMMObjectRef::Feature) keep.insert(r);
    m_selection->select(keep + refs, SelectionManager::Replace);

    m_busy = false;
}

void GisSelectionBridge::onBusSelectionChanged()
{
    if (m_busy || !m_selection || !m_canvas) return;
    m_busy = true;

    QHash<QString, QSet<long long>> byLayer;
    for (const SWMMObjectRef &r : m_selection->selection()) {
        QString layerId;
        long long fid = -1;
        if (GisObjectRef::parseFeature(r, &layerId, &fid))
            byLayer[layerId].insert(fid);
    }

    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *gis = qobject_cast<GISVectorLayer *>(l);
        if (!gis) continue;
        const QSet<long long> want = byLayer.value(gis->layerId());
        if (gis->selectedFeatureIds() != want)
            gis->setSelectedFeatureIds(want);
    }

    m_busy = false;
}
