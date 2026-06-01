/*!
 * \file   mesh2drunlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/mesh2drunlayer.h"

#include "plot/swmmjuliandatetime.h"
#include "layers/swmm2dresultslayer.h"

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
    return dateTimeToSwmmJulian(t0);
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
    // Edge flux needs the source's per-edge flux feed (newer engines only).
    if (attr == PlotAttribute::Mesh2DEdgeFlux)
        return m_layer && m_layer->source() && m_layer->hasVelocityData();
    if (!isMesh2DAttribute(attr)) return false;
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
        return m_layer->hasVelocityData();
    }
    return true;
}

void Mesh2DRunLayer::ensureZBedCache_() const
{
    if (m_zBedReady) return;
    if (!m_layer) return;

    // The layer exposes per-cell scene geometry via m_sceneTris (which holds
    // scene-space x/y but not z). Bed elevation comes from the mesh source's
    // vertex-z array.  We pull the geometry once and average the three
    // vertex z's per triangle.
    auto *src = m_layer->source();
    if (!src) return;

    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 3>> tris;
    if (!src->readMeshGeometry(vx, vy, vz, tris)) return;

    m_zBed.assign(tris.size(), 0.0f);
    for (std::size_t i = 0; i < tris.size(); ++i) {
        const auto &t = tris[i];
        const double z0 = (t[0] >= 0 && t[0] < static_cast<int>(vz.size())) ? vz[t[0]] : 0.0;
        const double z1 = (t[1] >= 0 && t[1] < static_cast<int>(vz.size())) ? vz[t[1]] : 0.0;
        const double z2 = (t[2] >= 0 && t[2] < static_cast<int>(vz.size())) ? vz[t[2]] : 0.0;
        m_zBed[i] = static_cast<float>((z0 + z1 + z2) / 3.0);
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
    std::vector<std::array<int, 3>> tris;
    if (!src->readMeshGeometry(vx, vy, vz, tris)) return;

    const int nV = static_cast<int>(vz.size());
    m_vertexZ.assign(nV, 0.0f);
    for (int i = 0; i < nV; ++i) m_vertexZ[i] = static_cast<float>(vz[i]);

    m_vertexTris.assign(nV, {});
    for (int t = 0; t < static_cast<int>(tris.size()); ++t) {
        for (int k = 0; k < 3; ++k) {
            const int v = tris[t][k];
            if (v >= 0 && v < nV) m_vertexTris[v].push_back(t);
        }
    }
    m_vertexAdjReady = true;
}

bool Mesh2DRunLayer::reconstructVelocityAtCell_(int triIdx,
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

    // q_e = flux / length. Clamp |q_e| ≤ 10 m/s to suppress wet/dry-front spikes
    // (per CF.2.3).
    const std::size_t base = static_cast<std::size_t>(triIdx) * 3;
    if (base + 2 >= flux.size() || base + 2 >= edge_len.size() ||
        base + 2 >= edge_nx.size() || base + 2 >= edge_ny.size())
        return false;

    double q[3] = {0.0, 0.0, 0.0};
    double nx[3] = {edge_nx[base + 0], edge_nx[base + 1], edge_nx[base + 2]};
    double ny[3] = {edge_ny[base + 0], edge_ny[base + 1], edge_ny[base + 2]};
    for (int e = 0; e < 3; ++e) {
        const double len = edge_len[base + e];
        if (len <= 0.0) return false;
        double qe = flux[base + e] / len;
        if (qe > 10.0)  qe = 10.0;
        if (qe < -10.0) qe = -10.0;
        q[e] = qe;
    }

    // Solve (NᵀN) v = Nᵀq via closed-form 2x2 inverse.
    const double a11 = nx[0]*nx[0] + nx[1]*nx[1] + nx[2]*nx[2];
    const double a12 = nx[0]*ny[0] + nx[1]*ny[1] + nx[2]*ny[2];
    const double a22 = ny[0]*ny[0] + ny[1]*ny[1] + ny[2]*ny[2];
    const double b1  = nx[0]*q[0]  + nx[1]*q[1]  + nx[2]*q[2];
    const double b2  = ny[0]*q[0]  + ny[1]*q[1]  + ny[2]*q[2];

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
                double sum = 0.0; int n = 0;
                for (int tri : inc)
                    if (tri >= 0 && tri < static_cast<int>(depths.size())) { sum += depths[tri]; ++n; }
                if (n > 0) {
                    const double d = sum / n;
                    value = (attr == PlotAttribute::Mesh2DDepth) ? d : d + zVtx;
                }
            }
            out.timesJulian.push_back(dateTimeToSwmmJulian(dt));
            out.values.push_back(value);
        }
        out.ok = !out.timesJulian.empty();
        if (!out.ok)
            out.errorMessage = QStringLiteral("No samples for vertex %1").arg(v);
        return;
    }

    // ── Edge-flux series (Mesh2DEdge ref + Mesh2DEdgeFlux attr) ─────────────
    if (ref.kind == ObjectRef::Kind::Mesh2DEdge) {
        if (attr != PlotAttribute::Mesh2DEdgeFlux) {
            out.errorMessage = QStringLiteral("Edge ref only supports edge flux");
            return;
        }
        auto *esrc = m_layer->source();
        const int flat = ref.triIdx * 3 + ref.edgeLocal;
        if (ref.triIdx < 0 || ref.triIdx >= esrc->triangleCount()
            || ref.edgeLocal < 0 || ref.edgeLocal > 2) {
            out.errorMessage = QStringLiteral("Mesh edge index out of range");
            return;
        }
        const int nTe = esrc->timeCount();
        if (nTe <= 0) { out.errorMessage = QStringLiteral("No time steps available yet"); return; }
        out.timesJulian.reserve(static_cast<std::size_t>(nTe));
        out.values.reserve(static_cast<std::size_t>(nTe));
        std::vector<float> fbuf;
        for (int t = 0; t < nTe; ++t) {
            const QDateTime dt = esrc->simTimeAt(t);
            if (!dt.isValid()) continue;
            double value = std::nan("");
            if (esrc->readEdgeFluxAt(t, fbuf) && flat < static_cast<int>(fbuf.size()))
                value = static_cast<double>(fbuf[flat]);
            out.timesJulian.push_back(dateTimeToSwmmJulian(dt));
            out.values.push_back(value);
        }
        out.ok = !out.timesJulian.empty();
        if (!out.ok)
            out.errorMessage = QStringLiteral("No edge-flux samples (re-run with current engine?)");
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
    if (needVel) {
        if (!src->readEdgeGeometry(edge_len, edge_nx, edge_ny)) {
            out.errorMessage = QStringLiteral("Edge geometry not available for velocity");
            return;
        }
    }

    const double dryDepth = m_layer->dryDepth();

    out.timesJulian.reserve(static_cast<std::size_t>(nT));
    out.values.reserve(static_cast<std::size_t>(nT));

    std::vector<float> depths;
    std::vector<float> flux;

    for (int t = 0; t < nT; ++t) {
        const QDateTime dt = src->simTimeAt(t);
        if (!dt.isValid()) continue;

        double value = std::nan("");

        if (attr == PlotAttribute::Mesh2DDepth || attr == PlotAttribute::Mesh2DHGL) {
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
            if (reconstructVelocityAtCell_(triIdx, flux, edge_len, edge_nx, edge_ny,
                                            d, dryDepth, vx, vy))
            {
                if      (attr == PlotAttribute::Mesh2DVelocityX) value = vx;
                else if (attr == PlotAttribute::Mesh2DVelocityY) value = vy;
                else /* Mesh2DVelocityMag */                     value = std::sqrt(vx*vx + vy*vy);
            }
            // dry cells leave value as NaN — rendered as a gap in the chart.
        }

        out.timesJulian.push_back(dateTimeToSwmmJulian(dt));
        out.values.push_back(value);
    }

    out.ok = !out.timesJulian.empty();
    if (!out.ok)
        out.errorMessage = QStringLiteral("No valid samples for cell %1").arg(triIdx);
}

} // namespace openswmmvis::plot
