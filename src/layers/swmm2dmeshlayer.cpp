/*!
 * \file   swmm2dmeshlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Rendering uses SWMM2DMeshGraphicsItem — a QGraphicsItem that paints
 * the triangulated mesh via QPainter, exactly like SWMMLayerItem does for
 * the SWMM network.  rebuildSceneGeometry() populates the scene-space caches
 * (m_sceneTris / m_sceneEdges / m_sceneNodes); the graphics item reads them
 * and calls update() on itself whenever repaintRequested() fires.
 */
#include "layers/swmm2dmeshlayer.h"

#include "mesh/meshcellstats.h"
#include "mesh/meshobjectref.h"

#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include "render/ifeaturerenderer.h"
// Contour generation for the iso-band / iso-line sublayer passes.
#include "contour/marchingtriangles.h"
// Slice B.5b — Rule Model mirror for 2D mesh layers.
#include "render/rastersymbollayers.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/symbollayer.h"
#include "render/symbolstyleadapter.h"
#include "render/symbolstyle.h"
// Slice Z.14-paint — polygon clip mask.
#include "render/maskclipresolver.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHash>
#include <QLocale>
#include <QPainter>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// Elevation colour ramp — same 5-stop ramp as SWMM2DMeshQSGRenderer
// ---------------------------------------------------------------------------
namespace {

void elevationColorRgb(double t, int &r, int &g, int &b)
{
    struct Stop { double t; int r, g, b; };
    static const Stop stops[] = {
        {0.00, 0x1a, 0x3d, 0x6b},
        {0.20, 0x2e, 0x8b, 0x57},
        {0.50, 0xc8, 0xd9, 0x4e},
        {0.75, 0xc8, 0xa0, 0x00},
        {1.00, 0xf0, 0xf0, 0xe8},
    };
    constexpr int N = int(sizeof(stops)/sizeof(*stops));
    t = qBound(0.0, t, 1.0);
    int i = 0;
    while (i < N - 2 && stops[i+1].t <= t) ++i;
    const Stop &lo = stops[i], &hi = stops[i+1];
    const double f = (hi.t > lo.t) ? (t - lo.t)/(hi.t - lo.t) : 0.0;
    r = qRound(lo.r + f*(hi.r - lo.r));
    g = qRound(lo.g + f*(hi.g - lo.g));
    b = qRound(lo.b + f*(hi.b - lo.b));
}

} // namespace

// ---------------------------------------------------------------------------
// SWMM2DMeshGraphicsItem
// ---------------------------------------------------------------------------

class SWMM2DMeshGraphicsItem : public QGraphicsItem
{
public:
    explicit SWMM2DMeshGraphicsItem(SWMM2DMeshLayer *layer, QGraphicsItem *parent = nullptr)
        : QGraphicsItem(parent), m_layer(layer)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        setCacheMode(QGraphicsItem::NoCache);
        setZValue(layer->layerZValue());
    }

    void geometryChanged() { prepareGeometryChange(); update(); }

    QRectF boundingRect() const override
    {
        return m_layer->m_sceneBBox;
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        // Mesh Tiled LOD plan P1.1 — while SWMM2DMeshQSGRenderer owns this
        // layer, the CPU path must not double-paint (mirrors
        // SWMM2DResultsLayer's gate).
        if (m_layer->qsgOwnsRendering()) return;
        if (!m_layer->isVisible()) return;

        const auto &tris  = m_layer->m_sceneTris;
        const auto &edges = m_layer->m_sceneEdges;

        if (tris.isEmpty()) return;

        const QRectF exposed = option->exposedRect;
        const bool hasElev   = (m_layer->m_zMax > m_layer->m_zMin);
        const double zMin    = m_layer->m_zMin;
        const double zMax    = m_layer->m_zMax;
        const double zRange  = zMax - zMin;
        const bool active    = m_layer->isActiveMesh();
        const int  fillAlpha = active ? 160 : 110;

        // Per-sublayer visibility + opacity. This is the fallback render
        // path (active when the QSG renderer does not own the layer — see
        // the qsgOwnsRendering gate above), so the layer-tree checkboxes and
        // opacity edits flow through these sublayer flags into the passes
        // below. fill + edges default on; nodes / bands / isolines default
        // off (see SWMM2DMeshLayer ctor). A null sublayer falls back to the
        // historic "always visible" behaviour for fill/edges.
        const auto *fillSub = m_layer->meshFillSublayer();
        const auto *edgeSub = m_layer->meshEdgeSublayer();
        const auto *nodeSub = m_layer->meshNodeSublayer();
        const auto *bandSub = m_layer->contourBandSublayer();
        const auto *isoSub  = m_layer->isolineSublayer();

        const bool  fillVisible  = !fillSub || fillSub->isVisible();
        // Zoom gates matching the QSG LOD policy (kEdgeMinCellAreaPx /
        // kMarkerMinCellAreaPx): wireframe and vertex dots appear
        // automatically once cells project large enough on screen, and stay
        // out of the way (sub-pixel noise / a dark wash) below that.
        const double kPaintPx  = p->worldTransform().m11();
        const double kCellAreaPx =
            (!m_layer->m_sceneTris.isEmpty() && !m_layer->m_sceneBBox.isNull())
                ? (m_layer->m_sceneBBox.width() * m_layer->m_sceneBBox.height()
                   / double(m_layer->m_sceneTris.size()))
                      * kPaintPx * kPaintPx
                : 0.0;
        const bool  edgesVisible = (!edgeSub || edgeSub->isVisible())
                                   && kCellAreaPx >= m_layer->edgeMinCellAreaPx();
        const bool  nodesVisible = nodeSub && nodeSub->isVisible()
                                   && kCellAreaPx >= m_layer->vertexMinCellAreaPx();
        const bool  bandsVisible = bandSub && bandSub->isVisible();
        const bool  isoVisible   = isoSub  && isoSub->isVisible();

        const qreal fillOpacity = fillSub ? fillSub->opacity() : 1.0;
        const qreal edgeOpacity = edgeSub ? edgeSub->opacity() : 1.0;

        // Multiply a colour's alpha channel by a 0..1 sublayer opacity.
        auto withOpacity = [](QColor c, qreal op) {
            c.setAlpha(qBound(0, int(c.alpha() * op + 0.5), 255));
            return c;
        };

        // ── Theming hook (Slice AC.4) ─────────────────────────────────────
        // Hillshade light + relief now come from the layer's Q_PROPERTYs
        // instead of hardcoded constants, so the properties-dialog controls
        // (azimuth / altitude / Z-exaggeration / min-lit) actually drive the
        // render. Same azimuth/altitude → direction formula as the QSG
        // renderer (swmm2dmeshqsgrenderer.cpp), so both paths agree. The
        // layer defaults (az=225°, alt=35.264°, zExag=3, minLit=0.15)
        // reproduce the historic NW-35° constants exactly.
        const double azRad      = qDegreesToRadians(m_layer->hillshadeAzimuth());
        const double altRad     = qDegreesToRadians(m_layer->hillshadeAltitude());
        const float  kLx        = float(std::sin(azRad) * std::cos(altRad));
        const float  kLy        = float(std::cos(azRad) * std::cos(altRad));
        const float  kLz        = float(std::sin(altRad));
        const float  kVertExag  = float(m_layer->hillshadeZExag());
        const float  kLitMin    = float(m_layer->hillshadeMinLit());

        p->save();
        p->setPen(Qt::NoPen);

        // Slice Z.14-paint — apply the layer's mask if one is configured.
        // The resolver returns ok=false on disabled / unresolvable masks
        // so paint continues unclipped; otherwise we install a clip path
        // for the duration of this paint pass (p->save above will
        // restore on the p->restore below).
        {
            const auto clip = OpenSWMM::Render::resolveMaskClip(
                m_layer, m_layer->maskSpec());
            if (clip.ok && !clip.path.isEmpty()) {
                if (clip.mode == OpenSWMM::Render::MaskMode::ClipInside) {
                    p->setClipPath(clip.path, Qt::IntersectClip);
                } else {
                    // ClipOutside: invert by subtracting the polygon from
                    // the painter's current clip region (≈ exposed rect).
                    QPainterPath all;
                    all.addRect(exposed.isNull()
                                    ? m_layer->m_sceneBBox.adjusted(-1, -1, 1, 1)
                                    : exposed);
                    p->setClipPath(all.subtracted(clip.path),
                                    Qt::IntersectClip);
                }
            }
        }

        // Use the spatial grid to walk only the triangles / edges whose
        // bbox intersects the exposed rect — paint cost goes from O(N)
        // to O(visible). Fall back to the full vector if the grid is
        // empty (no rebuild yet) or no exposed rect is given (full paint).
        const bool haveExposed = !exposed.isNull();
        const QVector<int> visibleTris  = (haveExposed && !m_layer->m_triGrid.isEmpty())
                                          ? m_layer->m_triGrid.query(exposed)
                                          : QVector<int>{};
        const QVector<int> visibleEdges = (haveExposed && !m_layer->m_edgeGrid.isEmpty())
                                          ? m_layer->m_edgeGrid.query(exposed)
                                          : QVector<int>{};
        const bool useTriIdx  = haveExposed && !m_layer->m_triGrid.isEmpty();
        const bool useEdgeIdx = haveExposed && !m_layer->m_edgeGrid.isEmpty();
        const int triCount  = useTriIdx  ? visibleTris.size()  : tris.size();
        const int edgeCount = useEdgeIdx ? visibleEdges.size() : edges.size();

        // ---- Level-of-detail (LOD) selection ---------------------------------
        // When the native triangles project below ~2 px, drawing millions of
        // per-triangle polygons is wasted work. Rather than replacing the mesh
        // with a uniform aggregation grid (which reads as an artificial grid
        // and discards the real cell shapes), we cull adaptively by on-screen
        // size: the real cells that still project above a few pixels are drawn
        // faithfully (via the size-sorted index, largest first), and tiny
        // sub-pixel cells are skipped. The coarse overview is drawn only as a
        // fallback floor when no real cell is large enough (e.g. a uniformly
        // fine mesh zoomed right out), so the mesh never vanishes. Zoomed-in
        // views keep the exact legacy per-triangle path below.
        const double pxPerScene = p->worldTransform().m11();   // scene units → device px
        constexpr double kLodTriPx = 2.0;
        // Progressive load: with the spatial grid still pending, an exact
        // paint would walk every triangle — stay on the overview until the
        // deferred build lands.
        const bool useLod = m_layer->hasOverview()
                         && ((m_layer->m_nativeTriSpan * pxPerScene) < kLodTriPx
                             || m_layer->m_triGrid.isEmpty());
        const auto &overview = m_layer->m_overviewTris;

        // Shared helpers for the LOD passes.
        auto exposedRejects = [&](const SWMM2DMeshLayer::SceneTri &t) -> bool {
            if (!haveExposed) return false;
            const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
            const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
            const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
            const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
            return (maxX < exposed.left() || minX > exposed.right() ||
                    maxY < exposed.top()  || minY > exposed.bottom());
        };
        // Style-driven fill colour — parity with the QSG fill's
        // schemeDrivesColor branch: the default "Terrain" ramp keeps the
        // byte-identical legacy path; a user-picked ramp, inversion or
        // Classified mode routes colour through the fill sublayer's
        // ClassificationScheme. (Previously this path hardcoded the legacy
        // ramp, so ramp edits only changed the GPU pipeline.)
        const auto *fillStyleQ = fillSub ? fillSub->fillStyle() : nullptr;
        const bool useRampFill = fillStyleQ ? fillStyleQ->useElevationRamp() : true;
        const QColor flatFill  = fillStyleQ ? fillStyleQ->fillColor()
                                            : QColor(70, 130, 180);
        const OpenSWMM::Render::ClassificationScheme fillScheme =
            fillStyleQ ? fillStyleQ->scheme()
                       : OpenSWMM::Render::ClassificationScheme();
        const bool fillClassified =
            fillScheme.mode()
            == OpenSWMM::Render::ClassificationScheme::ClassMode::Classified;
        const QString fillRampName = fillScheme.rampName();
        const bool fillDefaultRamp =
            fillRampName.isEmpty()
            || fillRampName.compare(QLatin1String("terrain"),
                                    Qt::CaseInsensitive) == 0;
        const bool fillSchemeDrives =
            fillClassified || fillScheme.invertRamp() || !fillDefaultRamp;
        const QVector<double> fillClassEdges =
            fillClassified ? fillScheme.levelEdges(zMin, zMax, {})
                           : QVector<double>{};

        // \p ca — the class colour's own alpha (255 for continuous ramps and
        // the flat fill): a fully transparent classified class must vanish.
        auto fillColor = [&](const SWMM2DMeshLayer::SceneTri &t,
                             int &cr, int &cg, int &cb, int &ca) {
            ca = 255;
            if (hasElev && useRampFill) {
                if (fillSchemeDrives) {
                    const QColor sc = fillClassified
                        ? fillScheme.colorForClass(
                              OpenSWMM::Render::ClassificationScheme::classIndexFor(
                                  double(t.zAvg), fillClassEdges),
                              fillScheme.classCount())
                        : fillScheme.colorForValue(double(t.zAvg), zMin, zMax);
                    cr = sc.red(); cg = sc.green(); cb = sc.blue();
                    if (fillClassified) ca = sc.alpha();
                } else {
                    elevationColorRgb((t.zAvg - zMin) / zRange, cr, cg, cb);
                }
                const float ax = float(t.b.x()-t.a.x()), ay = float(t.b.y()-t.a.y());
                const float bx = float(t.c.x()-t.a.x()), by = float(t.c.y()-t.a.y());
                const float az = (t.z1 - t.z0) * kVertExag;
                const float bz = (t.z2 - t.z0) * kVertExag;
                float nx = ay*bz - az*by;
                float ny = az*bx - ax*bz;
                float nz = ax*by - ay*bx;
                if (nz < 0.f) { nx=-nx; ny=-ny; nz=-nz; }
                const float nlen = std::sqrt(nx*nx+ny*ny+nz*nz);
                if (nlen > 1e-12f) { nx/=nlen; ny/=nlen; nz/=nlen; }
                const float lit = qBound(kLitMin, nx*kLx + ny*kLy + nz*kLz, 1.0f);
                cr = qBound(0, int(float(cr)*lit), 255);
                cg = qBound(0, int(float(cg)*lit), 255);
                cb = qBound(0, int(float(cb)*lit), 255);
            } else {
                cr = flatFill.red(); cg = flatFill.green(); cb = flatFill.blue();
            }
        };

        // ---- Pass 1: filled triangles (MeshFillSublayer) ---------------------
        if (fillVisible && useLod) {
            auto drawTri = [&](const SWMM2DMeshLayer::SceneTri &t) {
                int cr, cg, cb, ca; fillColor(t, cr, cg, cb, ca);
                const int fa = qBound(0,
                    int(fillAlpha * fillOpacity * (ca / 255.0) + 0.5), 255);
                p->setBrush(QColor(cr, cg, cb, fa));
                const QPointF pts[3] = {t.a, t.b, t.c};
                p->drawConvexPolygon(pts, 3);
            };

            // (a) Coarse overview as a continuous base. It is built by bbox
            //     coverage (see rebuildOverview), so every region of the mesh
            //     is filled with no holes — areas made of culled small cells
            //     are always backed by the overview and the mesh can never
            //     vanish, at any zoom.
            for (const SWMM2DMeshLayer::SceneTri &t : overview) {
                if (exposedRejects(t)) continue;
                drawTri(t);
            }

            // (b) On top, the real cells still large enough to matter on
            //     screen, largest first. Stop once a cell falls below the
            //     pixel threshold — the rest are smaller (index is sorted by
            //     descending area), so this is O(kept), not O(all). Large
            //     cells thus render with their true geometry while tiny cells
            //     stay represented by the overview base from (a).
            const double pxArea          = pxPerScene * pxPerScene;
            constexpr double kLodKeepPx2 = 16.0;   // real cells ≳ 4×4 px
            const double minAreaScene = (pxArea > 0.0) ? (kLodKeepPx2 / pxArea) : 0.0;
            const auto &bySize = m_layer->m_trisBySizeDesc;
            for (int idx : bySize) {
                const SWMM2DMeshLayer::SceneTri &t = tris[idx];
                const double ux = t.b.x()-t.a.x(), uy = t.b.y()-t.a.y();
                const double vx = t.c.x()-t.a.x(), vy = t.c.y()-t.a.y();
                const double areaScene = 0.5 * std::abs(ux*vy - uy*vx);
                if (areaScene < minAreaScene) break;   // sorted desc → rest smaller
                if (exposedRejects(t)) continue;
                drawTri(t);
            }
        }
        else if (fillVisible) {
            for (int i = 0; i < triCount; ++i) {
                const SWMM2DMeshLayer::SceneTri &t =
                    useTriIdx ? tris[visibleTris[i]] : tris[i];

                int cr, cg, cb, ca;
                fillColor(t, cr, cg, cb, ca);   // style-driven; same math as LOD pass
                const int fa = qBound(0,
                    int(fillAlpha * fillOpacity * (ca / 255.0) + 0.5), 255);

                p->setBrush(QColor(cr, cg, cb, fa));
                const QPointF pts[3] = {t.a, t.b, t.c};
                p->drawConvexPolygon(pts, 3);
            }
        }

        // ---- Pass 4: filled iso-bands (ContourBandSublayer) ------------------
        // Z-ordered above the hillshade fill and below the wireframe / lines.
        // Marching-triangles over the scene triangles; one fan-triangulated
        // convex polygon per (triangle, band). Off by default.
        if (hasElev && bandsVisible) {
            const auto *bandStyle = bandSub->bandStyle();
            const qreal bandOp    = qBound(0.0, bandSub->opacity(), 1.0);

            // At far zoom run marching-triangles over the coarse overview so
            // enabling bands on a multi-million-triangle mesh stays affordable.
            const auto &contourTris = useLod ? overview : tris;

            // Slice US.3 — class edges + colours from the band sublayer's
            // ClassificationScheme (method-aware: equal interval matches the
            // legacy even spacing; quantile / Jenks bin the vertex elevations).
            std::vector<double> levels;
            if (bandStyle) {
                QVector<double> zSamples;
                const auto m = bandStyle->scheme().method();
                if (m == OpenSWMM::Render::BinMethod::Quantile
                    || m == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || m == OpenSWMM::Render::BinMethod::StdDev) {
                    zSamples.reserve(contourTris.size() * 3);
                    for (const auto &t : contourTris) {
                        zSamples.push_back(double(t.z0));
                        zSamples.push_back(double(t.z1));
                        zSamples.push_back(double(t.z2));
                    }
                }
                const QVector<double> edges =
                    bandStyle->scheme().levelEdges(zMin, zMax, zSamples);
                levels.assign(edges.cbegin(), edges.cend());
            } else {
                levels = OpenSWMM::Contour::evenlySpacedLevelsInclusive(zMin, zMax, 9);
            }
            const int nBands = std::max(1, int(levels.size()) - 1);

            const auto extract = [](const SWMM2DMeshLayer::SceneTri &t,
                                    QPointF &p0, QPointF &p1, QPointF &p2,
                                    double  &v0, double  &v1, double  &v2) {
                p0 = t.a; p1 = t.b; p2 = t.c;
                v0 = t.z0; v1 = t.z1; v2 = t.z2;
            };
            const auto bands = OpenSWMM::Contour::marchingTrianglesIsobands(
                contourTris, levels, extract);

            p->setPen(Qt::NoPen);
            for (const auto &bp : bands) {
                if (bp.verts.size() < 3) continue;
                const int idx = std::min(bp.bandIndex, nBands - 1);
                QColor col = bandStyle
                    ? bandStyle->colorForBand(idx, nBands)
                    : OpenSWMM::Contour::viridisAt((double(idx) + 0.5) / double(nBands));
                // Multiply, don't overwrite: the band colour's own alpha (a
                // fully transparent class must vanish) composes with the
                // sublayer opacity slider.
                col.setAlphaF(col.alphaF() * bandOp);
                p->setBrush(col);
                QPolygonF poly;
                poly.reserve(int(bp.verts.size()));
                for (const QPointF &v : bp.verts) poly << v;
                p->drawPolygon(poly);
            }
        }

        // ---- Pass 2: edges (MeshEdgeSublayer) --------------------------------
        const float maxSlope = m_layer->m_maxSlope;
        const float invSlope = (maxSlope > 0.f) ? 1.0f / maxSlope : 0.0f;
        constexpr float kSlopeBreak = 0.35f;

        if (edgesVisible && !useLod) {
            for (int i = 0; i < edgeCount; ++i) {
                const SWMM2DMeshLayer::SceneEdge &e =
                    useEdgeIdx ? edges[visibleEdges[i]] : edges[i];

                const bool wide = hasElev && (e.slope * invSlope > kSlopeBreak);
                const int  alpha = wide ? 210 : 130;
                const qreal lw  = wide ? (active ? 0.9 : 0.6) : (active ? 0.35 : 0.25);

                // Use a cosmetic pen so width is in pixels regardless of zoom
                QPen pen(withOpacity(QColor(0, 0, 0, alpha), edgeOpacity));
                pen.setWidthF(lw);
                pen.setCosmetic(true);
                p->setPen(pen);
                p->setBrush(Qt::NoBrush);
                p->drawLine(e.line);
            }
        }

        // ---- Pass 3: bed-elevation iso-lines (IsolineSublayer) ---------------
        // Marching-triangles, N evenly-spaced levels in [zMin, zMax]. Off by
        // default; line colour / width / count come from IsolineStyle.
        if (hasElev && isoVisible) {
            const auto *isoStyle = isoSub->isolineStyle();
            const double widthPx = isoStyle ? isoStyle->lineWidthPx() : 1.0;
            const QColor lineCol = isoStyle ? isoStyle->color() : QColor(10, 10, 10, 220);

            const auto &contourTris = useLod ? overview : tris;

            // Slice US.3 — interior levels from the isoline ClassificationScheme.
            std::vector<double> levels;
            if (isoStyle) {
                QVector<double> zSamples;
                const auto m = isoStyle->scheme().method();
                if (m == OpenSWMM::Render::BinMethod::Quantile
                    || m == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || m == OpenSWMM::Render::BinMethod::StdDev) {
                    zSamples.reserve(contourTris.size() * 3);
                    for (const auto &t : contourTris) {
                        zSamples.push_back(double(t.z0));
                        zSamples.push_back(double(t.z1));
                        zSamples.push_back(double(t.z2));
                    }
                }
                const auto lv = isoStyle->levelsForRange(zMin, zMax, zSamples);
                levels.assign(lv.cbegin(), lv.cend());
            } else {
                levels = OpenSWMM::Contour::evenlySpacedLevels(zMin, zMax, 8);
            }

            const auto extract = [](const SWMM2DMeshLayer::SceneTri &t,
                                    QPointF &p0, QPointF &p1, QPointF &p2,
                                    double  &v0, double  &v1, double  &v2) {
                p0 = t.a; p1 = t.b; p2 = t.c;
                v0 = t.z0; v1 = t.z1; v2 = t.z2;
            };
            const auto segs = OpenSWMM::Contour::marchingTriangles(
                contourTris, levels, extract);

            QPen pen(withOpacity(lineCol, isoSub->opacity()));
            pen.setWidthF(widthPx);
            pen.setCosmetic(true);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            for (const auto &s : segs)
                p->drawLine(s.a, s.b);
        }

        // ---- Pass 5: mesh-vertex markers (MeshNodeSublayer) ------------------
        // Stylable replacement for the historic showMeshNodes toggle. Tagged
        // (SWMM-coupled) vertices get the highlight colour / size. Off by
        // default. Marker radius is specified in pixels, so convert to scene
        // units via the painter's current scale.
        if (nodesVisible && !useLod) {
            const auto &nodes = m_layer->m_sceneNodes;
            const auto *nodeStyle = nodeSub->nodeStyle();
            const QColor baseC   = nodeStyle ? nodeStyle->color() : QColor(40, 40, 40, 220);
            const QColor taggedC = nodeStyle ? nodeStyle->taggedColor()
                                             : QColor(0xff, 0x8c, 0x00, 235);
            const double baseSzPx   = nodeStyle ? nodeStyle->markerSizePx() : 3.0;
            const double taggedSzPx = nodeStyle ? nodeStyle->taggedSizePx() : 5.0;
            const bool   highlight  = nodeStyle ? nodeStyle->highlightTagged() : true;
            const qreal  nodeOp     = nodeSub->opacity();

            const QTransform wt = p->worldTransform();
            const double scale  = wt.m11();
            const double pxToScene = (scale > 0.0) ? (1.0 / scale) : 1.0;
            const QColor baseUsed   = withOpacity(baseC, nodeOp);
            const QColor taggedUsed = withOpacity(taggedC, nodeOp);

            p->setPen(Qt::NoPen);
            for (const SWMM2DMeshLayer::SceneNode &n : nodes) {
                if (haveExposed && !exposed.contains(n.pt)) continue;
                const bool tag = highlight && n.tagged;
                const double r = 0.5 * (tag ? taggedSzPx : baseSzPx) * pxToScene;
                p->setBrush(tag ? taggedUsed : baseUsed);
                p->drawEllipse(n.pt, r, r);
            }
        }

        // ---- Pass 3: selection highlight (cells / edges / vertices) ----------
        // Cyan, drawn on top of fill + edges. This is the active render path
        // (the QSG renderer is currently inactive), so all mesh-element
        // highlights are rendered here.
        const auto &nodes = m_layer->m_sceneNodes;
        const QSet<int> &selT = m_layer->highlightedTriangles();
        const QSet<int> &selE = m_layer->highlightedEdges();
        const QSet<int> &selV = m_layer->highlightedVertices();

        if (!selT.isEmpty()) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 200, 255, 110));
            const int nt = tris.size();
            for (int t : selT) {
                if (t < 0 || t >= nt) continue;
                const SWMM2DMeshLayer::SceneTri &tr = tris[t];
                const QPointF pts[3] = { tr.a, tr.b, tr.c };
                p->drawConvexPolygon(pts, 3);
            }
        }

        if (!selE.isEmpty()) {
            QPen epen(QColor(0, 200, 255, 235));
            epen.setWidthF(2.6);
            epen.setCosmetic(true);
            epen.setCapStyle(Qt::RoundCap);
            p->setPen(epen);
            p->setBrush(Qt::NoBrush);
            const int nt = tris.size();
            for (int flat : selE) {
                const int t = flat / 3, e = flat % 3;
                if (t < 0 || t >= nt) continue;
                const SWMM2DMeshLayer::SceneTri &tr = tris[t];
                // edge 0 → (b,c), 1 → (c,a), 2 → (a,b)
                QPointF p0, p1;
                switch (e) {
                case 0: p0 = tr.b; p1 = tr.c; break;
                case 1: p0 = tr.c; p1 = tr.a; break;
                default: p0 = tr.a; p1 = tr.b; break;
                }
                p->drawLine(p0, p1);
            }
        }

        if (!selV.isEmpty()) {
            const QTransform wt = p->worldTransform();
            const double scale = wt.m11();
            const double r = (scale > 0.0) ? (5.0 / scale) : 1.0;   // ~5 px
            p->setPen(QPen(QColor(0, 120, 200, 240), (scale > 0.0) ? 1.5 / scale : 1.0));
            p->setBrush(QColor(0, 200, 255, 200));
            for (int vi : selV) {
                if (vi < 0 || vi >= nodes.size()) continue;
                p->drawEllipse(nodes[vi].pt, r, r);
            }
        }

        p->restore();
    }

private:
    SWMM2DMeshLayer *m_layer;
};

// ---------------------------------------------------------------------------
// SWMM2DMeshLayer
// ---------------------------------------------------------------------------

SWMM2DMeshLayer::SWMM2DMeshLayer(mesh::MeshResult     result,
                                  const QString       &sourcePath,
                                  OpenSWMMVisWorkspace *parent,
                                  bool                  deferHeavyGeometry)
    : OpenSWMMVisLayer(parent),
      m_mesh(std::move(result)),
      m_sourcePath(sourcePath)
{
    setLayerType(OpenSWMMVisLayer::SWMM2DMeshLayer);
    setName(sourcePath.isEmpty() ? QStringLiteral("Mesh") : sourcePath);

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Default to a
    // SingleSymbolRenderer so renderer() never returns null.  Paint loop
    // still uses the hillshade ramp directly; refactor deferred to
    // 8.13.6.4.
    m_renderer = std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>();

    // Sublayer mix. QObject parent-child keeps them alive for the layer's
    // lifetime; sublayers() returns them in paint order. Defaults preserve
    // the historic visual: fill + edges on, nodes/contours/bands off.
    m_meshFillSublayer    = new OpenSWMM::Render::MeshFillSublayer(
        QStringLiteral("mesh.fill"), this);
    m_meshEdgeSublayer    = new OpenSWMM::Render::MeshEdgeSublayer(
        QStringLiteral("mesh.edges"), this);
    m_meshNodeSublayer    = new OpenSWMM::Render::MeshNodeSublayer(
        QStringLiteral("mesh.vertices"), this);
    m_contourBandSublayer = new OpenSWMM::Render::ContourBandSublayer(
        QStringLiteral("mesh.contourBands"), this);
    m_isolineSublayer     = new OpenSWMM::Render::IsolineSublayer(
        QStringLiteral("mesh.isolines"), this);

    // Mesh vertices default ON: the renderers' LOD gates
    // (kMarkerMinCellAreaPx — cells ≥ ~14 px across) mean the dots only
    // materialise once the user zooms in close enough to work with
    // individual vertices, so the historic "off to avoid noise" default is
    // superseded by the zoom gate.
    m_meshNodeSublayer->setVisible(true);

    // Seed the fill style's hillshade strength so hillshadeZExag()
    // (= strength × 10) defaults to 3.0 — the historic vertical-exaggeration
    // constant the paint path used before it became theme-driven (Slice
    // AC.4). Without this seed the sublayer default of 0.5 would render relief
    // at zExag=5.0, diverging from the legacy appearance.
    if (auto *fs = m_meshFillSublayer->fillStyle())
        fs->setHillshadeStrength(0.3);

    // Bind contour/isoline attribute to elevation so the dialog shows a
    // meaningful default and the renderer pulls z values when iterating
    // SceneTri.
    if (auto *bs = m_contourBandSublayer->bandStyle())
        bs->setAttribute(QStringLiteral("elevation"));
    if (auto *is = m_isolineSublayer->isolineStyle()) {
        is->setAttribute(QStringLiteral("elevation"));
        is->setColor(QColor(0x1a, 0x1a, 0x1a, 200));
    }

    // Route any sublayer (visibility / style) change to the renderer.
    // repaintRequested() is what the QSG renderer connects to in
    // SWMM2DMeshQSGRenderer::setLayer, so this single hop covers every
    // sublayer state mutation.
    auto wire = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        QObject::connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                         this, [this]() { emit repaintRequested(); });
    };
    wire(m_meshFillSublayer);
    wire(m_meshEdgeSublayer);
    wire(m_meshNodeSublayer);
    wire(m_contourBandSublayer);
    wire(m_isolineSublayer);

    if (!m_mesh.vertices.isEmpty())
    {
        const auto &v0 = m_mesh.vertices.first();
        double minX = v0.xy.x(), maxX = v0.xy.x();
        double minY = v0.xy.y(), maxY = v0.xy.y();
        for (const auto &v : m_mesh.vertices)
        {
            if (v.xy.x() < minX) minX = v.xy.x();
            if (v.xy.x() > maxX) maxX = v.xy.x();
            if (v.xy.y() < minY) minY = v.xy.y();
            if (v.xy.y() > maxY) maxY = v.xy.y();
        }
        setExtent(MapExtent(minX, minY, maxX, maxY));
    }
    if (deferHeavyGeometry) {
        // Progressive load (Mesh Tiled LOD P1.2): build only what the fill
        // needs to draw — the renderers fall back to the LOD pyramid until
        // finishSceneGeometryAsync() delivers edges/grids/adjacency/BCs.
        rebuildSceneGeometryLight();
    } else {
        rebuildSceneGeometry();
        resizeBCsToMesh();
        rebuildVertexAdjacency();
    }
}

SWMM2DMeshLayer::~SWMM2DMeshLayer()
{
    OGRCoordinateTransformation::DestroyCT(m_transform);
}

QString SWMM2DMeshLayer::sourcePath() const
{
    // Absolute path so the Properties window shows the full on-disk
    // mesh location.
    return m_sourcePath.isEmpty()
               ? m_sourcePath
               : QFileInfo(m_sourcePath).absoluteFilePath();
}

QString SWMM2DMeshLayer::sourceDescription() const
{
    const QString path = sourcePath();
    return path.isEmpty() ? tr("(generated mesh)") : path;
}

QVector<QPair<QString, QString>> SWMM2DMeshLayer::extendedMetadata() const
{
    QVector<QPair<QString, QString>> md;
    const QLocale loc;

    md.append({ tr("Vertices"),          loc.toString(vertexCount()) });
    md.append({ tr("Cells (triangles)"), loc.toString(triangleCount()) });
    md.append({ tr("Edges (total)"),     loc.toString(edgeCount()) });
    md.append({ tr("Boundary edges"),    loc.toString(boundaryEdgeCount()) });

    // Cell-area statistics (Part A, MESH_DECOUPLED_1D2D_REMAP_PLAN).
    const mesh::CellAreaStats as = mesh::computeCellAreaStats(m_mesh);
    if (as.count > 0)
    {
        md.append({ tr("Cell area (min)"),    QString::number(as.min,    'g', 4) });
        md.append({ tr("Cell area (max)"),    QString::number(as.max,    'g', 4) });
        md.append({ tr("Cell area (mean)"),   QString::number(as.mean,   'g', 4) });
        md.append({ tr("Cell area (median)"), QString::number(as.median, 'g', 4) });
    }

    md.append({ tr("Bed elevation (min / max)"),
                QStringLiteral("%1 / %2")
                    .arg(zMin(), 0, 'f', 3).arg(zMax(), 0, 'f', 3) });

    md.append({ tr("Max edge slope"), QString::number(maxSlope(), 'f', 4) });

    // Manning's n range — per-triangle, skipping NaN/unset cells.
    double nMin = std::numeric_limits<double>::infinity();
    double nMax = -std::numeric_limits<double>::infinity();
    int    nWith = 0;
    for (const auto &t : m_mesh.triangles) {
        if (std::isnan(t.mannings)) continue;
        nMin = std::min(nMin, t.mannings);
        nMax = std::max(nMax, t.mannings);
        ++nWith;
    }
    if (nWith > 0)
        md.append({ tr("Manning's n (min / max)"),
                    QStringLiteral("%1 / %2")
                        .arg(nMin, 0, 'g', 4).arg(nMax, 0, 'g', 4) });

    // Vertices coupled to a 1D SWMM node.
    int coupled = 0;
    for (const auto &v : m_mesh.vertices)
        if (!v.coupledNode.isEmpty()) ++coupled;
    md.append({ tr("Coupled vertices"), loc.toString(coupled) });
    if (!m_mesh.cellCouplings.isEmpty())
        md.append({ tr("Coupled cells (rows)"),
                    loc.toString(m_mesh.cellCouplings.size()) });

    const MapExtent e = extent();
    if (e.isValid())
        md.append({ tr("Extent (xmin, ymin → xmax, ymax)"),
                    QStringLiteral("%1, %2 → %3, %4")
                        .arg(e.xMin(), 0, 'f', 2).arg(e.yMin(), 0, 'f', 2)
                        .arg(e.xMax(), 0, 'f', 2).arg(e.yMax(), 0, 'f', 2) });

    return md;
}

// ---------------------------------------------------------------------------
// Renderer (Slice BI Phase 8.13.6.6)
// ---------------------------------------------------------------------------

OpenSWMM::Render::IFeatureRenderer *SWMM2DMeshLayer::renderer() const
{
    return m_renderer.get();
}

void SWMM2DMeshLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r)
        return;
    if (r.get() == m_renderer.get())
        return;
    m_renderer = std::move(r);
    emit rendererChanged();
}

// ---------------------------------------------------------------------------
// Scene-geometry helpers — pure functions of their inputs so the same code
// serves the synchronous full rebuild and the deferred background build
// (progressive load, Mesh Tiled LOD P1.2).
// ---------------------------------------------------------------------------

namespace {

/*! The heavy scene-side products: deduplicated wireframe edges + bbox
 *  arrays + spatial culling grids. Dominates the build time on multi-
 *  million-triangle meshes (a hash probe per triangle edge). */
struct MeshHeavyGeom
{
    QVector<SWMM2DMeshLayer::SceneEdge> sceneEdges;
    QVector<QRectF>                     triBBoxes;
    QVector<QRectF>                     edgeBBoxes;
    MeshSpatialGrid                     triGrid;
    MeshSpatialGrid                     edgeGrid;
    float                               maxSlope = 0.0f;
};

/*! Append one SceneTri per valid mesh triangle (shared by the light and
 *  full builds; skips degenerate vertex indices exactly as before). */
void appendSceneTris(const mesh::MeshResult &meshData,
                     const QVector<QPointF> &scenePts,
                     QVector<SWMM2DMeshLayer::SceneTri> &out)
{
    const int nVerts = meshData.vertices.size();
    out.reserve(meshData.triangles.size());
    for (const auto &t : meshData.triangles)
    {
        if (t.v0 < 0 || t.v0 >= nVerts) continue;
        if (t.v1 < 0 || t.v1 >= nVerts) continue;
        if (t.v2 < 0 || t.v2 >= nVerts) continue;
        SWMM2DMeshLayer::SceneTri st;
        st.a    = scenePts[t.v0];
        st.b    = scenePts[t.v1];
        st.c    = scenePts[t.v2];
        st.z0   = static_cast<float>(meshData.vertices[t.v0].z);
        st.z1   = static_cast<float>(meshData.vertices[t.v1].z);
        st.z2   = static_cast<float>(meshData.vertices[t.v2].z);
        st.zAvg = (st.z0 + st.z1 + st.z2) / 3.0f;
        out.append(st);
    }
}

MeshHeavyGeom buildMeshHeavyGeom(const mesh::MeshResult &meshData,
                                 const QVector<QPointF> &scenePts,
                                 const QVector<SWMM2DMeshLayer::SceneTri> &sceneTris)
{
    using SceneEdge = SWMM2DMeshLayer::SceneEdge;
    MeshHeavyGeom out;
    const int nVerts = meshData.vertices.size();

    QSet<QPair<int,int>> seen;
    seen.reserve(meshData.triangles.size() * 3);
    out.sceneEdges.reserve(meshData.triangles.size() * 3);

    auto pushEdge = [&](int a, int b) {
        if (a == b) return;
        const QPair<int,int> key = (a < b) ? qMakePair(a,b) : qMakePair(b,a);
        if (seen.contains(key)) return;
        seen.insert(key);

        const double za = meshData.vertices[a].z;
        const double zb = meshData.vertices[b].z;
        const double dx = meshData.vertices[a].xy.x() - meshData.vertices[b].xy.x();
        const double dy = meshData.vertices[a].xy.y() - meshData.vertices[b].xy.y();
        const double dist = std::sqrt(dx*dx + dy*dy);
        const float  slope = (dist > 1e-9) ? static_cast<float>(std::abs(za-zb) / dist) : 0.0f;
        if (slope > out.maxSlope) out.maxSlope = slope;

        SceneEdge e;
        e.line  = QLineF(scenePts[a], scenePts[b]);
        e.zAvg  = static_cast<float>((za + zb) * 0.5);
        e.slope = slope;
        out.sceneEdges.append(e);
    };

    for (const auto &t : meshData.triangles)
    {
        if (t.v0 < 0 || t.v0 >= nVerts) continue;
        if (t.v1 < 0 || t.v1 >= nVerts) continue;
        if (t.v2 < 0 || t.v2 >= nVerts) continue;
        pushEdge(t.v0, t.v1);
        pushEdge(t.v1, t.v2);
        pushEdge(t.v2, t.v0);
    }

    // Spatial grids over the bbox sets — O(visible) paint-time culling.
    out.triBBoxes.resize(sceneTris.size());
    for (int i = 0; i < sceneTris.size(); ++i) {
        const auto &t = sceneTris[i];
        const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
        const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
        const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
        const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
        out.triBBoxes[i] = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    }
    out.edgeBBoxes.resize(out.sceneEdges.size());
    for (int i = 0; i < out.sceneEdges.size(); ++i) {
        const QLineF &ln = out.sceneEdges[i].line;
        const double x0 = qMin(ln.x1(), ln.x2()), x1 = qMax(ln.x1(), ln.x2());
        const double y0 = qMin(ln.y1(), ln.y2()), y1 = qMax(ln.y1(), ln.y2());
        out.edgeBBoxes[i] = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    out.triGrid.rebuild(out.triBBoxes);
    out.edgeGrid.rebuild(out.edgeBBoxes);
    return out;
}

/*! CSR vertex→triangle adjacency (verbatim logic of the former member-only
 *  implementation; the member now delegates here). */
void buildVertexAdjacency(const mesh::MeshResult &meshData,
                          QVector<int> &ptr, QVector<int> &idx)
{
    const int nv = meshData.vertices.size();
    const int nt = meshData.triangles.size();

    QVector<int> counts(nv, 0);
    for (const auto &tri : meshData.triangles) {
        if (tri.v0 >= 0 && tri.v0 < nv) ++counts[tri.v0];
        if (tri.v1 >= 0 && tri.v1 < nv) ++counts[tri.v1];
        if (tri.v2 >= 0 && tri.v2 < nv) ++counts[tri.v2];
    }

    ptr.resize(nv + 1);
    ptr[0] = 0;
    for (int v = 0; v < nv; ++v)
        ptr[v + 1] = ptr[v] + counts[v];

    idx.resize(ptr[nv]);
    QVector<int> cursor = ptr;
    for (int t = 0; t < nt; ++t) {
        const auto &tri = meshData.triangles[t];
        if (tri.v0 >= 0 && tri.v0 < nv) idx[cursor[tri.v0]++] = t;
        if (tri.v1 >= 0 && tri.v1 < nv) idx[cursor[tri.v1]++] = t;
        if (tri.v2 >= 0 && tri.v2 < nv) idx[cursor[tri.v2]++] = t;
    }
}

/*! Per-(tri,edgeLocal) boundary flags from triangle adjacency (verbatim
 *  logic of resizeBCsToMesh's second half; the member delegates here). */
QVector<bool> buildBoundaryFlags(const mesh::MeshResult &meshData)
{
    const int nt = meshData.triangles.size();
    QHash<QPair<int,int>, int> edgeUseCount;
    edgeUseCount.reserve(nt * 3);
    for (int t = 0; t < nt; ++t) {
        const auto &tri = meshData.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const QPair<int,int> key = (va[e] < vb[e]) ? qMakePair(va[e], vb[e])
                                                       : qMakePair(vb[e], va[e]);
            ++edgeUseCount[key];
        }
    }

    QSet<QPair<int,int>> markerBoundary;
    for (const auto &e : meshData.boundaryEdges) {
        const QPair<int,int> key = (e.v0 < e.v1) ? qMakePair(e.v0, e.v1)
                                                 : qMakePair(e.v1, e.v0);
        markerBoundary.insert(key);
    }

    QVector<bool> flags(nt * 3, false);
    for (int t = 0; t < nt; ++t) {
        const auto &tri = meshData.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const QPair<int,int> key = (va[e] < vb[e]) ? qMakePair(va[e], vb[e])
                                                       : qMakePair(vb[e], va[e]);
            const int uses = edgeUseCount.value(key, 0);
            if (uses <= 1 || markerBoundary.contains(key))
                flags[t * 3 + e] = true;
        }
    }
    return flags;
}

} // namespace

// ---------------------------------------------------------------------------
// rebuildSceneGeometry — populates all scene caches from m_mesh
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::rebuildSceneGeometry()
{
    const int nVerts = m_mesh.vertices.size();

    m_sceneTris.clear();
    m_sceneEdges.clear();
    m_sceneNodes.clear();
    m_triBBoxes.clear();
    m_edgeBBoxes.clear();
    m_triGrid.clear();
    m_edgeGrid.clear();
    m_sceneBBox = QRectF();
    m_zMin = std::numeric_limits<double>::max();
    m_zMax = std::numeric_limits<double>::lowest();
    m_maxSlope = 0.0f;

    if (nVerts == 0 || m_mesh.triangles.isEmpty()) {
        ++m_geomRevision;
        return;
    }

    bool hasElevation = false;
    for (const auto &v : m_mesh.vertices)
        if (v.z != 0.0) { hasElevation = true; break; }

    // ── Scene-space vertex positions (with OGR reprojection + Y-flip) ──────
    QVector<QPointF> scenePts;
    scenePts.reserve(nVerts);
    for (const auto &v : m_mesh.vertices)
    {
        double x = v.xy.x(), y = v.xy.y();
        if (m_transform) m_transform->Transform(1, &x, &y);
        scenePts.append(QPointF(x, -y));  // Y-flip: scene grows downward
        if (hasElevation)
        {
            if (v.z < m_zMin) m_zMin = v.z;
            if (v.z > m_zMax) m_zMax = v.z;
        }
    }
    if (!hasElevation) { m_zMin = 0.0; m_zMax = 0.0; }

    // ── Node dots ────────────────────────────────────────────────────────────
    m_sceneNodes.reserve(nVerts);
    bool first = true;
    for (int i = 0; i < nVerts; ++i)
    {
        SceneNode n;
        n.pt     = scenePts[i];
        n.z      = static_cast<float>(m_mesh.vertices[i].z);
        n.tagged = !m_mesh.vertices[i].coupledNode.isEmpty();
        m_sceneNodes.append(n);

        if (first) { m_sceneBBox = QRectF(n.pt, QSizeF(0,0)); first = false; }
        else {
            if (n.pt.x() < m_sceneBBox.left())   m_sceneBBox.setLeft(n.pt.x());
            if (n.pt.x() > m_sceneBBox.right())  m_sceneBBox.setRight(n.pt.x());
            if (n.pt.y() < m_sceneBBox.top())    m_sceneBBox.setTop(n.pt.y());
            if (n.pt.y() > m_sceneBBox.bottom()) m_sceneBBox.setBottom(n.pt.y());
        }
    }

    // ── Triangles ───────────────────────────────────────────────────────────
    appendSceneTris(m_mesh, scenePts, m_sceneTris);

    // ── Edges + spatial grids (the heavy tail — shared with the deferred
    //    background build; see buildMeshHeavyGeom below) ─────────────────────
    MeshHeavyGeom heavy = buildMeshHeavyGeom(m_mesh, scenePts, m_sceneTris);
    m_sceneEdges  = std::move(heavy.sceneEdges);
    m_triBBoxes   = std::move(heavy.triBBoxes);
    m_edgeBBoxes  = std::move(heavy.edgeBBoxes);
    m_triGrid     = std::move(heavy.triGrid);
    m_edgeGrid    = std::move(heavy.edgeGrid);
    m_maxSlope    = heavy.maxSlope;

    // ── LOD overview for far-zoom rendering ──────────────────────────────────
    rebuildOverview();

    ++m_geomRevision;

    // Notify the graphics item (if any) that its geometry changed.
    if (m_graphicsItem)
        m_graphicsItem->geometryChanged();
}

// ---------------------------------------------------------------------------
// Progressive load (Mesh Tiled LOD P1.2)
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::rebuildSceneGeometryLight()
{
    const int nVerts = m_mesh.vertices.size();

    m_sceneTris.clear();
    m_sceneEdges.clear();
    m_sceneNodes.clear();
    m_triBBoxes.clear();
    m_edgeBBoxes.clear();
    m_triGrid.clear();
    m_edgeGrid.clear();
    m_sceneBBox = QRectF();
    m_zMin = std::numeric_limits<double>::max();
    m_zMax = std::numeric_limits<double>::lowest();
    m_maxSlope = 0.0f;
    m_sceneGeomComplete = false;

    if (nVerts == 0 || m_mesh.triangles.isEmpty()) {
        m_sceneGeomComplete = true;
        ++m_geomRevision;
        return;
    }

    bool hasElevation = false;
    for (const auto &v : m_mesh.vertices)
        if (v.z != 0.0) { hasElevation = true; break; }

    QVector<QPointF> scenePts;
    scenePts.reserve(nVerts);
    for (const auto &v : m_mesh.vertices)
    {
        double x = v.xy.x(), y = v.xy.y();
        if (m_transform) m_transform->Transform(1, &x, &y);
        scenePts.append(QPointF(x, -y));
        if (hasElevation)
        {
            if (v.z < m_zMin) m_zMin = v.z;
            if (v.z > m_zMax) m_zMax = v.z;
        }
    }
    if (!hasElevation) { m_zMin = 0.0; m_zMax = 0.0; }

    m_sceneNodes.reserve(nVerts);
    bool first = true;
    for (int i = 0; i < nVerts; ++i)
    {
        SceneNode n;
        n.pt     = scenePts[i];
        n.z      = static_cast<float>(m_mesh.vertices[i].z);
        n.tagged = !m_mesh.vertices[i].coupledNode.isEmpty();
        m_sceneNodes.append(n);

        if (first) { m_sceneBBox = QRectF(n.pt, QSizeF(0,0)); first = false; }
        else {
            if (n.pt.x() < m_sceneBBox.left())   m_sceneBBox.setLeft(n.pt.x());
            if (n.pt.x() > m_sceneBBox.right())  m_sceneBBox.setRight(n.pt.x());
            if (n.pt.y() < m_sceneBBox.top())    m_sceneBBox.setTop(n.pt.y());
            if (n.pt.y() > m_sceneBBox.bottom()) m_sceneBBox.setBottom(n.pt.y());
        }
    }

    appendSceneTris(m_mesh, scenePts, m_sceneTris);

    // Keep the (implicitly shared) scene points so the background heavy
    // build doesn't have to re-project; freed on adoption.
    m_pendingScenePts = scenePts;

    // The LOD pyramid IS the progressive-first render — build it now so the
    // layer draws the coarse levels the moment it joins the canvas.
    rebuildOverview();

    ++m_geomRevision;
    if (m_graphicsItem)
        m_graphicsItem->geometryChanged();
}

void SWMM2DMeshLayer::finishSceneGeometryAsync()
{
    if (m_sceneGeomComplete || m_heavyBuildRunning) return;
    m_heavyBuildRunning = true;

    // Snapshots are implicitly shared; a concurrent GUI edit detaches the
    // layer's copy, so the worker always sees a consistent state.
    const mesh::MeshResult   meshSnap = m_mesh;
    QVector<QPointF>         ptsSnap  = m_pendingScenePts;
    const QVector<SceneTri>  trisSnap = m_sceneTris;
    const quint64            revAtLaunch = m_geomRevision;

    if (ptsSnap.isEmpty()) {
        // Rare re-run path (e.g. a CRS change forced a full synchronous
        // rebuild mid-defer): re-project here so the worker matches the
        // current transform.
        ptsSnap.reserve(meshSnap.vertices.size());
        for (const auto &v : meshSnap.vertices) {
            double x = v.xy.x(), y = v.xy.y();
            if (m_transform) m_transform->Transform(1, &x, &y);
            ptsSnap.append(QPointF(x, -y));
        }
    }

    struct DeferredGeom {
        MeshHeavyGeom             heavy;
        QVector<int>              vertTriPtr;
        QVector<int>              vertTriIdx;
        QVector<bool>             isBoundary;
        QVector<mesh::MeshEdgeBC> bcDefaults;
    };

    auto *watcher = new QFutureWatcher<QSharedPointer<DeferredGeom>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, revAtLaunch]() {
        QSharedPointer<DeferredGeom> d = watcher->result();
        watcher->deleteLater();
        m_heavyBuildRunning = false;

        // Stale result (geometry re-projected mid-build) — rebuild again.
        if (revAtLaunch != m_geomRevision) {
            if (!m_sceneGeomComplete) finishSceneGeometryAsync();
            return;
        }

        m_sceneEdges  = std::move(d->heavy.sceneEdges);
        m_triBBoxes   = std::move(d->heavy.triBBoxes);
        m_edgeBBoxes  = std::move(d->heavy.edgeBBoxes);
        m_triGrid     = std::move(d->heavy.triGrid);
        m_edgeGrid    = std::move(d->heavy.edgeGrid);
        m_maxSlope    = d->heavy.maxSlope;
        m_vertTriPtr  = std::move(d->vertTriPtr);
        m_vertTriIdx  = std::move(d->vertTriIdx);
        m_isBoundary  = std::move(d->isBoundary);
        // BCs loaded from the file (already correctly sized) win; otherwise
        // adopt the default-Wall slots built on the worker.
        if (m_bc.size() != d->bcDefaults.size())
            m_bc = std::move(d->bcDefaults);
        m_pendingScenePts = QVector<QPointF>();
        m_sceneGeomComplete = true;

        emit sceneGeometryReady();
        if (m_graphicsItem) m_graphicsItem->geometryChanged();
        emit repaintRequested();
    });
    watcher->setFuture(QtConcurrent::run(
        [meshSnap, ptsSnap, trisSnap]() -> QSharedPointer<DeferredGeom> {
            auto d = QSharedPointer<DeferredGeom>::create();
            d->heavy = buildMeshHeavyGeom(meshSnap, ptsSnap, trisSnap);
            buildVertexAdjacency(meshSnap, d->vertTriPtr, d->vertTriIdx);
            d->isBoundary = buildBoundaryFlags(meshSnap);
            d->bcDefaults.resize(meshSnap.triangles.size() * 3);
            return d;
        }));
}

// ---------------------------------------------------------------------------
// rebuildOverview — coarse LOD decimation for far-zoom rendering
// ---------------------------------------------------------------------------
//
// Bins triangle centroids into a fixed-resolution grid (~15k cells), averages
// elevation per cell, and emits one quad (two SceneTris) per occupied cell.
// Corner heights are averaged from the up-to-4 neighbouring cell means so the
// existing hillshade face-normal still produces relief on the coarse mesh.
// Empty cells (holes in the source mesh) emit nothing, so the overview keeps
// the mesh's outline. Cost is one O(triangles) binning pass at load time.
namespace {

/*! One overview ("pyramid") bake. Extracted from rebuildOverview() so
 *  the same code serves the synchronous load-time build and the
 *  background rebuild (SWMM2DMeshLayer::rebuildOverviewAsync). Pure
 *  function of its inputs — safe to run on a worker thread against
 *  detached (implicitly-shared) snapshots of the scene caches. */
struct MeshOverviewData
{
    QVector<SWMM2DMeshLayer::SceneTri> overviewTris;
    QVector<int>                       trisBySizeDesc;
    double                             nativeTriSpan = 0.0;
};

MeshOverviewData buildMeshOverviewData(
    const QVector<SWMM2DMeshLayer::SceneTri> &sceneTris,
    const QRectF &sceneBBox)
{
    using SceneTri = SWMM2DMeshLayer::SceneTri;
    MeshOverviewData out;

    const int nTri = sceneTris.size();
    if (nTri == 0 || sceneBBox.isNull()) return out;

    const double bw = sceneBBox.width();
    const double bh = sceneBBox.height();
    const double area = bw * bh;
    if (area <= 0.0) return out;

    // Representative native triangle edge length (scene units). sqrt(area /
    // triCount) is the side of an equal-area square per triangle — close
    // enough for the painter's pixel-size LOD test.
    out.nativeTriSpan = std::sqrt(area / double(nTri));

    // Small meshes draw full-res fast enough that an overview would only add
    // zoom popping; skip it for them.
    constexpr int kOverviewMinTris = 200000;
    if (nTri < kOverviewMinTris) return out;

    // Aim for ~60k cells (~120k overview triangles) regardless of source
    // size. Still trivially cheap on the QSG path (a few MB of vertices) and
    // acceptable on the QPainter fallback, while quartering the on-screen
    // block size vs the original 15k bake (~5 px instead of ~10 px cells on a
    // full-screen zoom-to-extent) — the coarse bake read as "jagged /
    // truncated" at full extent.
    constexpr int kTargetCells = 60000;
    const double aspect = (bh > 0.0) ? (bw / bh) : 1.0;
    const int cols = qMax(1, int(std::round(std::sqrt(double(kTargetCells) * aspect))));
    const int rows = qMax(1, int(std::round(double(kTargetCells) / double(cols))));

    const double cw = bw / double(cols);
    const double ch = bh / double(rows);
    const double ox = sceneBBox.left();
    const double oy = sceneBBox.top();
    if (cw <= 0.0 || ch <= 0.0) return out;

    // Accumulate by *bbox coverage*, not centroid: a cell receives every
    // triangle whose bounding box overlaps it. A large triangle therefore
    // fills all the overview cells it spans (centroid binning would fill only
    // one and leave holes under the rest), so the overview is a gap-free floor.
    QVector<double> zsum(cols * rows, 0.0);
    QVector<int>    cnt (cols * rows, 0);
    for (const SceneTri &t : sceneTris) {
        const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
        const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
        const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
        const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
        const int ci0 = qBound(0, int((minX - ox) / cw), cols - 1);
        const int ci1 = qBound(0, int((maxX - ox) / cw), cols - 1);
        const int cj0 = qBound(0, int((minY - oy) / ch), rows - 1);
        const int cj1 = qBound(0, int((maxY - oy) / ch), rows - 1);
        for (int cj = cj0; cj <= cj1; ++cj)
            for (int ci = ci0; ci <= ci1; ++ci) {
                const int k = cj * cols + ci;
                zsum[k] += t.zAvg;
                ++cnt[k];
            }
    }

    auto cellMean = [&](int i, int j, double &out) -> bool {
        if (i < 0 || j < 0 || i >= cols || j >= rows) return false;
        const int k = j * cols + i;
        if (cnt[k] == 0) return false;
        out = zsum[k] / double(cnt[k]);
        return true;
    };
    // Corner height = mean of the up-to-4 surrounding cell means.
    auto cornerZ = [&](int i, int j) -> float {
        double s = 0.0; int n = 0; double m = 0.0;
        for (int dj = -1; dj <= 0; ++dj)
            for (int di = -1; di <= 0; ++di)
                if (cellMean(i + di, j + dj, m)) { s += m; ++n; }
        return n ? float(s / n) : 0.0f;
    };

    out.overviewTris.reserve(cols * rows * 2);
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            if (cnt[j * cols + i] == 0) continue;   // hole — emit no fill
            const double x0 = ox + i * cw, x1 = x0 + cw;
            const double y0 = oy + j * ch, y1 = y0 + ch;
            const float z00 = cornerZ(i,     j);
            const float z10 = cornerZ(i + 1, j);
            const float z01 = cornerZ(i,     j + 1);
            const float z11 = cornerZ(i + 1, j + 1);

            SceneTri t1;
            t1.a = QPointF(x0, y0); t1.b = QPointF(x1, y0); t1.c = QPointF(x1, y1);
            t1.z0 = z00; t1.z1 = z10; t1.z2 = z11;
            t1.zAvg = (z00 + z10 + z11) / 3.0f;
            out.overviewTris.append(t1);

            SceneTri t2;
            t2.a = QPointF(x0, y0); t2.b = QPointF(x1, y1); t2.c = QPointF(x0, y1);
            t2.z0 = z00; t2.z1 = z11; t2.z2 = z01;
            t2.zAvg = (z00 + z11 + z01) / 3.0f;
            out.overviewTris.append(t2);
        }
    }

    // Size-sorted triangle index for adaptive far-zoom culling (see the
    // header). Sort indices by descending scene-space area so the painter can
    // walk the largest cells first and stop once they fall below the on-screen
    // pixel threshold. Areas are computed into a scratch array and discarded.
    {
        QVector<float> areaTmp(nTri);
        for (int i = 0; i < nTri; ++i) {
            const SceneTri &t = sceneTris[i];
            const double ux = t.b.x() - t.a.x(), uy = t.b.y() - t.a.y();
            const double vx = t.c.x() - t.a.x(), vy = t.c.y() - t.a.y();
            areaTmp[i] = float(0.5 * std::abs(ux * vy - uy * vx));
        }
        out.trisBySizeDesc.resize(nTri);
        for (int i = 0; i < nTri; ++i) out.trisBySizeDesc[i] = i;
        std::sort(out.trisBySizeDesc.begin(), out.trisBySizeDesc.end(),
                  [&areaTmp](int p, int q) { return areaTmp[p] > areaTmp[q]; });
    }
    return out;
}

} // namespace

void SWMM2DMeshLayer::rebuildOverview()
{
    MeshOverviewData d = buildMeshOverviewData(m_sceneTris, m_sceneBBox);
    m_overviewTris   = std::move(d.overviewTris);
    m_trisBySizeDesc = std::move(d.trisBySizeDesc);
    m_nativeTriSpan  = d.nativeTriSpan;
}

void SWMM2DMeshLayer::rebuildOverviewAsync()
{
    if (m_overviewBuildRunning) return;
    m_overviewBuildRunning = true;
    emit overviewBuildStarted(name());

    // Snapshot the inputs. QVector is implicitly shared: the worker only
    // reads its own reference, and any concurrent GUI-thread edit (e.g.
    // applyMeshVertexZ) detaches the layer's copy first, so the worker sees
    // a consistent snapshot.
    const QVector<SceneTri> tris = m_sceneTris;
    const QRectF            bbox = m_sceneBBox;

    auto *watcher = new QFutureWatcher<MeshOverviewData>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        MeshOverviewData d = watcher->result();
        watcher->deleteLater();
        m_overviewTris   = std::move(d.overviewTris);
        m_trisBySizeDesc = std::move(d.trisBySizeDesc);
        m_nativeTriSpan  = d.nativeTriSpan;
        m_overviewBuildRunning = false;
        emit overviewBuildFinished(true);
        // Both pipelines redraw with the fresh pyramid.
        if (m_graphicsItem) m_graphicsItem->geometryChanged();
        emit repaintRequested();
    });
    watcher->setFuture(QtConcurrent::run([tris, bbox]() {
        return buildMeshOverviewData(tris, bbox);
    }));
}


// MeshSpatialGrid — definitions live in src/layers/meshspatialgrid.cpp so
// the spatial-index logic can be unit-tested without linking the whole
// layer. See include/layers/meshspatialgrid.h for the storage layout and
// threading contract.

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::setActiveMesh(bool active)
{
    if (m_active == active) return;
    m_active = active;
    emit activeMeshChanged(m_active);
    emit repaintRequested();
}

void SWMM2DMeshLayer::setQsgOwnsRendering(bool own)
{
    if (m_qsgOwnsRendering == own) return;
    m_qsgOwnsRendering = own;
    // Both pipelines need a repaint: the CPU item must clear (or redraw)
    // and the canvas must regrab the QSG frame.
    if (m_graphicsItem) m_graphicsItem->geometryChanged();
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Slice §V.VA — mesh-editing foundation
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::resizeBCsToMesh()
{
    // NB: `slots` is a Qt keyword macro — pick a different local name.
    const int nt = m_mesh.triangles.size();
    const int nslots = nt * 3;
    if (m_bc.size() != nslots) {
        m_bc.resize(nslots);
        // Default-constructed MeshEdgeBC == Wall + zero params + empty group.
    }

    // Precompute boundary status from triangle adjacency — logic lives in
    // buildBoundaryFlags so the deferred background build shares it.
    m_isBoundary = buildBoundaryFlags(m_mesh);
}

void SWMM2DMeshLayer::rebuildVertexAdjacency()
{
    // Logic lives in buildVertexAdjacency so the deferred background build
    // (finishSceneGeometryAsync) shares it.
    buildVertexAdjacency(m_mesh, m_vertTriPtr, m_vertTriIdx);
}

int SWMM2DMeshLayer::pickVertexAt(double sx, double sy,
                                   double tolPx, double pxPerSceneUnit) const
{
    if (m_sceneNodes.isEmpty() || pxPerSceneUnit <= 0.0) return -1;
    const double tolScene = tolPx / pxPerSceneUnit;
    const double tolSq = tolScene * tolScene;
    int best = -1;
    double bestSq = tolSq;
    for (int i = 0; i < m_sceneNodes.size(); ++i) {
        const double dx = m_sceneNodes[i].pt.x() - sx;
        const double dy = m_sceneNodes[i].pt.y() - sy;
        const double d2 = dx * dx + dy * dy;
        if (d2 <= bestSq) {
            bestSq = d2;
            best = i;
        }
    }
    return best;
}

int SWMM2DMeshLayer::pickEdgeAt(double sx, double sy,
                                 double tolPx, double pxPerSceneUnit,
                                 bool boundaryOnly) const
{
    if (m_sceneNodes.isEmpty() || pxPerSceneUnit <= 0.0) return -1;
    const double tolScene = tolPx / pxPerSceneUnit;
    const double tolSq = tolScene * tolScene;

    const int nt = m_mesh.triangles.size();
    int best = -1;
    double bestSq = tolSq;
    for (int t = 0; t < nt; ++t) {
        const auto &tri = m_mesh.triangles[t];
        if (tri.v0 < 0 || tri.v0 >= m_sceneNodes.size()) continue;
        if (tri.v1 < 0 || tri.v1 >= m_sceneNodes.size()) continue;
        if (tri.v2 < 0 || tri.v2 >= m_sceneNodes.size()) continue;

        const QPointF &p0 = m_sceneNodes[tri.v0].pt;
        const QPointF &p1 = m_sceneNodes[tri.v1].pt;
        const QPointF &p2 = m_sceneNodes[tri.v2].pt;
        // Edge local e is opposite vertex e (matches engine convention).
        const QPointF *endpoints[3][2] = {
            {&p1, &p2}, {&p2, &p0}, {&p0, &p1}
        };
        for (int e = 0; e < 3; ++e) {
            const int flat = t * 3 + e;
            if (boundaryOnly && (flat >= m_isBoundary.size() || !m_isBoundary[flat])) continue;
            const QPointF &a = *endpoints[e][0];
            const QPointF &b = *endpoints[e][1];
            const double dx = b.x() - a.x();
            const double dy = b.y() - a.y();
            const double lenSq = dx * dx + dy * dy;
            if (lenSq <= 0.0) continue;
            double u = ((sx - a.x()) * dx + (sy - a.y()) * dy) / lenSq;
            u = qBound(0.0, u, 1.0);
            const double px = a.x() + u * dx;
            const double py = a.y() + u * dy;
            const double ex = sx - px, ey = sy - py;
            const double d2 = ex * ex + ey * ey;
            if (d2 <= bestSq) {
                bestSq = d2;
                best = flat;
            }
        }
    }
    return best;
}

int SWMM2DMeshLayer::locateTriangleAt(double sx, double sy) const
{
    // Bbox cull + barycentric point-in-triangle test for one triangle index.
    auto hits = [&](int t) -> bool {
        const SceneTri &tri = m_sceneTris[t];
        const double minX = std::min({tri.a.x(), tri.b.x(), tri.c.x()});
        const double maxX = std::max({tri.a.x(), tri.b.x(), tri.c.x()});
        if (sx < minX || sx > maxX) return false;
        const double minY = std::min({tri.a.y(), tri.b.y(), tri.c.y()});
        const double maxY = std::max({tri.a.y(), tri.b.y(), tri.c.y()});
        if (sy < minY || sy > maxY) return false;

        // Barycentric in (sx,sy):
        const double v0x = tri.c.x() - tri.a.x(), v0y = tri.c.y() - tri.a.y();
        const double v1x = tri.b.x() - tri.a.x(), v1y = tri.b.y() - tri.a.y();
        const double v2x = sx - tri.a.x(),        v2y = sy - tri.a.y();
        const double d00 = v0x * v0x + v0y * v0y;
        const double d01 = v0x * v1x + v0y * v1y;
        const double d11 = v1x * v1x + v1y * v1y;
        const double d20 = v2x * v0x + v2y * v0y;
        const double d21 = v2x * v1x + v2y * v1y;
        const double denom = d00 * d11 - d01 * d01;
        if (denom == 0.0) return false;
        const double u = (d11 * d20 - d01 * d21) / denom;
        const double v = (d00 * d21 - d01 * d20) / denom;
        const double w = 1.0 - u - v;
        // Tolerate tiny negative on the boundary.
        return (u >= -1e-9 && v >= -1e-9 && w >= -1e-9);
    };

    // Fast path: the spatial grid (already built for paint culling) narrows the
    // search to the one cell containing the point — O(candidates) instead of a
    // full O(n) scan. The containing triangle is guaranteed to be in that cell.
    if (!m_triGrid.isEmpty()) {
        const int *b = nullptr, *e = nullptr;
        m_triGrid.candidatesAtPoint(sx, sy, b, e);
        for (const int *p = b; p < e; ++p)
            if (hits(*p)) return *p;
        return -1;
    }

    // Fallback: linear scan when the grid isn't built (e.g. degenerate mesh).
    const int nt = m_sceneTris.size();
    for (int t = 0; t < nt; ++t)
        if (hits(t)) return t;
    return -1;
}

int SWMM2DMeshLayer::pickCellAt(const QPointF &scenePt) const
{
    return locateTriangleAt(scenePt.x(), scenePt.y());
}

QVector<int> SWMM2DMeshLayer::pickCellsInRect(const QRectF &sceneRect) const
{
    QVector<int> hits;
    if (sceneRect.isNull() || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const SceneTri &t = m_sceneTris[i];
        const QPointF centroid((t.a.x() + t.b.x() + t.c.x()) / 3.0,
                               (t.a.y() + t.b.y() + t.c.y()) / 3.0);
        if (sceneRect.contains(centroid))
            hits.push_back(i);
    }
    return hits;
}

QVector<int> SWMM2DMeshLayer::pickCellsInPolygon(const QPolygonF &scenePoly) const
{
    QVector<int> hits;
    if (scenePoly.size() < 3 || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const SceneTri &t = m_sceneTris[i];
        const QPointF centroid((t.a.x() + t.b.x() + t.c.x()) / 3.0,
                               (t.a.y() + t.b.y() + t.c.y()) / 3.0);
        if (scenePoly.containsPoint(centroid, Qt::OddEvenFill))
            hits.push_back(i);
    }
    return hits;
}

QVector<double> SWMM2DMeshLayer::elevationSamples(int maxSamples) const
{
    const int nv = m_mesh.vertices.size();
    QVector<double> out;
    if (nv == 0) return out;

    // Stride so very large meshes stay responsive under a "resample" click;
    // step 1 (every vertex) for small meshes.
    const int cap  = std::max(1, maxSamples);
    const int step = (nv > cap) ? (nv + cap - 1) / cap : 1;
    out.reserve((nv + step - 1) / step);
    for (int i = 0; i < nv; i += step) {
        const double z = m_mesh.vertices[i].z;
        if (std::isfinite(z)) out.push_back(z);
    }
    return out;
}

double SWMM2DMeshLayer::sampleZAt(double sx, double sy) const
{
    const int t = locateTriangleAt(sx, sy);
    if (t < 0) return std::numeric_limits<double>::quiet_NaN();
    const SceneTri &tri = m_sceneTris[t];
    // Recompute barycentric weights and blend Z.
    const double v0x = tri.c.x() - tri.a.x(), v0y = tri.c.y() - tri.a.y();
    const double v1x = tri.b.x() - tri.a.x(), v1y = tri.b.y() - tri.a.y();
    const double v2x = sx - tri.a.x(),        v2y = sy - tri.a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    if (denom == 0.0) return std::numeric_limits<double>::quiet_NaN();
    const double u = (d11 * d20 - d01 * d21) / denom;
    const double v = (d00 * d21 - d01 * d20) / denom;
    const double w = 1.0 - u - v;
    // Weights: a→w, b→v, c→u (mirrors the v0/v1 construction above).
    return w * tri.z0 + v * tri.z1 + u * tri.z2;
}

bool SWMM2DMeshLayer::isBoundaryEdge(int triIdx, int edgeLocal) const
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2) return false;
    const int flat = triIdx * 3 + edgeLocal;
    if (flat < 0 || flat >= m_isBoundary.size()) return false;
    return m_isBoundary[flat];
}

bool SWMM2DMeshLayer::applyMeshVertexZ(int vertexIdx, double z)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].z == z) return true;
    m_mesh.vertices[vertexIdx].z = z;

    // Incremental update. Changing an elevation moves nothing in XY, so the
    // spatial grids, triangle/edge bounding boxes, the area-based size-sorted
    // LOD index and the overview *cell layout* are all unchanged. We only need
    // to refresh the z-dependent fields of the elements incident to this
    // vertex — O(incident) — instead of the full O(N) rebuildSceneGeometry(),
    // which on a multi-million-cell mesh turned one edit (or one per selected
    // vertex) into a multi-second stall. Falls back to a full rebuild if the
    // scene caches aren't in the expected 1:1 shape (e.g. some triangles were
    // skipped as degenerate during the last full build).
    const bool canIncremental =
        m_sceneTris.size() == m_mesh.triangles.size()
        && vertexIdx < m_sceneNodes.size()
        && (vertexIdx + 1) < m_vertTriPtr.size();
    if (canIncremental) {
        m_sceneNodes[vertexIdx].z = float(z);
        const int beg = m_vertTriPtr[vertexIdx];
        const int end = m_vertTriPtr[vertexIdx + 1];
        for (int k = beg; k < end; ++k) {
            const int ti = m_vertTriIdx[k];
            if (ti < 0 || ti >= m_sceneTris.size()) continue;
            const auto &mt = m_mesh.triangles[ti];
            SceneTri &st = m_sceneTris[ti];
            st.z0   = float(m_mesh.vertices[mt.v0].z);
            st.z1   = float(m_mesh.vertices[mt.v1].z);
            st.z2   = float(m_mesh.vertices[mt.v2].z);
            st.zAvg = (st.z0 + st.z1 + st.z2) / 3.0f;
        }
        // Keep the elevation range a valid superset (expand only). A loosened
        // range slightly compresses the colour ramp until the next full
        // rebuild; re-tightening exactly would cost O(N). Incident mesh edges'
        // slope styling and the far-zoom overview colour are left until the
        // next full rebuild (both are secondary to the fill/hillshade update).
        if (z < m_zMin) m_zMin = z;
        if (z > m_zMax) m_zMax = z;
    } else if (!m_sceneGeomComplete) {
        // Progressive load still finishing (no adjacency yet) — refresh the
        // light caches only; the background build delivers the rest.
        rebuildSceneGeometryLight();
        finishSceneGeometryAsync();
    } else {
        rebuildSceneGeometry();
    }

    // The renderer caches shaded per-triangle RGB, isobands and isolines behind
    // a geomRevision-keyed check (SWMM2DMeshQSGRenderer). The incremental
    // branch above changes z0/z1/z2/zAvg — the exact inputs to the hillshade —
    // without going through rebuildSceneGeometry(), which is where the bump
    // normally lives. Without this the caches report a hit and re-upload the
    // pre-edit colours: repaintRequested fires, the frame redraws, and the
    // shading does not change. Unconditional (the rebuild branches bump too);
    // a redundant increment costs nothing but a cache miss.
    ++m_geomRevision;

    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    emit repaintRequested();
    return true;
}

bool SWMM2DMeshLayer::applyMeshEdgeBC(int triIdx, int edgeLocal, const mesh::MeshEdgeBC &bc)
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2) return false;
    if (triIdx >= m_mesh.triangles.size()) return false;
    const int flat = triIdx * 3 + edgeLocal;
    if (flat >= m_bc.size()) return false;
    if (m_bc[flat] == bc) return true;
    m_bc[flat] = bc;
    emit attributeChanged(mesh::MeshObjectRef::edge(m_sourcePath, triIdx, edgeLocal).name);
    emit repaintRequested();
    return true;
}

QPair<int,int> SWMM2DMeshLayer::findEdgeNeighbour(int triIdx, int edgeLocal) const
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2) return {-1, -1};
    if (triIdx >= m_mesh.triangles.size())             return {-1, -1};
    // Progressive load — adjacency not built yet (deferred heavy geometry).
    if (m_vertTriPtr.size() != m_mesh.vertices.size() + 1) return {-1, -1};
    const auto &tri = m_mesh.triangles[triIdx];
    // Local edge convention matches resizeBCsToMesh's edge-use scan:
    //   edge 0 = (v1, v2),  edge 1 = (v2, v0),  edge 2 = (v0, v1).
    int va = -1, vb = -1;
    switch (edgeLocal) {
    case 0: va = tri.v1; vb = tri.v2; break;
    case 1: va = tri.v2; vb = tri.v0; break;
    case 2: va = tri.v0; vb = tri.v1; break;
    }
    if (va < 0 || vb < 0 || va >= m_vertTriPtr.size() - 1) return {-1, -1};
    // Walk triangles incident to va; the neighbour must also be incident to vb.
    const int beg = m_vertTriPtr[va];
    const int end = m_vertTriPtr[va + 1];
    for (int k = beg; k < end; ++k) {
        const int t2 = m_vertTriIdx[k];
        if (t2 == triIdx || t2 < 0 || t2 >= m_mesh.triangles.size()) continue;
        const auto &t = m_mesh.triangles[t2];
        // Edge (va, vb) in t2 — direction doesn't matter.
        if      ((t.v1 == va && t.v2 == vb) || (t.v1 == vb && t.v2 == va)) return {t2, 0};
        else if ((t.v2 == va && t.v0 == vb) || (t.v2 == vb && t.v0 == va)) return {t2, 1};
        else if ((t.v0 == va && t.v1 == vb) || (t.v0 == vb && t.v1 == va)) return {t2, 2};
    }
    return {-1, -1};  // boundary edge — no neighbour
}

bool SWMM2DMeshLayer::applyMeshEdgeConveyance(int triIdx, int edgeLocal, double conveyance)
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2)        return false;
    if (triIdx >= m_mesh.triangles.size())                    return false;
    if (!(conveyance >= 0.0 && conveyance <= 1.0))            return false;
    const int flat = triIdx * 3 + edgeLocal;
    if (flat >= m_bc.size())                                  return false;

    bool changed = false;
    if (m_bc[flat].conveyance != conveyance) {
        m_bc[flat].conveyance = conveyance;
        changed = true;
    }
    // Mirror to the neighbour slot when this is an interior edge — keeps
    // the GUI in sync with the engine's symmetry invariant (the engine's
    // post-parse drain does the same thing).
    const auto nbr = findEdgeNeighbour(triIdx, edgeLocal);
    if (nbr.first >= 0 && nbr.second >= 0) {
        const int nflat = nbr.first * 3 + nbr.second;
        if (nflat < m_bc.size() && m_bc[nflat].conveyance != conveyance) {
            m_bc[nflat].conveyance = conveyance;
            changed = true;
        }
    }
    if (!changed) return true;
    emit attributeChanged(mesh::MeshObjectRef::edge(m_sourcePath, triIdx, edgeLocal).name);
    emit repaintRequested();
    return true;
}

void SWMM2DMeshLayer::setHighlightedVertices(const QSet<int> &indices)
{
    if (m_selVertices == indices) return;
    m_selVertices = indices;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHighlightedEdges(const QSet<int> &flatIndices)
{
    if (m_selEdges == flatIndices) return;
    m_selEdges = flatIndices;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHighlightedTriangles(const QSet<int> &indices)
{
    if (m_selTriangles == indices) return;
    m_selTriangles = indices;
    emit repaintRequested();
}

bool SWMM2DMeshLayer::applyMeshVertexTag(int vertexIdx, const QString &tag)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].tag == tag) return true;
    m_mesh.vertices[vertexIdx].tag = tag;
    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    emit repaintRequested();
    return true;
}

bool SWMM2DMeshLayer::applyMeshVertexCoupledNode(int vertexIdx, const QString &node)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].coupledNode == node) return true;
    m_mesh.vertices[vertexIdx].coupledNode = node;
    // Clearing the coupling resets Cd/Area so a re-coupled vertex starts
    // from the documented engine defaults.
    if (node.isEmpty()) {
        m_mesh.vertices[vertexIdx].couplingCd   = 0.65;
        m_mesh.vertices[vertexIdx].couplingArea = 1.0;
    }
    // The scene node "tagged" flag tracks coupling — repaint so the
    // SWMM-coupled-vertex glyph reflects the change.
    if (vertexIdx < m_sceneNodes.size())
        m_sceneNodes[vertexIdx].tagged = !node.isEmpty();
    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    emit repaintRequested();
    return true;
}

QVector<mesh::CellCoupling> SWMM2DMeshLayer::applyCellCouplings(
    const QVector<mesh::CellCoupling> &rows)
{
    QVector<mesh::CellCoupling> previous = m_mesh.cellCouplings;
    QVector<mesh::CellCoupling> cleaned;
    cleaned.reserve(rows.size());
    for (const mesh::CellCoupling &cc : rows)
    {
        if (cc.tri < 0 || cc.tri >= m_mesh.triangles.size()) continue;
        if (cc.nodeId.isEmpty()) continue;
        cleaned.append(cc);
    }
    m_mesh.cellCouplings = cleaned;
    emit repaintRequested();
    return previous;
}

bool SWMM2DMeshLayer::applyMeshVertexCouplingCd(int vertexIdx, double cd)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].coupledNode.isEmpty()) return false;
    if (!(cd > 0.0)) return false;
    if (m_mesh.vertices[vertexIdx].couplingCd == cd) return true;
    m_mesh.vertices[vertexIdx].couplingCd = cd;
    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    return true;
}

bool SWMM2DMeshLayer::applyMeshVertexCouplingArea(int vertexIdx, double area)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].coupledNode.isEmpty()) return false;
    if (!(area > 0.0)) return false;
    if (m_mesh.vertices[vertexIdx].couplingArea == area) return true;
    m_mesh.vertices[vertexIdx].couplingArea = area;
    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    return true;
}

bool SWMM2DMeshLayer::applyMeshTriangleMannings(int triIdx, double mannings)
{
    if (triIdx < 0 || triIdx >= m_mesh.triangles.size()) return false;
    if (!(mannings > 0.0)) return false;
    if (m_mesh.triangles[triIdx].mannings == mannings) return true;
    m_mesh.triangles[triIdx].mannings = mannings;
    emit attributeChanged(mesh::MeshObjectRef::cell(m_sourcePath, triIdx).name);
    emit repaintRequested();
    return true;
}

bool SWMM2DMeshLayer::applyMeshTriangleTag(int triIdx, const QString &tag)
{
    if (triIdx < 0 || triIdx >= m_mesh.triangles.size()) return false;
    if (m_mesh.triangles[triIdx].tag == tag) return true;
    m_mesh.triangles[triIdx].tag = tag;
    emit attributeChanged(mesh::MeshObjectRef::cell(m_sourcePath, triIdx).name);
    emit repaintRequested();
    return true;
}

// ---------------------------------------------------------------------------
// Layer-level Q_PROPERTYs are now thin shims onto sublayer state. The
// sublayer's setVisible() / style->setX() emits invalidated(), which the
// ctor wired to repaintRequested(), so external observers continue to
// see exactly one repaint per logical edit.
// ---------------------------------------------------------------------------

bool SWMM2DMeshLayer::showMeshNodes() const
{
    return m_meshNodeSublayer && m_meshNodeSublayer->isVisible();
}

void SWMM2DMeshLayer::setShowMeshNodes(bool show)
{
    if (m_meshNodeSublayer) m_meshNodeSublayer->setVisible(show);
}

bool SWMM2DMeshLayer::showEdges() const
{
    return m_meshEdgeSublayer && m_meshEdgeSublayer->isVisible();
}

void SWMM2DMeshLayer::setShowEdges(bool show)
{
    if (m_meshEdgeSublayer) m_meshEdgeSublayer->setVisible(show);
}

void SWMM2DMeshLayer::setEdgeZoomMinCellPx(double px)
{
    px = qBound(0.0, px, 500.0);
    if (qFuzzyCompare(m_edgeMinCellPx + 1.0, px + 1.0)) return;
    m_edgeMinCellPx = px;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setVertexZoomMinCellPx(double px)
{
    px = qBound(0.0, px, 500.0);
    if (qFuzzyCompare(m_vertexMinCellPx + 1.0, px + 1.0)) return;
    m_vertexMinCellPx = px;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHillshadeAzimuth(double degrees)
{
    // Wrap into [0, 360) so the UI can spin past the boundary cleanly.
    while (degrees <  0.0) degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;
    if (qFuzzyCompare(m_hillshadeAz, degrees)) return;
    m_hillshadeAz = degrees;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHillshadeAltitude(double degrees)
{
    degrees = qBound(0.0, degrees, 90.0);
    if (qFuzzyCompare(m_hillshadeAlt, degrees)) return;
    m_hillshadeAlt = degrees;
    emit repaintRequested();
}

// Slice U-? — hillshadeZExag and hillshadeMinLit are stored as a single
// hillshadeStrength on the mesh-fill sublayer style. We map the existing
// (zExag, minLit) API onto two virtual axes of that knob so the existing
// dialog and saved JSON continue to work without exposing two near-
// duplicate sliders. zExag drives visible relief, minLit clamps the
// shadow floor; we round-trip through the style by storing zExag in
// hillshadeStrength (0..1 := zExag/10) and keeping minLit in a local
// shadow field that the renderer reads back from the layer. The shim
// keeps the existing dialog editable while we phase in a richer style.
double SWMM2DMeshLayer::hillshadeZExag() const
{
    if (m_meshFillSublayer && m_meshFillSublayer->fillStyle())
        return m_meshFillSublayer->fillStyle()->hillshadeStrength() * 10.0;
    return 3.0;
}

double SWMM2DMeshLayer::hillshadeMinLit() const
{
    return m_hillshadeMinLit;
}

void SWMM2DMeshLayer::setHillshadeZExag(double factor)
{
    factor = qBound(0.0, factor, 100.0);
    if (m_meshFillSublayer && m_meshFillSublayer->fillStyle())
        m_meshFillSublayer->fillStyle()->setHillshadeStrength(factor / 10.0);
    else
        emit repaintRequested();
}

void SWMM2DMeshLayer::setHillshadeMinLit(double minLit)
{
    minLit = qBound(0.0, minLit, 1.0);
    if (qFuzzyCompare(m_hillshadeMinLit, minLit)) return;
    // Plain per-layer member. This was a `thread_local` namespace global:
    // the GUI thread's edits were invisible to the scene-graph render
    // thread, so the Shadow-floor control silently never reached the live
    // QSG terrain fill (and all mesh layers shared one value).
    m_hillshadeMinLit = minLit;
    emit repaintRequested();
}

// Bed-elevation contour setters — forward to the isoline sublayer.
bool SWMM2DMeshLayer::showContours() const
{
    return m_isolineSublayer && m_isolineSublayer->isVisible();
}

int SWMM2DMeshLayer::contourIntervalCount() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->isoValueCount();
    return 10;
}

QColor SWMM2DMeshLayer::contourColor() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->color();
    return QColor(0x1a, 0x1a, 0x1a, 200);
}

double SWMM2DMeshLayer::contourLineWidth() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->lineWidthPx();
    return 1.0;
}

void SWMM2DMeshLayer::setShowContours(bool show)
{
    if (m_isolineSublayer) m_isolineSublayer->setVisible(show);
}

void SWMM2DMeshLayer::setContourIntervalCount(int n)
{
    n = qBound(1, n, 200);
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        m_isolineSublayer->isolineStyle()->setIsoValueCount(n);
    if (m_contourBandSublayer && m_contourBandSublayer->bandStyle())
        m_contourBandSublayer->bandStyle()->setBandCount(n);
}

void SWMM2DMeshLayer::setContourColor(const QColor &c)
{
    if (!c.isValid()) return;
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        m_isolineSublayer->isolineStyle()->setColor(c);
}

void SWMM2DMeshLayer::setContourLineWidth(double widthPx)
{
    widthPx = qBound(0.25, widthPx, 10.0);
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        m_isolineSublayer->isolineStyle()->setLineWidthPx(widthPx);
}

// Filled iso-bands — forward to the contour-band sublayer.
bool SWMM2DMeshLayer::filledContours() const
{
    return m_contourBandSublayer && m_contourBandSublayer->isVisible();
}

double SWMM2DMeshLayer::filledContoursOpacity() const
{
    return m_contourBandSublayer ? m_contourBandSublayer->opacity() : 0.55;
}

void SWMM2DMeshLayer::setFilledContours(bool filled)
{
    if (m_contourBandSublayer) m_contourBandSublayer->setVisible(filled);
}

void SWMM2DMeshLayer::setFilledContoursOpacity(double a)
{
    a = qBound(0.0, a, 1.0);
    if (m_contourBandSublayer) m_contourBandSublayer->setOpacity(a);
}

QList<OpenSWMM::Render::ISublayer *> SWMM2DMeshLayer::sublayers() const
{
    // Paint order is list order (bottom-up):
    //   fill (static)         → terrain base
    //   contour bands         → filled isobands above hillshade
    //   edges                 → mesh wireframe
    //   isolines              → labelled contour lines
    //   vertex markers (top)  → coupled-node glyphs
    // Slice GUI-2026-05-30 §2 — order is user-customisable and cached in
    // m_sublayerOrder; seeded once from the defaults above.
    if (m_sublayerOrder.isEmpty()) {
        OpenSWMM::Render::ISublayer *defaults[] = {
            m_meshFillSublayer,
            m_contourBandSublayer,
            m_meshEdgeSublayer,
            m_isolineSublayer,
            m_meshNodeSublayer,
        };
        for (auto *s : defaults)
            if (s) m_sublayerOrder.append(s);
    }
    return m_sublayerOrder;
}

bool SWMM2DMeshLayer::moveSublayer(int from, int to)
{
    (void) sublayers();      // force lazy seed
    if (from < 0 || from >= m_sublayerOrder.size()
        || to   < 0 || to   >= m_sublayerOrder.size()
        || from == to)
        return false;
    m_sublayerOrder.move(from, to);
    emit repaintRequested();
    return true;
}

void SWMM2DMeshLayer::populateScene(QGraphicsScene *scene,
                                     const MapExtent &,
                                     const SpatialReferenceSystem *)
{
    if (!scene || m_graphicsItem) return;
    m_graphicsItem = new SWMM2DMeshGraphicsItem(this);
    scene->addItem(m_graphicsItem);
}

void SWMM2DMeshLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!m_graphicsItem) return;
    if (scene) scene->removeItem(m_graphicsItem);
    delete m_graphicsItem;
    m_graphicsItem = nullptr;
}

void SWMM2DMeshLayer::refreshScene(QGraphicsScene *scene,
                                    const MapExtent &extent,
                                    const SpatialReferenceSystem *canvasSRS)
{
    if (!scene) return;

    if (!isVisible()) {
        depopulateScene(scene);
        return;
    }

    if (!m_graphicsItem)
        populateScene(scene, extent, canvasSRS);
    else
        m_graphicsItem->update();
}

void SWMM2DMeshLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    OGRCoordinateTransformation::DestroyCT(m_transform);
    m_transform = nullptr;

    if (srs() && newCanvasSRS
        && srs()->ogrSpatialReference()
        && newCanvasSRS->ogrSpatialReference()
        && !srs()->ogrSpatialReference()->IsSame(newCanvasSRS->ogrSpatialReference()))
    {
        m_transform = OGRCreateCoordinateTransformation(
            srs()->ogrSpatialReference(),
            newCanvasSRS->ogrSpatialReference());
    }

    rebuildSceneGeometry();
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Mesh layer's own Q_PROPERTYs form the first subject (the legacy
// Display/Hillshade/Contours tabs). Each individually-stylable sublayer
// is added as a separate subject so the dialog surfaces its full style
// bag (colours, marker sizes, dash patterns, label toggles, ...) rather
// than just the layer-level boolean shims.
// ---------------------------------------------------------------------------

// ─── Slice B.5b — Rule Model mirror over mesh decoration state ─────────

OpenSWMM::Render::RuleList *SWMM2DMeshLayer::ruleList()
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

const OpenSWMM::Render::RuleList *SWMM2DMeshLayer::ruleList() const
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

void SWMM2DMeshLayer::buildRuleListLazy() const
{
    using namespace OpenSWMM::Render;

    auto *self = const_cast<SWMM2DMeshLayer *>(this);
    m_ruleList = std::make_unique<RuleList>(self);

    // Seed one Rule per archetype the mesh layer paints. Each Rule
    // wraps a SingleSymbolRenderer whose SymbolStyle layers carry the
    // Z.6 typed spec props. The QSG renderer (Slice Z.6a step 2) reads
    // these as specs from the legacy sublayer style fields each frame;
    // the connect handlers below (Slice Z.6a step 3) translate Rule
    // edits back into those legacy fields so dialog changes drive paint.
    //
    // Six seeds — "Contour bands" and "Contour lines" are separate Rules
    // because the QSG renderer paints them as two passes (filled iso-
    // bands + iso-line strokes), each driven by its own ContourSpec.
    struct Seed { const char *name; SymbolLayer (*build)(); };
    const Seed seeds[] = {
        {"Mesh fill",
            [] { return RasterColorRampSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Hillshade",
            [] { return HillshadeSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Contour bands",
            [] {
                ContourSymbolLayerSpec s;
                s.mode = ContourMode::Filled;
                return s.toSymbolLayer();
            }},
        {"Contour lines",
            [] {
                ContourSymbolLayerSpec s;
                s.mode = ContourMode::Lines;
                return s.toSymbolLayer();
            }},
        {"Mesh edges",
            [] { return MeshEdgeSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Mesh nodes",
            [] { return MeshNodeSymbolLayerSpec{}.toSymbolLayer(); }},
    };

    QVector<Rule *> ruleHandles;
    ruleHandles.reserve(int(sizeof(seeds) / sizeof(*seeds)));
    for (const Seed &s : seeds) {
        auto single = std::make_unique<SingleSymbolRenderer>();
        SymbolStyle style = single->symbol();
        style.layers.clear();
        style.layers.append(s.build());
        single->setSymbol(style);
        Rule *r = m_ruleList->append(std::make_unique<Rule>(
            QString::fromLatin1(s.name), std::move(single)));
        ruleHandles.append(r);
    }

    // ── Slice Z.6a step 3 — Rule → legacy-style propagation ─────────────
    //
    // When the user edits a Rule's SymbolStyle props through the dialog
    // (SymbolStyleAdapter → notifyRendererStateChanged), rendererReplaced
    // fires. Each handler below extracts the typed spec from the Rule's
    // first SymbolLayer and writes the relevant fields back onto the
    // matching legacy sublayer style (or onto the mesh layer's hillshade
    // members). Setting those fields emits invalidated() on the SublayerStyle,
    // which the ctor's wire() lambda routes to repaintRequested(); the
    // SWMM2DMeshQSGRenderer then re-samples those legacy fields into its
    // per-frame specs on the next rebuild.
    //
    // Renderer swaps (graduated / categorised) silently no-op: the typed
    // mesh-decoration specs don't apply to those classes, and the paint
    // pipeline continues reading the current legacy field values.
    auto firstLayer = [](Rule *r) -> const SymbolLayer * {
        if (!r) return nullptr;
        auto *single = dynamic_cast<const SingleSymbolRenderer *>(r->renderer());
        if (!single) return nullptr;
        if (single->symbol().layers.isEmpty()) return nullptr;
        return &single->symbol().layers.first();
    };

    // ── Mesh fill (RasterColorRampSpec) ────────────────────────────────
    QObject::connect(ruleHandles[0], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[0]);
            if (!layer || layer->kind != SymbolLayerKind::RasterColorRamp) return;
            const auto spec = RasterColorRampSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshFillSublayer) {
                sub->setOpacity(spec.opacity);
                if (auto *st = sub->fillStyle())
                    st->setFillColor(spec.noDataColor);
            }
        });

    // ── Hillshade (HillshadeSpec) — drives both the mesh-layer hillshade
    //    transform (azimuth / altitude / zexag / shadow floor) and the
    //    fill-style's hillshade-strength multiplier. ─────────────────────
    QObject::connect(ruleHandles[1], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[1]);
            if (!layer || layer->kind != SymbolLayerKind::Hillshade) return;
            const auto spec = HillshadeSymbolLayerSpec::fromSymbolLayer(*layer);
            self->setHillshadeAzimuth(spec.azimuthDeg);
            self->setHillshadeAltitude(spec.altitudeDeg);
            self->setHillshadeZExag(spec.zExaggeration);
            self->setHillshadeMinLit(spec.shadowFloor);
            if (auto *fill = self->m_meshFillSublayer; fill && fill->fillStyle())
                fill->fillStyle()->setHillshadeStrength(spec.strength);
        });

    // ── Contour bands (ContourSpec mode=Filled) ─────────────────────────
    QObject::connect(ruleHandles[2], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[2]);
            if (!layer || layer->kind != SymbolLayerKind::Contour) return;
            const auto spec = ContourSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_contourBandSublayer; sub && sub->bandStyle()) {
                auto *st = sub->bandStyle();
                st->setBandCount(spec.binner.binCount());
                st->setSmoothBands(spec.smoothBands);
                if (!spec.ramp.stops.isEmpty()) {
                    st->setLowColor(spec.ramp.stops.first().second);
                    st->setHighColor(spec.ramp.stops.last().second);
                }
            }
        });

    // ── Contour lines (ContourSpec mode=Lines) ──────────────────────────
    QObject::connect(ruleHandles[3], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[3]);
            if (!layer || layer->kind != SymbolLayerKind::Contour) return;
            const auto spec = ContourSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_isolineSublayer; sub && sub->isolineStyle()) {
                auto *st = sub->isolineStyle();
                st->setIsoValueCount(spec.binner.binCount());
                st->setColor(spec.lineColor);
                st->setLineWidthPx(spec.lineWidthPx);
                st->setLabels(spec.labelEveryN > 0);
            }
        });

    // ── Mesh edges (MeshEdgeSpec) ───────────────────────────────────────
    QObject::connect(ruleHandles[4], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[4]);
            if (!layer || layer->kind != SymbolLayerKind::MeshEdge) return;
            const auto spec = MeshEdgeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshEdgeSublayer; sub && sub->edgeStyle()) {
                auto *st = sub->edgeStyle();
                st->setColor(spec.color);
                st->setLineWidthPx(spec.width);
                st->setDashPattern(spec.penStyle);
                st->setUseSlopeDrivenWidth(spec.useSlopeDrivenWidth);
                st->setSlopeBreak(spec.slopeBreak);
                st->setWideWidthPx(spec.wideWidthPx);
                st->setWideColor(spec.wideColor);
            }
        });

    // ── Mesh nodes (MeshNodeSpec wraps MarkerSpec) ──────────────────────
    //
    // MeshNodeStyle::MarkerShape only models 4 shapes (Circle / Square /
    // Triangle / Diamond — integer values 0..3 match Render::MarkerShape).
    // Richer Z.4 shapes round-trip through the SymbolLayer props but fall
    // back to Circle on the legacy sublayer; a future mesh-node renderer
    // that consumes the spec directly will pick up the richer value.
    QObject::connect(ruleHandles[5], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[5]);
            if (!layer || layer->kind != SymbolLayerKind::MeshNode) return;
            const auto spec = MeshNodeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshNodeSublayer; sub && sub->nodeStyle()) {
                auto *st = sub->nodeStyle();
                st->setColor(spec.marker.fillColor);
                st->setMarkerSizePx(spec.marker.sizePx);
                st->setOutlineColor(spec.marker.outlineColor);
                st->setOutlineWidthPx(spec.marker.outlineWidth);
                int shapeIdx = static_cast<int>(spec.marker.shape);
                if (shapeIdx < 0 || shapeIdx > 3) shapeIdx = 0;
                st->setShape(static_cast<MeshNodeStyle::MarkerShape>(shapeIdx));
            }
        });
}

std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
SWMM2DMeshLayer::styleSubjects()
{
    using namespace OpenSWMM::Render;
    using openswmmvis::ui::ILayerStyleSubject;
    using openswmmvis::ui::LayerStyleSubject;
    std::vector<std::unique_ptr<ILayerStyleSubject>> out;

    // Layer-level terrain styling (hillshade light + bed-elevation contour
    // controls) keeps its dedicated MeshHillshadeEditor, registered for the
    // SWMM2DMeshLayer class — so the layer itself is the propertyObject.
    out.push_back(std::make_unique<LayerStyleSubject>(
        tr("Mesh / TIN"), this,
        QStringLiteral("mesh.layer"),
        QString()));

    // Per-sublayer styling now mounts the registered 2D adapter editors
    // (color pickers / combos via symbolstyleeditors2d) instead of the
    // generic QPropertyModel grid — which lacks a QColor item and so showed
    // no colour picker. The adapter edits the matching Rule; the Rule →
    // legacy-style back-prop wired in buildRuleListLazy() applies the change
    // to the painted sublayer style.
    //
    // The Rule specs are default-seeded in buildRuleListLazy(), so before
    // handing an adapter to the dialog we forward-seed each Rule's
    // SymbolLayer from the *current* legacy sublayer style. Without this the
    // editor would open on defaults and the back-prop would reset the user's
    // styling on the first edit.
    if (!m_ruleList)
        buildRuleListLazy();

    const QString sect = tr("Sublayers");

    // Replace a Rule's single SymbolLayer with one carrying the current
    // legacy-style values (so the adapter/editor opens on real values).
    auto setRuleLayer = [](Rule *r, const SymbolLayer &sl) {
        if (!r) return;
        auto *single = dynamic_cast<SingleSymbolRenderer *>(r->renderer());
        if (!single) return;
        SymbolStyle sym = single->symbol();
        sym.layers.clear();
        sym.layers.append(sl);
        single->setSymbol(sym);
    };
    auto addAdapter = [&](Rule *r, const QString &title, const QString &id) {
        if (!r) return;
        if (QObject *adapter = SymbolStyleAdapter::createFor(r, this))
            out.push_back(std::make_unique<LayerStyleSubject>(title, adapter, id, sect));
    };

    const int n = m_ruleList->count();
    Rule *rEdge = n > 4 ? m_ruleList->at(4) : nullptr;  // Mesh edges
    Rule *rNode = n > 5 ? m_ruleList->at(5) : nullptr;  // Mesh nodes

    // The fill, contour-band and contour-line sublayers are intentionally NOT
    // exposed here: their colour band / classification / sampling is edited
    // solely by the "Mesh / TIN" tab's ClassificationEditors (MeshFillStyle /
    // ContourBandStyle scheme + IsolineStyle). The SymbolStyleAdapter path can
    // only carry a flat low/high gradient, which cannot represent the scheme
    // and previously duplicated (and, for bands, fought) the Mesh/TIN editors.
    // Only edges and vertex markers — whose colour/width/dash/marker styling
    // lives nowhere else — remain in the Sublayers section.

    // Mesh edges.
    if (rEdge && m_meshEdgeSublayer && m_meshEdgeSublayer->edgeStyle()) {
        auto *st = m_meshEdgeSublayer->edgeStyle();
        MeshEdgeSymbolLayerSpec spec;
        spec.color               = st->color();
        spec.width               = st->lineWidthPx();
        spec.penStyle            = st->dashPattern();
        spec.useSlopeDrivenWidth = st->useSlopeDrivenWidth();
        spec.slopeBreak          = st->slopeBreak();
        spec.wideWidthPx         = st->wideWidthPx();
        spec.wideColor           = st->wideColor();
        setRuleLayer(rEdge, spec.toSymbolLayer());
        addAdapter(rEdge, m_meshEdgeSublayer->displayName(), m_meshEdgeSublayer->id());
    }

    // Mesh nodes (vertex markers).
    if (rNode && m_meshNodeSublayer && m_meshNodeSublayer->nodeStyle()) {
        auto *st = m_meshNodeSublayer->nodeStyle();
        MeshNodeSymbolLayerSpec spec;
        spec.marker.fillColor    = st->color();
        spec.marker.sizePx       = st->markerSizePx();
        spec.marker.outlineColor = st->outlineColor();
        spec.marker.outlineWidth = st->outlineWidthPx();
        spec.marker.shape        = static_cast<MarkerShape>(static_cast<int>(st->shape()));
        setRuleLayer(rNode, spec.toSymbolLayer());
        addAdapter(rNode, m_meshNodeSublayer->displayName(), m_meshNodeSublayer->id());
    }

    return out;
}
