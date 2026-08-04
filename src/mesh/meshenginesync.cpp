/*!
 * \file   meshenginesync.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshenginesync.h"

#include "mesh/meshbctype.h"

#include <openswmm/engine/openswmm_engine.h>  // swmm_get_flow_units
#include <openswmm/engine/openswmm_2d.h>

#include <cmath>

namespace mesh {

namespace {

// Engine [2D_BOUNDARY_CONDITIONS] type codes (openswmm_2d.h).
constexpr int kEngWall           = 0;
constexpr int kEngNormalFlow     = 1;
constexpr int kEngSpecifiedStage = 2;
constexpr int kEngSpecifiedFlow  = 3;
constexpr int kEngRatingCurve    = 4;

// Display flow unit -> m³/s. The engine's SPECIFIED_FLOW BC is stored in SI
// (m³/s per metre of edge) regardless of FLOW_UNITS, while the layer carries
// the BC discharge in the project's display flow units. Index by the
// swmm_get_flow_units() enum: CFS, GPM, MGD, CMS, LPS, MLD.
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
                           QStringList *warnings)
{
    auto warn = [&](const QString &m) { if (warnings) warnings->append(m); };

    // The GUI keeps the engine OPENED (not initialized), so make the parsed
    // mesh editable: this lets the edit setters run and drains the authored
    // BC / conveyance rows so per-edge edits are written on save.
    swmm_2d_prepare_for_edit(engine);

    int nv = 0;
    if (swmm_2d_vertex_count(engine, &nv) != 0 || nv <= 0)
        return false;  // engine carries no 2D mesh — nothing to sync

    if (nv != mesh.vertices.size()) {
        warn(QStringLiteral("2D mesh sync skipped: engine has %1 vertices, "
                            "layer has %2 — mesh edits were NOT saved.")
                 .arg(nv).arg(mesh.vertices.size()));
        return false;
    }

    const double factor = deriveLengthFactor(engine, mesh);
    double flowFactor = 1.0;
    {
        int fu = 3;  // default CMS
        if (swmm_get_flow_units(engine, &fu) == 0) flowFactor = flowUnitToCms(fu);
    }

    // ---- Vertex elevation, coupling, and descriptive tag -------------------
    // Push every vertex so additions / changes / clears all propagate. The
    // coupled SWMM node and the descriptive [2D_VERTICES] TAG-column label are
    // independent fields; an empty value clears the corresponding slot.
    for (int i = 0; i < nv; ++i) {
        swmm_2d_set_vertex_z(engine, i, mesh.vertices[i].z * factor);
        swmm_2d_set_vertex_coupled_node(
            engine, i, mesh.vertices[i].coupledNode.toUtf8().constData());
        // Coupling Cd/Area ride along for coupled vertices only (the engine
        // keeps them SI/as-authored — no length factor applies).
        if (!mesh.vertices[i].coupledNode.isEmpty()) {
            swmm_2d_set_vertex_coupling_cd(engine, i, mesh.vertices[i].couplingCd);
            swmm_2d_set_vertex_coupling_area(engine, i, mesh.vertices[i].couplingArea);
        }
        swmm_2d_set_vertex_tag(
            engine, i, mesh.vertices[i].tag.toUtf8().constData());
    }

    int nt = 0;
    if (swmm_2d_triangle_count(engine, &nt) != 0 || nt <= 0)
        return true;  // no triangles to touch

    // ---- Per-triangle Manning's n + init depth + descriptive tag ----------
    if (nt == mesh.triangles.size()) {
        for (int t = 0; t < nt; ++t) {
            const double n = mesh.triangles[t].mannings;
            if (std::isfinite(n) && n > 0.0)   // NaN = unset; keep engine value
                swmm_2d_set_triangle_mannings(engine, t, n);
            const double d = mesh.triangles[t].initDepth;
            if (std::isfinite(d) && d >= 0.0)  // NaN = unset; keep engine value
                swmm_2d_set_triangle_init_depth(engine, t, d);
            swmm_2d_set_triangle_tag(
                engine, t, mesh.triangles[t].tag.toUtf8().constData());
        }
    } else {
        warn(QStringLiteral("2D triangle sync skipped: engine has %1 triangles, "
                            "layer has %2 — roughness/tag edits were NOT saved.")
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
        swmm_2d_triangle_coupling_rows(engine, &engineRows);
        if (!mesh.cellCouplings.isEmpty() || engineRows > 0) {
            swmm_2d_clear_triangle_couplings(engine);
            for (const auto &cc : mesh.cellCouplings) {
                if (cc.tri < 0 || cc.tri >= nt || cc.nodeId.isEmpty()) continue;
                swmm_2d_add_triangle_coupling(
                    engine, cc.tri, cc.nodeId.toUtf8().constData(),
                    cc.cd, cc.area);
            }
        }
    }

    // ---- Per-edge conveyance + boundary conditions -------------------------
    if (bcs.isEmpty())
        return true;  // no BC/conveyance state authored on the layer

    if (bcs.size() != nt * 3) {
        warn(QStringLiteral("2D edge sync skipped: engine has %1 edges, layer "
                            "has %2 — conveyance/BC edits were NOT saved.")
                 .arg(nt * 3).arg(bcs.size()));
        return true;  // vertex Z still synced above
    }

    using T = MeshBCTypes::Type;
    for (int t = 0; t < nt; ++t) {
        for (int e = 0; e < 3; ++e) {
            const MeshEdgeBC &b = bcs[t * 3 + e];

            // Conveyance is dimensionless; push every edge so resets back to
            // the 1.0 default propagate too (interior edges are mirrored by
            // the engine).
            swmm_2d_set_edge_conveyance(engine, t, e, b.conveyance);

            // Clear all name slots first so a stale timeseries/curve name from
            // the loaded model can't mask a freshly-edited constant value
            // (the engine's writer prefers a name over a scalar).
            swmm_2d_set_edge_bc_tseries_name(engine, t, e, "");
            swmm_2d_set_edge_bc_flow_tseries_name(engine, t, e, "");
            swmm_2d_set_edge_bc_rating_curve_name(engine, t, e, "");

            switch (b.type) {
            case T::Wall:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngWall);
                break;
            case T::NormalFlow:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngNormalFlow);
                swmm_2d_set_edge_bc_slope(engine, t, e, b.slope);
                break;
            case T::SpecifiedStageConst:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedStage);
                swmm_2d_set_edge_bc_head(engine, t, e, b.head * factor);
                break;
            case T::SpecifiedStageTS:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedStage);
                swmm_2d_set_edge_bc_tseries_name(
                    engine, t, e, b.tseries.toUtf8().constData());
                break;
            case T::SpecifiedFlowConst:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedFlow);
                swmm_2d_set_edge_bc_flow(engine, t, e, b.flow * flowFactor);
                break;
            case T::SpecifiedFlowTS:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngSpecifiedFlow);
                swmm_2d_set_edge_bc_flow_tseries_name(
                    engine, t, e, b.tseries.toUtf8().constData());
                break;
            case T::RatingCurve:
                swmm_2d_set_edge_bc_type(engine, t, e, kEngRatingCurve);
                swmm_2d_set_edge_bc_rating_curve_name(
                    engine, t, e, b.curve.toUtf8().constData());
                break;
            }
        }
    }

    return true;
}

} // namespace mesh
