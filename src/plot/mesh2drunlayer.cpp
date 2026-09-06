/*!
 * \file   mesh2drunlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/mesh2drunlayer.h"

#include "core/swmmdatetime.h"
#include "layers/swmm2dresultslayer.h"
#include "mesh/meshcellgeom.h"

#include <QDateTime>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace openswmmvis::plot {

Mesh2DRunLayer::Mesh2DRunLayer(SWMM2DResultsLayer *layer)
    : m_layer(layer)
{}

QString Mesh2DRunLayer::scenarioName() const
{
    return m_layer ? m_layer->name() : QStringLiteral("(2D layer closed)");
}

UnitSystem Mesh2DRunLayer::unitSystem() const
{
    // 2D mesh internally stores SI units (depths in m, velocities in m/s) per
    // CF.MVP & engine 2D module. A future polish may add a per-layer unit
    // system query when the GUI grows project-wide unit toggles.
    return UnitSystem::SI;
}

double Mesh2DRunLayer::startDateJulian() const
{
    if (!m_layer || !m_layer->source())
        return std::nan("");
    const QDateTime t0 = m_layer->source()->simTimeAt(0);
    if (!t0.isValid())
        return std::nan("");
    return core::qDateTimeToSwmmDateTime(t0);
}

int Mesh2DRunLayer::periodCount() const
{
    if (!m_layer || !m_layer->source())
        return 0;
    return m_layer->source()->timeCount();
}

int Mesh2DRunLayer::reportStepSeconds() const
{
    if (!m_layer || !m_layer->source())
        return 0;
    const auto *src = m_layer->source();
    if (src->timeCount() < 2)
        return 0;
    const QDateTime t0 = src->simTimeAt(0);
    const QDateTime t1 = src->simTimeAt(1);
    if (!t0.isValid() || !t1.isValid())
        return 0;
    return static_cast<int>(t0.secsTo(t1));
}

QString Mesh2DRunLayer::persistenceKey() const
{
    if (!m_layer) return {};
    return QStringLiteral("mesh2d://") + m_layer->name();
}

bool Mesh2DRunLayer::supportsAttribute(PlotAttribute attr) const
{
    // Edge flow/flux need the source's per-edge flux feed (newer engines only).
    // Gate on edge-flux availability, not hasVelocityData() — the latter tracks
    // the currently-shown frame and is false on dry frames.
    if (attr == PlotAttribute::Mesh2DEdgeFlux || attr == PlotAttribute::Mesh2DEdgeFlow)
        return m_layer && m_layer->source() && m_layer->hasEdgeFluxData();
    if (!isMesh2DAttribute(attr)) return false;
    // Rainfall series come straight from the per-face HDF5 datasets; the live
    // in-process source doesn't stream them, and older files lack rain_cum.
    if (attr == PlotAttribute::Mesh2DRainfall)
        return m_layer && m_layer->source()
            && m_layer->source()->hasFaceField("Mesh2_face_rainfall");
    if (attr == PlotAttribute::Mesh2DRainVolume)
        return m_layer && m_layer->source()
            && m_layer->source()->hasFaceField("Mesh2_face_rain_cum");
    // Depth/HGL are also valid for a vertex ref (interpolated); the kind is
    // checked in getSeriesAt, so just allow the attribute here.
    // Velocity attributes require edge flux + edge geometry. Older HDF5
    // files lack them; supportsAttribute() lets the dialog disable
    // those checkboxes when the layer can't supply them.
    if (attr == PlotAttribute::Mesh2DVelocityMag ||
        attr == PlotAttribute::Mesh2DVelocityX   ||
        attr == PlotAttribute::Mesh2DVelocityY)
    {
        if (!m_layer || !m_layer->source()) return false;
        return m_layer->hasEdgeFluxData();
    }
    return true;
}

void Mesh2DRunLayer::ensureZBedCache_() const
{
    if (m_zBedReady) return;
    if (!m_layer) return;

    // The layer exposes per-cell scene geometry via m_sceneTris (which holds
    // scene-space x/y but not z). Bed elevation comes from the mesh source's
    // vertex-z array.  We pull the CELLS once (per-face series are per cell,
    // not per display triangle) and average the vertex z's per cell.
    auto *src = m_layer->source();
    if (!src) return;

    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    if (!src->readCells(vx, vy, vz, cells)) return;

    m_zBed.assign(cells.size(), 0.0f);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto &c = cells[i];
        const int nv = (c[3] >= 0) ? 4 : 3;
        double sum = 0.0;
        for (int k = 0; k < nv; ++k)
            sum += (c[k] >= 0 && c[k] < static_cast<int>(vz.size())) ? vz[c[k]] : 0.0;
        m_zBed[i] = static_cast<float>(sum / nv);
    }
    m_zBedReady = true;
}

void Mesh2DRunLayer::ensureVertexAdjCache_() const
{
    if (m_vertexAdjReady) return;
    if (!m_layer) return;
    auto *src = m_layer->source();
    if (!src) return;

    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    if (!src->readCells(vx, vy, vz, cells)) return;

    const int nV = static_cast<int>(vz.size());
    m_vertexZ.assign(nV, 0.0f);
    for (int i = 0; i < nV; ++i) m_vertexZ[i] = static_cast<float>(vz[i]);

    m_vertexTris.assign(nV, {});
    m_cellNv.assign(cells.size(), 3);
    for (int t = 0; t < static_cast<int>(cells.size()); ++t) {
        const int nv = (cells[t][3] >= 0) ? 4 : 3;
        m_cellNv[t] = static_cast<unsigned char>(nv);
        for (int k = 0; k < nv; ++k) {
            const int v = cells[t][k];
            if (v >= 0 && v < nV) m_vertexTris[v].push_back(t);
        }
    }
    m_vertexAdjReady = true;
}

bool Mesh2DRunLayer::reconstructVelocityAtCell_(int triIdx, int nv,
                                                const std::vector<float>& flux,
                                                const std::vector<float>& edge_len,
                                                const std::vector<float>& edge_nx,
                                                const std::vector<float>& edge_ny,
                                                double depth,
                                                double dryDepth,
                                                double& vx_out,
                                                double& vy_out) const
{
    vx_out = std::nan("");
    vy_out = std::nan("");
    if (depth < dryDepth) return false;
    if (nv < 3 || nv > mesh::kEdgeStride) return false;

    // q_e = flux / length. Clamp |q_e| ≤ 10 m/s to suppress wet/dry-front spikes
    // (per CF.2.3). Edge slots are padded to mesh::kEdgeStride per cell.
    const std::size_t last = static_cast<std::size_t>(mesh::edgeSlot(triIdx, nv - 1));
    if (last >= flux.size() || last >= edge_len.size() ||
        last >= edge_nx.size() || last >= edge_ny.size())
        return false;

    double q[mesh::kEdgeStride] = {0.0, 0.0, 0.0, 0.0};
    double nx[mesh::kEdgeStride] = {0.0, 0.0, 0.0, 0.0};
    double ny[mesh::kEdgeStride] = {0.0, 0.0, 0.0, 0.0};
    for (int e = 0; e < nv; ++e) {
        const std::size_t slot = static_cast<std::size_t>(mesh::edgeSlot(triIdx, e));
        const double len = edge_len[slot];
        if (len <= 0.0) return false;
        double qe = flux[slot] / len;
        if (qe > 10.0)  qe = 10.0;
        if (qe < -10.0) qe = -10.0;
        q[e]  = qe;
        nx[e] = edge_nx[slot];
        ny[e] = edge_ny[slot];
    }

    // Solve (NᵀN) v = Nᵀq via closed-form 2x2 inverse (nv-agnostic: the
    // normal equations sum over the cell's edges).
    double a11 = 0.0, a12 = 0.0, a22 = 0.0, b1 = 0.0, b2 = 0.0;
    for (int e = 0; e < nv; ++e) {
        a11 += nx[e]*nx[e];
        a12 += nx[e]*ny[e];
        a22 += ny[e]*ny[e];
        b1  += nx[e]*q[e];
        b2  += ny[e]*q[e];
    }

    const double det = a11 * a22 - a12 * a12;
    if (std::fabs(det) < 1e-12) return false;
    vx_out = ( a22 * b1 - a12 * b2) / det;
    vy_out = (-a12 * b1 + a11 * b2) / det;
    return true;
}

void Mesh2DRunLayer::getSeriesAt(const ObjectRef& ref,
                                  PlotAttribute attr,
                                  SeriesData& out) const
{
    out.ok = false;
    out.errorMessage.clear();
    out.timesJulian.clear();
    out.values.clear();

    if (!m_layer || !m_layer->source()) {
        out.errorMessage = QStringLiteral("2D layer not available");
        return;
    }

    // ── Vertex depth/HGL series (Mesh2DVertex ref) — interpolated as the
    //    mean of the triangles incident on the vertex. ───────────────────────
    if (ref.kind == ObjectRef::Kind::Mesh2DVertex) {
        if (attr != PlotAttribute::Mesh2DDepth && attr != PlotAttribute::Mesh2DHGL) {
            out.errorMessage = QStringLiteral("Vertex series supports depth / HGL only");
            return;
        }
        ensureVertexAdjCache_();
        auto *vsrc = m_layer->source();
        const int v = ref.triIdx;   // triIdx carries the vertex index for this kind
        if (v < 0 || v >= static_cast<int>(m_vertexTris.size())) {
            out.errorMessage = QStringLiteral("Mesh vertex index out of range");
            return;
        }
        const int nTv = vsrc->timeCount();
        if (nTv <= 0) { out.errorMessage = QStringLiteral("No time steps available yet"); return; }
        const std::vector<int> &inc = m_vertexTris[v];
        const double zVtx = (v < static_cast<int>(m_vertexZ.size())) ? m_vertexZ[v] : 0.0;
        out.timesJulian.reserve(static_cast<std::size_t>(nTv));
        out.values.reserve(static_cast<std::size_t>(nTv));
        std::vector<float> depths;
        for (int t = 0; t < nTv; ++t) {
            const QDateTime dt = vsrc->simTimeAt(t);
            if (!dt.isValid()) continue;
            double value = std::nan("");
            if (!inc.empty() && vsrc->readDepthsAt(t, depths)) {
                // 1/nv weighting per incident cell (the plan's rule, mirroring
                // the engine's median-dual /nv), normalised by 3 so an
                // all-triangle mesh is the plain mean it always was: a
                // triangle weighs 1, a quad 3/4.
                double sum = 0.0, wsum = 0.0;
                for (int tri : inc)
                    if (tri >= 0 && tri < static_cast<int>(depths.size())) {
                        const double w = (tri < static_cast<int>(m_cellNv.size())
                                          && m_cellNv[tri] == 4) ? 0.75 : 1.0;
                        sum  += depths[tri] * w;
                        wsum += w;
                    }
                if (wsum > 0.0) {
                    const double d = sum / wsum;
                    value = (attr == PlotAttribute::Mesh2DDepth) ? d : d + zVtx;
                }
            }
            out.timesJulian.push_back(core::qDateTimeToSwmmDateTime(dt));
            out.values.push_back(value);
        }
        out.ok = !out.timesJulian.empty();
        if (!out.ok)
            out.errorMessage = QStringLiteral("No samples for vertex %1").arg(v);
        return;
    }

    // ── Edge series (Mesh2DEdge ref): volumetric flow Q (Mesh2DEdgeFlow) or
    //    unit-width flux q = Q / edge length (Mesh2DEdgeFlux). Both come from the
    //    engine's single Mesh2_edge_flux dataset (volumetric F_e, m³/s). ─────────
    if (ref.kind == ObjectRef::Kind::Mesh2DEdge) {
        if (attr != PlotAttribute::Mesh2DEdgeFlux && attr != PlotAttribute::Mesh2DEdgeFlow) {
            out.errorMessage = QStringLiteral("Edge ref only supports edge flow / flux");
            return;
        }
        auto *esrc = m_layer->source();
        ensureVertexAdjCache_();   // m_cellNv — a quad has four edges
        const int nvEdge = (ref.triIdx >= 0 && ref.triIdx < static_cast<int>(m_cellNv.size()))
                               ? int(m_cellNv[ref.triIdx]) : 3;
        const int flat = mesh::edgeSlot(ref.triIdx, ref.edgeLocal);
        if (ref.triIdx < 0 || ref.triIdx >= esrc->triangleCount()
            || ref.edgeLocal < 0 || ref.edgeLocal >= nvEdge) {
            out.errorMessage = QStringLiteral("Mesh edge index out of range");
            return;
        }
        const int nTe = esrc->timeCount();
        if (nTe <= 0) { out.errorMessage = QStringLiteral("No time steps available yet"); return; }

        // Unit-width flux divides the volumetric flux by the static edge length;
        // read it once (the same reader the velocity reconstruction uses).
        const bool wantUnitFlux = (attr == PlotAttribute::Mesh2DEdgeFlux);
        double edgeLen = 0.0;
        if (wantUnitFlux) {
            std::vector<float> len, nx, ny;
            if (!esrc->readEdgeGeometry(len, nx, ny) || flat >= static_cast<int>(len.size())) {
                out.errorMessage = QStringLiteral("Edge geometry not available for unit flux");
                return;
            }
            edgeLen = static_cast<double>(len[flat]);
        }

        out.timesJulian.reserve(static_cast<std::size_t>(nTe));
        out.values.reserve(static_cast<std::size_t>(nTe));
        std::vector<float> fbuf;
        for (int t = 0; t < nTe; ++t) {
            const QDateTime dt = esrc->simTimeAt(t);
            if (!dt.isValid()) continue;
            double value = std::nan("");
            if (esrc->readEdgeFluxAt(t, fbuf) && flat < static_cast<int>(fbuf.size())) {
                const double flux = static_cast<double>(fbuf[flat]);   // F_e, m³/s
                value = wantUnitFlux ? (edgeLen > 1e-9 ? flux / edgeLen : std::nan(""))
                                     : flux;
            }
            out.timesJulian.push_back(core::qDateTimeToSwmmDateTime(dt));
            out.values.push_back(value);
        }
        out.ok = !out.timesJulian.empty();
        if (!out.ok)
            out.errorMessage = QStringLiteral("No edge samples (re-run with current engine?)");
        return;
    }

    if (ref.kind != ObjectRef::Kind::Mesh2DCell) {
        out.errorMessage = QStringLiteral("ObjectRef is not a Mesh2D cell");
        return;
    }
    if (!isMesh2DAttribute(attr)) {
        out.errorMessage = QStringLiteral("Attribute is not Mesh2D");
        return;
    }

    auto *src = m_layer->source();
    const int triIdx = ref.triIdx;
    if (triIdx < 0 || triIdx >= src->triangleCount()) {
        out.errorMessage = QStringLiteral("Mesh cell index out of range");
        return;
    }
    const int nT = src->timeCount();
    if (nT <= 0) {
        out.errorMessage = QStringLiteral("No time steps available yet");
        return;
    }

    // Pre-fetch cached pieces depending on attribute.
    const bool needZBed = (attr == PlotAttribute::Mesh2DHGL);
    if (needZBed) ensureZBedCache_();

    const bool needVel = (attr == PlotAttribute::Mesh2DVelocityMag ||
                          attr == PlotAttribute::Mesh2DVelocityX   ||
                          attr == PlotAttribute::Mesh2DVelocityY);
    std::vector<float> edge_len, edge_nx, edge_ny;
    int cellNv = 3;
    if (needVel) {
        if (!src->readEdgeGeometry(edge_len, edge_nx, edge_ny)) {
            out.errorMessage = QStringLiteral("Edge geometry not available for velocity");
            return;
        }
        ensureVertexAdjCache_();   // m_cellNv — RT0 sums over the cell's nv edges
        if (triIdx < static_cast<int>(m_cellNv.size())) cellNv = int(m_cellNv[triIdx]);
    }

    const double dryDepth = m_layer->dryDepth();

    // Rainfall: intensity is stored m/s → report mm/hr (unitSystem() is SI);
    // cumulative volume is stored m³ and reported as-is.
    const bool wantRain = (attr == PlotAttribute::Mesh2DRainfall ||
                           attr == PlotAttribute::Mesh2DRainVolume);
    const char *rainDataset = (attr == PlotAttribute::Mesh2DRainfall)
                                  ? "Mesh2_face_rainfall" : "Mesh2_face_rain_cum";
    const double rainScale = (attr == PlotAttribute::Mesh2DRainfall)
                                 ? 1000.0 * 3600.0 : 1.0;

    out.timesJulian.reserve(static_cast<std::size_t>(nT));
    out.values.reserve(static_cast<std::size_t>(nT));

    std::vector<float> depths;
    std::vector<float> flux;
    std::vector<float> rain;

    for (int t = 0; t < nT; ++t) {
        const QDateTime dt = src->simTimeAt(t);
        if (!dt.isValid()) continue;

        double value = std::nan("");

        if (wantRain) {
            if (!src->readFaceFieldAt(rainDataset, t, rain)) continue;
            if (triIdx >= static_cast<int>(rain.size())) continue;
            value = static_cast<double>(rain[triIdx]) * rainScale;
        }
        else if (attr == PlotAttribute::Mesh2DDepth || attr == PlotAttribute::Mesh2DHGL) {
            if (!src->readDepthsAt(t, depths)) continue;
            if (triIdx >= static_cast<int>(depths.size())) continue;
            const double d = static_cast<double>(depths[triIdx]);
            if (attr == PlotAttribute::Mesh2DDepth) {
                value = d;
            } else {
                const double z = (m_zBedReady && triIdx < static_cast<int>(m_zBed.size()))
                                    ? static_cast<double>(m_zBed[triIdx])
                                    : 0.0;
                value = d + z;
            }
        }
        else if (needVel) {
            if (!src->readDepthsAt(t, depths)) continue;
            if (!src->readEdgeFluxAt(t, flux)) continue;
            if (triIdx >= static_cast<int>(depths.size())) continue;
            const double d = static_cast<double>(depths[triIdx]);
            double vx = std::nan(""), vy = std::nan("");
            if (reconstructVelocityAtCell_(triIdx, cellNv, flux, edge_len, edge_nx, edge_ny,
                                            d, dryDepth, vx, vy))
            {
                if      (attr == PlotAttribute::Mesh2DVelocityX) value = vx;
                else if (attr == PlotAttribute::Mesh2DVelocityY) value = vy;
                else /* Mesh2DVelocityMag */                     value = std::sqrt(vx*vx + vy*vy);
            }
            // dry cells leave value as NaN — rendered as a gap in the chart.
        }

        out.timesJulian.push_back(core::qDateTimeToSwmmDateTime(dt));
        out.values.push_back(value);
    }

    out.ok = !out.timesJulian.empty();
    if (!out.ok)
        out.errorMessage = QStringLiteral("No valid samples for cell %1").arg(triIdx);
}

} // namespace openswmmvis::plot
