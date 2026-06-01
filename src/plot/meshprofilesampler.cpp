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

    // Per-cell max-depth envelope — computed once, indexed by triangle.
    QVector<float> perTriMax;
    if (out.hasResults)
        perTriMax = results->maxDepthPerCell();

    // Walk each segment, emitting a sample every `step` units, and always
    // emit an exact sample at each vertex so corners aren't skipped.
    auto emitSample = [&](const QPointF &sp, double chain) {
        Sample s;
        s.scenePt  = sp;
        s.chainage = chain;
        const double g = mesh->sampleZAt(sp.x(), sp.y());
        s.ground = g;   // NaN off-mesh — propagated to the plot as a gap.
        if (results) {
            const int tri = results->pickCellAt(sp);
            s.triIdx = tri;
            if (tri >= 0) {
                s.depthNow = results->depthAtSceneNow(sp);
                if (tri < perTriMax.size())
                    s.maxDepth = perTriMax[tri];
            }
        }
        out.samples.push_back(s);
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

    return out;
}

} // namespace MeshProfileSampler
