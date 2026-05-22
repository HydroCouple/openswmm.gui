/*!
 * \file   swmm2dresultslayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice CF.MVP — 2D inundation results layer implementation. The graphics
 * item closely mirrors `SWMM2DMeshGraphicsItem` from
 * [src/layers/swmm2dmeshlayer.cpp]; only the per-triangle colour function
 * differs (depth → inundation ramp instead of elevation → terrain ramp).
 */
#include "layers/swmm2dresultslayer.h"

#include "io/mesh2dh5reader.h"
#include "map/mapextent.h"

#include "render/ifeaturerenderer.h"
#include "render/renderers/graduatedrenderer.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

// ---------------------------------------------------------------------------
// Inundation colour ramp — dry → light cyan → cyan → blue → magenta → yellow
// Stops chosen so wet areas pop visually against the terrain-ramped mesh
// layer below (which is heavily green/brown).
// ---------------------------------------------------------------------------

namespace {

struct Stop { double t; int r, g, b; };
constexpr Stop kInundationStops[] = {
    {0.00, 0x9d, 0xe2, 0xf2},  // light cyan — barely wet
    {0.20, 0x1e, 0x88, 0xe5},  // bright blue
    {0.50, 0x05, 0x3c, 0x8a},  // deep navy
    {0.75, 0xc6, 0x28, 0x28},  // crimson — danger
    {1.00, 0xff, 0xeb, 0x3b},  // yellow — saturated
};

void inundationColorRgba(double depth, double dry_depth, double max_depth,
                          int& r, int& g, int& b, int& a)
{
    if (depth < dry_depth || max_depth <= dry_depth) {
        r = g = b = 0;
        a = 0;
        return;
    }
    const double t = std::clamp((depth - dry_depth) / (max_depth - dry_depth),
                                 0.0, 1.0);
    constexpr int N = int(sizeof(kInundationStops) / sizeof(*kInundationStops));
    int i = 0;
    while (i < N - 2 && kInundationStops[i + 1].t <= t) ++i;
    const Stop& lo = kInundationStops[i];
    const Stop& hi = kInundationStops[i + 1];
    const double f = (hi.t > lo.t) ? (t - lo.t) / (hi.t - lo.t) : 0.0;
    r = int(std::lround(lo.r + f * (hi.r - lo.r)));
    g = int(std::lround(lo.g + f * (hi.g - lo.g)));
    b = int(std::lround(lo.b + f * (hi.b - lo.b)));
    a = 210;
}

// CF.2 — sequential velocity-magnitude ramp (Viridis-inspired). Distinct
// hues from the depth ramp so arrows read clearly against the wet fill.
constexpr Stop kVelocityStops[] = {
    {0.00, 0x44, 0x01, 0x54},  // dark purple
    {0.30, 0x35, 0x60, 0x8d},  // teal-blue
    {0.60, 0x21, 0x90, 0x8c},  // teal
    {0.85, 0x5e, 0xc9, 0x62},  // lime
    {1.00, 0xfd, 0xe7, 0x25},  // yellow
};

void velocityColorRgb(double vmag, double max_v, int& r, int& g, int& b)
{
    const double tv = (max_v > 1e-9)
                        ? std::clamp(vmag / max_v, 0.0, 1.0)
                        : 0.0;
    constexpr int N = int(sizeof(kVelocityStops) / sizeof(*kVelocityStops));
    int i = 0;
    while (i < N - 2 && kVelocityStops[i + 1].t <= tv) ++i;
    const Stop& lo = kVelocityStops[i];
    const Stop& hi = kVelocityStops[i + 1];
    const double f = (hi.t > lo.t) ? (tv - lo.t) / (hi.t - lo.t) : 0.0;
    r = int(std::lround(lo.r + f * (hi.r - lo.r)));
    g = int(std::lround(lo.g + f * (hi.g - lo.g)));
    b = int(std::lround(lo.b + f * (hi.b - lo.b)));
}

} // namespace

// ---------------------------------------------------------------------------
// SWMM2DResultsGraphicsItem
// ---------------------------------------------------------------------------

class SWMM2DResultsGraphicsItem : public QGraphicsItem
{
public:
    explicit SWMM2DResultsGraphicsItem(SWMM2DResultsLayer* layer,
                                        QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), layer_(layer)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        setCacheMode(QGraphicsItem::NoCache);
        setZValue(layer->layerZValue());
    }

    void geometryChanged() { prepareGeometryChange(); update(); }

    QRectF boundingRect() const override { return layer_->m_sceneBBox; }

    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* option,
               QWidget*) override
    {
        if (!layer_->isVisible()) return;
        const auto& tris = layer_->m_sceneTris;
        if (tris.isEmpty()) return;

        const QRectF exposed = option->exposedRect;
        const double dryDepth = layer_->dryDepth();
        const double maxDepth = layer_->maxDepth();

        p->save();
        p->setPen(Qt::NoPen);

        for (const auto& t : tris) {
            // Bounding-box cull
            const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
            const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
            const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
            const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
            if (!exposed.isNull() &&
                (maxX < exposed.left()  || minX > exposed.right() ||
                 maxY < exposed.top()   || minY > exposed.bottom())) continue;

            int r, g, b, a;
            inundationColorRgba(t.depth, dryDepth, maxDepth, r, g, b, a);
            if (a == 0) continue;  // dry → don't paint

            p->setBrush(QColor(r, g, b, a));
            const QPointF pts[3] = { t.a, t.b, t.c };
            p->drawConvexPolygon(pts, 3);
        }

        // CF.3 — second pass: outline highlighted cells (box / lasso picks).
        // Drawn AFTER the depth fill so the outline sits visibly on top, even
        // over dry cells. Width compensated for current zoom so the outline
        // stays a constant ~2px regardless of zoom level.
        const QSet<int>& hi = layer_->highlightedCells();
        if (!hi.isEmpty()) {
            const QTransform wt = p->worldTransform();
            const double scale = wt.m11();
            const double penWidthScene = (scale > 0.0) ? (2.0 / scale) : 1.0;

            QPen outlinePen(QColor(255, 215, 0, 230));   // gold outline
            outlinePen.setWidthF(penWidthScene);
            outlinePen.setJoinStyle(Qt::RoundJoin);
            p->setPen(outlinePen);
            p->setBrush(Qt::NoBrush);

            const int nTris = tris.size();
            for (int idx : hi) {
                if (idx < 0 || idx >= nTris) continue;
                const auto& t = tris[idx];
                // Skip cells outside the exposed rect (cheap cull).
                const double tx0 = std::min({t.a.x(), t.b.x(), t.c.x()});
                const double tx1 = std::max({t.a.x(), t.b.x(), t.c.x()});
                const double ty0 = std::min({t.a.y(), t.b.y(), t.c.y()});
                const double ty1 = std::max({t.a.y(), t.b.y(), t.c.y()});
                if (!exposed.isNull() &&
                    (tx1 < exposed.left()  || tx0 > exposed.right() ||
                     ty1 < exposed.top()   || ty0 > exposed.bottom())) continue;
                const QPointF pts[3] = { t.a, t.b, t.c };
                p->drawConvexPolygon(pts, 3);
            }
        }

        p->restore();
    }

private:
    SWMM2DResultsLayer* layer_;
};

// ---------------------------------------------------------------------------
// SWMM2DVelocityArrowsItem — centroid arrow overlay (CF.2.3)
// ---------------------------------------------------------------------------
//
// Draws one arrow per wet triangle: anchored at the cached scene-space
// centroid, length log-scaled by |v|, colour = magnitude on the velocity
// ramp. Renders above the depth fill via z-ordering (layerZValue + 0.5).
// Arrow length is kept constant in pixels by dividing by the painter's
// world-transform scale, so glyphs don't blow up at zoom-in or vanish at
// zoom-out.

class SWMM2DVelocityArrowsItem : public QGraphicsItem
{
public:
    explicit SWMM2DVelocityArrowsItem(SWMM2DResultsLayer* layer,
                                       QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), layer_(layer)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        setCacheMode(QGraphicsItem::NoCache);
        // Render above the depth fill (which uses layerZValue()).
        setZValue(layer->layerZValue() + 0.5);
    }

    void geometryChanged() { prepareGeometryChange(); update(); }

    QRectF boundingRect() const override { return layer_->m_sceneBBox; }

    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* option,
               QWidget*) override
    {
        if (!layer_->isVisible()) return;
        if (!layer_->velocityVectorsVisible()) return;
        if (!layer_->hasVelocityData()) return;

        const auto& tris = layer_->m_sceneTris;
        if (tris.isEmpty()) return;

        const QRectF exposed   = option->exposedRect;
        const double dryDepth  = layer_->dryDepth();
        const double maxVel    = std::max(layer_->maxVelocity(), 1e-6);
        const double arrowPx   = layer_->velocityArrowScale();
        const qreal  alpha     = std::clamp<qreal>(layer_->velocityOpacity(), 0.0, 1.0);

        // Convert pixel-space arrow length to scene units so glyphs render
        // at a constant on-screen size regardless of zoom.
        const QTransform xf = p->worldTransform();
        const double scale  = std::max(std::abs(xf.m11()), 1e-9);  // assume uniform scale
        const double pxToScene = 1.0 / scale;

        // Pen width also kept constant in pixels.
        QPen pen;
        pen.setCosmetic(true);
        pen.setWidthF(1.5);

        p->save();
        p->setOpacity(alpha);

        // Skip cells whose magnitude is below 0.01% of the running peak —
        // numerical noise without clipping real flow on slow shallow runs.
        const double vmagSkip = std::max(static_cast<double>(maxVel) * 1e-4,
                                          1e-9);

        for (const auto& t : tris) {
            if (t.depth < dryDepth) continue;
            if (t.vmag < vmagSkip) continue;          // sub-threshold cell

            // Cheap viewport cull around the centroid.
            if (!exposed.isNull() &&
                !exposed.contains(t.centroid)) continue;

            // Log-scaled arrow length: arrowPx * log1p(vmag / vmagRef)
            // where vmagRef = max_vel ⇒ glyph never exceeds arrowPx.
            const double mag_norm  = std::clamp(t.vmag / maxVel, 0.0, 1.0);
            const double len_scene = pxToScene * arrowPx * std::log1p(mag_norm) /
                                      std::log1p(1.0);
            if (len_scene <= 0.0) continue;

            const double inv_vmag = 1.0 / t.vmag;
            const double dx = t.vx * inv_vmag * len_scene;
            const double dy = t.vy * inv_vmag * len_scene;

            int r, g, b;
            velocityColorRgb(t.vmag, maxVel, r, g, b);
            pen.setColor(QColor(r, g, b));
            p->setPen(pen);

            const QPointF tail = t.centroid;
            const QPointF head(tail.x() + dx, tail.y() + dy);
            p->drawLine(tail, head);

            // Chevron arrowhead — two short legs at ±25° from the back-facing direction.
            const double headLen = len_scene * 0.32;
            const double cosA = std::cos(0.43);   // ≈25°
            const double sinA = std::sin(0.43);
            const double ux = -dx / len_scene;    // unit vector head → tail
            const double uy = -dy / len_scene;
            const QPointF left ( head.x() + headLen * (ux * cosA - uy * sinA),
                                  head.y() + headLen * (ux * sinA + uy * cosA) );
            const QPointF right( head.x() + headLen * (ux * cosA + uy * sinA),
                                  head.y() + headLen * (-ux * sinA + uy * cosA) );
            p->drawLine(head, left);
            p->drawLine(head, right);
        }

        p->restore();
    }

private:
    SWMM2DResultsLayer* layer_;
};

// ===========================================================================
// EngineMesh2DSource
// ===========================================================================

EngineMesh2DSource::EngineMesh2DSource(std::vector<double>            vx,
                                         std::vector<double>            vy,
                                         std::vector<double>            vz,
                                         std::vector<std::array<int,3>> tris)
    : vx_(std::move(vx)), vy_(std::move(vy)), vz_(std::move(vz)),
      tris_(std::move(tris))
{}

void EngineMesh2DSource::pushDepths(std::vector<float> depths,
                                     QDateTime simTime,
                                     double elapsedSec)
{
    // If the runner already delivered flux for this same tick (flux arrived
    // first because of queue ordering or because depth bulk happens to be
    // slower), fold into the existing Tick rather than emitting a new frame.
    if (!history_.empty() &&
        history_.back().depths.empty() &&
        std::abs(history_.back().elapsed_sec - elapsedSec) < 1e-6)
    {
        history_.back().depths   = std::move(depths);
        history_.back().sim_time = simTime;
        return;
    }
    Tick t;
    t.depths      = std::move(depths);
    t.sim_time    = simTime;
    t.elapsed_sec = elapsedSec;
    history_.emplace_back(std::move(t));
}

void EngineMesh2DSource::pushFlux(std::vector<float> flux,
                                   QDateTime simTime,
                                   double elapsedSec)
{
    // Pair with the most recently pushed depths frame when the elapsed times
    // match; otherwise (depths came earlier and a new tick has begun, or
    // depths haven't arrived yet) append a tick with empty depths so that
    // readEdgeFluxAt at this index still works.
    if (!history_.empty() &&
        std::abs(history_.back().elapsed_sec - elapsedSec) < 1e-6)
    {
        history_.back().flux = std::move(flux);
        return;
    }
    Tick t;
    t.flux        = std::move(flux);
    t.sim_time    = simTime;
    t.elapsed_sec = elapsedSec;
    history_.emplace_back(std::move(t));
}

void EngineMesh2DSource::setEdgeGeometry(std::vector<float> length,
                                          std::vector<float> nx,
                                          std::vector<float> ny)
{
    edge_length_ = std::move(length);
    edge_nx_     = std::move(nx);
    edge_ny_     = std::move(ny);
}

bool EngineMesh2DSource::readMeshGeometry(std::vector<double>& vx,
                                           std::vector<double>& vy,
                                           std::vector<double>& vz,
                                           std::vector<std::array<int, 3>>& tris)
{
    vx   = vx_;
    vy   = vy_;
    vz   = vz_;
    tris = tris_;
    return true;
}

bool EngineMesh2DSource::readDepthsAt(int timeIdx, std::vector<float>& depths)
{
    if (timeIdx < 0 || timeIdx >= static_cast<int>(history_.size())) {
        depths.assign(tris_.size(), 0.0f);
        return false;
    }
    depths = history_[timeIdx].depths;
    return true;
}

QDateTime EngineMesh2DSource::simTimeAt(int i) const
{
    if (i < 0 || i >= static_cast<int>(history_.size())) return {};
    return history_[i].sim_time;
}

bool EngineMesh2DSource::readEdgeFluxAt(int timeIdx, std::vector<float>& flux)
{
    const size_t n3 = tris_.size() * 3;
    if (timeIdx < 0 || timeIdx >= static_cast<int>(history_.size())) {
        flux.assign(n3, 0.0f);
        return false;
    }
    const auto& src = history_[timeIdx].flux;
    if (src.empty()) {
        // Tick was pushed via pushDepths only — engine lacks the flux API.
        flux.assign(n3, 0.0f);
        return false;
    }
    flux = src;
    return true;
}

bool EngineMesh2DSource::readEdgeGeometry(std::vector<float>& length,
                                           std::vector<float>& nx,
                                           std::vector<float>& ny)
{
    if (edge_length_.empty()) return false;
    length = edge_length_;
    nx     = edge_nx_;
    ny     = edge_ny_;
    return true;
}

// ===========================================================================
// HDF5Mesh2DSource
// ===========================================================================

HDF5Mesh2DSource::HDF5Mesh2DSource()
    : reader_(std::make_unique<openswmmvis::io::Mesh2DH5Reader>())
{}

HDF5Mesh2DSource::~HDF5Mesh2DSource() = default;

bool HDF5Mesh2DSource::open(const QString& path)
{
    path_ = path;
    return reader_->open(path);
}

int HDF5Mesh2DSource::vertexCount() const   { return reader_->vertexCount(); }
int HDF5Mesh2DSource::triangleCount() const { return reader_->triangleCount(); }
int HDF5Mesh2DSource::timeCount() const     { return reader_->timeCount(); }

bool HDF5Mesh2DSource::readMeshGeometry(std::vector<double>& vx,
                                          std::vector<double>& vy,
                                          std::vector<double>& vz,
                                          std::vector<std::array<int, 3>>& tris)
{
    if (!reader_->readMeshGeometry(vx, vy, vz)) return false;
    return reader_->readTriangles(tris);
}

bool HDF5Mesh2DSource::readDepthsAt(int timeIdx, std::vector<float>& depths)
{
    return reader_->readDepthsAt(timeIdx, depths);
}

bool HDF5Mesh2DSource::readEdgeFluxAt(int timeIdx, std::vector<float>& flux)
{
    return reader_->readEdgeFluxAt(timeIdx, flux);
}

bool HDF5Mesh2DSource::readEdgeGeometry(std::vector<float>& length,
                                         std::vector<float>& nx,
                                         std::vector<float>& ny)
{
    return reader_->readEdgeGeometry(length, nx, ny);
}

QDateTime HDF5Mesh2DSource::simTimeAt(int timeIdx) const
{
    if (!sim_start_.isValid() || !reader_) return {};
    // /time is in days since simulation start per Default2DOutputPlugin
    // ("units = days since simulation start"). Re-read each call instead of
    // caching so live-tail growth is reflected; the call is O(1) once HDF5
    // has parsed the file metadata.
    std::vector<double> times;
    if (!reader_->readTimes(times)) return {};
    if (timeIdx < 0 || timeIdx >= static_cast<int>(times.size())) return {};
    return sim_start_.addMSecs(qint64(times[timeIdx] * 86400.0 * 1000.0));
}

// ===========================================================================
// SWMM2DResultsLayer
// ===========================================================================

SWMM2DResultsLayer::SWMM2DResultsLayer(const QString& name,
                                         OpenSWMMVisWorkspace* parent)
    : OpenSWMMVisLayer(name, parent)
{
    setLayerType(OpenSWMMVisLayer::SWMM2DResultsLayer);

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Default to a
    // GraduatedRenderer because this layer's primary visual axis is a
    // continuous depth attribute.  Paint loop still uses dry_depth_ /
    // max_depth_ directly; refactor deferred to 8.13.6.4.
    m_renderer = std::make_unique<OpenSWMM::Render::GraduatedRenderer>();
}

SWMM2DResultsLayer::~SWMM2DResultsLayer() = default;

// ---------------------------------------------------------------------------
// Renderer (Slice BI Phase 8.13.6.6)
// ---------------------------------------------------------------------------

OpenSWMM::Render::IFeatureRenderer *SWMM2DResultsLayer::renderer() const
{
    return m_renderer.get();
}

void SWMM2DResultsLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r)
        return;
    if (r.get() == m_renderer.get())
        return;
    m_renderer = std::move(r);
    emit rendererChanged();
}

void SWMM2DResultsLayer::setSource(std::unique_ptr<IMesh2DSource> source)
{
    source_ = std::move(source);
    current_time_idx_ = -1;
    current_depths_.clear();
    current_flux_.clear();
    have_velocity_ = false;
    rebuildSceneGeometry_();

    const int n = source_ ? source_->timeCount() : 0;
    emit timeRangeChanged(0, std::max(0, n - 1));

    // Auto-seed max_velocity_ from a single global RT0 scan so the colour
    // ramp + arrow-length log scaling are anchored to the run's peak speed.
    // The layer's per-frame applyCurrentFlux_() only auto-GROWS max_velocity_;
    // without this seed it starts at the default (1 m/s) and shallow-flow
    // demos (snoopy peak ≈ 0.14 mm/s) end up with sub-pixel arrows.
    // Cheap: 480 frames × 128 cells × 3 edges + 2×2 inverse per cell.
    if (source_ && have_edge_geom_ && !tris_.empty() && n > 0 &&
        !max_velocity_user_set_)
    {
        constexpr float kQMax = 10.0f;
        const int nTri = static_cast<int>(tris_.size());
        float scanned_max = 0.0f;
        std::vector<float> fluxBuf;
        for (int t = 0; t < n; ++t) {
            if (!source_->readEdgeFluxAt(t, fluxBuf)) continue;
            if (static_cast<int>(fluxBuf.size()) != nTri * 3) continue;
            for (int i = 0; i < nTri; ++i) {
                double a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
                for (int e = 0; e < 3; ++e) {
                    const int idx = i * 3 + e;
                    const double len = edge_length_[idx];
                    if (len <= 1e-12) continue;
                    double q = fluxBuf[idx] / len;
                    if (q >  kQMax) q =  kQMax;
                    if (q < -kQMax) q = -kQMax;
                    const double nx = edge_nx_[idx];
                    const double ny = edge_ny_[idx];
                    a00 += nx * nx; a01 += nx * ny; a11 += ny * ny;
                    b0  += nx * q;  b1  += ny * q;
                }
                const double det = a00 * a11 - a01 * a01;
                if (std::abs(det) < 1e-12) continue;
                const double inv_det = 1.0 / det;
                const double vx = ( a11 * b0 - a01 * b1) * inv_det;
                const double vy = (-a01 * b0 + a00 * b1) * inv_det;
                const float vmag = static_cast<float>(std::sqrt(vx*vx + vy*vy));
                if (vmag > scanned_max) scanned_max = vmag;
            }
        }
        if (scanned_max > 0.0f) {
            max_velocity_ = scanned_max;
            have_velocity_ = true;
        }
    }

    // Show the latest frame immediately if any are available.
    if (n > 0) setCurrentTimeIndex(n - 1);
    else {
        if (graphics_item_) graphics_item_->geometryChanged();
        if (arrows_item_)   arrows_item_->geometryChanged();
    }
}

void SWMM2DResultsLayer::setCurrentTimeIndex(int t)
{
    if (!source_) return;
    const int n = source_->timeCount();
    if (n == 0) return;
    t = std::clamp(t, 0, n - 1);
    if (t == current_time_idx_ && !current_depths_.empty()) return;

    current_time_idx_ = t;
    source_->readDepthsAt(t, current_depths_);
    // Edge flux is optional — sources without it return false and leave
    // current_flux_ untouched. applyCurrentFlux_ checks the size and bails.
    if (!source_->readEdgeFluxAt(t, current_flux_)) {
        current_flux_.clear();
    }

    // Auto-track running max depth (unless the user explicitly pinned it)
    if (!max_depth_user_set_ && !current_depths_.empty()) {
        const float peak = *std::max_element(current_depths_.begin(),
                                              current_depths_.end());
        if (peak > max_depth_) max_depth_ = peak;
    }

    applyCurrentDepths_();
    applyCurrentFlux_();
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit currentTimeChanged(t);
}

void SWMM2DResultsLayer::refreshTimeRange()
{
    if (!source_) return;
    const int n = source_->timeCount();
    emit timeRangeChanged(0, std::max(0, n - 1));
    if (n > 0 && current_time_idx_ < n - 1) {
        setCurrentTimeIndex(n - 1);
    }
}

void SWMM2DResultsLayer::setDryDepth(double d)
{
    if (d == dry_depth_) return;
    dry_depth_ = d;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setMaxDepth(double d)
{
    if (d == max_depth_) return;
    max_depth_ = d;
    max_depth_user_set_ = true;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setVelocityVectorsVisible(bool v)
{
    if (v == velocity_visible_) return;
    velocity_visible_ = v;
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::setVelocityOpacity(qreal alpha)
{
    alpha = std::clamp<qreal>(alpha, 0.0, 1.0);
    if (alpha == velocity_opacity_) return;
    velocity_opacity_ = alpha;
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::setVelocityArrowScale(double scale)
{
    if (scale <= 0.0) return;
    if (scale == velocity_arrow_scale_) return;
    velocity_arrow_scale_ = scale;
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::setMaxVelocity(double v)
{
    if (v <= 0.0) return;
    if (v == max_velocity_) return;
    max_velocity_ = v;
    max_velocity_user_set_ = true;
    if (arrows_item_) arrows_item_->geometryChanged();
}

std::pair<float, int> SWMM2DResultsLayer::currentPeak() const
{
    if (current_depths_.empty()) return {0.0f, -1};
    auto it = std::max_element(current_depths_.begin(), current_depths_.end());
    return { *it, static_cast<int>(std::distance(current_depths_.begin(), it)) };
}

void SWMM2DResultsLayer::setCurrentSimTime(QDateTime t)
{
    if (!source_ || !t.isValid()) return;
    const int n = source_->timeCount();
    if (n == 0) return;

    // Linear scan for nearest time — adequate for the demo case's ~60-240
    // frame count. Replace with binary search if frame counts ever grow
    // large enough to matter on a presentation laptop.
    int best   = 0;
    qint64 bestDelta = std::numeric_limits<qint64>::max();
    for (int i = 0; i < n; ++i) {
        const QDateTime ti = source_->simTimeAt(i);
        if (!ti.isValid()) continue;
        const qint64 d = std::abs(t.msecsTo(ti));
        if (d < bestDelta) { bestDelta = d; best = i; }
    }
    setCurrentTimeIndex(best);
}

// ---------------------------------------------------------------------------
// CF.3 — cell selection / highlight
// ---------------------------------------------------------------------------

QVector<int> SWMM2DResultsLayer::pickCellsInRect(const QRectF& sceneRect) const
{
    QVector<int> hits;
    if (sceneRect.isNull() || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        if (sceneRect.contains(m_sceneTris[i].centroid))
            hits.push_back(i);
    }
    return hits;
}

QVector<int> SWMM2DResultsLayer::pickCellsInPolygon(const QPolygonF& scenePoly) const
{
    QVector<int> hits;
    if (scenePoly.size() < 3 || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        if (scenePoly.containsPoint(m_sceneTris[i].centroid, Qt::OddEvenFill))
            hits.push_back(i);
    }
    return hits;
}

namespace {
// Barycentric point-in-triangle test for scene-space coordinates.
inline bool pointInTriangle(const QPointF& p,
                            const QPointF& a,
                            const QPointF& b,
                            const QPointF& c)
{
    const double d1 = (p.x() - b.x()) * (a.y() - b.y()) -
                      (a.x() - b.x()) * (p.y() - b.y());
    const double d2 = (p.x() - c.x()) * (b.y() - c.y()) -
                      (b.x() - c.x()) * (p.y() - c.y());
    const double d3 = (p.x() - a.x()) * (c.y() - a.y()) -
                      (c.x() - a.x()) * (p.y() - a.y());
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}
} // namespace

int SWMM2DResultsLayer::pickCellAt(const QPointF& scenePt) const
{
    // Linear scan over m_sceneTris. Stops at first hit.
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const auto& t = m_sceneTris[i];
        if (pointInTriangle(scenePt, t.a, t.b, t.c))
            return i;
    }
    return -1;
}

void SWMM2DResultsLayer::highlightCells(const QSet<int>& triIdxSet)
{
    if (m_highlighted == triIdxSet) return;
    m_highlighted = triIdxSet;
    if (graphics_item_)
        graphics_item_->update();
    emit highlightedCellsChanged();
}

void SWMM2DResultsLayer::clearHighlights()
{
    if (m_highlighted.isEmpty()) return;
    m_highlighted.clear();
    if (graphics_item_)
        graphics_item_->update();
    emit highlightedCellsChanged();
}

// ---------------------------------------------------------------------------
// Scene plumbing
// ---------------------------------------------------------------------------

void SWMM2DResultsLayer::rebuildSceneGeometry_()
{
    m_sceneTris.clear();
    m_sceneBBox = QRectF();

    if (!source_) return;

    if (!source_->readMeshGeometry(vx_, vy_, vz_, tris_)) {
        return;
    }
    if (tris_.empty()) return;

    // Scene-space points: identity transform + Y-flip (scene grows downward,
    // matching the mesh layer's convention). CRS transforms come later via
    // onCanvasCRSChanged when the layer SRS framework is wired.
    const int nVerts = static_cast<int>(vx_.size());
    QVector<QPointF> scenePts;
    scenePts.reserve(nVerts);
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (int i = 0; i < nVerts; ++i) {
        const double sx =  vx_[i];
        const double sy = -vy_[i];
        scenePts.append(QPointF(sx, sy));
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
    }
    if (nVerts > 0) {
        m_sceneBBox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
        setExtent(MapExtent(minX, -maxY, maxX, -minY));  // un-flip Y for layer extent
    }

    m_sceneTris.resize(static_cast<int>(tris_.size()));
    for (int i = 0; i < static_cast<int>(tris_.size()); ++i) {
        SceneTri& st = m_sceneTris[i];
        st.a = scenePts[tris_[i][0]];
        st.b = scenePts[tris_[i][1]];
        st.c = scenePts[tris_[i][2]];
        st.centroid = QPointF((st.a.x() + st.b.x() + st.c.x()) / 3.0,
                               (st.a.y() + st.b.y() + st.c.y()) / 3.0);
        st.depth = 0.0f;
        st.vx = st.vy = st.vmag = 0.0f;
    }

    // Pull time-invariant edge geometry once per source swap. When the source
    // can't provide it (older engine without the bulk API, .h5 file without
    // the geometry datasets), the velocity overlay simply stays empty.
    edge_length_.clear();
    edge_nx_.clear();
    edge_ny_.clear();
    have_edge_geom_ = false;
    if (source_) {
        have_edge_geom_ = source_->readEdgeGeometry(edge_length_,
                                                     edge_nx_, edge_ny_);
        if (!have_edge_geom_) {
            edge_length_.clear();
            edge_nx_.clear();
            edge_ny_.clear();
        }
    }
}

void SWMM2DResultsLayer::applyCurrentDepths_()
{
    if (current_depths_.size() != tris_.size()) return;
    for (int i = 0; i < static_cast<int>(tris_.size()); ++i) {
        m_sceneTris[i].depth = current_depths_[i];
    }
}

void SWMM2DResultsLayer::applyCurrentFlux_()
{
    have_velocity_ = false;
    if (!have_edge_geom_ || tris_.empty() ||
        current_flux_.size() != tris_.size() * 3 ||
        edge_length_.size() != tris_.size() * 3) {
        // No flux data this tick — wipe per-tri velocities so an old frame
        // doesn't ghost when the user scrubs into a flux-less region.
        for (auto& st : m_sceneTris) {
            st.vx = st.vy = st.vmag = 0.0f;
        }
        return;
    }

    // RT0 cell-centred velocity reconstruction. For each triangle with three
    // outward unit normals n_e and signed normal speeds q_e = flux_e/length_e,
    // solve the 3×2 least-squares system N · v ≈ q in closed form via the
    // normal equations: (NᵀN) v = Nᵀ q, with NᵀN a 2×2 SPD matrix.
    constexpr float kQMax  = 10.0f;       // clamp |q_e| against wet/dry-front spikes (m/s)
    const double    dryEps = dry_depth_;

    float running_max = 0.0f;
    const int nTri = static_cast<int>(tris_.size());
    for (int t = 0; t < nTri; ++t) {
        SceneTri& st = m_sceneTris[t];

        if (st.depth < dryEps) {
            st.vx = st.vy = st.vmag = 0.0f;
            continue;
        }

        double a00 = 0.0, a01 = 0.0, a11 = 0.0;  // NᵀN entries
        double b0  = 0.0, b1  = 0.0;             // Nᵀ q entries
        for (int e = 0; e < 3; ++e) {
            const int idx = t * 3 + e;
            const double nx = edge_nx_[idx];
            const double ny = edge_ny_[idx];
            const double len = edge_length_[idx];
            if (len <= 1e-12) continue;
            double q = current_flux_[idx] / len;
            // Clamp against wet/dry-front spikes (flux can blow up when
            // length-integrated edge flux divides by a near-zero length).
            if (q >  kQMax) q =  kQMax;
            if (q < -kQMax) q = -kQMax;
            a00 += nx * nx;
            a01 += nx * ny;
            a11 += ny * ny;
            b0  += nx * q;
            b1  += ny * q;
        }
        const double det = a00 * a11 - a01 * a01;
        if (std::abs(det) < 1e-12) {
            st.vx = st.vy = st.vmag = 0.0f;
            continue;
        }
        const double inv_det = 1.0 / det;
        const double vx_model = ( a11 * b0 - a01 * b1) * inv_det;
        const double vy_model = (-a01 * b0 + a00 * b1) * inv_det;

        // Scene-space velocity: vy is flipped so the arrow points the right
        // way after the rebuildSceneGeometry_() Y-flip on vertex coords.
        st.vx   = static_cast<float>(vx_model);
        st.vy   = static_cast<float>(-vy_model);
        st.vmag = static_cast<float>(std::sqrt(vx_model * vx_model +
                                                vy_model * vy_model));
        if (st.vmag > running_max) running_max = st.vmag;
    }

    have_velocity_ = (running_max > 0.0f);

    // Auto-grow the velocity ramp's upper bound (unless the user pinned it).
    if (!max_velocity_user_set_ && running_max > max_velocity_) {
        max_velocity_ = running_max;
    }
}

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

void SWMM2DResultsLayer::populateScene(QGraphicsScene* scene,
                                         const MapExtent& /*canvasExtent*/,
                                         const SpatialReferenceSystem* /*canvasSRS*/)
{
    if (!scene) return;
    if (graphics_item_) {
        scene->removeItem(graphics_item_);
        delete graphics_item_;
        graphics_item_ = nullptr;
    }
    if (arrows_item_) {
        scene->removeItem(arrows_item_);
        delete arrows_item_;
        arrows_item_ = nullptr;
    }
    graphics_item_ = new SWMM2DResultsGraphicsItem(this);
    scene->addItem(graphics_item_);
    arrows_item_ = new SWMM2DVelocityArrowsItem(this);
    scene->addItem(arrows_item_);
    graphics_item_->geometryChanged();
    arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::depopulateScene(QGraphicsScene* scene)
{
    if (graphics_item_) {
        if (scene) scene->removeItem(graphics_item_);
        delete graphics_item_;
        graphics_item_ = nullptr;
    }
    if (arrows_item_) {
        if (scene) scene->removeItem(arrows_item_);
        delete arrows_item_;
        arrows_item_ = nullptr;
    }
}

void SWMM2DResultsLayer::refreshScene(QGraphicsScene* scene,
                                        const MapExtent& canvasExtent,
                                        const SpatialReferenceSystem* canvasSRS)
{
    if (!graphics_item_) {
        populateScene(scene, canvasExtent, canvasSRS);
        return;
    }
    graphics_item_->geometryChanged();
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::onCanvasCRSChanged(
        const SpatialReferenceSystem* /*newCanvasSRS*/)
{
    // Reprojection seam — mirror SWMM2DMeshLayer's transform path when the
    // layer gains an explicit SRS. For the MVP demo case (no reprojection)
    // the identity transform set up in rebuildSceneGeometry_() is sufficient.
    rebuildSceneGeometry_();
    applyCurrentDepths_();
    applyCurrentFlux_();
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
}
