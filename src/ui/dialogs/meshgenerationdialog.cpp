/*!
 * \file   meshgenerationdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/meshgenerationdialog.h"

#include "core/unitsystem.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisvectorlayer.h"

#include "mesh/meshgenerator.h"
#include "mesh/meshresult.h"
#include "mesh/dtmthinner.h"
#include "mesh/inpmeshwriter.h"

#include <openswmm/engine/openswmm_nodes.h>

#include <gdal_priv.h>
#include <ogr_feature.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QtConcurrent/QtConcurrent>

#include <QApplication>
#include <QButtonGroup>
#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// PSLG geometry utilities
// ---------------------------------------------------------------------------

namespace {

// Ramer-Douglas-Peucker: recursive step on pts[start..end] (inclusive).
// Marks keep[i] = true for any point that deviates > eps² from the chord.
void rdpStep(const QVector<QPointF> &pts, int start, int end,
             double eps2, QVector<bool> &keep)
{
    if (end <= start + 1) return;
    const QPointF &a = pts[start], &b = pts[end];
    const double dx = b.x()-a.x(), dy = b.y()-a.y();
    const double len2 = dx*dx + dy*dy;
    double maxD2 = 0; int maxI = start;
    for (int i = start+1; i < end; ++i)
    {
        double d2;
        if (len2 < 1e-20) {
            const double ex = pts[i].x()-a.x(), ey = pts[i].y()-a.y();
            d2 = ex*ex + ey*ey;
        } else {
            const double t = ((pts[i].x()-a.x())*dx + (pts[i].y()-a.y())*dy) / len2;
            const double px = a.x()+t*dx - pts[i].x();
            const double py = a.y()+t*dy - pts[i].y();
            d2 = px*px + py*py;
        }
        if (d2 > maxD2) { maxD2 = d2; maxI = i; }
    }
    if (maxD2 > eps2) {
        keep[maxI] = true;
        rdpStep(pts, start, maxI, eps2, keep);
        rdpStep(pts, maxI, end,   eps2, keep);
    }
}

// Simplify an open polyline with RDP.  First and last points are always kept.
// Returns pts unchanged when epsilon <= 0 or pts.size() <= 2.
QVector<QPointF> simplifyPolyline(const QVector<QPointF> &pts, double epsilon)
{
    if (epsilon <= 0.0 || pts.size() <= 2) return pts;
    const double eps2 = epsilon * epsilon;
    QVector<bool> keep(pts.size(), false);
    keep.first() = keep.last() = true;
    rdpStep(pts, 0, pts.size()-1, eps2, keep);
    QVector<QPointF> out;
    out.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        if (keep[i]) out.append(pts[i]);
    return out;
}

// Simplify a closed polygon ring with RDP.
// If the ring is closed (first == last), the closing vertex is stripped
// before simplification and re-added at the end so the polygon stays closed.
QVector<QPointF> simplifyRing(const QVector<QPointF> &ring, double epsilon)
{
    if (epsilon <= 0.0 || ring.size() < 4) return ring;
    const bool closed = (ring.first() == ring.last());
    // Work on an open version (strip the repeated closing vertex).
    const QVector<QPointF> open = closed ? ring.mid(0, ring.size()-1) : ring;
    if (open.size() < 3) return ring;
    // Treat the ring as a polyline from open[0] back to open[0].
    // Run RDP on the open sequence — endpoints are always kept (open[0]).
    const double eps2 = epsilon * epsilon;
    QVector<bool> keep(open.size(), false);
    // Always keep both endpoints of the open sequence so the ring
    // remains geometrically correct after re-closing.
    keep.first() = true;
    keep.last()  = true;
    rdpStep(open, 0, open.size()-1, eps2, keep);
    // Collect kept vertices.
    QVector<QPointF> simplified;
    simplified.reserve(open.size());
    for (int i = 0; i < open.size(); ++i)
        if (keep[i]) simplified.append(open[i]);
    if (simplified.size() < 3) return ring;  // degenerate — return original
    if (closed) simplified.append(simplified.first());  // re-close
    return simplified;
}

// Snap near-coincident Steiner points across all sources to the same grid
// cell (quantized at 1/snapEps) and de-duplicate.  Only untagged points
// (marker == 0) are candidates for merging — tagged points (SWMM nodes)
// are always kept because they carry coupling identity.
void snapAndDedupe(QVector<mesh::SteinerPoint> &pts, double snapEps)
{
    if (snapEps <= 0.0 || pts.isEmpty()) return;
    const double inv = 1.0 / snapEps;
    // Two-pass: build a set of occupied cells, mark duplicates.
    QSet<QPair<qint64,qint64>> occupied;
    occupied.reserve(pts.size());
    QVector<bool> drop(pts.size(), false);
    for (int i = 0; i < pts.size(); ++i)
    {
        const auto &p = pts[i];
        if (p.marker != 0) continue;  // tagged — always keep
        const auto key = qMakePair(
            static_cast<qint64>(std::round(p.xy.x() * inv)),
            static_cast<qint64>(std::round(p.xy.y() * inv)));
        if (occupied.contains(key))
            drop[i] = true;
        else
            occupied.insert(key);
    }
    // Remove dropped entries (iterate backwards to preserve indices).
    for (int i = pts.size()-1; i >= 0; --i)
        if (drop[i]) pts.removeAt(i);
}

} // namespace

// ---------------------------------------------------------------------------
// Worker function — runs on QtConcurrent thread, NO widget access allowed.
// ---------------------------------------------------------------------------

static void
runMeshPipeline(QPromise<MeshGenerationDialog::PipelineResult> &promise,
                MeshGenerationDialog::PipelineInputs            in)
{
    using PResult = MeshGenerationDialog::PipelineResult;

    // Adds a failure result and marks the promise done.
    // Callers `return` immediately after calling this.
    auto fail = [&](const QString &msg) {
        PResult r; r.ok = false; r.errorMsg = msg;
        promise.addResult(r);
    };

    promise.setProgressRange(0, 100);

    auto progress = [&](int pct, const QString &msg) {
        promise.setProgressValueAndText(pct, msg);
    };

    // ── Domain + holes ──────────────────────────────────────────────
    progress(5, QObject::tr("Building input PSLG…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    mesh::MeshGenerator g;
    g.setDomains(in.domains);

    // Interior rings → constraint segments (hole boundary edges) +
    // centroid seed point that tells Triangle to leave the region unmeshed.
    for (const QVector<QPointF> &ring : std::as_const(in.holeRings))
    {
        if (ring.size() < 3) continue;
        mesh::ConstraintSegment cs;
        cs.path   = ring;
        cs.marker = 0;
        g.addConstraintSegment(cs);

        const int n = ring.size() - (ring.first() == ring.last() ? 1 : 0);
        if (n <= 0) continue;
        double cx = 0.0, cy = 0.0;
        for (int i = 0; i < n; ++i) { cx += ring[i].x(); cy += ring[i].y(); }
        g.addHole(QPointF(cx / n, cy / n));
    }

    for (const auto &cs : std::as_const(in.constraintSegs))
        g.addConstraintSegment(cs);
    for (const auto &rm : std::as_const(in.regionMarkers))
        g.addRegion(rm);
    g.setOptions(in.genOpts);

    // ── DTM (optional) — open once, shared for all elevation sampling ──
    // The DEM drives three steps: feature z-interpolation, terrain
    // thinning / grid sampling, and post-Triangle vertex elevation fill.
    // A single DTMThinner instance covers all three so the file is only
    // opened once and the same bilinear sampler is used throughout.
    //
    // When no DTM is provided, vertex z is filled by inverse-distance
    // interpolation from junction rim elevations carried on the SWMM
    // node Steiner points (invert + maxDepth, set in collectInputs).
    const bool useDTM = !in.dtmPath.isEmpty();

    mesh::DTMThinner thinner;
    OGRCoordinateTransformation *meshToDTM = nullptr;
    OGRCoordinateTransformation *dtmToMesh = nullptr;

    if (useDTM)
    {
        progress(20, QObject::tr("Opening DTM raster…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

        if (!thinner.open(in.dtmPath))
        {
            fail(QObject::tr("DTM open failed: %1").arg(thinner.errorMsg()));
            return;
        }

        // CRS transforms shared by all DTM sampling below.
        // GDAL 3 changed the default axis order for geographic CRSs to the
        // ISO/OGC standard (lat-first for EPSG:4326).  Force traditional
        // GIS order (x=east/lon, y=north/lat) on every SRS object we create
        // so that coordinate transforms and IsSame() behave consistently.
        const QString dtmCRSWkt = thinner.crsWkt();
        qDebug() << "[CRS] meshCRSWkt empty:" << in.meshCRSWkt.isEmpty()
                 << "| dtmCRSWkt empty:" << dtmCRSWkt.isEmpty();
        if (!in.meshCRSWkt.isEmpty() && !dtmCRSWkt.isEmpty())
        {
            OGRSpatialReference mSRS, dSRS;
            mSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            dSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (mSRS.importFromWkt(in.meshCRSWkt.toUtf8().constData()) == OGRERR_NONE &&
                dSRS.importFromWkt(dtmCRSWkt.toUtf8().constData())  == OGRERR_NONE)
            {
                const bool same = mSRS.IsSame(&dSRS);
                qDebug() << "[CRS] IsSame:" << same;
                if (!same)
                {
                    meshToDTM = OGRCreateCoordinateTransformation(&mSRS, &dSRS);
                    dtmToMesh = OGRCreateCoordinateTransformation(&dSRS, &mSRS);
                    qDebug() << "[CRS] transforms created:"
                             << "meshToDTM=" << (meshToDTM?"OK":"FAIL")
                             << "dtmToMesh=" << (dtmToMesh?"OK":"FAIL");
                }
            }
        }
    }
    else
    {
        progress(20, QObject::tr("Using junction rim elevations (no DTM selected)…"));
    }

    // elevCache — keeps exact z values for every point we place as a
    // Steiner vertex.  Keyed by quantised mesh-CRS (x,y) at 1e7 precision.
    // The post-Triangle elevation loop consults this first so those vertices
    // are never re-sampled.
    QHash<QPair<qint64,qint64>, double> elevCache;

    // Flat seed arrays for IDW fall-back when no DTM is supplied. Populated
    // alongside elevCache below — only steiner points with known z become
    // seeds.
    QVector<QPointF> seedXY;
    QVector<double>  seedZ;

    // ── Step 1: feature Steiner points — assign z from DEM or model ──
    // SWMM nodes, conduit vertices, aux-layer points, etc.  Their (x,y)
    // is already in mesh CRS; we transform to DTM CRS to sample, then
    // store back in mesh CRS.  When no DTM is selected we keep whatever
    // z the input already carries (junctions arrive with rim z set).
    progress(25, useDTM
                 ? QObject::tr("Interpolating feature elevations from DTM…")
                 : QObject::tr("Reading junction rim elevations…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    for (const auto &sp0 : std::as_const(in.steinerPoints))
    {
        mesh::SteinerPoint sp = sp0;
        if (!sp.hasZ && useDTM)
        {
            double sx = sp.xy.x(), sy = sp.xy.y();
            if (meshToDTM) meshToDTM->Transform(1, &sx, &sy);
            const double z = thinner.sampleAt(sx, sy);
            if (std::isfinite(z)) { sp.z = z; sp.hasZ = true; }
        }
        if (sp.hasZ)
        {
            elevCache.insert(
                qMakePair(qRound64(sp.xy.x() * 1e7), qRound64(sp.xy.y() * 1e7)), sp.z);
            seedXY.append(sp.xy);
            seedZ .append(sp.z);
        }
        g.addSteinerPoint(sp);
    }

    // ── Step 2: DTM terrain points — thinning or full-grid sampling ──
    // Domain bounding box in mesh CRS → transform corners to DTM CRS.
    // Skipped entirely when no DTM is supplied; in that mode the only
    // vertices added to the PSLG come from steiner features + Triangle's
    // own refinement, and z is filled later via IDW from seedXY/seedZ.
    double bx0 = std::numeric_limits<double>::max(),  by0 = bx0;
    double bx1 = std::numeric_limits<double>::lowest(), by1 = bx1;
    for (const auto &dom : std::as_const(in.domains))
        for (const auto &p : dom)
        {
            if (p.x() < bx0) bx0 = p.x(); if (p.x() > bx1) bx1 = p.x();
            if (p.y() < by0) by0 = p.y(); if (p.y() > by1) by1 = p.y();
        }

    qDebug() << "[Mesh] domain bbox (mesh CRS):"
             << bx0 << by0 << "--" << bx1 << by1;
    if (useDTM)
        qDebug() << "[Mesh] DTM pixelSize:" << thinner.pixelSize()
                 << "| CRS wkt present:" << !thinner.crsWkt().isEmpty()
                 << "| meshToDTM:" << (meshToDTM ? "YES" : "NO");

    if (useDTM && bx0 < bx1 && by0 < by1)
    {
        double dx0 = bx0, dy0 = by0, dx1 = bx1, dy1 = by1;
        if (meshToDTM)
        {
            double xs[4] = {bx0, bx1, bx0, bx1};
            double ys[4] = {by0, by0, by1, by1};
            meshToDTM->Transform(4, xs, ys);
            dx0 = *std::min_element(xs, xs+4); dx1 = *std::max_element(xs, xs+4);
            dy0 = *std::min_element(ys, ys+4); dy1 = *std::max_element(ys, ys+4);
        }

        qDebug() << "[Mesh] DTM bbox (DTM CRS):" << dx0 << dy0 << "--" << dx1 << dy1;

        const double gStep = (in.thinnerOpts.gridSpacing > 0.0)
                              ? in.thinnerOpts.gridSpacing
                              : thinner.pixelSize();
        if (gStep <= 0.0)
        {
            if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
            if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
            fail(QObject::tr("DTM pixel size is invalid (%1).").arg(gStep));
            return;
        }

        qDebug() << "[Mesh] gStep:" << gStep << "| doThinning:" << in.doThinning;

        // Probe the DTM at the centre of the domain to verify sampleAt works.
        {
            const double cx = (dx0 + dx1) * 0.5, cy = (dy0 + dy1) * 0.5;
            const double zc = thinner.sampleAt(cx, cy);
            qDebug() << "[Mesh] centre probe at (" << cx << "," << cy
                     << ") -> z =" << zc
                     << (std::isfinite(zc) ? "(OK)" : "(NaN — DTM may not cover domain)");
        }

        QVector<QPointF> candidates;
        QVector<double>  candidateZ;

        if (in.doThinning)
        {
            progress(30, QObject::tr("Terrain-adaptive thinning…"));
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

            const MapExtent dtmBbox(dx0, dy0, dx1, dy1);
            candidates = thinner.generatePoints(dtmBbox, in.thinnerOpts, &candidateZ);
            qDebug() << "[Mesh] thinning retained" << candidates.size() << "points";
            progress(36, QObject::tr("Thinning retained %1 terrain points…")
                         .arg(candidates.size()));
        }
        else
        {
            // No thinning: read every raster pixel that overlaps the domain bbox
            // in a single bulk RasterIO call.  Pixel centres are returned in
            // DTM CRS; the common loop below transforms them to mesh CRS.
            progress(30, QObject::tr("Sampling DTM terrain points…"));
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

            const MapExtent dtmBbox(dx0, dy0, dx1, dy1);
            thinner.readPixels(dtmBbox, candidates, candidateZ);

            qDebug() << "[Mesh] bulk readPixels returned" << candidates.size() << "points";
            progress(36, QObject::tr("Sampled %1 DTM grid points…")
                         .arg(candidates.size()));
        }

        // Reproject all candidates from DTM CRS → mesh CRS.
        QVector<QPointF> candidatesMesh;
        candidatesMesh.reserve(candidates.size());
        for (int ci = 0; ci < candidates.size(); ++ci)
        {
            double cx = candidates[ci].x(), cy = candidates[ci].y();
            if (dtmToMesh) dtmToMesh->Transform(1, &cx, &cy);
            candidatesMesh.append(QPointF(cx, cy));
        }

        // ── Option A: Poisson-disk minimum spacing filter (mesh CRS) ────────
        // Iterates candidates in order; accepts a point only if no already-
        // accepted point is within minSpacing.  Uses a spatial hash grid of
        // cell size = minSpacing for O(N) average complexity.
        if (in.thinnerOpts.useMinSpacing)
        {
            const double spacing = (in.thinnerOpts.minSpacing > 0.0)
                                   ? in.thinnerOpts.minSpacing
                                   : gStep * 2.0;
            const double spacing2  = spacing * spacing;
            const double invCell   = 1.0 / spacing;

            using CellKey = QPair<qint32, qint32>;
            QHash<CellKey, QVector<int>> cellMap;
            cellMap.reserve(candidatesMesh.size());

            QVector<bool> kept(candidatesMesh.size(), false);
            for (int ci = 0; ci < candidatesMesh.size(); ++ci)
            {
                const double px = candidatesMesh[ci].x();
                const double py = candidatesMesh[ci].y();
                const qint32 cx = qint32(std::floor(px * invCell));
                const qint32 cy = qint32(std::floor(py * invCell));

                bool tooClose = false;
                for (qint32 dy = -1; dy <= 1 && !tooClose; ++dy)
                    for (qint32 dx = -1; dx <= 1 && !tooClose; ++dx)
                    {
                        auto it = cellMap.constFind(qMakePair(cx+dx, cy+dy));
                        if (it == cellMap.constEnd()) continue;
                        for (const int j : *it)
                        {
                            const double ddx = candidatesMesh[j].x() - px;
                            const double ddy = candidatesMesh[j].y() - py;
                            if (ddx*ddx + ddy*ddy < spacing2) { tooClose = true; break; }
                        }
                    }

                if (!tooClose)
                {
                    kept[ci] = true;
                    cellMap[qMakePair(cx, cy)].append(ci);
                }
            }

            // Compact: remove rejected candidates.
            QVector<QPointF> filtMesh;
            QVector<double>  filtZ;
            filtMesh.reserve(candidatesMesh.size());
            filtZ.reserve(candidatesMesh.size());
            for (int ci = 0; ci < candidatesMesh.size(); ++ci)
                if (kept[ci]) { filtMesh.append(candidatesMesh[ci]); filtZ.append(candidateZ[ci]); }

            qDebug() << "[Mesh] Poisson-disk:" << candidatesMesh.size()
                     << "->" << filtMesh.size() << "points (spacing" << spacing << ")";
            progress(37, QObject::tr("Poisson-disk filter: %1 → %2 points…")
                         .arg(candidatesMesh.size()).arg(filtMesh.size()));

            candidatesMesh = std::move(filtMesh);
            candidateZ     = std::move(filtZ);
        }

        // Add all surviving terrain candidates as PSLG Steiner vertices.
        elevCache.reserve(elevCache.size() + candidatesMesh.size());
        for (int ci = 0; ci < candidatesMesh.size(); ++ci)
        {
            const double cx = candidatesMesh[ci].x(), cy = candidatesMesh[ci].y();
            mesh::SteinerPoint sp;
            sp.xy   = QPointF(cx, cy);
            sp.z    = candidateZ[ci];
            sp.hasZ = true;
            g.addSteinerPoint(sp);
            elevCache.insert(qMakePair(qRound64(cx * 1e7), qRound64(cy * 1e7)),
                             candidateZ[ci]);
        }

        progress(38, QObject::tr("%1 DTM Steiner points added to PSLG")
                     .arg(candidatesMesh.size()));
    }
    else if (useDTM)
    {
        qDebug() << "[Mesh] domain bbox invalid — skipping DTM sampling";
    }

    // ── Run Triangle ────────────────────────────────────────────────
    progress(40, QObject::tr("Running Triangle…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    mesh::MeshResult result = g.generate();
    if (!result.ok)
    {
        if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
        if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
        fail(QObject::tr("Triangle: %1").arg(result.errorMsg)); return;
    }

    // ── Elevation fill for all mesh vertices ─────────────────────────
    // Vertices that were PSLG Steiner points (features + terrain) already
    // have their exact z in elevCache.  Only Triangle-inserted refinement
    // vertices need a fresh value — either by DTM sample (preferred) or
    // by inverse-distance interpolation from the seed points.
    progress(70, useDTM
                 ? QObject::tr("Sampling DTM elevations…")
                 : QObject::tr("Interpolating elevations from junction rims…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    if (useDTM)
    {
        const int nv = result.vertices.size();
        for (int i = 0; i < nv; ++i)
        {
            const double vx = result.vertices[i].xy.x();
            const double vy = result.vertices[i].xy.y();

            const auto key = qMakePair(qRound64(vx * 1e7), qRound64(vy * 1e7));
            const auto it  = elevCache.constFind(key);
            if (it != elevCache.constEnd())
            {
                result.vertices[i].z = it.value();
                continue;
            }

            double x = vx, y = vy;
            if (meshToDTM) meshToDTM->Transform(1, &x, &y);
            result.vertices[i].z = thinner.sampleAt(x, y);

            if ((i & 0x3FFF) == 0 && promise.isCanceled())
            {
                if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
                if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
                fail(QObject::tr("Cancelled.")); return;
            }
        }
    }
    else
    {
        // No DTM: IDW from junction rim seeds. Power=2 (Shepard's method)
        // is the standard default; weights = 1/d^2 give smooth surfaces
        // that exactly honour the seed elevations at the seed locations.
        // Brute-force O(V*S) — adequate for typical SWMM networks
        // (hundreds of junctions, ~10^4 mesh vertices).
        if (seedXY.isEmpty())
        {
            fail(QObject::tr(
                "No DTM and no junctions with rim elevations inside the "
                "meshing domain — cannot interpolate vertex elevations.\n"
                "Either add a DTM or include at least one junction in "
                "the domain."));
            return;
        }
        const int nv = result.vertices.size();
        const int ns = seedXY.size();
        for (int i = 0; i < nv; ++i)
        {
            const double vx = result.vertices[i].xy.x();
            const double vy = result.vertices[i].xy.y();

            const auto key = qMakePair(qRound64(vx * 1e7), qRound64(vy * 1e7));
            const auto it  = elevCache.constFind(key);
            if (it != elevCache.constEnd())
            {
                result.vertices[i].z = it.value();
                continue;
            }

            double wsum = 0.0, zsum = 0.0;
            bool exact = false;
            for (int s = 0; s < ns; ++s)
            {
                const double dx = seedXY[s].x() - vx;
                const double dy = seedXY[s].y() - vy;
                const double d2 = dx*dx + dy*dy;
                if (d2 < 1e-18)
                {
                    result.vertices[i].z = seedZ[s];
                    exact = true;
                    break;
                }
                const double w = 1.0 / d2;   // power-2 IDW
                wsum += w;
                zsum += w * seedZ[s];
            }
            if (!exact)
                result.vertices[i].z = (wsum > 0.0) ? (zsum / wsum) : 0.0;

            if ((i & 0x3FFF) == 0 && promise.isCanceled())
            {
                fail(QObject::tr("Cancelled.")); return;
            }
        }
    }

    if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
    if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);

    // ── Vertical unit conversion ─────────────────────────────────────
    // Convert all DTM-sampled Z values from the raster's native vertical unit
    // to the requested output vertical unit (SWMM model unit or explicit choice).
    if (in.zConversionFactor != 1.0) {
        for (auto &v : result.vertices) {
            if (std::isfinite(v.z))
                v.z *= in.zConversionFactor;
        }
    }

    // ── CouplingMap ──────────────────────────────────────────────────
    mesh::CouplingMap coupling;
    for (int i = 0; i < result.vertices.size(); ++i)
    {
        const QString tag = in.nodeMarkerToTag.value(result.vertices[i].marker);
        if (!tag.isEmpty()) coupling.vertexToNode.insert(i, tag);
    }
    for (int i = 0; i < result.triangles.size(); ++i)
    {
        const QString &tag = result.triangles[i].tag;
        if (!tag.isEmpty() && tag.startsWith(QStringLiteral("subcatch_")))
            coupling.triangleToNode.insert(i, tag.mid(int(qstrlen("subcatch_"))));
    }

    // ── Write ────────────────────────────────────────────────────────
    progress(85, QObject::tr("Writing mesh file…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    QString writeErr;
    if (!mesh::InpMeshWriter::write(in.outputMode, in.inpPath, in.meshOutputPath,
                                     result, coupling, in.manningsN, &writeErr))
    {
        fail(QObject::tr("Write failed: %1").arg(writeErr)); return;
    }

    progress(95, QObject::tr("Done — adding layer…"));

    PResult out;
    out.ok         = true;
    out.meshResult = std::move(result);
    out.coupling   = std::move(coupling);
    out.meshPath   = (in.outputMode == mesh::MeshOutputMode::External)
                         ? in.meshOutputPath : QString();
    out.outputMode = in.outputMode;
    promise.addResult(std::move(out));
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MeshGenerationDialog::MeshGenerationDialog(SWMMVisProjectWindow *pw,
                                           QWidget *parent)
    : QDialog(parent),
      m_pw(pw)
{
    setWindowTitle(tr("Generate 2D Mesh"));
    resize(520, 510);
    buildUi();
    seedDefaults();

    // Keep suffix labels and defaults in sync if the user somehow changes
    // flow units while the dialog is open.
    connect(UnitSystem::instance(), &UnitSystem::unitsChanged,
            this, [this]() { updateUnitDisplay(); });
}

MeshGenerationDialog::~MeshGenerationDialog()
{
    if (m_watcher && m_watcher->isRunning())
        m_watcher->cancel();
}

// ---------------------------------------------------------------------------
// UI build
// ---------------------------------------------------------------------------

void MeshGenerationDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // ── Tab widget ──────────────────────────────────────────────────
    auto *tabs = new QTabWidget(this);

    // ================================================================
    // Tab 1 — Sources
    // "What data feeds in": terrain, GIS layers, SWMM coupling
    // ================================================================
    auto *sourcesPage = new QWidget;
    auto *sourcesVBox = new QVBoxLayout(sourcesPage);
    sourcesVBox->setContentsMargins(8, 8, 8, 8);

    // Sources group
    {
        auto *g = new QGroupBox(tr("Sources"), sourcesPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_dtmCombo = new QComboBox(g);
        m_dtmCombo->setToolTip(tr(
            "DTM raster used to sample vertex elevations and drive\n"
            "terrain-adaptive thinning. Optional — when set to (none)\n"
            "the mesh is generated without terrain Steiner points and\n"
            "vertex z is interpolated (IDW) from SWMM junction rim\n"
            "elevations (invert + maxDepth)."));
        f->addRow(tr("&DTM raster:"), m_dtmCombo);

        // Auto-detected DTM vertical unit (informational)
        m_dtmVertUnitLabel = new QLabel(tr("—"), g);
        m_dtmVertUnitLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_dtmVertUnitLabel->setToolTip(tr("Vertical unit detected from the DTM raster's embedded CRS metadata."));
        f->addRow(tr("DTM vertical unit:"), m_dtmVertUnitLabel);

        // Output mesh vertical CRS
        m_meshVertCRSCombo = new QComboBox(g);
        m_meshVertCRSCombo->setToolTip(tr(
            "Vertical unit for Z values written to the output mesh.\n"
            "Choose to match the SWMM model's unit system so that node invert\n"
            "elevations, crown elevations, and mesh Z values are consistent.\n"
            "'Match flow units' converts automatically from the DTM vertical unit."));
        m_meshVertCRSCombo->addItem(tr("Match flow units (auto-convert)"), QStringLiteral("auto"));
        m_meshVertCRSCombo->addItem(tr("Metres (m)"),                      QStringLiteral("m"));
        m_meshVertCRSCombo->addItem(tr("Feet (ft)"),                       QStringLiteral("ft"));
        f->addRow(tr("Mesh vertical unit:"), m_meshVertCRSCombo);

        // Z conversion factor — auto-populated, user-editable override.
        m_zFactorSpin = new QDoubleSpinBox(g);
        m_zFactorSpin->setRange(0.0001, 10000.0);
        m_zFactorSpin->setDecimals(6);
        m_zFactorSpin->setSingleStep(0.001);
        m_zFactorSpin->setValue(1.0);
        m_zFactorSpin->setToolTip(
            tr("Multiplication factor applied to every raw DTM Z value before\n"
               "it is written to the mesh.\n"
               "Auto-computed from the DTM vertical unit and the chosen mesh\n"
               "vertical unit; override here when the auto-detected value is wrong.\n"
               "MeshZ = DTM_Z \xc3\x97 factor"));
        f->addRow(tr("Z conversion (\xc3\x97):"), m_zFactorSpin);

        // Update detected label, auto-select mesh unit, and recompute factor when DTM changes.
        connect(m_dtmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() {
                    auto *raster = qobject_cast<GISRasterLayer *>(
                        static_cast<QObject *>(m_dtmCombo->currentData().value<void *>()));
                    if (raster) {
                        const QString unit = raster->detectVerticalUnit();
                        m_dtmVertUnitLabel->setText(
                            unit == QLatin1String("ft") ? tr("ft (feet)") : tr("m (metres)"));
                    } else {
                        m_dtmVertUnitLabel->setText(tr("—"));
                    }
                    updateZFactor();
                });

        connect(m_meshVertCRSCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updateZFactor(); });

        m_domainLabel = new QLabel(g);
        m_domainLabel->setStyleSheet(QStringLiteral("color: gray;"));
        f->addRow(tr("Domain:"), m_domainLabel);

        sourcesVBox->addWidget(g);
    }

    // Auxiliary feature layers group
    {
        auto *g   = new QGroupBox(tr("Auxiliary feature layers (optional)"), sourcesPage);
        auto *lay = new QVBoxLayout(g);

        auto *boundaryRow = new QHBoxLayout;
        boundaryRow->addWidget(new QLabel(tr("&Boundary polygon:"), g));
        m_boundaryLayerCombo = new QComboBox(g);
        m_boundaryLayerCombo->setToolTip(tr(
            "Polygon layer whose features define the meshing boundary.  "
            "Interior rings (holes) in those polygons are respected — "
            "Triangle leaves those regions unmeshed.  When (none), the mesh "
            "domain falls back to the SWMM model bounding rectangle + 5%."));
        boundaryRow->addWidget(m_boundaryLayerCombo, 1);
        lay->addLayout(boundaryRow);

        lay->addWidget(new QLabel(tr("Constraining &points (check to include):"), g));
        m_pointLayersList = new QListWidget(g);
        m_pointLayersList->setToolTip(tr("Every feature in each checked layer is added as a Steiner point."));
        m_pointLayersList->setMaximumHeight(100);
        m_pointLayersList->setSelectionMode(QAbstractItemView::NoSelection);
        lay->addWidget(m_pointLayersList);

        lay->addWidget(new QLabel(tr("Constraining &lines (check to include):"), g));
        m_lineLayersList = new QListWidget(g);
        m_lineLayersList->setToolTip(tr("Every feature in each checked layer becomes a constraint segment."));
        m_lineLayersList->setMaximumHeight(100);
        m_lineLayersList->setSelectionMode(QAbstractItemView::NoSelection);
        lay->addWidget(m_lineLayersList);

        sourcesVBox->addWidget(g);
    }

    // 1D–2D coupling group
    {
        auto *g   = new QGroupBox(tr("1D – 2D Coupling (SWMM objects)"), sourcesPage);
        auto *lay = new QVBoxLayout(g);

        m_includeJunctions = new QCheckBox(tr("Junctions / outfalls / storage  →  Steiner vertices  (tag = node id)"), g);
        m_includeConduits  = new QCheckBox(tr("Conduits  →  constraint segments  (marker = conduit id)"), g);
        m_includeSubcatch  = new QCheckBox(tr("Subcatchments  →  triangle regions  (tag = subcatchment id)"), g);

        lay->addWidget(m_includeJunctions);
        lay->addWidget(m_includeConduits);
        lay->addWidget(m_includeSubcatch);

        sourcesVBox->addWidget(g);
    }

    sourcesVBox->addStretch();
    tabs->addTab(sourcesPage, tr("Sources"));

    // ================================================================
    // Tab 2 — Quality
    // "How to triangulate": Triangle knobs, PSLG opts, terrain thinning
    // ================================================================
    auto *qualityPage = new QWidget;
    auto *qualityVBox = new QVBoxLayout(qualityPage);
    qualityVBox->setContentsMargins(8, 8, 8, 8);

    // Triangle quality group
    {
        auto *g = new QGroupBox(tr("Triangle quality"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_maxAreaSpin = new QDoubleSpinBox(g);
        m_maxAreaSpin->setRange(0.0, 1e12);
        m_maxAreaSpin->setDecimals(2);
        m_maxAreaSpin->setSpecialValueText(tr("(no cap)"));
        // tooltip updated by updateUnitDisplay()
        f->addRow(tr("Max triangle area:"), m_maxAreaSpin);

        m_minAngleSpin = new QDoubleSpinBox(g);
        m_minAngleSpin->setRange(0.0, 33.0);
        m_minAngleSpin->setDecimals(1);
        m_minAngleSpin->setSuffix(QStringLiteral(" °"));
        m_minAngleSpin->setToolTip(tr("Minimum triangle angle (0–33° reliable; above 33° may not terminate)."));
        f->addRow(tr("Min angle:"), m_minAngleSpin);

        m_maxSteinerSpin = new QSpinBox(g);
        m_maxSteinerSpin->setRange(-1, 10'000'000);
        m_maxSteinerSpin->setSpecialValueText(tr("(unlimited)"));
        f->addRow(tr("Max Steiner points:"), m_maxSteinerSpin);

        m_allowSteiner = new QCheckBox(tr("Allow Steiner refinement on boundary"), g);
        f->addRow(QString(), m_allowSteiner);

        qualityVBox->addWidget(g);
    }

    // PSLG optimisation group
    {
        auto *g = new QGroupBox(tr("PSLG Optimisation"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_simplifyEpsSpin = new QDoubleSpinBox(g);
        m_simplifyEpsSpin->setRange(0.0, 1000.0);
        m_simplifyEpsSpin->setDecimals(3);
        m_simplifyEpsSpin->setSingleStep(0.1);
        // suffix set by updateUnitDisplay()
        m_simplifyEpsSpin->setSpecialValueText(tr("(off)"));
        m_simplifyEpsSpin->setToolTip(tr(
            "Ramer-Douglas-Peucker tolerance applied to every polygon ring and "
            "polyline path before it enters Triangle.\n\n"
            "Points that deviate less than this distance from a straight line "
            "between their neighbours are removed.  0 = disabled.\n"
            "Typical: 0.1 m (tight) – 1.0 m (coarse)."));
        f->addRow(tr("Geometry simplification ε:"), m_simplifyEpsSpin);

        m_snapEpsSpin = new QDoubleSpinBox(g);
        m_snapEpsSpin->setRange(0.0, 100.0);
        m_snapEpsSpin->setDecimals(4);
        m_snapEpsSpin->setSingleStep(0.01);
        // suffix set by updateUnitDisplay()
        m_snapEpsSpin->setSpecialValueText(tr("(off)"));
        m_snapEpsSpin->setToolTip(tr(
            "Radius within which near-coincident untagged Steiner points are "
            "merged into one.  SWMM-tagged points are never merged.\n"
            "0 = disabled.  Typical: 0.01–0.5 m."));
        f->addRow(tr("Steiner snap radius:"), m_snapEpsSpin);

        qualityVBox->addWidget(g);
    }

    // Terrain-adaptive thinning group
    {
        auto *g = new QGroupBox(tr("Terrain-Adaptive Thinning"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_thinningBox = new QCheckBox(tr("Enable normal-deviation terrain simplification"), g);
        m_thinningBox->setToolTip(tr(
            "When checked: uses the normal-deviation algorithm to SELECT which "
            "DTM pixels to keep — only terrain-significant points (ridges, "
            "valleys, slope breaks) become Steiner vertices, flat areas are "
            "thinned away.\n\n"
            "When unchecked: every DTM pixel in the domain at the configured "
            "grid spacing becomes a Steiner vertex (no filtering).\n\n"
            "Either way, the selected DTM points are always added to the PSLG "
            "when a DTM raster is configured.  Each point carries its exact DEM "
            "elevation and is not re-sampled after triangulation."));
        f->addRow(QString(), m_thinningBox);

        m_thinningToleranceSpin = new QDoubleSpinBox(g);
        m_thinningToleranceSpin->setRange(-1.0, 1.0);
        m_thinningToleranceSpin->setDecimals(8);
        m_thinningToleranceSpin->setSingleStep(0.001);
        m_thinningToleranceSpin->setToolTip(tr(
            "Removal threshold: a vertex is removed when the minimum dot "
            "product of its vertex-normal with every surrounding face-normal "
            "is ≥ this value (smooth surface).\n\n"
            "score < threshold → terrain feature (ridge/valley/slope break) "
            "→ vertex is KEPT.\n"
            "score ≥ threshold → flat or uniform-slope area → vertex is "
            "REMOVED.\n\n"
            "0.99 → keep bends > ~8° (fine detail)\n"
            "0.95 → keep bends > ~18° (default — channels, levees, ridges)\n"
            "0.90 → keep bends > ~26° (coarse — prominent breaks only)"));
        f->addRow(tr("Normal dot threshold:"), m_thinningToleranceSpin);

        m_thinningIterationsSpin = new QSpinBox(g);
        m_thinningIterationsSpin->setRange(0, std::numeric_limits<int>::max());
        m_thinningIterationsSpin->setSpecialValueText(tr("(unlimited)"));
        m_thinningIterationsSpin->setToolTip(tr(
            "Number of normal-deviation thinning passes.  "
            "0 (unlimited) runs until no further vertices qualify for removal."));
        f->addRow(tr("Thinning passes:"), m_thinningIterationsSpin);

        m_thinningMaxPointsSpin = new QSpinBox(g);
        m_thinningMaxPointsSpin->setRange(0, 500000);
        m_thinningMaxPointsSpin->setSpecialValueText(tr("(unlimited)"));
        m_thinningMaxPointsSpin->setSingleStep(5000);
        f->addRow(tr("Max thinning points:"), m_thinningMaxPointsSpin);

        // ── Option A: Poisson-disk minimum spacing ─────────────────────────
        m_minSpacingBox = new QCheckBox(tr("Min point spacing (Poisson-disk):"), g);
        m_minSpacingBox->setToolTip(tr(
            "Post-thinning pass: enforces a minimum Euclidean distance between "
            "any two surviving DTM Steiner points.\n\n"
            "Points are accepted greedily in raster order; any candidate closer "
            "than this distance to an already-accepted point is dropped.\n\n"
            "Use to prevent micro-clusters from dominating the mesh even after "
            "normal-deviation thinning.  0 = auto (2 × pixel size)."));
        m_minSpacingSpin = new QDoubleSpinBox(g);
        m_minSpacingSpin->setRange(0.0, 1e9);
        m_minSpacingSpin->setDecimals(3);
        m_minSpacingSpin->setSingleStep(1.0);
        // suffix set by updateUnitDisplay()
        m_minSpacingSpin->setSpecialValueText(tr("(auto)"));
        f->addRow(m_minSpacingBox, m_minSpacingSpin);


        qualityVBox->addWidget(g);
    }

    auto syncThinning = [this]() {
        const bool on = m_thinningBox->isChecked();
        m_thinningToleranceSpin->setEnabled(on);
        m_thinningIterationsSpin->setEnabled(on);
        m_thinningMaxPointsSpin->setEnabled(on);
        m_minSpacingBox->setEnabled(on);
        m_minSpacingSpin->setEnabled(on && m_minSpacingBox->isChecked());
    };
    connect(m_thinningBox,   &QCheckBox::toggled, this, syncThinning);
    connect(m_minSpacingBox, &QCheckBox::toggled, m_minSpacingSpin, &QWidget::setEnabled);
    syncThinning();

    qualityVBox->addStretch();
    tabs->addTab(qualityPage, tr("Quality"));

    // ================================================================
    // Tab 3 — Hydraulics
    // Manning's roughness; will grow when ManningsSampler lands.
    // ================================================================
    auto *hydraulicsPage = new QWidget;
    auto *hydraulicsVBox = new QVBoxLayout(hydraulicsPage);
    hydraulicsVBox->setContentsMargins(8, 8, 8, 8);

    {
        auto *g   = new QGroupBox(tr("Roughness (Manning's n)"), hydraulicsPage);
        g->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        auto *lay = new QVBoxLayout(g);
        auto *roughGroup = new QButtonGroup(g);

        auto *constRow = new QHBoxLayout;
        m_manningsConstant  = new QRadioButton(tr("Constant value:"), g);
        m_manningsValueSpin = new QDoubleSpinBox(g);
        m_manningsValueSpin->setRange(0.001, 1.0);
        m_manningsValueSpin->setDecimals(4);
        m_manningsValueSpin->setSingleStep(0.005);
        constRow->addWidget(m_manningsConstant);
        constRow->addWidget(m_manningsValueSpin);
        constRow->addStretch();
        lay->addLayout(constRow);

        m_manningsCategorical = new QRadioButton(tr("Categorical raster"), g);
        m_manningsCategorical->setEnabled(false);
        m_manningsCategorical->setToolTip(tr("Full support coming in a future release."));

        m_manningsField = new QRadioButton(tr("Shapefile attribute field"), g);
        m_manningsField->setEnabled(false);
        m_manningsField->setToolTip(tr("Full support coming in a future release."));

        roughGroup->addButton(m_manningsConstant);
        roughGroup->addButton(m_manningsCategorical);
        roughGroup->addButton(m_manningsField);

        lay->addWidget(m_manningsCategorical);
        lay->addWidget(m_manningsField);

        hydraulicsVBox->addWidget(g);
    }

    hydraulicsVBox->addStretch();
    tabs->addTab(hydraulicsPage, tr("Hydraulics"));

    outer->addWidget(tabs, 1);

    // ================================================================
    // Footer — Output destination (always visible, outside tabs)
    // ================================================================
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    outer->addWidget(sep);

    auto *outputForm = new QFormLayout;
    outputForm->setContentsMargins(0, 4, 0, 4);
    outputForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *modeRow  = new QHBoxLayout;
    auto *outGroup = new QButtonGroup(this);
    m_outputExternal = new QRadioButton(tr("External .2dm"), this);
    m_outputExternal->setToolTip(tr("Write a standalone .2dm file referenced via [2D_MESH_FILE] in the .inp."));
    m_outputInline   = new QRadioButton(tr("Inline in .inp"), this);
    m_outputInline->setToolTip(tr("Embed the mesh directly inside the SWMM .inp file."));
    outGroup->addButton(m_outputExternal);
    outGroup->addButton(m_outputInline);
    modeRow->addWidget(m_outputExternal);
    modeRow->addWidget(m_outputInline);
    modeRow->addStretch();
    outputForm->addRow(tr("Output:"), modeRow);

    auto *pathRow = new QHBoxLayout;
    m_meshPathEdit  = new QLineEdit(this);
    m_meshPathEdit->setPlaceholderText(tr("(default: <project>.2dm)"));
    m_browseMeshBtn = new QPushButton(tr("Browse…"), this);
    pathRow->addWidget(m_meshPathEdit, 1);
    pathRow->addWidget(m_browseMeshBtn);
    outputForm->addRow(tr("Mesh file:"), pathRow);

    outer->addLayout(outputForm);

    // ── Embedded progress (hidden until generation starts) ──────────
    m_progressLabel = new QLabel(this);
    m_progressLabel->setAlignment(Qt::AlignCenter);
    m_progressLabel->setVisible(false);
    outer->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);
    outer->addWidget(m_progressBar);

    // ── Buttons ─────────────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(this);
    m_generateBtn = bb->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    m_cancelBtn   = bb->addButton(tr("Close"),    QDialogButtonBox::RejectRole);
    outer->addWidget(bb);

    connect(m_browseMeshBtn, &QPushButton::clicked, this, &MeshGenerationDialog::onBrowseMeshPath);
    connect(m_generateBtn,   &QPushButton::clicked, this, &MeshGenerationDialog::onAccept);
    connect(m_cancelBtn,     &QPushButton::clicked, this, &MeshGenerationDialog::onCancelOrReject);

    auto refreshPath = [this]() {
        const bool ext = m_outputExternal->isChecked();
        m_meshPathEdit->setEnabled(ext);
        m_browseMeshBtn->setEnabled(ext);
    };
    connect(m_outputExternal, &QRadioButton::toggled, this, refreshPath);
    connect(m_outputInline,   &QRadioButton::toggled, this, refreshPath);
    refreshPath();
}

// ---------------------------------------------------------------------------
// Defaults + layer combo population
// ---------------------------------------------------------------------------

void MeshGenerationDialog::updateZFactor()
{
    if (!m_zFactorSpin) return;

    // Determine DTM vertical unit from the selected raster.
    auto *raster = qobject_cast<GISRasterLayer *>(
        static_cast<QObject *>(m_dtmCombo ? m_dtmCombo->currentData().value<void *>() : nullptr));
    const QString dtmUnit = raster ? raster->detectVerticalUnit() : QStringLiteral("m");
    const double  dtmToSI = (dtmUnit == QLatin1String("ft")) ? 0.3048 : 1.0;

    // Determine desired output vertical unit.
    const QString outSel = m_meshVertCRSCombo ? m_meshVertCRSCombo->currentData().toString()
                                              : QStringLiteral("auto");
    double outToSI = 1.0;
    if (outSel == QLatin1String("auto")) {
        outToSI = (m_pw && m_pw->unitSystem() && !m_pw->unitSystem()->isSI()) ? 0.3048 : 1.0;
    } else if (outSel == QLatin1String("ft")) {
        outToSI = 0.3048;
    }

    QSignalBlocker b(m_zFactorSpin);
    m_zFactorSpin->setValue(dtmToSI / outToSI);
}

void MeshGenerationDialog::updateUnitDisplay()
{
    const UnitSystem *us  = UnitSystem::instance();
    const QString     len = us->lengthLabel();       // "ft" or "m"
    const QString     len2 = len + QStringLiteral("\xB2"); // "ft²" or "m²"
    const QString     suf  = QStringLiteral(" ") + len;

    if (m_simplifyEpsSpin) m_simplifyEpsSpin->setSuffix(suf);
    if (m_snapEpsSpin)     m_snapEpsSpin->setSuffix(suf);
    if (m_minSpacingSpin)  m_minSpacingSpin->setSuffix(suf);

    if (m_maxAreaSpin)
        m_maxAreaSpin->setToolTip(
            tr("Upper bound on triangle area (%1). 0 = no cap.").arg(len2));
}

void MeshGenerationDialog::seedDefaults()
{
    m_includeJunctions->setChecked(true);
    m_includeConduits->setChecked(true);
    m_includeSubcatch->setChecked(true);
    m_maxAreaSpin->setValue(0.0);
    m_minAngleSpin->setValue(33.0);
    m_maxSteinerSpin->setValue(-1);
    m_allowSteiner->setChecked(true);
    // Scale distance defaults to the project's length unit.
    // SI canonical values: simplifyEps = 0.1 m, snapEps = 0.01 m.
    const double toUnit = UnitSystem::instance()->isSI() ? 1.0 : 1.0 / 0.3048;
    m_simplifyEpsSpin->setValue(0.1  * toUnit);
    m_snapEpsSpin->setValue(    0.01 * toUnit);
    m_thinningBox->setChecked(false);
    m_thinningToleranceSpin->setValue(0.70); // default normal dot threshold
    m_thinningIterationsSpin->setValue(0);
    m_thinningMaxPointsSpin->setValue(0);
    m_minSpacingBox->setChecked(false);
    m_minSpacingSpin->setValue(0.0);
    m_manningsConstant->setChecked(true);
    m_manningsValueSpin->setValue(0.035);
    m_outputExternal->setChecked(true);
    updateUnitDisplay();   // set suffixes and tooltip after values are seeded
    populateLayerCombos();
    updateZFactor();       // seed factor from current DTM + mesh vertical unit

    if (m_pw && m_pw->modelLayer())
    {
        const MapExtent ext = m_pw->modelLayer()->extent();
        m_domainLabel->setText(ext.isValid()
            ? tr("model extent [%1, %2 → %3, %4]")
                  .arg(ext.xMin(),0,'f',2).arg(ext.yMin(),0,'f',2)
                  .arg(ext.xMax(),0,'f',2).arg(ext.yMax(),0,'f',2)
            : tr("(model extent not available)"));

        const QString inp = m_pw->modelLayer()->modelFilePath();
        if (!inp.isEmpty())
        {
            const QFileInfo fi(inp);
            m_meshPathEdit->setText(
                fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".2dm")));
        }
    }
}

void MeshGenerationDialog::populateLayerCombos()
{
    if (!m_pw || !m_pw->canvas()) return;
    const auto &layers = m_pw->canvas()->layers();

    m_dtmCombo->clear();
    // Allow generation without a DTM — elevations fall back to IDW from
    // junction rim elevations (invert + maxDepth on each SWMM node).
    m_dtmCombo->addItem(tr("(none — use junction rim elevations)"),
                         QVariant::fromValue<void *>(nullptr));
    for (auto *L : layers)
        if (auto *r = qobject_cast<GISRasterLayer *>(L))
            m_dtmCombo->addItem(r->name(), QVariant::fromValue<void *>(r));

    // Trigger the DTM-changed connection to update the detected vertical unit label.
    emit m_dtmCombo->currentIndexChanged(m_dtmCombo->currentIndex());

    m_boundaryLayerCombo->clear();
    m_boundaryLayerCombo->addItem(tr("(none)"),
                                   QVariant::fromValue<void *>(nullptr));
    m_boundaryLayerCombo->addItem(tr("Use SWMM subcatchment polygons"),
                                   QVariant::fromValue<void *>(reinterpret_cast<void *>(0x1)));
    for (auto *L : layers)
        if (auto *v = qobject_cast<GISVectorLayer *>(L))
            m_boundaryLayerCombo->addItem(v->name(), QVariant::fromValue<void *>(v));

    auto fillList = [&](QListWidget *list) {
        list->clear();
        for (auto *L : layers)
            if (auto *v = qobject_cast<GISVectorLayer *>(L))
            {
                auto *item = new QListWidgetItem(v->name(), list);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                item->setData(Qt::UserRole, QVariant::fromValue<void *>(v));
            }
        if (list->count() == 0)
            list->addItem(tr("(no vector layers)"));
    };
    fillList(m_pointLayersList);
    fillList(m_lineLayersList);
}

// ---------------------------------------------------------------------------
// Input collection (main thread — reads widgets + SWMMModelLayer)
// ---------------------------------------------------------------------------

bool MeshGenerationDialog::collectInputs(PipelineInputs *out, QString *errOut) const
{
    auto fail = [&](const QString &m) {
        if (errOut) *errOut = m;
        return false;
    };

    if (!m_pw || !m_pw->modelLayer() || !m_pw->modelLayer()->engine())
        return fail(tr("No active SWMM project."));

    SWMMModelLayer *layer = m_pw->modelLayer();
    out->inpPath = layer->modelFilePath();
    if (out->inpPath.isEmpty())
        return fail(tr("Save the project to a .inp file first (File → Save As)."));

    const MapExtent modelExt = layer->extent();
    if (!modelExt.isValid())
        return fail(tr("Model has no spatial extent — add at least one node first."));

    // ── DTM path (optional) ──────────────────────────────────────────
    // When no DTM is selected, vertex elevations are filled by IDW
    // interpolation from SWMM junction rim elevations (invert + maxDepth).
    auto *dtmLayer = static_cast<GISRasterLayer *>(
        m_dtmCombo->currentData().value<void *>());
    out->dtmPath = dtmLayer ? dtmLayer->filePath() : QString();

    // ── PSLG optimisation parameters ─────────────────────────────────
    out->pslgSimplifyEps = m_simplifyEpsSpin->value();
    out->pslgSnapEps     = m_snapEpsSpin->value();

    // ── Mesh CRS — initialised first so every source can reproject to it ──
    // All PSLG inputs (domain polygons, hole rings, constraint segments,
    // Steiner points) must be in the same CRS before Triangle runs.
    // The SWMM model's native CRS is the authoritative mesh CRS; everything
    // else is transformed to it.  The WKT is also forwarded to the worker
    // so it can reproject mesh vertices to the DTM's CRS before sampling.
    OGRSpatialReference meshOGRSRS;
    meshOGRSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    bool meshHasSRS = false;
    if (auto *srs = layer->srs())
        if (auto *ogrSRS = srs->ogrSpatialReference())
        {
            char *wkt = nullptr;
            if (ogrSRS->exportToWkt(&wkt) == OGRERR_NONE)
            {
                out->meshCRSWkt = QString::fromUtf8(wkt);
                meshHasSRS = (meshOGRSRS.importFromWkt(wkt) == OGRERR_NONE);
                if (meshHasSRS)
                    meshOGRSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            }
            CPLFree(wkt);
        }

    // Build an OGRCoordinateTransformation from layerSRS → meshSRS.
    // Returns nullptr when no transform is needed (same CRS or one unknown).
    // Caller owns the returned object and must call DestroyCT().
    auto makeTransform = [&](const OpenSWMMVisLayer *srcLayer)
        -> OGRCoordinateTransformation *
    {
        if (!meshHasSRS || !srcLayer || !srcLayer->srs()) return nullptr;
        OGRSpatialReference *layerOGRSRS = srcLayer->srs()->ogrSpatialReference();
        if (!layerOGRSRS) return nullptr;
        layerOGRSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (meshOGRSRS.IsSame(layerOGRSRS)) return nullptr;
        return OGRCreateCoordinateTransformation(layerOGRSRS, &meshOGRSRS);
    };

    // Transform a mutable (x,y) pair using ct (if non-null).
    auto xformPt = [](OGRCoordinateTransformation *ct, double &x, double &y) {
        if (ct) ct->Transform(1, &x, &y);
    };

    // ── Domain + hole rings ──────────────────────────────────────────
    // Each polygon source may carry a CRS transform (boundary GIS layer
    // in a different projection).  activeCT is set per-source below and
    // read by the pushOgrPolygon lambda so every ring vertex is in mesh CRS
    // before it is appended to out->domains / out->holeRings.
    OGRCoordinateTransformation *activeCT = nullptr;  // set per polygon source

    auto pushOgrPolygon = [&](const OGRPolygon *poly) {
        if (!poly) return;
        const OGRLinearRing *ext = poly->getExteriorRing();
        if (!ext || ext->getNumPoints() < 3) return;

        QVector<QPointF> extPts;
        extPts.reserve(ext->getNumPoints());
        for (int i = 0; i < ext->getNumPoints(); ++i)
        {
            double x = ext->getX(i), y = ext->getY(i);
            xformPt(activeCT, x, y);
            extPts << QPointF(x, y);
        }
        // Simplify exterior ring with RDP before feeding to Triangle.
        // Reduces boundary segment count for dense GIS/survey boundaries.
        out->domains.append(QPolygonF(simplifyRing(extPts, out->pslgSimplifyEps)));

        for (int h = 0; h < poly->getNumInteriorRings(); ++h)
        {
            const OGRLinearRing *hole = poly->getInteriorRing(h);
            if (!hole || hole->getNumPoints() < 3) continue;
            QVector<QPointF> ring;
            ring.reserve(hole->getNumPoints());
            for (int i = 0; i < hole->getNumPoints(); ++i)
            {
                double x = hole->getX(i), y = hole->getY(i);
                xformPt(activeCT, x, y);
                ring << QPointF(x, y);
            }
            out->holeRings.append(simplifyRing(ring, out->pslgSimplifyEps));
        }
    };

    auto walkOgrGeom = [&](const OGRGeometry *geom) {
        if (!geom) return;
        const auto gt = wkbFlatten(geom->getGeometryType());
        if (gt == wkbPolygon)
            pushOgrPolygon(geom->toPolygon());
        else if (gt == wkbMultiPolygon)
        {
            const auto *mp = geom->toMultiPolygon();
            for (int i = 0; i < mp->getNumGeometries(); ++i)
                pushOgrPolygon(mp->getGeometryRef(i)->toPolygon());
        }
    };

    void *boundaryPtr = m_boundaryLayerCombo->currentData().value<void *>();
    void * const kSubcatch = reinterpret_cast<void *>(0x1);

    if (boundaryPtr == kSubcatch)
    {
        // Subcatchment polygons are in the model's native CRS (= mesh CRS);
        // activeCT stays nullptr.
        QVector<QVector<QPointF>> rawPolys;
        OGRMultiPolygon mp;
        for (int i = 0; i < layer->cachedSubcatchCount(); ++i)
        {
            auto verts = layer->cachedSubcatchVertices(i);
            if (verts.size() < 3) continue;
            rawPolys.append(verts);
            OGRPolygon poly;
            OGRLinearRing ring;
            for (const QPointF &p : verts) ring.addPoint(p.x(), p.y());
            if (verts.first() != verts.last())
                ring.addPoint(verts.first().x(), verts.first().y());
            poly.addRing(&ring);
            mp.addGeometry(&poly);
        }
        if (rawPolys.isEmpty())
            return fail(tr("No subcatchment polygons found in the model."));

        OGRGeometry *unioned = mp.UnaryUnion();
        if (unioned)
        {
            walkOgrGeom(unioned);
            OGRGeometryFactory::destroyGeometry(unioned);
        }
        if (out->domains.isEmpty())
            for (const auto &v : rawPolys)
                out->domains.append(QPolygonF(v));
    }
    else if (auto *bLayer = static_cast<GISVectorLayer *>(boundaryPtr))
    {
        if (auto *ol = bLayer->ogrLayer())
        {
            // Set activeCT so pushOgrPolygon reprojects vertices to mesh CRS.
            activeCT = makeTransform(bLayer);

            // Collect every polygon/multipolygon from the layer into one
            // OGRMultiPolygon, then dissolve with UnaryUnion.  This removes
            // internal boundaries between adjacent or overlapping features so
            // the PSLG boundary ring is a clean outer shell.
            // (OGRGeometryCollection::addGeometry clones its argument, so
            // destroying each OGRFeature after adding is safe.)
            OGRMultiPolygon mp;
            ol->ResetReading();
            OGRFeature *f = nullptr;
            while ((f = ol->GetNextFeature()) != nullptr)
            {
                const OGRGeometry *geom = f->GetGeometryRef();
                if (geom)
                {
                    const auto gt = wkbFlatten(geom->getGeometryType());
                    if (gt == wkbPolygon)
                        mp.addGeometry(geom);
                    else if (gt == wkbMultiPolygon)
                    {
                        const auto *mpSrc = geom->toMultiPolygon();
                        for (int i = 0; i < mpSrc->getNumGeometries(); ++i)
                            mp.addGeometry(mpSrc->getGeometryRef(i));
                    }
                }
                OGRFeature::DestroyFeature(f);
            }

            OGRGeometry *dissolved = mp.UnaryUnion();
            if (dissolved)
            {
                walkOgrGeom(dissolved);
                OGRGeometryFactory::destroyGeometry(dissolved);
            }
            else
            {
                // Fallback: walk collected polygons without dissolve.
                for (int i = 0; i < mp.getNumGeometries(); ++i)
                    walkOgrGeom(mp.getGeometryRef(i));
            }

            if (activeCT) { OGRCoordinateTransformation::DestroyCT(activeCT); activeCT = nullptr; }
        }
    }

    if (out->domains.isEmpty())
    {
        const double m = 0.05;
        const double dx = modelExt.width() * m, dy = modelExt.height() * m;
        QPolygonF box;
        box << QPointF(modelExt.xMin()-dx, modelExt.yMin()-dy)
            << QPointF(modelExt.xMax()+dx, modelExt.yMin()-dy)
            << QPointF(modelExt.xMax()+dx, modelExt.yMax()+dy)
            << QPointF(modelExt.xMin()-dx, modelExt.yMax()+dy);
        out->domains.append(box);
    }

    // ── PSLG spatial helpers (need domains to be finalised first) ────
    QRectF domainBBox;
    for (const QPolygonF &dom : out->domains)
        domainBBox = domainBBox.united(dom.boundingRect());

    auto inDomain = [&](const QPointF &p) -> bool {
        if (!domainBBox.contains(p)) return false;
        for (const QPolygonF &dom : out->domains)
            if (dom.containsPoint(p, Qt::OddEvenFill)) return true;
        return false;
    };

    auto dedupeSegPath = [](const QVector<QPointF> &src) {
        QVector<QPointF> r;
        r.reserve(src.size());
        for (const QPointF &p : src)
            if (r.isEmpty() || (p - r.last()).manhattanLength() > 1e-9)
                r.append(p);
        return r;
    };

    // Strip intermediate vertices that lie outside the domain bounding box.
    // An unconstrained intermediate vertex outside the domain would create a
    // PSLG crossing with the domain boundary without a shared intersection
    // vertex, making the graph non-planar and causing Triangle to abort.
    // Endpoint filtering (applied below per segment) is the primary guard;
    // this strips only interior runaway vertices on long conduits.
    auto clipIntermediateToDomain = [&](const QVector<QPointF> &path) {
        if (path.size() <= 2) return path;
        QVector<QPointF> r;
        r.reserve(path.size());
        r.append(path.first());
        for (int k = 1; k < path.size()-1; ++k)
            if (domainBBox.contains(path[k])) r.append(path[k]);
        r.append(path.last());
        return dedupeSegPath(r);
    };

    // ── Steiner points (SWMM nodes — already in mesh CRS) ────────────
    // SWMM model coordinates are in the model's native CRS (= mesh CRS),
    // so no transform needed.  Filter out nodes that lie outside the
    // meshing domain to reduce Triangle's input vertex count.
    //
    // Each node also carries its rim elevation (invert + maxDepth) so the
    // worker can either (a) prefer this over a DTM sample, or (b) use it
    // as the only elevation source when no DTM is selected.
    SWMM_Engine engineForRim = layer->engine();
    int nextMarker = 100;
    if (m_includeJunctions->isChecked())
    {
        for (int c = SWMMModelLayer::CatJunctions; c <= SWMMModelLayer::CatDividers; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->nodeIndex(name);
                if (idx < 0) continue;
                double x = 0, y = 0;
                if (!layer->cachedNodeCoord(idx, &x, &y)) continue;
                // Skip nodes outside the meshing domain — Triangle ignores
                // them anyway but filtering early shrinks the PSLG.
                if (!inDomain(QPointF(x, y))) continue;
                mesh::SteinerPoint sp;
                sp.xy = QPointF(x, y); sp.marker = nextMarker; sp.tag = name;
                if (engineForRim)
                {
                    double invert = 0.0, maxDepth = 0.0;
                    const int er1 = swmm_node_get_invert_elev(engineForRim, idx, &invert);
                    const int er2 = swmm_node_get_max_depth   (engineForRim, idx, &maxDepth);
                    if (er1 == SWMM_OK && er2 == SWMM_OK)
                    {
                        sp.z    = invert + maxDepth;
                        sp.hasZ = true;
                    }
                }
                out->steinerPoints.append(sp);
                out->nodeMarkerToTag.insert(nextMarker, name);
                ++nextMarker;
            }
        }
    }

    // ── Constraint segments (SWMM links — already in mesh CRS) ───────
    // Deduplicate consecutive duplicate vertices (digitising artefacts
    // produce zero-length sub-segments that can stall Triangle).
    // Skip links whose entire bbox falls outside the domain.
    if (m_includeConduits->isChecked())
    {
        for (int c = SWMMModelLayer::CatConduits; c <= SWMMModelLayer::CatOutlets; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->linkIndex(name);
                if (idx < 0) continue;
                // Dedupe then simplify — RDP removes nearly-collinear intermediate
                // vertices from GIS-digitised conduit alignments (e.g. road-
                // following conduits), reducing constraint segment count.
                QVector<QPointF> path = simplifyPolyline(
                    clipIntermediateToDomain(
                        dedupeSegPath(layer->cachedLinkPolyline(idx))),
                    out->pslgSimplifyEps);
                if (path.size() < 2) continue;
                // Require BOTH endpoints inside the domain polygon.  A conduit
                // whose from-node or to-node is outside the domain would cross
                // the domain boundary without a vertex at the crossing, making
                // the PSLG non-planar and causing Triangle to abort.
                // Intermediate vertices may still briefly exit the domain (e.g.
                // a curved conduit near a concave boundary) — clip those in the
                // worker's per-segment domain check below.
                if (!inDomain(path.first()) || !inDomain(path.last())) continue;
                mesh::ConstraintSegment cs;
                cs.path = std::move(path); cs.marker = nextMarker; cs.tag = name;
                out->constraintSegs.append(cs);
                out->edgeMarkerToTag.insert(nextMarker, name);
                ++nextMarker;
            }
        }
    }

    // ── Aux point layers (may be in a different CRS) ─────────────────
    // Apply a spatial filter to the OGR layer so only features within the
    // domain bbox are returned — avoids full-file scans of large shapefiles.
    // Reproject coordinates to mesh CRS when the layer CRS differs.
    for (int i = 0; i < m_pointLayersList->count(); ++i)
    {
        auto *item = m_pointLayersList->item(i);
        if (!item || item->checkState() != Qt::Checked) continue;
        auto *vp = static_cast<GISVectorLayer *>(item->data(Qt::UserRole).value<void *>());
        if (!vp || !vp->ogrLayer()) continue;
        OGRLayer *ol = vp->ogrLayer();

        ol->SetSpatialFilterRect(domainBBox.left(),
                                  std::min(domainBBox.top(), domainBBox.bottom()),
                                  domainBBox.right(),
                                  std::max(domainBBox.top(), domainBBox.bottom()));
        ol->ResetReading();

        OGRCoordinateTransformation *ct = makeTransform(vp);
        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr)
        {
            if (auto *geom = f->GetGeometryRef())
            {
                const auto gt = wkbFlatten(geom->getGeometryType());
                if (gt == wkbPoint)
                {
                    const auto *p = geom->toPoint();
                    double x = p->getX(), y = p->getY();
                    xformPt(ct, x, y);
                    if (inDomain(QPointF(x, y)))
                        out->steinerPoints.append({QPointF(x, y), 0, {}});
                }
                else if (gt == wkbMultiPoint)
                {
                    const auto *mp = geom->toMultiPoint();
                    for (int j = 0; j < mp->getNumGeometries(); ++j)
                    {
                        const auto *pp = mp->getGeometryRef(j)->toPoint();
                        double x = pp->getX(), y = pp->getY();
                        xformPt(ct, x, y);
                        if (inDomain(QPointF(x, y)))
                            out->steinerPoints.append({QPointF(x, y), 0, {}});
                    }
                }
            }
            OGRFeature::DestroyFeature(f);
        }
        if (ct) OGRCoordinateTransformation::DestroyCT(ct);
        ol->SetSpatialFilter(nullptr);  // clear filter for other callers
    }

    // ── Aux line layers (may be in a different CRS) ───────────────────
    for (int i = 0; i < m_lineLayersList->count(); ++i)
    {
        auto *item = m_lineLayersList->item(i);
        if (!item || item->checkState() != Qt::Checked) continue;
        auto *vl = static_cast<GISVectorLayer *>(item->data(Qt::UserRole).value<void *>());
        if (!vl || !vl->ogrLayer()) continue;
        OGRLayer *ol = vl->ogrLayer();

        ol->SetSpatialFilterRect(domainBBox.left(),
                                  std::min(domainBBox.top(), domainBBox.bottom()),
                                  domainBBox.right(),
                                  std::max(domainBBox.top(), domainBBox.bottom()));
        ol->ResetReading();

        OGRCoordinateTransformation *ct = makeTransform(vl);
        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr)
        {
            auto pushLS = [&](const OGRLineString *ls) {
                if (!ls || ls->getNumPoints() < 2) return;
                QVector<QPointF> raw;
                raw.reserve(ls->getNumPoints());
                for (int j = 0; j < ls->getNumPoints(); ++j)
                {
                    double x = ls->getX(j), y = ls->getY(j);
                    xformPt(ct, x, y);
                    raw.append(QPointF(x, y));
                }
                auto path = simplifyPolyline(
                    clipIntermediateToDomain(dedupeSegPath(raw)),
                    out->pslgSimplifyEps);
                if (path.size() < 2) return;
                // Same planar-graph rule: both endpoints must be inside the domain.
                if (inDomain(path.first()) && inDomain(path.last()))
                    out->constraintSegs.append({std::move(path), 0, {}});
            };
            if (auto *gg = f->GetGeometryRef())
            {
                const auto gt = wkbFlatten(gg->getGeometryType());
                if (gt == wkbLineString)
                    pushLS(gg->toLineString());
                else if (gt == wkbMultiLineString)
                {
                    const auto *ml = gg->toMultiLineString();
                    for (int j = 0; j < ml->getNumGeometries(); ++j)
                        pushLS(ml->getGeometryRef(j)->toLineString());
                }
            }
            OGRFeature::DestroyFeature(f);
        }
        if (ct) OGRCoordinateTransformation::DestroyCT(ct);
        ol->SetSpatialFilter(nullptr);
    }

    // ── Region markers (subcatchments) ───────────────────────────────
    // Use name-based objectExtent() for the seed point — this is safe,
    // consistent, and avoids any index-mapping assumption between
    // categoryCount(CatSubcatchments) and cachedSubcatchVertices(i).
    // The bounding-box centroid is a reliable interior point for all
    // but highly concave subcatchments; Triangle propagates the region
    // attribute to every triangle whose circumcenter is reachable from
    // the seed, so a small positional error is acceptable.
    if (m_includeSubcatch->isChecked())
    {
        const auto cat = SWMMModelLayer::CatSubcatchments;
        for (int row = 0; row < layer->categoryCount(cat); ++row)
        {
            const QString name = layer->objectNameAt(cat, row);
            if (name.isEmpty()) continue;
            const MapExtent ce = layer->objectExtent(name);
            if (!ce.isValid()) continue;
            mesh::RegionMarker rm;
            rm.xy        = QPointF((ce.xMin()+ce.xMax())*0.5,
                                   (ce.yMin()+ce.yMax())*0.5);
            rm.attribute = nextMarker;
            rm.tag       = QStringLiteral("subcatch_%1").arg(name);
            out->regionMarkers.append(rm);
            ++nextMarker;
        }
    }

    // ── Steiner point snap-and-deduplicate ────────────────────────────
    // Merge near-coincident untagged Steiner points from different sources
    // (aux point layers, survey pins) that nominally represent the same
    // location.  Tagged SWMM points (marker != 0) are never merged.
    // Must run AFTER all points are collected and BEFORE quality options.
    snapAndDedupe(out->steinerPoints, out->pslgSnapEps);

    // ── Quality options ──────────────────────────────────────────────
    out->genOpts.maxArea          = m_maxAreaSpin->value();
    out->genOpts.minAngle         = m_minAngleSpin->value();
    out->genOpts.maxSteinerPoints = m_maxSteinerSpin->value();
    out->genOpts.allowSteiner     = m_allowSteiner->isChecked();
    out->genOpts.quiet            = true;

    // ── Thinning ─────────────────────────────────────────────────────
    out->doThinning                      = m_thinningBox->isChecked();
    out->thinnerOpts.normalDotThreshold  = m_thinningToleranceSpin->value();
    out->thinnerOpts.maxIterations       = m_thinningIterationsSpin->value();
    out->thinnerOpts.maxPoints           = m_thinningMaxPointsSpin->value();
    out->thinnerOpts.gridSpacing         = 0.0;
    out->thinnerOpts.useMinSpacing       = m_minSpacingBox->isChecked();
    out->thinnerOpts.minSpacing          = m_minSpacingSpin->value();

    // ── Output ───────────────────────────────────────────────────────
    out->outputMode    = m_outputExternal->isChecked()
                             ? mesh::MeshOutputMode::External
                             : mesh::MeshOutputMode::Inline;
    out->meshOutputPath = m_meshPathEdit->text().trimmed();
    out->manningsN      = m_manningsValueSpin->value();

    // ── Vertical Z conversion factor ─────────────────────────────────
    out->zConversionFactor = m_zFactorSpin ? m_zFactorSpin->value() : 1.0;

    return true;
}

// ---------------------------------------------------------------------------
// Accept → launch threaded pipeline
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onAccept()
{
    PipelineInputs inputs;
    QString err;
    if (!collectInputs(&inputs, &err))
    {
        QMessageBox::critical(this, tr("Cannot generate mesh"), err);
        return;
    }

    // Show embedded progress bar and switch Generate→disabled, Cancel→"Stop".
    m_progressBar->setValue(0);
    m_progressLabel->setText(tr("Starting…"));
    m_progressBar->setVisible(true);
    m_progressLabel->setVisible(true);
    m_generateBtn->setEnabled(false);
    m_cancelBtn->setText(tr("Cancel Generation"));

    // Watcher forwards progress + completion to the main thread.
    m_watcher = new QFutureWatcher<PipelineResult>(this);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::progressValueChanged,
            m_progressBar, &QProgressBar::setValue);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::progressTextChanged,
            m_progressLabel, &QLabel::setText);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::finished,
            this, &MeshGenerationDialog::onMeshFinished);

    m_watcher->setFuture(
        QtConcurrent::run(runMeshPipeline, std::move(inputs)));
}

// ---------------------------------------------------------------------------
// Completion handler (called on main thread via queued signal)
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onMeshFinished()
{
    // Restore UI state regardless of outcome.
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_generateBtn->setEnabled(true);
    m_cancelBtn->setText(tr("Cancel"));

    m_cancelBtn->setEnabled(true);  // re-enable in all paths

    if (m_watcher->isCanceled())
    {
        m_watcher->deleteLater();
        m_watcher = nullptr;
        return;
    }

    const PipelineResult result = m_watcher->result();
    m_watcher->deleteLater();
    m_watcher = nullptr;

    if (!result.ok)
    {
        QMessageBox::critical(this, tr("Mesh generation failed"),
            result.errorMsg.isEmpty() ? tr("Unknown error.") : result.errorMsg);
        return;
    }

    // Add the generated mesh layer to the canvas (main thread — safe).
    if (auto *canvas = m_pw->canvas())
    {
        for (auto *L : canvas->layers())
            if (auto *m = qobject_cast<SWMM2DMeshLayer *>(L))
                m->setActiveMesh(false);

        const bool isExt = (result.outputMode == mesh::MeshOutputMode::External);
        auto *meshLayer  = new SWMM2DMeshLayer(result.meshResult, result.meshPath);
        meshLayer->setActiveMesh(isExt);
        meshLayer->setName(result.meshPath.isEmpty()
                               ? QStringLiteral("Mesh (inline)")
                               : QFileInfo(result.meshPath).fileName());

        // Propagate the SWMM model's CRS to the mesh layer so
        // rebuildSceneGeometry() can reproject from model CRS → canvas CRS.
        // Without this the mesh renders at raw local coordinates and never
        // appears on the map.
        if (m_pw->modelLayer() && m_pw->modelLayer()->srs())
            meshLayer->setSRS(
                new SpatialReferenceSystem(*m_pw->modelLayer()->srs(), meshLayer),
                /*ownsSRS=*/true);

        canvas->addLayer(meshLayer, /*pushUndo=*/true);
    }

    m_pw->setHasChanges(true);
    accept();
}

// ---------------------------------------------------------------------------
// Cancel / close
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onCancelOrReject()
{
    if (m_watcher && m_watcher->isRunning())
    {
        // Worker is active — request cancellation; onMeshFinished will
        // restore the UI and clean up the watcher.
        m_watcher->cancel();
        m_cancelBtn->setEnabled(false);  // prevent double-cancel
        m_progressLabel->setText(tr("Cancelling…"));
    }
    else
    {
        reject();
    }
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onBrowseMeshPath()
{
    const QString picked = QFileDialog::getSaveFileName(
        this, tr("Choose Mesh File"), m_meshPathEdit->text(),
        tr("SWMMVis 2D Mesh (*.2dm);;All files (*)"));
    if (!picked.isEmpty())
        m_meshPathEdit->setText(picked);
}
