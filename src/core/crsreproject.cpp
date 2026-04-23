/*!
 * \file   crsreproject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "core/crsreproject.h"
#include "map/spatialreferencesystem.h"

#include <QByteArray>
#include <QDebug>

#include <memory>
#include <vector>

#ifdef HAVE_OPENSWMMCORE
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <ogr_spatialref.h>
#endif

namespace CRSReproject {

#ifdef HAVE_OPENSWMMCORE

namespace {

// Transform a packed (x[], y[]) pair in place. Returns count of points that
// transformed cleanly (OGR sets the components it couldn't transform; we
// don't filter here — the engine accepts whatever we give back).
int transformInPlace(OGRCoordinateTransformation *t,
                     std::vector<double> &xs,
                     std::vector<double> &ys)
{
    const int n = static_cast<int>(xs.size());
    if (n == 0 || !t) return 0;
    // OGR's Transform() returns TRUE if all points succeeded; we still pass
    // whatever it produced back to the engine — partial reprojection beats
    // refusing the whole model.
    t->Transform(n, xs.data(), ys.data());
    return n;
}

} // anonymous

bool reprojectModel(SWMM_Engine engine,
                    const SpatialReferenceSystem &oldSRS,
                    const SpatialReferenceSystem &newSRS,
                    Stats *stats,
                    QString *errorOut)
{
    Stats local{};
    auto setStats = [&]() { if (stats) *stats = local; };

    std::unique_ptr<OGRCoordinateTransformation, decltype(&OGRCoordinateTransformation::DestroyCT)>
        xform(oldSRS.createTransformationTo(newSRS),
              &OGRCoordinateTransformation::DestroyCT);

    if (!xform)
    {
        if (errorOut)
            *errorOut = QStringLiteral("Could not build OGR transform from %1 to %2.")
                            .arg(oldSRS.toAuthority(), newSRS.toAuthority());
        setStats();
        return false;
    }

    // ---- Nodes (bulk) ------------------------------------------------------
    {
        const int n = swmm_node_count(engine);
        if (n > 0)
        {
            std::vector<double> xs(n), ys(n);
            if (swmm_spatial_get_node_coords_bulk(engine, xs.data(), ys.data(), n) == 0)
            {
                local.nodes = transformInPlace(xform.get(), xs, ys);
                swmm_spatial_set_node_coords_bulk(engine, xs.data(), ys.data(), n);
            }
        }
    }

    // ---- Link vertices (per-link, variable length) -------------------------
    {
        const int nLinks = swmm_link_count(engine);
        for (int i = 0; i < nLinks; ++i)
        {
            int vc = 0;
            if (swmm_spatial_get_link_vertex_count(engine, i, &vc) != 0 || vc <= 0)
                continue;
            std::vector<double> xs(vc), ys(vc);
            if (swmm_spatial_get_link_vertices(engine, i, xs.data(), ys.data(), vc) != 0)
                continue;
            local.linkVertices += transformInPlace(xform.get(), xs, ys);
            swmm_spatial_set_link_vertices(engine, i, xs.data(), ys.data(), vc);
        }
    }

    // ---- Subcatchment polygons (per-sub, variable length) ------------------
    {
        const int nSub = swmm_subcatch_count(engine);
        for (int i = 0; i < nSub; ++i)
        {
            int vc = 0;
            if (swmm_spatial_get_subcatch_polygon_count(engine, i, &vc) != 0 || vc <= 0)
                continue;
            std::vector<double> xs(vc), ys(vc);
            if (swmm_spatial_get_subcatch_polygon(engine, i, xs.data(), ys.data(), vc) != 0)
                continue;
            local.polygonVerts += transformInPlace(xform.get(), xs, ys);
            swmm_spatial_set_subcatch_polygon(engine, i, xs.data(), ys.data(), vc);

            // Subcatch reference (centroid) coordinate, if present.
            double cx = 0, cy = 0;
            if (swmm_spatial_get_subcatch_coord(engine, i, &cx, &cy) == 0)
            {
                std::vector<double> sx{cx}, sy{cy};
                transformInPlace(xform.get(), sx, sy);
                swmm_spatial_set_subcatch_coord(engine, i, sx[0], sy[0]);
            }
        }
    }

    // ---- Write the new CRS string to the engine ----------------------------
    {
        const QString wkt = newSRS.toWkt();
        const QString auth = newSRS.toAuthority();
        // Prefer an authority code (compact, round-trips cleanly via the .inp
        // [PROJECTION] section); fall back to WKT if no authority is known.
        const QByteArray crsStr = (auth.isEmpty() || auth == QStringLiteral("Local"))
                                      ? wkt.toUtf8()
                                      : auth.toUtf8();
        swmm_spatial_set_crs(engine, crsStr.constData());
    }

    setStats();
    return local.nodes > 0 || local.linkVertices > 0 || local.polygonVerts > 0;
}

#else // !HAVE_OPENSWMMCORE

bool reprojectModel(SWMM_Engine, const SpatialReferenceSystem &,
                    const SpatialReferenceSystem &,
                    Stats *stats, QString *errorOut)
{
    if (stats) *stats = {};
    if (errorOut) *errorOut = QStringLiteral("OpenSWMMCore not available.");
    return false;
}

#endif // HAVE_OPENSWMMCORE

} // namespace CRSReproject
