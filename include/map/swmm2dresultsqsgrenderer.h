/*!
 * \file   swmm2dresultsqsgrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * VS.8 — Qt Quick Scene Graph renderer for SWMM2DResultsLayer. The GPU
 * replacement for the QGraphicsItem QPainter passes (the "final
 * paint-replacement slice" promised in swmm2dresultslayer.h §S5.6).
 *
 * Rendering (node z-order, bottom → top):
 *   Pass 2 — filled contour bands (marching-triangles isobands or flat
 *            per-cell classification, per ContourBandStyle). This is now the
 *            depth fill; dry cells stay transparent so the SWMM2DMeshLayer
 *            terrain shows through.
 *   Pass 3 — isolines (thick-segment quads; separate node for index
 *            contours). Dash patterns are not supported on the GPU path —
 *            lines render solid.
 *   Pass 3b — isoline labels: rasterised textures placed along chained
 *            polylines every ~250 screen px, rotated to the line direction.
 *   Pass 4 — velocity-vector glyphs (per-glyph colour via
 *            VelocityVectorStyle::colorForSpeed); also convey flow direction.
 *   Pass 6 — cell-highlight overlay (translucent cyan fill + gold edges,
 *            matching the CPU CF.3 pass).
 *
 * 2026-06-21 — the Pass 1 Gouraud depth-fill (depth color ramp) and the
 * Pass 5 flow-direction arrows were removed (redundant with contour bands
 * and velocity vectors, respectively).
 *
 * Hosted as the "results2d" item in resources/qml/swmmlayer.qml, BELOW the
 * 1D SWMMLayerQSGRenderer so the network draws above the flood map.
 * MapCanvas drives it synchronously from paintEvent Layer 2b, exactly like
 * the 1D renderer (repaint() + grabFramebuffer()).
 */
#ifndef SWMM2DRESULTSQSGRENDERER_H
#define SWMM2DRESULTSQSGRENDERER_H

#include "map/mapextent.h"

#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QString>

#include <vector>

class QSGTexture;
class SWMM2DResultsLayer;

namespace OpenSWMM::Contour {
struct IsoBandPolygon;
struct IsoLineSegment;
}

class SWMM2DResultsQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    explicit SWMM2DResultsQSGRenderer(QQuickItem *parent = nullptr);
    ~SWMM2DResultsQSGRenderer() override;

    void setLayer(SWMM2DResultsLayer *layer);
    void setMapExtent(const MapExtent &extent);

    /*! Drop caches and mark content dirty — used by MapCanvas when the
     *  QSG/CPU ownership toggles so a stale frame is never composited. */
    void forceRebuild();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    QPointer<SWMM2DResultsLayer> m_layer;
    MapExtent                    m_extent;

    // Fixed scene-space anchor (bbox centre) — keeps float vertex coords
    // small even in UTM coordinates; stable across pans.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // Set on layer change, depth-frame change, style change, or zoom;
    // cleared after a full rebuild. Pure pan only updates the matrix.
    bool m_contentDirty = true;

    // Layer frame index last rebuilt into the QSG tree. Guards against the
    // currentTimeChanged → m_contentDirty signal being consumed by an
    // interleaved paint before the canvas grabs the framebuffer (which left
    // bands/fill a tick stale until a zoom re-dirtied the renderer).
    int  m_lastRenderedTime = -1;

    // ── Isoline-label texture cache ────────────────────────────────────
    // Keyed by "text|fontPt|halo|color" so style edits re-rasterise.
    // QSGTexture ownership is ours; released via deleteLater() (textures
    // must die on the render thread).
    mutable QHash<QString, QSGTexture *> m_labelTextureCache;
    void clearLabelTextureCache();

    // ── Per-frame contour geometry caches ──────────────────────────────
    // Marching output depends on the depth frame + classification params
    // but is invariant under pan/zoom, so ticks rebuild and interactions
    // reuse. Key: (time index, range, level params, triangle count).
    int     m_bandCacheTime  = -1;
    double  m_bandCacheLo    = 0.0;
    double  m_bandCacheHi    = 0.0;
    int     m_bandCacheCount = -1;
    size_t  m_bandCacheTris  = 0;
    quint64 m_bandCacheRev   = 0;   ///< Slice US.2 — ClassificationScheme::revision()
    std::vector<OpenSWMM::Contour::IsoBandPolygon> m_cachedBands;

    int     m_isoCacheTime   = -1;
    double  m_isoCacheLo     = 0.0;
    double  m_isoCacheHi     = 0.0;
    quint64 m_isoCacheParams = 0;   ///< hash of (mode, count, interval, base)
    size_t  m_isoCacheTris   = 0;
    std::vector<OpenSWMM::Contour::IsoLineSegment> m_cachedSegs;
};

#endif // SWMM2DRESULTSQSGRENDERER_H
