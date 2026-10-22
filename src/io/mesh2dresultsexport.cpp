/**
 * @file      mesh2dresultsexport.cpp
 * @brief     Export a 2D results run (an IMesh2DSource — the engine's CF/UGRID `.h5`, or any
 *            in-memory source) to GIS.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "io/mesh2dresultsexport.h"

#include "io/gdaldrivers.h"
#include "layers/meshspatialgrid.h"
#include "layers/swmm2dresultslayer.h" // IMesh2DSource
#include "mesh/meshcellgeom.h"
#include "mesh/meshresult.h"
#include "mesh/naturalnbinterpolator.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>

#include <nanoflann.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace openswmmvis::io
{

namespace
{

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/**
 * @brief Read the current GDAL error for a GIS results diagnostic.
 * @return Error text or an unknown-error fallback.
 */
QString lastGdalError()
{
    const char *msg = CPLGetLastErrorMsg();
    return (msg && *msg) ? QString::fromUtf8(msg) : QStringLiteral("unknown GDAL error");
}

/**
 * @brief Choose a GDAL driver for the selected output container.
 * @param[in] f Export format.
 * @return Borrowed static driver name; unknown values fall back to GTiff.
 */
const char *driverNameFor(Mesh2DExportFormat f)
{
    switch (f) {
    case Mesh2DExportFormat::Shapefile:
        return "ESRI Shapefile";
    case Mesh2DExportFormat::GeoPackage:
        return "GPKG";
    case Mesh2DExportFormat::GeoTiff:
        return "GTiff";
    }
    return "GTiff";
}

/*! Build an OGRSpatialReference from WKT (caller releases; null when empty or
 *  unparseable — an unreadable CRS costs the `.prj`, not the export).
 * @param[in] wkt CRS definition; empty disables CRS assignment.
 * @return Owned spatial reference requiring release/delete by the caller, or null for empty/invalid WKT.
 */
OGRSpatialReference *srsFromWkt(const QString &wkt)
{
    if (wkt.isEmpty())
        return nullptr;
    auto *srs = new OGRSpatialReference();
    if (srs->importFromWkt(wkt.toUtf8().constData()) != OGRERR_NONE) {
        delete srs;
        return nullptr;
    }
    return srs;
}

/*! The requested variables, in a stable display order.
 * @param[in] mask Mesh2DVariable bit set; unknown bits are ignored.
 * @return Selected supported variables in depth/head/x/y/speed order.
 */
std::vector<Mesh2DVariable> requestedVariables(unsigned mask)
{
    std::vector<Mesh2DVariable> out;
    for (Mesh2DVariable v : {Mesh2DDepth, Mesh2DHead, Mesh2DVx, Mesh2DVy, Mesh2DVmag})
        if (mask & unsigned(v))
            out.push_back(v);
    return out;
}

/*! Field / band name for the n-th selected step: `t0001`… — 5 characters, well
 *  inside the DBF's 10-character limit, and identical across formats so one
 *  sidecar CSV describes every output.
 * @param[in] ordinal Zero-based selected-step ordinal, independent of the actual source frame.
 * @return t plus one-based ordinal padded to at least four decimal digits.
 */
QString stepFieldName(int ordinal /*0-based*/)
{
    return QStringLiteral("t%1").arg(ordinal + 1, 4, 10, QLatin1Char('0'));
}

constexpr const char *kMaxFieldName = "max";

// ---------------------------------------------------------------------------
// CellMesh — the source's cells in MODEL units, in mesh:: types so the
// existing geometry helpers apply verbatim. The sub-triangle fan is cached
// because point location runs once per raster pixel and mesh::cellContains
// would otherwise re-derive a quad's Begnudelli–Sanders split every time.
// ---------------------------------------------------------------------------

struct CellMesh {
    QVector<mesh::MeshVertex> verts;
    std::vector<mesh::MeshTriangle> cells;
    std::vector<double> cx, cy; ///< area centroid per cell
    std::vector<double> area;   ///< model units²
    std::vector<double> zMean;  ///< mean bed elevation, model units

    std::vector<std::array<int, 3>> sub; ///< sub-triangle fan
    std::vector<int> subStart;           ///< CSR cell → fan slice

    /**
     * @brief Count native cells, rather than display triangles.
     * @return Cell vector length narrowed to int.
     */
    int cellCount() const { return int(cells.size()); }
};

/**
 * @brief Copy native geometry into scaled mesh types and cache centroid/display-fan data.
 * @param[in] src Non-null borrowed geometry source on its permitted thread.
 * @param[in] unitFactor Multiplier from SI metres to model coordinate/elevation units.
 * @param[out] out Scaled geometry/area/cache data, possibly partially assigned on failure.
 * @return False for failed/empty geometry or invalid first-three corner indices; an out-of-range fourth corner is treated as triangle padding.
 */
bool buildCellMesh(IMesh2DSource *src, double unitFactor, CellMesh &out)
{
    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    if (!src->readCells(vx, vy, vz, cells) || cells.empty())
        return false;

    out.verts.resize(int(vx.size()));
    for (size_t i = 0; i < vx.size(); ++i) {
        out.verts[int(i)].xy = QPointF(vx[i] * unitFactor, vy[i] * unitFactor);
        out.verts[int(i)].z = vz[i] * unitFactor;
    }

    const int nv = out.verts.size();
    const size_t n = cells.size();
    out.cells.resize(n);
    out.cx.resize(n);
    out.cy.resize(n);
    out.area.resize(n);
    out.zMean.resize(n);
    out.subStart.assign(n + 1, 0);
    out.sub.clear();
    out.sub.reserve(n * 2);

    for (size_t c = 0; c < n; ++c) {
        mesh::MeshTriangle t;
        t.v0 = cells[c][0];
        t.v1 = cells[c][1];
        t.v2 = cells[c][2];
        t.v3 = cells[c][3];
        // A truncated or corrupt file must not index past the vertex array.
        if (t.v0 < 0 || t.v0 >= nv || t.v1 < 0 || t.v1 >= nv || t.v2 < 0 || t.v2 >= nv)
            return false;
        if (t.v3 >= nv)
            t.v3 = -1;
        out.cells[c] = t;

        const mesh::CellGeom g = mesh::cellGeom(out.verts, t);
        out.cx[c] = g.centroid.x();
        out.cy[c] = g.centroid.y();
        out.area[c] = g.area;
        out.zMean[c] = g.zMean;

        out.subStart[c] = int(out.sub.size());
        for (int s = 0; s < g.nSub; ++s)
            out.sub.push_back(g.sub[s]);
    }
    out.subStart[n] = int(out.sub.size());
    return true;
}

/*! Barycentric point-in-triangle over the cached fan slice of \p cell.
 * @param[in] m Cached scaled mesh.
 * @param[in] cell In-range native-cell ID with a cached display fan.
 * @param[in] x Map X coordinate in model units.
 * @param[in] y Map Y coordinate in model units.
 * @return True in any nondegenerate fan triangle, allowing barycentric tolerance 1e-9; false otherwise.
 */
bool cellContainsFast(const CellMesh &m, int cell, double x, double y)
{
    for (int s = m.subStart[size_t(cell)]; s < m.subStart[size_t(cell) + 1]; ++s) {
        const std::array<int, 3> &tri = m.sub[size_t(s)];
        const QPointF &a = m.verts[tri[0]].xy;
        const QPointF &b = m.verts[tri[1]].xy;
        const QPointF &c = m.verts[tri[2]].xy;
        const double d = (b.y() - c.y()) * (a.x() - c.x()) + (c.x() - b.x()) * (a.y() - c.y());
        if (std::abs(d) < 1e-300)
            continue;
        const double l1 = ((b.y() - c.y()) * (x - c.x()) + (c.x() - b.x()) * (y - c.y())) / d;
        const double l2 = ((c.y() - a.y()) * (x - c.x()) + (a.x() - c.x()) * (y - c.y())) / d;
        const double l3 = 1.0 - l1 - l2;
        constexpr double eps = 1e-9;
        if (l1 >= -eps && l2 >= -eps && l3 >= -eps)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Field loading
// ---------------------------------------------------------------------------

constexpr const char *kDsHead = "Mesh2_face_head";
constexpr const char *kDsVx = "Mesh2_face_vx";
constexpr const char *kDsVy = "Mesh2_face_vy";
constexpr const char *kDsMaxDepth = "Mesh2_face_max_depth";
constexpr const char *kDsMaxVel = "Mesh2_face_max_velocity";

/*! One variable at one frame, converted to MODEL units.
 *
 *  Head falls back to bed + depth for a source with no head field (the live
 *  in-process source). Speed is built from the two components.
 * @param[in] src Borrowed synchronous result source.
 * @param[in] m Scaled cell geometry and mean bed elevations.
 * @param[in] var One supported output variable.
 * @param[in] frame Available zero-based source report index.
 * @param[in] unitFactor SI-to-model multiplier for length or velocity.
 * @param[out] out Cell-aligned model-unit values, assigned after reads succeed; absent tails and nonfinite inputs become zero.
 * @return False on a required field read failure, leaving out unchanged.
 */
bool loadFrame(IMesh2DSource *src, const CellMesh &m, Mesh2DVariable var, int frame,
               double unitFactor, std::vector<float> &out)
{
    const int n = m.cellCount();
    const double invFactor = (unitFactor != 0.0) ? 1.0 / unitFactor : 1.0;
    std::vector<float> a, b;

    switch (var) {
    case Mesh2DDepth:
        if (!src->readDepthsAt(frame, a))
            return false;
        break;
    case Mesh2DHead:
        if (src->hasFaceField(kDsHead)) {
            if (!src->readFaceFieldAt(kDsHead, frame, a))
                return false;
        } else {
            if (!src->readDepthsAt(frame, a))
                return false;
            // Everything in `a` is engine SI at this point and the tail scales
            // it, so the model-unit bed elevation is converted back to metres
            // before it is added.
            const int lim = std::min(n, int(a.size()));
            for (int i = 0; i < lim; ++i)
                a[size_t(i)] += float(m.zMean[size_t(i)] * invFactor);
        }
        break;
    case Mesh2DVx:
        if (!src->readFaceFieldAt(kDsVx, frame, a))
            return false;
        break;
    case Mesh2DVy:
        if (!src->readFaceFieldAt(kDsVy, frame, a))
            return false;
        break;
    case Mesh2DVmag:
        if (!src->readFaceFieldAt(kDsVx, frame, a))
            return false;
        if (!src->readFaceFieldAt(kDsVy, frame, b))
            return false;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            a[i] = float(std::hypot(double(a[i]), double(b[i])));
        break;
    }

    out.assign(size_t(n), 0.0f);
    const size_t lim = std::min(size_t(n), a.size());
    for (size_t i = 0; i < lim; ++i)
        out[i] = std::isfinite(a[i]) ? float(double(a[i]) * unitFactor) : 0.0f;
    return true;
}

/*! Whole-run maximum of \p var in MODEL units: the engine's ENVELOPES dataset
 *  when the file carries it (one read), else a scan of every frame — the only
 *  part of the export that is slow on a long run, so the scan reports progress
 *  and honours cancellation.
 * @param[in] src Borrowed result source.
 * @param[in] m Cell geometry defining output length and bed datum.
 * @param[in] var Supported whole-run depth, head, or speed variable.
 * @param[in] unitFactor SI-to-model value multiplier.
 * @param[in] progress Optional callback during fallback scans, reporting scanned frames.
 * @param[out] cancelled Reset false on entry, set true if callback cancels.
 * @param[out] out Whole-run maximum values, possibly partial on failure/cancellation.
 * @return True after envelope read or all-frame scan; false for no frames, failed reads, or cancellation.
 */
bool loadMax(IMesh2DSource *src, const CellMesh &m, Mesh2DVariable var, double unitFactor,
             const Mesh2DExportProgress &progress, bool &cancelled, std::vector<float> &out)
{
    const int n = m.cellCount();
    cancelled = false;

    const char *envelope = (var == Mesh2DDepth)  ? kDsMaxDepth
                           : (var == Mesh2DVmag) ? kDsMaxVel
                                                 : nullptr;
    if (envelope && src->readFaceEnvelope(envelope, out)) {
        out.resize(size_t(n), 0.0f);
        for (float &v : out)
            v = std::isfinite(v) ? float(double(v) * unitFactor) : 0.0f;
        return true;
    }

    const int frames = src->timeCount();
    if (frames <= 0)
        return false;
    out.assign(size_t(n), -std::numeric_limits<float>::max());
    std::vector<float> frame;
    for (int t = 0; t < frames; ++t) {
        if (!loadFrame(src, m, var, t, unitFactor, frame))
            return false;
        for (size_t i = 0; i < out.size() && i < frame.size(); ++i)
            out[i] = std::max(out[i], frame[i]);
        if (progress &&
            !progress(t + 1, frames, QStringLiteral("Scanning every frame for the maximum…"))) {
            cancelled = true;
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Vector writer (Shapefile / GeoPackage)
// ---------------------------------------------------------------------------

/*! The polygon ring of cell \p c, closed and wound CLOCKWISE — the Shapefile
 *  specification's outer-ring convention, which GeoPackage also accepts.
 * @param[in] m Scaled native mesh.
 * @param[in] c In-range cell ID.
 * @param[in,out] ring OGR ring receiving cell corners and closure; caller should provide an empty ring.
 */
void appendCellRing(const CellMesh &m, int c, OGRLinearRing &ring)
{
    const mesh::MeshTriangle &t = m.cells[size_t(c)];
    const int nv = t.vertexCount();
    const bool ccw = mesh::cellSignedArea(m.verts, t) > 0.0;
    for (int k = 0; k < nv; ++k) {
        const QPointF &p = m.verts[t.vertex(ccw ? (nv - 1 - k) : k)].xy;
        ring.addPoint(p.x(), p.y());
    }
    ring.closeRings();
}

struct VariableData {
    Mesh2DVariable var = Mesh2DDepth;
    std::vector<std::vector<float>> steps; ///< one per selected time step
    std::vector<float> max;                ///< empty unless hasMax
    bool hasMax = false;
};

/**
 * @brief Write native cell polygons and selected time/max fields into a dataset layer.
 * @param[in,out] ds Non-null open destination dataset, owned by caller.
 * @param[in] layerName Name for the new polygon layer.
 * @param[in] m Scaled cell geometry.
 * @param[in] data Cell-aligned values for every requested field.
 * @param[in] srs Nullable borrowed spatial reference assigned to the new layer.
 * @param[out] error Diagnostic assigned on failure.
 * @return True after all features and any supported transaction commit; false may leave created layer/schema artifacts for outer cleanup.
 */
bool writeVectorLayer(GDALDataset *ds, const QString &layerName, const CellMesh &m,
                      const VariableData &data, OGRSpatialReference *srs, QString &error)
{
    CPLErrorReset();
    OGRLayer *layer = ds->CreateLayer(layerName.toUtf8().constData(), srs, wkbPolygon, nullptr);
    if (!layer) {
        error =
            QStringLiteral("Could not create the layer \"%1\": %2").arg(layerName, lastGdalError());
        return false;
    }

    OGRFieldDefn idField("cell_id", OFTInteger);
    if (layer->CreateField(&idField) != OGRERR_NONE) {
        error = QStringLiteral("Could not create the cell_id column: %1").arg(lastGdalError());
        return false;
    }
    const int nSteps = int(data.steps.size());
    for (int s = 0; s < nSteps; ++s) {
        OGRFieldDefn f(stepFieldName(s).toUtf8().constData(), OFTReal);
        f.SetWidth(18);
        f.SetPrecision(6);
        if (layer->CreateField(&f) != OGRERR_NONE) {
            error = QStringLiteral("Could not create the column \"%1\": %2")
                        .arg(stepFieldName(s), lastGdalError());
            return false;
        }
    }
    if (data.hasMax) {
        OGRFieldDefn f(kMaxFieldName, OFTReal);
        f.SetWidth(18);
        f.SetPrecision(6);
        if (layer->CreateField(&f) != OGRERR_NONE) {
            error = QStringLiteral("Could not create the max column: %1").arg(lastGdalError());
            return false;
        }
    }

    // One transaction for the layer: GeoPackage otherwise commits per feature
    // and a large mesh takes minutes instead of seconds.
    const bool transacted = layer->StartTransaction() == OGRERR_NONE;
    OGRFeatureDefn *defn = layer->GetLayerDefn();

    for (int c = 0; c < m.cellCount(); ++c) {
        OGRFeature *feat = OGRFeature::CreateFeature(defn);
        if (!feat) {
            error = QStringLiteral("Could not allocate a feature.");
            if (transacted)
                layer->RollbackTransaction();
            return false;
        }
        auto *poly = new OGRPolygon();
        OGRLinearRing ring;
        appendCellRing(m, c, ring);
        poly->addRing(&ring);
        feat->SetGeometryDirectly(poly);

        int f = 0;
        feat->SetField(f++, c);
        for (int s = 0; s < nSteps; ++s)
            feat->SetField(f++, double(data.steps[size_t(s)][size_t(c)]));
        if (data.hasMax)
            feat->SetField(f++, double(data.max[size_t(c)]));

        CPLErrorReset();
        const OGRErr err = layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);
        if (err != OGRERR_NONE) {
            error = QStringLiteral("Could not write cell %1: %2").arg(c).arg(lastGdalError());
            if (transacted)
                layer->RollbackTransaction();
            return false;
        }
    }
    if (transacted && layer->CommitTransaction() != OGRERR_NONE) {
        error = QStringLiteral("Could not commit \"%1\": %2").arg(layerName, lastGdalError());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Raster grid + interpolation
// ---------------------------------------------------------------------------

struct RasterGrid {
    double xmin = 0.0, ymax = 0.0, cellSize = 1.0;
    int w = 0, h = 0;

    /**
     * @brief Locate a pixel-center column coordinate.
     * @param[in] i Zero-based column.
     * @return xmin plus (i+0.5) times cellSize, in model coordinate units.
     */
    double pixelX(int i) const { return xmin + (double(i) + 0.5) * cellSize; }
    /**
     * @brief Locate a north-up pixel-center row coordinate.
     * @param[in] j Zero-based row.
     * @return ymax minus (j+0.5) times cellSize, in model coordinate units.
     */
    double pixelY(int j) const { return ymax - (double(j) + 0.5) * cellSize; }
};

/*! Half the square root of the median cell area — see Mesh2DGridHint.
 * @param[in] m Mesh with one model-unit squared area per cell.
 * @return Half the square root of the upper median area, or zero for empty/nonpositive median area.
 */
double medianCellSize(const CellMesh &m)
{
    if (m.cellCount() == 0)
        return 0.0;
    std::vector<double> areas = m.area;
    const size_t mid = areas.size() / 2;
    std::nth_element(areas.begin(), areas.begin() + mid, areas.end());
    const double median = areas[mid];
    return (median > 0.0) ? std::sqrt(median) / 2.0 : 0.0;
}

/**
 * @brief Size a north-up square-pixel grid covering mesh vertex bounds.
 * @param[in] m Nonempty finite scaled mesh.
 * @param[in] cellSize Positive pixel width in model units, with resulting dimensions representable as int.
 * @return Grid rounded up to cover the bounds, or zero dimensions for nonpositive size/degenerate bounds.
 */
RasterGrid makeGrid(const CellMesh &m, double cellSize)
{
    RasterGrid g;
    double xmin = std::numeric_limits<double>::max();
    double ymin = xmin, xmax = -xmin, ymax = -xmin;
    for (const mesh::MeshVertex &v : m.verts) {
        xmin = std::min(xmin, v.xy.x());
        xmax = std::max(xmax, v.xy.x());
        ymin = std::min(ymin, v.xy.y());
        ymax = std::max(ymax, v.xy.y());
    }
    if (!(xmax > xmin) || !(ymax > ymin) || !(cellSize > 0.0))
        return g;
    g.xmin = xmin;
    g.ymax = ymax;
    g.cellSize = cellSize;
    g.w = std::max(1, int(std::ceil((xmax - xmin) / cellSize)));
    g.h = std::max(1, int(std::ceil((ymax - ymin) / cellSize)));
    return g;
}

struct PtAdaptor {
    const double *xs = nullptr;
    const double *ys = nullptr;
    std::size_t n = 0;

    /**
     * @brief Expose the borrowed centroid count to nanoflann.
     * @return Number of points in the adaptor.
     */
    std::size_t kdtree_get_point_count() const { return n; }
    /**
     * @brief Read a centroid component through the nanoflann adaptor.
     * @param[in] i In-range point index.
     * @param[in] dim Axis index; zero selects X and other values select Y.
     * @return Borrowed-array coordinate in model units.
     */
    double kdtree_get_pt(std::size_t i, std::size_t dim) const { return dim == 0 ? xs[i] : ys[i]; }
    /**
     * @brief Request automatic nanoflann bounding-box computation.
     * @tparam BBOX Nanoflann bounding-box representation; its unnamed reference is unused.
     * @return Always false, indicating no precomputed box is supplied.
     */
    template <class BBOX> bool kdtree_get_bbox(BBOX &) const { return false; }
};

using Kd2 = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<double, PtAdaptor>,
                                                PtAdaptor, 2>;

/*! Cell adjacency across shared edges, for the Green–Gauss gradient.
 *
 *  Slot `[cell * kEdgeStride + k]` holds the neighbour across local edge k, or
 *  -1 on a boundary edge. Edge length and outward normal are recomputed here in
 *  MODEL units, so the module works the same whether or not the source carries
 *  the engine's cached (SI) edge geometry. */
struct CellAdjacency {
    std::vector<int> nbr;
    std::vector<double> len, nx, ny;
};

/**
 * @brief Pair shared native edges and derive model-unit edge lengths/outward normals.
 * @param[in] m Valid manifold mesh; at most two cells should share an undirected edge.
 * @return Four-slot cell adjacency with -1 boundary neighbors and zero triangle padding.
 */
CellAdjacency buildAdjacency(const CellMesh &m)
{
    const int n = m.cellCount();
    // NB: not `slots` — Qt defines that as a keyword macro.
    const size_t slotCount = size_t(mesh::edgeSlotCount(n));
    CellAdjacency a;
    a.nbr.assign(slotCount, -1);
    a.len.assign(slotCount, 0.0);
    a.nx.assign(slotCount, 0.0);
    a.ny.assign(slotCount, 0.0);

    struct EdgeKey {
        int lo, hi, cell, k;
    };
    std::vector<EdgeKey> keys;
    keys.reserve(size_t(n) * 4);

    for (int c = 0; c < n; ++c) {
        const mesh::MeshTriangle &t = m.cells[size_t(c)];
        const int nv = t.vertexCount();
        for (int k = 0; k < nv; ++k) {
            int va = 0, vb = 0;
            mesh::edgeEndpoints(t, k, va, vb);
            const QPointF &A = m.verts[va].xy, &B = m.verts[vb].xy;
            const double ex = B.x() - A.x(), ey = B.y() - A.y();
            const double L = std::hypot(ex, ey);
            const size_t slot = size_t(mesh::edgeSlot(c, k));
            a.len[slot] = L;
            if (L > 0.0) {
                double nxv = ey / L, nyv = -ex / L; // right normal
                const double mx = 0.5 * (A.x() + B.x()) - m.cx[size_t(c)];
                const double my = 0.5 * (A.y() + B.y()) - m.cy[size_t(c)];
                if (nxv * mx + nyv * my < 0.0) {
                    nxv = -nxv;
                    nyv = -nyv;
                } // outward
                a.nx[slot] = nxv;
                a.ny[slot] = nyv;
            }
            keys.push_back({std::min(va, vb), std::max(va, vb), c, k});
        }
    }

    std::sort(keys.begin(), keys.end(), [](const EdgeKey &p, const EdgeKey &q) {
        if (p.lo != q.lo)
            return p.lo < q.lo;
        if (p.hi != q.hi)
            return p.hi < q.hi;
        return p.cell < q.cell;
    });
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i].lo == keys[i + 1].lo && keys[i].hi == keys[i + 1].hi) {
            const EdgeKey &p = keys[i], &q = keys[i + 1];
            a.nbr[size_t(mesh::edgeSlot(p.cell, p.k))] = q.cell;
            a.nbr[size_t(mesh::edgeSlot(q.cell, q.k))] = p.cell;
            ++i; // an edge is shared by at most two cells
        }
    }
    return a;
}

/*! Green–Gauss cell gradients of \p phi plus the neighbour-stencil bounds that
 *  clamp the reconstruction, so a steep wet/dry front cannot overshoot.
 * @param[in] m Cell areas, geometry, and vertex counts in model units.
 * @param[in] adj Matching four-slot adjacency/edge normals.
 * @param[in] phi One scalar per cell.
 * @param[out] gx Cell-aligned X derivative in scalar units per model length.
 * @param[out] gy Cell-aligned Y derivative in scalar units per model length.
 * @param[out] lo Minimum of each cell and its immediate neighbors.
 * @param[out] hi Maximum over that same stencil.
 * @details Boundary edges use the cell value (zero-gradient extension); nonpositive-area cells retain zero gradients.
 */
void greenGaussGradients(const CellMesh &m, const CellAdjacency &adj, const std::vector<float> &phi,
                         std::vector<float> &gx, std::vector<float> &gy, std::vector<float> &lo,
                         std::vector<float> &hi)
{
    const int n = m.cellCount();
    gx.assign(size_t(n), 0.0f);
    gy.assign(size_t(n), 0.0f);
    lo.assign(size_t(n), 0.0f);
    hi.assign(size_t(n), 0.0f);
    for (int c = 0; c < n; ++c) {
        const double phic = double(phi[size_t(c)]);
        double sx = 0.0, sy = 0.0, mn = phic, mx = phic;
        const int nv = m.cells[size_t(c)].vertexCount();
        for (int k = 0; k < nv; ++k) {
            const size_t slot = size_t(mesh::edgeSlot(c, k));
            const int nb = adj.nbr[slot];
            const double phie =
                (nb >= 0) ? 0.5 * (phic + double(phi[size_t(nb)])) : phic; // zero-gradient boundary
            sx += phie * adj.nx[slot] * adj.len[slot];
            sy += phie * adj.ny[slot] * adj.len[slot];
            if (nb >= 0) {
                mn = std::min(mn, double(phi[size_t(nb)]));
                mx = std::max(mx, double(phi[size_t(nb)]));
            }
        }
        const double A = m.area[size_t(c)];
        if (A > 0.0) {
            gx[size_t(c)] = float(sx / A);
            gy[size_t(c)] = float(sy / A);
        }
        lo[size_t(c)] = float(mn);
        hi[size_t(c)] = float(mx);
    }
}

/*! Interpolation state, built once for the whole raster export. */
struct Interpolator {
    Mesh2DInterp method = Mesh2DInterp::GreenGauss;
    MeshSpatialGrid grid;
    mesh::NaturalNeighbourInterpolator nn;
    PtAdaptor adaptor;
    std::unique_ptr<Kd2> kd;
    CellAdjacency adj;
    int k = 8;
    double power = 2.0;
};

/**
 * @brief Build reusable mesh point-location and selected interpolation state.
 * @param[in] m Cell mesh whose centroid arrays outlive the returned IDW adaptor.
 * @param[in] opt Method and IDW parameters; neighbor count is clamped to 1..cellCount().
 * @param[out] in Point-location grid, weights/index machinery, and chosen effective method.
 * @param[in,out] warnings Appended with a natural-neighbor failure message when falling back to IDW.
 */
void buildInterpolator(const CellMesh &m, const Mesh2DExportOptions &opt, Interpolator &in,
                       QStringList &warnings)
{
    const int n = m.cellCount();
    QVector<QRectF> boxes(n);
    for (int c = 0; c < n; ++c) {
        const mesh::MeshTriangle &t = m.cells[size_t(c)];
        const int nv = t.vertexCount();
        double x0 = m.verts[t.vertex(0)].xy.x(), x1 = x0;
        double y0 = m.verts[t.vertex(0)].xy.y(), y1 = y0;
        for (int k = 1; k < nv; ++k) {
            const QPointF &p = m.verts[t.vertex(k)].xy;
            x0 = std::min(x0, p.x());
            x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y());
            y1 = std::max(y1, p.y());
        }
        boxes[c] = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    in.grid.rebuild(boxes);
    in.method = opt.interp;
    in.k = std::max(1, opt.idwNeighbours);
    in.power = opt.idwPower;

    if (in.method == Mesh2DInterp::NaturalNeighbour) {
        QVector<QPointF> seeds(n);
        for (int c = 0; c < n; ++c)
            seeds[c] = QPointF(m.cx[size_t(c)], m.cy[size_t(c)]);
        QString err;
        if (!in.nn.build(seeds, &err)) {
            warnings << QStringLiteral(
                            "Natural neighbour could not triangulate the cell centres (%1), "
                            "so inverse distance weighting was used instead.")
                            .arg(err);
            in.method = Mesh2DInterp::Idw;
        }
    }
    if (in.method == Mesh2DInterp::Idw) {
        in.adaptor = {m.cx.data(), m.cy.data(), size_t(n)};
        in.kd = std::make_unique<Kd2>(2, in.adaptor, nanoflann::KDTreeSingleIndexAdaptorParams(10));
        in.kd->buildIndex();
        in.k = std::min(in.k, n);
    }
    if (in.method == Mesh2DInterp::GreenGauss)
        in.adj = buildAdjacency(m);
}

/*! Per-pixel weights for one band of raster rows, in CSR form.
 *
 *  Every method resolves the containing cell first, so a pixel outside the mesh
 *  (a hole, a concave notch, a bounding-box corner) carries no stencil and is
 *  written as NoData — natural neighbour would otherwise extrapolate happily
 *  across a hole, because its Delaunay hull spans it.
 *
 *  Green–Gauss stores the containing cell plus the offset from its centroid, so
 *  the per-field apply is a gradient evaluation instead of a weighted sum. */
struct Stencil {
    std::vector<int> start; ///< size = pixels + 1
    std::vector<int> cell;
    std::vector<float> w;
    std::vector<float> dx, dy; ///< Green–Gauss only, parallel to `cell`
    std::vector<int> owner;    ///< containing cell per pixel (-1 = outside)
};

/**
 * @brief Find the first grid candidate containing a map point.
 * @param[in] m Cached mesh geometry.
 * @param[in] grid Spatial index built for m.
 * @param[in] x Map X coordinate.
 * @param[in] y Map Y coordinate.
 * @return Native cell ID or -1 outside the mesh, including holes.
 */
int locateCell(const CellMesh &m, const MeshSpatialGrid &grid, double x, double y)
{
    const int *b = nullptr, *e = nullptr;
    grid.candidatesAtPoint(x, y, b, e);
    for (const int *it = b; it != e; ++it) {
        const int c = *it;
        if (c >= 0 && c < m.cellCount() && cellContainsFast(m, c, x, y))
            return c;
    }
    return -1;
}

/**
 * @brief Cache interpolation weights or gradient offsets for a raster row block.
 * @param[in] m Scaled cell geometry.
 * @param[in] g Output raster grid.
 * @param[in,out] in Initialized method/index state; natural-neighbor queries may use mutable internals.
 * @param[in] row0 First zero-based raster row.
 * @param[in] rows Block height within g.h.
 * @param[out] st Replaced CSR pixel stencil and containing-cell IDs; exterior pixels have no weights.
 */
void buildStencil(const CellMesh &m, const RasterGrid &g, Interpolator &in, int row0, int rows,
                  Stencil &st)
{
    const size_t pixels = size_t(rows) * size_t(g.w);
    st.start.assign(pixels + 1, 0);
    st.owner.assign(pixels, -1);
    st.cell.clear();
    st.w.clear();
    st.dx.clear();
    st.dy.clear();
    st.cell.reserve(pixels);
    st.w.reserve(pixels);

    QVector<QPair<int, double>> nnw;
    const size_t k = size_t(std::max(1, in.k));
    std::vector<uint32_t> idx(k);
    std::vector<double> d2(k);

    for (size_t p = 0; p < pixels; ++p) {
        st.start[p] = int(st.cell.size());
        const int j = row0 + int(p / size_t(g.w));
        const int i = int(p % size_t(g.w));
        const double x = g.pixelX(i), y = g.pixelY(j);

        const int owner = locateCell(m, in.grid, x, y);
        st.owner[p] = owner;
        if (owner < 0)
            continue;

        switch (in.method) {
        case Mesh2DInterp::GreenGauss:
            st.cell.push_back(owner);
            st.w.push_back(1.0f);
            st.dx.push_back(float(x - m.cx[size_t(owner)]));
            st.dy.push_back(float(y - m.cy[size_t(owner)]));
            break;

        case Mesh2DInterp::NaturalNeighbour:
            if (in.nn.weightsAt(x, y, nnw) && !nnw.isEmpty()) {
                for (const auto &pr : nnw) {
                    st.cell.push_back(pr.first);
                    st.w.push_back(float(pr.second));
                }
            } else {
                // Inside the mesh but outside the centroid hull (a border
                // pixel): the containing cell's own value is the honest answer.
                st.cell.push_back(owner);
                st.w.push_back(1.0f);
            }
            break;

        case Mesh2DInterp::Idw: {
            const double q[2] = {x, y};
            const size_t found = in.kd->knnSearch(q, k, idx.data(), d2.data());
            size_t exact = found;
            for (size_t t = 0; t < found; ++t)
                if (d2[t] < 1e-24) {
                    exact = t;
                    break;
                }
            if (exact < found) {
                st.cell.push_back(int(idx[exact]));
                st.w.push_back(1.0f);
            } else {
                const size_t base = st.cell.size();
                double sum = 0.0;
                for (size_t t = 0; t < found; ++t) {
                    const double wgt = 1.0 / std::pow(std::sqrt(d2[t]), in.power);
                    st.cell.push_back(int(idx[t]));
                    st.w.push_back(float(wgt));
                    sum += wgt;
                }
                if (sum > 0.0)
                    for (size_t t = base; t < st.w.size(); ++t)
                        st.w[t] = float(double(st.w[t]) / sum);
            }
            break;
        }
        }
    }
    st.start[pixels] = int(st.cell.size());
}

/*! Apply \p st to one field. Pass null gradients to sample the containing
 *  cell's raw value (what the dry mask wants under Green–Gauss).
 * @param[in] st Cached block stencil with cell-aligned indices.
 * @param[in] in Interpolation method used to construct st.
 * @param[in] phi Cell scalar field.
 * @param[in] gx Optional matching X gradients; used only when both gradient pointers exist.
 * @param[in] gy Optional matching Y gradients.
 * @param[in] lo Optional lower bounds, used only together with hi.
 * @param[in] hi Optional upper bounds for clamping reconstruction.
 * @param[out] buf Replaced row-major pixel values, kMesh2DNoData for exterior/empty stencils.
 */
void applyStencil(const Stencil &st, const Interpolator &in, const std::vector<float> &phi,
                  const std::vector<float> *gx, const std::vector<float> *gy,
                  const std::vector<float> *lo, const std::vector<float> *hi,
                  std::vector<float> &buf)
{
    const size_t pixels = st.owner.size();
    buf.assign(pixels, kMesh2DNoData);
    for (size_t p = 0; p < pixels; ++p) {
        if (st.owner[p] < 0)
            continue;
        const int b = st.start[p], e = st.start[p + 1];
        if (b == e)
            continue;

        if (in.method == Mesh2DInterp::GreenGauss) {
            const int c = st.cell[size_t(b)];
            double v = double(phi[size_t(c)]);
            if (gx && gy)
                v += double((*gx)[size_t(c)]) * double(st.dx[size_t(b)]) +
                     double((*gy)[size_t(c)]) * double(st.dy[size_t(b)]);
            if (lo && hi)
                v = std::clamp(v, double((*lo)[size_t(c)]), double((*hi)[size_t(c)]));
            buf[p] = float(v);
        } else {
            double v = 0.0;
            for (int t = b; t < e; ++t)
                v += double(st.w[size_t(t)]) * double(phi[size_t(st.cell[size_t(t)])]);
            buf[p] = float(v);
        }
    }
}

// ---------------------------------------------------------------------------
// Sidecar
// ---------------------------------------------------------------------------

/**
 * @brief Write the selected field/band-to-source-time mapping sidecar.
 * @param[in] path CSV path opened for writing.
 * @param[in] src Non-null borrowed source for timestamps.
 * @param[in] steps Selected zero-based source frames in field order.
 * @param[in] includeMax Append a maximum-field mapping row when true.
 * @param[in] unitFactor Value multiplier recorded as provenance.
 * @param[out] error Open-failure diagnostic.
 * @return False on open failure; true after submitting text without an explicit final stream-error check.
 */
bool writeTimesCsv(const QString &path, IMesh2DSource *src, const std::vector<int> &steps,
                   bool includeMax, double unitFactor, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = QStringLiteral("Could not write \"%1\".").arg(path);
        return false;
    }
    const QString factor = QString::number(unitFactor, 'g', 12);
    QTextStream out(&f);
    out << "field,band,frame_index,datetime_iso,unit_factor_from_m\n";
    for (size_t s = 0; s < steps.size(); ++s) {
        const QDateTime dt = src->simTimeAt(steps[s]);
        out << stepFieldName(int(s)) << ',' << (s + 1) << ',' << steps[s] << ','
            << (dt.isValid() ? dt.toString(Qt::ISODate) : QString()) << ',' << factor << '\n';
    }
    if (includeMax)
        out << kMaxFieldName << ",,,," << factor << '\n';
    return true;
}

// ---------------------------------------------------------------------------
// Raster writer
// ---------------------------------------------------------------------------

/*! One GeoTIFF: `fields.size()` Float32 bands over \p g.
 *
 *  \param depthMask     single depth field masking every band (the max raster)
 *  \param depthFields   per-band depth fields (the time-step raster)
 * @param[in] path GeoTIFF path created or replaced by this write.
 * @param[in] m Cell geometry in model units.
 * @param[in] g Positive-dimension north-up raster grid.
 * @param[in,out] in Reusable interpolation state for m.
 * @param[in] fields Cell-aligned scalar values, one array per output band.
 * @param[in] bandNames Optional descriptions in matching band order.
 * @param[in] dryDepthModel Dry threshold in model length units.
 * @param[in] maskDry Enable comparison against the supplied depth masks.
 * @param[in] srsWkt Optional CRS WKT assigned to the dataset.
 * @param[in] progress Optional synchronous callback between row blocks.
 * @param[in,out] progressDone Work counter incremented per written block.
 * @param[in] progressTotal Callback denominator.
 * @param[out] cancelled Set true on callback cancellation; caller initializes it otherwise.
 * @param[out] error Driver/create/write failure diagnostic.
 * @return True after writing/closing all bands; false closes the handle but leaves path cleanup to the caller.
 */
bool writeRasterFile(const QString &path, const CellMesh &m, const RasterGrid &g, Interpolator &in,
                     const std::vector<std::vector<float>> &fields, const QStringList &bandNames,
                     const std::vector<float> *depthMask,
                     const std::vector<std::vector<float>> *depthFields, double dryDepthModel,
                     bool maskDry, const QString &srsWkt, const Mesh2DExportProgress &progress,
                     int &progressDone, int progressTotal, bool &cancelled, QString &error)
{
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) {
        error = QStringLiteral("This build of GDAL has no GeoTIFF driver.");
        return false;
    }
    char **opts = nullptr;
    opts = CSLSetNameValue(opts, "TILED", "YES");
    opts = CSLSetNameValue(opts, "INTERLEAVE", "BAND");
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    opts = CSLSetNameValue(opts, "BIGTIFF", "IF_SAFER");

    CPLErrorReset();
    GDALDataset *ds =
        drv->Create(path.toUtf8().constData(), g.w, g.h, int(fields.size()), GDT_Float32, opts);
    CSLDestroy(opts);
    if (!ds) {
        error = QStringLiteral("Could not create \"%1\": %2").arg(path, lastGdalError());
        return false;
    }
    double gt[6] = {g.xmin, g.cellSize, 0.0, g.ymax, 0.0, -g.cellSize};
    ds->SetGeoTransform(gt);
    if (!srsWkt.isEmpty())
        ds->SetProjection(srsWkt.toUtf8().constData());
    for (int b = 0; b < int(fields.size()); ++b) {
        GDALRasterBand *band = ds->GetRasterBand(b + 1);
        band->SetNoDataValue(double(kMesh2DNoData));
        if (b < bandNames.size())
            band->SetDescription(bandNames[b].toUtf8().constData());
    }

    // Green–Gauss gradients are per field, computed once for the whole raster.
    const bool gg = in.method == Mesh2DInterp::GreenGauss;
    std::vector<std::vector<float>> gx(fields.size()), gy(fields.size()), lo(fields.size()),
        hi(fields.size());
    if (gg)
        for (size_t b = 0; b < fields.size(); ++b)
            greenGaussGradients(m, in.adj, fields[b], gx[b], gy[b], lo[b], hi[b]);

    // Row bands: cap the CSR stencil at roughly 64 MB, and keep the height a
    // multiple of 256 so tiled writes land on whole tile rows.
    const size_t perRowBytes = size_t(g.w) * 8 * sizeof(int);
    int rowsPerBand =
        int(std::max<size_t>(1, (size_t(64) << 20) / std::max<size_t>(perRowBytes, 1)));
    rowsPerBand = std::max(256, (rowsPerBand / 256) * 256);
    rowsPerBand = std::min(rowsPerBand, g.h);

    Stencil st;
    std::vector<float> buf, maskBuf;
    for (int row0 = 0; row0 < g.h; row0 += rowsPerBand) {
        const int rows = std::min(rowsPerBand, g.h - row0);
        buildStencil(m, g, in, row0, rows, st);

        for (size_t b = 0; b < fields.size(); ++b) {
            applyStencil(st, in, fields[b], gg ? &gx[b] : nullptr, gg ? &gy[b] : nullptr,
                         gg ? &lo[b] : nullptr, gg ? &hi[b] : nullptr, buf);

            if (maskDry) {
                // Mask on the matching depth: the run maximum for a max raster,
                // the same frame for a time-step raster. The mask samples the
                // containing cell's depth with no gradient, so the shoreline
                // follows the solver's own wet/dry cell state.
                const std::vector<float> *dep = depthMask;
                if (!dep && depthFields && b < depthFields->size())
                    dep = &(*depthFields)[b];
                if (dep) {
                    applyStencil(st, in, *dep, nullptr, nullptr, nullptr, nullptr, maskBuf);
                    for (size_t p = 0; p < buf.size() && p < maskBuf.size(); ++p)
                        if (maskBuf[p] == kMesh2DNoData || double(maskBuf[p]) < dryDepthModel)
                            buf[p] = kMesh2DNoData;
                }
            }

            if (ds->GetRasterBand(int(b) + 1)
                    ->RasterIO(GF_Write, 0, row0, g.w, rows, buf.data(), g.w, rows, GDT_Float32, 0,
                               0, nullptr) != CE_None) {
                error = QStringLiteral("Could not write \"%1\": %2").arg(path, lastGdalError());
                GDALClose(ds);
                return false;
            }
        }
        if (progress && !progress(++progressDone, progressTotal,
                                  QStringLiteral("Writing %1…").arg(QFileInfo(path).fileName()))) {
            cancelled = true;
            GDALClose(ds);
            return false;
        }
    }
    GDALClose(ds);
    return true;
}

/*! Remove everything written so far. Shapefiles carry sidecars, so the whole
 *  `<stem>.*` set goes with the `.shp`.
 * @param[in] files Tracked output paths to delete, including known sidecars for Shapefiles.
 * @details Best-effort cleanup; removal failures are ignored and replaced previous files cannot be restored.
 */
void removeOutputs(const QStringList &files)
{
    for (const QString &f : files) {
        const QFileInfo fi(f);
        if (fi.suffix().compare(QLatin1String("shp"), Qt::CaseInsensitive) == 0) {
            const QString stem = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
            for (const char *ext : {"shp", "shx", "dbf", "prj", "cpg", "qix"})
                QFile::remove(stem + QLatin1Char('.') + QLatin1String(ext));
        } else {
            QFile::remove(f);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QString mesh2DVariableKey(Mesh2DVariable variable)
{
    switch (variable) {
    case Mesh2DDepth:
        return QStringLiteral("depth");
    case Mesh2DHead:
        return QStringLiteral("head");
    case Mesh2DVx:
        return QStringLiteral("vx");
    case Mesh2DVy:
        return QStringLiteral("vy");
    case Mesh2DVmag:
        return QStringLiteral("vmag");
    }
    return QStringLiteral("field");
}

QString mesh2DVariableLabel(Mesh2DVariable variable)
{
    switch (variable) {
    case Mesh2DDepth:
        return QStringLiteral("Depth");
    case Mesh2DHead:
        return QStringLiteral("Water surface elevation");
    case Mesh2DVx:
        return QStringLiteral("Velocity X");
    case Mesh2DVy:
        return QStringLiteral("Velocity Y");
    case Mesh2DVmag:
        return QStringLiteral("Speed");
    }
    return QStringLiteral("Field");
}

Mesh2DGridHint mesh2DGridHint(IMesh2DSource *source, double unitFactor)
{
    Mesh2DGridHint hint;
    if (!source)
        return hint;
    CellMesh m;
    if (!buildCellMesh(source, unitFactor, m) || m.cellCount() == 0)
        return hint;

    double xmin = std::numeric_limits<double>::max();
    double ymin = xmin, xmax = -xmin, ymax = -xmin;
    for (const mesh::MeshVertex &v : m.verts) {
        xmin = std::min(xmin, v.xy.x());
        xmax = std::max(xmax, v.xy.x());
        ymin = std::min(ymin, v.xy.y());
        ymax = std::max(ymax, v.xy.y());
    }
    hint.extentWidth = xmax - xmin;
    hint.extentHeight = ymax - ymin;
    hint.suggestedCellSize = medianCellSize(m);
    return hint;
}

bool exportMesh2DResults(const Mesh2DExportInputs &inputs, const Mesh2DExportOptions &options,
                         const Mesh2DExportProgress &progress, Mesh2DExportReport *report)
{
    Mesh2DExportReport local;
    Mesh2DExportReport &rep = report ? *report : local;
    rep.files.clear();
    rep.error.clear();
    rep.warnings.clear();

    auto fail = [&](const QString &why) {
        rep.error = why;
        removeOutputs(rep.files);
        rep.files.clear();
        return false;
    };
    const QString kCancelled = QStringLiteral("Cancelled");

    if (!inputs.source)
        return fail(QStringLiteral("No 2D results source."));
    if (options.basePath.isEmpty())
        return fail(QStringLiteral("No output path."));

    const std::vector<Mesh2DVariable> vars = requestedVariables(options.variables);
    if (vars.empty())
        return fail(QStringLiteral("No variables were selected."));
    if (options.timeSteps.empty() && !options.includeMax)
        return fail(QStringLiteral("No time steps were selected."));

    gdalcaps::ensureRegistered();
    if (!gdalcaps::driverAvailable(driverNameFor(options.format)))
        return fail(QStringLiteral("This build of GDAL has no %1 driver.")
                        .arg(QLatin1String(driverNameFor(options.format))));

    const QFileInfo baseInfo(options.basePath);
    if (!baseInfo.absoluteDir().exists() && !QDir().mkpath(baseInfo.absolutePath()))
        return fail(
            QStringLiteral("Could not create the folder \"%1\".").arg(baseInfo.absolutePath()));

    CellMesh mesh;
    if (!buildCellMesh(inputs.source, inputs.unitFactor, mesh))
        return fail(QStringLiteral("The results carry no usable mesh geometry."));

    const int frames = inputs.source->timeCount();
    for (int t : options.timeSteps)
        if (t < 0 || t >= frames)
            return fail(
                QStringLiteral("Time step %1 is outside the run (%2 steps).").arg(t).arg(frames));
    // A live source may thin its history while the progress callback pumps
    // events; the indices above would then name different frames. The caller
    // pins the history, and this is the check that the pin held.
    const int generation0 = inputs.source->historyGeneration();

    const int stepCount = int(options.timeSteps.size());
    const int total =
        std::max(1, int(vars.size()) * (stepCount + (options.includeMax ? 2 : 0)) + 4);
    int done = 0;
    bool cancelled = false;
    auto tick = [&](const QString &what) { return !progress || progress(++done, total, what); };

    // ---- load every requested field -----------------------------------
    std::vector<VariableData> data;
    data.reserve(vars.size());
    std::vector<std::vector<float>> depthSteps; // raster dry mask
    std::vector<float> depthMax;

    for (Mesh2DVariable var : vars) {
        const QString lower = mesh2DVariableLabel(var).toLower();
        VariableData vd;
        vd.var = var;
        vd.steps.resize(size_t(stepCount));
        for (int s = 0; s < stepCount; ++s) {
            if (!loadFrame(inputs.source, mesh, var, options.timeSteps[size_t(s)],
                           inputs.unitFactor, vd.steps[size_t(s)]))
                return fail(QStringLiteral("Could not read %1 at time step %2.")
                                .arg(lower)
                                .arg(options.timeSteps[size_t(s)]));
            if (!tick(QStringLiteral("Reading %1…").arg(lower)))
                return fail(kCancelled);
        }
        if (options.includeMax && (unsigned(var) & kMesh2DMaxVariables)) {
            if (!loadMax(inputs.source, mesh, var, inputs.unitFactor, progress, cancelled, vd.max))
                return fail(cancelled
                                ? kCancelled
                                : QStringLiteral("Could not read the maximum %1.").arg(lower));
            vd.hasMax = true;
            if (!tick(QStringLiteral("Reading the maximum %1…").arg(lower)))
                return fail(kCancelled);
        }
        if (var == Mesh2DDepth) {
            depthSteps = vd.steps;
            if (vd.hasMax)
                depthMax = vd.max;
        }
        data.push_back(std::move(vd));
    }

    // The dry mask needs depth even when depth itself was not selected.
    const bool needMask = options.format == Mesh2DExportFormat::GeoTiff && options.maskDry;
    if (needMask && depthSteps.empty() && stepCount > 0) {
        depthSteps.resize(size_t(stepCount));
        for (int s = 0; s < stepCount; ++s)
            if (!loadFrame(inputs.source, mesh, Mesh2DDepth, options.timeSteps[size_t(s)],
                           inputs.unitFactor, depthSteps[size_t(s)]))
                return fail(QStringLiteral("Could not read the depth needed for the dry mask."));
    }
    if (needMask && depthMax.empty() && options.includeMax) {
        bool c2 = false;
        if (!loadMax(inputs.source, mesh, Mesh2DDepth, inputs.unitFactor, progress, c2, depthMax) &&
            c2)
            return fail(kCancelled);
    }
    if (inputs.source->historyGeneration() != generation0)
        return fail(QStringLiteral("The live history was thinned while it was being "
                                   "read — export again."));

    const double dryDepthModel = inputs.dryDepthM * inputs.unitFactor;
    const QString base = options.basePath;
    QString err;

    // ---- write ---------------------------------------------------------
    if (options.format == Mesh2DExportFormat::GeoTiff) {
        // The mesh is already built here, so take the default straight from it
        // rather than reading the whole geometry a second time.
        const double cs = options.cellSize > 0.0 ? options.cellSize : medianCellSize(mesh);
        const RasterGrid grid = makeGrid(mesh, cs);
        if (grid.w == 0 || grid.h == 0)
            return fail(QStringLiteral("The raster grid is empty — check the cell size."));

        Interpolator interp;
        buildInterpolator(mesh, options, interp, rep.warnings);
        if (!tick(QStringLiteral("Preparing the interpolation…")))
            return fail(kCancelled);

        for (const VariableData &vd : data) {
            const QString key = mesh2DVariableKey(vd.var);
            if (stepCount > 0) {
                QStringList bandNames;
                for (int s = 0; s < stepCount; ++s) {
                    const QDateTime dt = inputs.source->simTimeAt(options.timeSteps[size_t(s)]);
                    bandNames << (dt.isValid() ? dt.toString(Qt::ISODate) : stepFieldName(s));
                }
                const QString path = QStringLiteral("%1_%2.tif").arg(base, key);
                rep.files << path;
                if (!writeRasterFile(path, mesh, grid, interp, vd.steps, bandNames, nullptr,
                                     needMask ? &depthSteps : nullptr, dryDepthModel,
                                     options.maskDry, inputs.srsWkt, progress, done, total,
                                     cancelled, err))
                    return fail(cancelled ? kCancelled : err);
            }
            if (vd.hasMax) {
                const QString path = QStringLiteral("%1_%2_max.tif").arg(base, key);
                rep.files << path;
                const std::vector<std::vector<float>> one{vd.max};
                if (!writeRasterFile(path, mesh, grid, interp, one,
                                     QStringList{QStringLiteral("max")},
                                     (needMask && !depthMax.empty()) ? &depthMax : nullptr, nullptr,
                                     dryDepthModel, options.maskDry, inputs.srsWkt, progress, done,
                                     total, cancelled, err))
                    return fail(cancelled ? kCancelled : err);
            }
        }
    } else {
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName(driverNameFor(options.format));
        if (!drv)
            return fail(QStringLiteral("This build of GDAL has no %1 driver.")
                            .arg(QLatin1String(driverNameFor(options.format))));
        OGRSpatialReference *srs = srsFromWkt(inputs.srsWkt);
        auto releaseSrs = [&] {
            if (srs)
                srs->Release();
            srs = nullptr;
        };

        if (options.format == Mesh2DExportFormat::GeoPackage) {
            const QString path = base + QStringLiteral(".gpkg");
            QFile::remove(path);
            CPLErrorReset();
            GDALDataset *ds = drv->Create(path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
            if (!ds) {
                releaseSrs();
                return fail(
                    QStringLiteral("Could not create \"%1\": %2").arg(path, lastGdalError()));
            }
            rep.files << path;
            for (const VariableData &vd : data) {
                const QString key = mesh2DVariableKey(vd.var);
                if (!writeVectorLayer(ds, key, mesh, vd, srs, err)) {
                    GDALClose(ds);
                    releaseSrs();
                    return fail(err);
                }
                if (!tick(QStringLiteral("Writing %1…").arg(key))) {
                    GDALClose(ds);
                    releaseSrs();
                    return fail(kCancelled);
                }
            }
            GDALClose(ds);
        } else {
            for (const VariableData &vd : data) {
                const QString key = mesh2DVariableKey(vd.var);
                const QString path = QStringLiteral("%1_%2.shp").arg(base, key);
                QFile::remove(path);
                CPLErrorReset();
                GDALDataset *ds =
                    drv->Create(path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
                if (!ds) {
                    releaseSrs();
                    return fail(
                        QStringLiteral("Could not create \"%1\": %2").arg(path, lastGdalError()));
                }
                rep.files << path;
                const bool ok = writeVectorLayer(ds, key, mesh, vd, srs, err);
                GDALClose(ds);
                if (!ok) {
                    releaseSrs();
                    return fail(err);
                }
                if (!tick(QStringLiteral("Writing %1…").arg(key))) {
                    releaseSrs();
                    return fail(kCancelled);
                }
            }
        }
        releaseSrs();
    }

    const QString csv = base + QStringLiteral("_times.csv");
    if (!writeTimesCsv(csv, inputs.source, options.timeSteps, options.includeMax, inputs.unitFactor,
                       err))
        return fail(err);
    rep.files << csv;

    if (progress)
        progress(total, total, QStringLiteral("Done"));
    return true;
}

} // namespace openswmmvis::io
