/*!
 * \file   meshprofilesampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/meshprofilesampler.h"

#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MeshProfileSampler
{

namespace {

/*! Characteristic cell size of the mesh in scene units — sqrt(bbox area /
 *  triangle count). Falls back to a fraction of the bbox diagonal when the
 *  count is unavailable. Used to choose a resample step that follows the
 *  terrain without oversampling. */
double characteristicCellSize(SWMM2DMeshLayer *mesh)
{
    if (!mesh) return 0.0;
    const QRectF bb = mesh->m_sceneBBox;
    const int nTri  = mesh->m_sceneTris.size();
    if (bb.isNull()) return 0.0;
    if (nTri > 0) {
        const double area = std::abs(bb.width() * bb.height());
        if (area > 0.0)
            return std::sqrt(area / static_cast<double>(nTri));
    }
    // No triangles cached — use a small fraction of the diagonal.
    return 0.01 * std::hypot(bb.width(), bb.height());
}

} // namespace

MeshProfile buildMeshProfile(SWMM2DMeshLayer    *mesh,
                             SWMM2DResultsLayer *results,
                             const QVector<QPointF> &scenePolyline,
                             double stepHint)
{
    MeshProfile out;
    if (!mesh || scenePolyline.size() < 2)
        return out;

    out.hasResults = results && results->source()
                     && results->source()->timeCount() > 0;

    // Total polyline length (scene units == map units; scene is a pure Y-flip).
    double totalLen = 0.0;
    for (int i = 1; i < scenePolyline.size(); ++i)
        totalLen += std::hypot(scenePolyline[i].x() - scenePolyline[i - 1].x(),
                               scenePolyline[i].y() - scenePolyline[i - 1].y());
    if (totalLen <= 0.0)
        return out;

    // Choose the resample step. Honour stepHint when positive; otherwise use
    // half a characteristic cell so the terrain reads smoothly. Enforce the
    // sample cap so long traces stay bounded.
    double step = stepHint;
    if (step <= 0.0) {
        const double cell = characteristicCellSize(mesh);
        step = (cell > 0.0) ? cell * 0.5 : totalLen / 200.0;
    }
    if (step <= 0.0) step = totalLen / 200.0;
    if (totalLen / step > kMaxSamples)
        step = totalLen / kMaxSamples;

    // Max-depth envelope — barycentric per-vertex (smooth across wet cells).
    // No per-cell max mask any more: vertMax carries 0 as the never-wet
    // NO-DATA sentinel, so maxDepthAtSceneInterp's vertex-scoped dryness
    // (CellSurfaceInterp) already paints nothing where no corner was ever wet,
    // and a once-wet shoreline cell tapers to its sub-cell intercept instead
    // of truncating at the cell edge (WSE-extrapolation plan, Defect 2).
    QVector<float> vertMax;
    if (out.hasResults)
        vertMax = results->maxDepthPerVertex();

    // Mesh-triangle index per emitted sample, parallel to out.samples. Drives
    // the cell-boundary crossing pass below (where the line leaves one mesh
    // cell for the next). Kept on the mesh (ground) triangulation so it works
    // for bed-only profiles too.
    QVector<int> meshTriOf;

    // Build one fully-populated sample at a scene point. Shared by the regular
    // resample pass and the cell-crossing fold-in below so both produce
    // identical ground/depth columns.
    auto makeSample = [&](const QPointF &sp, double chain) -> Sample {
        Sample s;
        s.scenePt  = sp;
        s.chainage = chain;
        s.ground = mesh->sampleZAt(sp.x(), sp.y());   // NaN off-mesh — a plot gap.
        if (results) {
            const int tri = results->pickCellAt(sp);
            s.triIdx = tri;
            if (tri >= 0) {
                // Barycentric (smooth) depth + envelope — same interpolation
                // basis as the ground line, so WSE = ground + depth no longer
                // steps at cell boundaries (matches the contour map).
                s.depthNow = results->depthAtSceneInterp(sp);
                s.maxDepth = results->maxDepthAtSceneInterp(sp, vertMax);
                s.cellHasSurface = results->cellHasSurface(tri);
            }
        }
        return s;
    };

    // Walk each segment, emitting a sample every `step` units, and always
    // emit an exact sample at each vertex so corners aren't skipped.
    auto emitSample = [&](const QPointF &sp, double chain) {
        out.samples.push_back(makeSample(sp, chain));
        meshTriOf.push_back(mesh->locateTriangleAt(sp.x(), sp.y()));
    };

    double chainAtVertex = 0.0;
    emitSample(scenePolyline.first(), 0.0);

    for (int i = 1; i < scenePolyline.size(); ++i) {
        const QPointF a = scenePolyline[i - 1];
        const QPointF b = scenePolyline[i];
        const double segLen = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (segLen <= 0.0) continue;
        const QPointF dir((b.x() - a.x()) / segLen, (b.y() - a.y()) / segLen);

        // Interior points along this segment at multiples of `step`.
        double d = step;
        while (d < segLen - 1e-9) {
            const QPointF sp(a.x() + dir.x() * d, a.y() + dir.y() * d);
            emitSample(sp, chainAtVertex + d);
            d += step;
        }
        chainAtVertex += segLen;
        emitSample(b, chainAtVertex);   // exact vertex sample
    }

    // Cell-boundary crossings: wherever the mesh-triangle index changes
    // between consecutive samples, the line has crossed a cell edge. Refine
    // the exact crossing along the straight chord between the two samples
    // (every consecutive pair is collinear with the polyline) by bisecting on
    // triangle membership, then record (chainage, ground) for a dot on the
    // ground line. Land the dot on whichever side is on-mesh so the ground is
    // finite even at the mesh boundary.
    auto refineCrossing = [&](const QPointF &pa, double ca, int triA,
                              const QPointF &pb, double cb) -> CellCrossing {
        double lo = 0.0, hi = 1.0;     // lo stays in triA, hi is past the edge
        for (int it = 0; it < 24; ++it) {
            const double mid = 0.5 * (lo + hi);
            const QPointF pm(pa.x() + (pb.x() - pa.x()) * mid,
                             pa.y() + (pb.y() - pa.y()) * mid);
            if (mesh->locateTriangleAt(pm.x(), pm.y()) == triA) lo = mid;
            else                                                hi = mid;
        }
        const double t = (triA >= 0) ? lo : hi;   // pick the on-mesh side
        CellCrossing c;
        c.scenePt  = QPointF(pa.x() + (pb.x() - pa.x()) * t,
                             pa.y() + (pb.y() - pa.y()) * t);
        c.chainage = ca + (cb - ca) * t;
        c.ground   = mesh->sampleZAt(c.scenePt.x(), c.scenePt.y());
        return c;
    };

    for (int i = 1; i < out.samples.size(); ++i) {
        if (meshTriOf[i] == meshTriOf[i - 1]) continue;
        const Sample &A = out.samples[i - 1];
        const Sample &B = out.samples[i];
        const CellCrossing c = refineCrossing(A.scenePt, A.chainage, meshTriOf[i - 1],
                                              B.scenePt, B.chainage);
        if (std::isfinite(c.ground))
            out.crossings.push_back(c);
    }

    // Fold the cell-edge crossings into the sample list as real ground
    // vertices. The resampled ground line is piecewise-linear between
    // resample points, but the terrain has a slope break at each cell edge —
    // so a crossing dot (true terrain at the edge) floats off the straight
    // chord, and zooming the chart in magnifies that gap. Inserting the
    // crossing as a vertex makes the curve bend exactly at the edge so the
    // dot sits on the line at every zoom. ground is copied from the crossing
    // (not re-sampled) so the dot and the vertex are bit-identical.
    if (!out.crossings.isEmpty()) {
        out.samples.reserve(out.samples.size() + out.crossings.size());
        for (const CellCrossing &c : out.crossings) {
            Sample s = makeSample(c.scenePt, c.chainage);
            s.ground = c.ground;
            out.samples.push_back(s);
        }
        std::stable_sort(out.samples.begin(), out.samples.end(),
                         [](const Sample &a, const Sample &b) {
                             return a.chainage < b.chainage;
                         });
    }

    return out;
}

} // namespace MeshProfileSampler
