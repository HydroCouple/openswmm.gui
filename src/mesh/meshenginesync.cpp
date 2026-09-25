/*!
 * \file   meshenginesync.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshenginesync.h"

#include "mesh/meshbctype.h"
#include "mesh/meshcellgeom.h"

#include <openswmm/engine/openswmm_engine.h>  // swmm_get_flow_units
#include <openswmm/engine/openswmm_2d.h>

#include <QElapsedTimer>

#include <cmath>
#include <cstddef>
#include <vector>

Q_LOGGING_CATEGORY(lcSavePerf, "openswmm.save.perf")

namespace mesh {

namespace {

// Per-element push counters for one pushMeshEditsToEngine call. Logged from the
// destructor so every exit point reports, including
// the early bail-outs that skip most of the work.
struct SyncPerf
{
    int nv = 0, nt = 0;
    int zPushed = 0, vAttrPushed = 0, triPushed = 0, edgePushed = 0;
    const char *outcome = "ok";
    QElapsedTimer timer;

    SyncPerf() { timer.start(); }

    ~SyncPerf()
    {
        qCInfo(lcSavePerf).nospace()
            << "[save][2d-sync] nv=" << nv << " nt=" << nt
            << " zPushed=" << zPushed << " vAttrPushed=" << vAttrPushed
            << " triPushed=" << triPushed << " edgePushed=" << edgePushed
            << " outcome=" << outcome << " ms=" << timer.elapsed();
    }
};

// Engine [2D_BOUNDARY_CONDITIONS] type codes (openswmm_2d.h).
constexpr int kEngWall           = 0;
constexpr int kEngNormalFlow     = 1;
constexpr int kEngSpecifiedStage = 2;
constexpr int kEngSpecifiedFlow  = 3;
constexpr int kEngRatingCurve    = 4;

// Display flow unit -> m³/s. The engine's SPECIFIED_FLOW BC is stored in
// display flow units per metre until initialize() scales it to m³/s/m in
// place; the layer always carries the project's display flow units. Applied
// only for an initialized (mesh-scaled) engine — see pushMeshEditsToEngine.
// Index by the swmm_get_flow_units() enum: CFS, GPM, MGD, CMS, LPS, MLD.
double flowUnitToCms(int flowUnits)
{
    switch (flowUnits) {
    case 0: return 0.0283168466;   // CFS  (ft³/s)
    case 1: return 6.30901964e-05; // GPM  (US gal/min)
    case 2: return 0.0438126364;   // MGD  (US Mgal/day)
    case 3: return 1.0;            // CMS  (m³/s)
    case 4: return 0.001;          // LPS  (L/s)
    case 5: return 0.0115740741;   // MLD  (ML/day)
    default: return 1.0;
    }
}

// Derive the layer→engine length factor from an unedited vertex's XY.
// XY are never edited in the GUI, so engine_coord / layer_coord is the exact
// conversion the engine applied on load (1.0 for SI, 0.3048 for US). Falls
// back to 1.0 when every vertex sits at the origin.
double deriveLengthFactor(SWMM_Engine engine, const MeshResult &mesh)
{
    int best = -1;
    double bestMag = 0.0;
    bool bestUseX = true;
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        const double ax = std::abs(mesh.vertices[i].xy.x());
        const double ay = std::abs(mesh.vertices[i].xy.y());
        const double m  = std::max(ax, ay);
        if (m > bestMag) { bestMag = m; best = i; bestUseX = (ax >= ay); }
    }
    if (best < 0 || bestMag < 1e-9) return 1.0;

    double ex = 0.0, ey = 0.0, ez = 0.0;
    if (swmm_2d_vertex_get_xyz(engine, best, &ex, &ey, &ez) != 0) return 1.0;

    const double layerC = bestUseX ? mesh.vertices[best].xy.x()
                                   : mesh.vertices[best].xy.y();
    const double engC   = bestUseX ? ex : ey;
    if (std::abs(layerC) < 1e-9) return 1.0;
    const double f = engC / layerC;
    // Guard against a degenerate/zero factor that would collapse the mesh.
    return (std::isfinite(f) && std::abs(f) > 1e-12) ? f : 1.0;
}

} // namespace

bool pushMeshEditsToEngine(SWMM_Engine engine,
                           const MeshResult &mesh,
                           const QVector<MeshEdgeBC> &bcs,
                           QStringList *warnings,
                           bool *outTrianglesSynced,
                           bool *outWriteRejected)
{
    auto warn = [&](const QString &m) { if (warnings) warnings->append(m); };
    if (outTrianglesSynced) *outTrianglesSynced = false;

    if (outWriteRejected) *outWriteRejected = false;
    SyncPerf perf;
    const auto accepted = [&](int rc, const char *field, int item = -1, int edge = -1) {
        if (rc == 0) return true;
        QString where = QString::fromLatin1(field);
        if (item >= 0) where += QStringLiteral(" (item %1)").arg(item + 1);
        if (edge >= 0) where += QStringLiteral(" (edge %1)").arg(edge + 1);
        warn(QStringLiteral("2D mesh sync failed: %1, engine error %2. "
                            "Correct the mesh value and retry Save.").arg(where).arg(rc));
        if (outWriteRejected) *outWriteRejected = true;
        if (outTrianglesSynced) *outTrianglesSynced = false;
        perf.outcome = "engine-rejected";
        return false;
    };

    int nv = 0;
    if (swmm_2d_vertex_count(engine, &nv) != 0 || nv <= 0) {
        perf.outcome = "no-engine-mesh";
        return false;  // engine carries no 2D mesh — nothing to sync
    }
    perf.nv = nv;

    if (nv != mesh.vertices.size()) {
        warn(QStringLiteral("2D mesh sync skipped: engine has %1 vertices, "
                            "layer has %2 — mesh edits were NOT saved.")
                 .arg(nv).arg(mesh.vertices.size()));
        perf.outcome = "vertex-count-mismatch";
        return false;
    }

    // The GUI keeps the engine OPENED (not initialized), so make the parsed
    // mesh editable: this lets the edit setters run and drains the authored
    // BC / conveyance rows so per-edge edits are written on save.
    if (!accepted(swmm_2d_prepare_for_edit(engine), "prepare mesh for editing")) return false;

    const double factor = deriveLengthFactor(engine, mesh);
    // The engine holds BC constants in DISPLAY units until swmm_engine_initialize
    // scales them into SI alongside the mesh (SurfaceRouter2D::initialize). The
    // GUI's editing engine is only OPENED, so the engine expects — and its
    // writer emits — display flow units per metre; pushing m³/s/m here made a
    // US project's constant SPECIFIED_FLOW shrink by 0.0283 on every
    // save/reopen. Convert only once the engine has been initialized (the
    // flow scaling is FLOW_UNITS-driven and independent of the mesh's
    // `;; UNITS: SI (m)` tag, so the lifecycle state — not the length factor
    // — is the right discriminator).
    double flowFactor = 1.0;
    {
        int state = SWMM_STATE_NONE;
        if (!accepted(swmm_engine_get_state(engine, &state), "engine state")) return false;
        const bool initialized = state >= SWMM_STATE_INITIALIZED
                              && state != SWMM_STATE_BUILDING;
        int fu = 3;  // default CMS
        if (initialized) {
            if (!accepted(swmm_get_flow_units(engine, &fu), "flow units")) return false;
            flowFactor = flowUnitToCms(fu);
        }
    }

    // ---- Vertex elevation --------------------------------------------------
    // In ONE call, not one per vertex: the scalar swmm_2d_set_vertex_z rescans
    // every triangle to refresh that vertex's derived geometry, so a per-vertex
    // loop is O(nVertices x nTriangles) inside the engine — minutes on a
    // million-cell mesh. The bulk setter writes all Zs and recomputes the
    // derived geometry once, landing on bitwise-identical values.
    {
        std::vector<double> zs(static_cast<std::size_t>(nv));
        for (int i = 0; i < nv; ++i)
            zs[static_cast<std::size_t>(i)] = mesh.vertices[i].z * factor;
        if (!accepted(swmm_2d_set_vertex_z_bulk(engine, zs.data(), nv),
                      "vertex elevations")) return false;
        perf.zPushed = nv;
    }

    // ---- Vertex coupling and descriptive tag -------------------------------
    // Push every vertex so additions / changes / clears all propagate. The
    // coupled SWMM node and the descriptive [2D_VERTICES] TAG-column label are
    // independent fields; an empty value clears the corresponding slot.
    for (int i = 0; i < nv; ++i) {
        ++perf.vAttrPushed;
        if (!accepted(swmm_2d_set_vertex_coupled_node(
            engine, i, mesh.vertices[i].coupledNode.toUtf8().constData()), "vertex coupled node", i)) return false;
        // Coupling Cd/Area ride along for coupled vertices only (the engine
        // keeps them SI/as-authored — no length factor applies).
        if (!mesh.vertices[i].coupledNode.isEmpty()) {
            if (!accepted(swmm_2d_set_vertex_coupling_cd(engine, i, mesh.vertices[i].couplingCd), "vertex coupling coefficient", i)) return false;
            if (!accepted(swmm_2d_set_vertex_coupling_area(engine, i, mesh.vertices[i].couplingArea), "vertex coupling area", i)) return false;
        }
        if (!accepted(swmm_2d_set_vertex_tag(
            engine, i, mesh.vertices[i].tag.toUtf8().constData()), "vertex tag", i)) return false;
    }

    int nt = 0;
    if (!accepted(swmm_2d_triangle_count(engine, &nt), "cell count")) return false;
    if (nt <= 0) {
        perf.outcome = "no-triangles";
        return true;  // no triangles to touch
    }
    perf.nt = nt;

    // ---- Per-triangle Manning's n + init depth + descriptive tag ----------
    if (nt == mesh.triangles.size()) {
        for (int t = 0; t < nt; ++t) {
            const double n = mesh.triangles[t].mannings;
            if (std::isfinite(n) && n > 0.0) { // NaN = unset; keep engine value
                if (!accepted(swmm_2d_set_triangle_mannings(engine, t, n),
                              "cell roughness", t)) return false;
            }
            const double d = mesh.triangles[t].initDepth;
            // INIT_DEPTH is a depth above the bed, so it carries the mesh's
            // vertical units and converts with the same factor as vertex Z.
            if (std::isfinite(d) && d >= 0.0) { // NaN = unset; keep engine value
                if (!accepted(swmm_2d_set_triangle_init_depth(engine, t, d * factor),
                              "cell initial depth", t)) return false;
            }
            if (!accepted(swmm_2d_set_triangle_tag(
                engine, t, mesh.triangles[t].tag.toUtf8().constData()), "cell tag", t)) return false;
            ++perf.triPushed;
        }
        if (outTrianglesSynced) *outTrianglesSynced = true;
    } else {
        warn(QStringLiteral("2D triangle sync skipped: engine has %1 triangles, "
                            "layer has %2 — per-cell roughness / initial depth / "
                            "tag edits were NOT written to the engine.")
                 .arg(nt).arg(mesh.triangles.size()));
    }

    // ---- Node→cell couplings (Plan Part C) ----------------------------------
    // The row set is re-authored wholesale: clear, then append one row per
    // layer-side CellCoupling. Skipped entirely when the layer has no rows
    // AND the engine has none either — so models that never used cell
    // coupling see zero API traffic. Area is authored in m² on both sides;
    // no length factor applies (same rule as vertex coupling Cd/Area).
    if (nt == mesh.triangles.size()) {
        int engineRows = 0;
        if (!accepted(swmm_2d_triangle_coupling_rows(engine, &engineRows), "cell coupling count")) return false;
        if (!mesh.cellCouplings.isEmpty() || engineRows > 0) {
            if (!accepted(swmm_2d_clear_triangle_couplings(engine), "clear cell couplings")) return false;
            for (const auto &cc : mesh.cellCouplings) {
                if (!accepted(swmm_2d_add_triangle_coupling(
                    engine, cc.tri, cc.nodeId.toUtf8().constData(),
                    cc.cd, cc.area), "cell coupling", cc.tri)) return false;
            }
        }
    }

    // ---- Per-edge conveyance + boundary conditions -------------------------
    if (bcs.isEmpty()) {
        perf.outcome = "no-edge-state";
        return true;  // no BC/conveyance state authored on the layer
    }

    // The layer's edge vector is stride-kEdgeStride (mesh::edgeSlot); the
    // engine's per-edge setters take (cell, localEdge), so only the cell's
    // real edges (vertexCount()) are pushed — a triangle's padding slot 3 is
    // never sent. Needs the layer's cell list to know each cell's edge count.
    if (bcs.size() != edgeSlotCount(nt) || nt != mesh.triangles.size()) {
        warn(QStringLiteral("2D edge sync skipped: engine has %1 edge slots, layer "
                            "has %2 — conveyance/BC edits were NOT saved.")
                 .arg(edgeSlotCount(nt)).arg(bcs.size()));
        perf.outcome = "edge-count-mismatch";
        return true;  // vertex Z still synced above
    }

    using T = MeshBCTypes::Type;
    for (int t = 0; t < nt; ++t) {
        const int ne = mesh.triangles[t].vertexCount();
        for (int e = 0; e < ne; ++e) {
            const MeshEdgeBC &b = bcs[edgeSlot(t, e)];

            // Conveyance is dimensionless; push every edge so resets back to
            // the 1.0 default propagate too (interior edges are mirrored by
            // the engine).
            if (!accepted(swmm_2d_set_edge_conveyance(engine, t, e, b.conveyance), "edge conveyance", t, e)) return false;
            ++perf.edgePushed;

            // Clear all name slots first so a stale timeseries/curve name from
            // the loaded model can't mask a freshly-edited constant value
            // (the engine's writer prefers a name over a scalar).
            if (!accepted(swmm_2d_set_edge_bc_tseries_name(engine, t, e, ""), "edge stage time series", t, e)) return false;
            if (!accepted(swmm_2d_set_edge_bc_flow_tseries_name(engine, t, e, ""), "edge flow time series", t, e)) return false;
            if (!accepted(swmm_2d_set_edge_bc_rating_curve_name(engine, t, e, ""), "edge rating curve", t, e)) return false;

            switch (b.type) {
            case T::Wall:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngWall), "edge boundary type", t, e)) return false;
                break;
            case T::NormalFlow:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngNormalFlow), "edge boundary type", t, e)) return false;
                if (!accepted(swmm_2d_set_edge_bc_slope(engine, t, e, b.slope), "edge boundary slope", t, e)) return false;
                break;
            case T::SpecifiedStageConst:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedStage), "edge boundary type", t, e)) return false;
                if (!accepted(swmm_2d_set_edge_bc_head(engine, t, e, b.head * factor), "edge boundary head", t, e)) return false;
                break;
            case T::SpecifiedStageTS:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedStage), "edge boundary type", t, e)) return false;
                if (!accepted(swmm_2d_set_edge_bc_tseries_name(
                    engine, t, e, b.tseries.toUtf8().constData()), "edge stage time series", t, e)) return false;
                break;
            case T::SpecifiedFlowConst:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedFlow), "edge boundary type", t, e)) return false;
                if (!accepted(swmm_2d_set_edge_bc_flow(engine, t, e, b.flow * flowFactor), "edge boundary flow", t, e)) return false;
                break;
            case T::SpecifiedFlowTS:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedFlow), "edge boundary type", t, e)) return false;
                if (!accepted(swmm_2d_set_edge_bc_flow_tseries_name(
                    engine, t, e, b.tseries.toUtf8().constData()), "edge flow time series", t, e)) return false;
                break;
            case T::RatingCurve:
                if (!accepted(swmm_2d_set_edge_bc_type(engine, t, e, kEngRatingCurve), "edge boundary type", t, e)) return false;
                if (!accepted(swmm_2d_set_edge_bc_rating_curve_name(
                    engine, t, e, b.curve.toUtf8().constData()), "edge rating curve", t, e)) return false;
                break;
            }
        }
    }

    return true;
}

} // namespace mesh
