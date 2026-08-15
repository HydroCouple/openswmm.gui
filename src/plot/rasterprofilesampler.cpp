/*!
 * \file   rasterprofilesampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/rasterprofilesampler.h"

#include "map/spatialreferencesystem.h"
#include "mesh/dtmsampler.h"

#include <ogr_spatialref.h>

#include <cmath>
#include <limits>
#include <memory>

namespace RasterProfileSampler
{

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/*! Scene → map: the scene is a pure Y-flip of the canvas CRS (the convention
 *  MapToolMeshProfile emits and SWMM2DMeshLayer::sampleZAt consumes). */
inline QPointF sceneToMap(const QPointF &sp) { return QPointF(sp.x(), -sp.y()); }

} // namespace

ProfileSection::Section
buildRasterProfile(const QString &rasterPath,
                   int band,
                   const SpatialReferenceSystem *canvasSRS,
                   const QVector<QPointF> &scenePolyline,
                   double vertFactor,
                   double stepHint)
{
    ProfileSection::Section out;
    if (rasterPath.isEmpty() || scenePolyline.size() < 2)
        return out;

    mesh::DTMSampler dtm;
    if (!dtm.open(rasterPath, band > 0 ? band : 1))
        return out;

    // Polyline length in scene units. Scene is a pure Y-flip of the canvas CRS,
    // so scene distance == canvas-CRS distance == the chart's x axis.
    double totalLen = 0.0;
    for (int i = 1; i < scenePolyline.size(); ++i)
        totalLen += std::hypot(scenePolyline[i].x() - scenePolyline[i - 1].x(),
                               scenePolyline[i].y() - scenePolyline[i - 1].y());
    if (totalLen <= 0.0)
        return out;

    // Canvas CRS → raster CRS. GISRasterLayer::valueAt skips this (it assumes
    // the raster is in the canvas CRS), which is why this sampler goes to
    // DTMSampler directly: a reprojected DEM would otherwise be probed at the
    // wrong pixel and the ground line would drift off the network.
    std::unique_ptr<SpatialReferenceSystem> rasterSRS;
    OGRCoordinateTransformation *canvasToRaster = nullptr;
    if (canvasSRS) {
        const QString wkt = dtm.crsWkt();
        if (!wkt.isEmpty())
            rasterSRS.reset(SpatialReferenceSystem::fromWktOrProj(wkt));
        if (rasterSRS && !canvasSRS->equals(*rasterSRS)) {
            canvasToRaster = canvasSRS->createTransformationTo(*rasterSRS);
            // A transform is REQUIRED here and GDAL couldn't build one. Falling
            // through would probe the DEM with raw canvas coordinates — the
            // silently-wrong ground line this sampler exists to prevent. Return
            // nothing instead, so the caller shows an empty profile rather than
            // a plausible-looking lie.
            if (!canvasToRaster)
                return out;
        }
    }

    auto mapToRaster = [canvasToRaster](const QPointF &mp) {
        if (!canvasToRaster) return mp;
        double x = mp.x(), y = mp.y();
        if (canvasToRaster->Transform(1, &x, &y)) return QPointF(x, y);
        return QPointF(kNaN, kNaN);   // off-domain — DTMSampler returns NaN
    };

    // Resample step, in SCENE units. Half a DEM pixel reads the terrain without
    // oversampling it. The pixel size is in raster-CRS units, so when the CRSs
    // differ convert it with the scale factor the transform applies to this
    // path — the ratio of the polyline's total length in raster units to its
    // total length in scene units. (Endpoint-to-endpoint would collapse to zero
    // on a closed trace; the full walk doesn't.)
    double step = stepHint;
    if (step <= 0.0) {
        const double pxRaster = dtm.pixelSize();
        double sceneToRasterScale = 1.0;
        if (canvasToRaster && pxRaster > 0.0) {
            double lenRaster = 0.0;
            QPointF prev = mapToRaster(sceneToMap(scenePolyline.first()));
            for (int i = 1; i < scenePolyline.size(); ++i) {
                const QPointF cur = mapToRaster(sceneToMap(scenePolyline[i]));
                const double d = std::hypot(cur.x() - prev.x(), cur.y() - prev.y());
                if (std::isfinite(d)) lenRaster += d;
                prev = cur;
            }
            if (lenRaster > 0.0)
                sceneToRasterScale = lenRaster / totalLen;   // totalLen > 0 above
        }
        // px in scene units = px in raster units / (raster units per scene unit).
        const double pxScene = (pxRaster > 0.0 && sceneToRasterScale > 0.0)
                                   ? pxRaster / sceneToRasterScale
                                   : 0.0;
        step = (pxScene > 0.0) ? pxScene * 0.5 : totalLen / 200.0;
    }
    if (step <= 0.0) step = totalLen / 200.0;
    if (totalLen / step > ProfileSection::kMaxSamples)
        step = totalLen / ProfileSection::kMaxSamples;

    // Walk each segment emitting a sample every `step` units, plus an exact
    // sample at every vertex so corners aren't skipped (mirrors
    // MeshProfileSampler::buildMeshProfile).
    QVector<QPointF> rasterPts;   // parallel to out.samples — bulk-sampled below

    auto emitSample = [&](const QPointF &sp, double chain) {
        ProfileSection::Sample s;
        s.scenePt  = sp;
        s.chainage = chain;
        s.ground   = kNaN;        // filled from the bulk sample below
        out.samples.push_back(s);
        rasterPts.push_back(mapToRaster(sceneToMap(sp)));
    };

    double chainAtVertex = 0.0;
    emitSample(scenePolyline.first(), 0.0);

    for (int i = 1; i < scenePolyline.size(); ++i) {
        const QPointF a = scenePolyline[i - 1];
        const QPointF b = scenePolyline[i];
        const double segLen = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (segLen <= 0.0) continue;
        const QPointF dir((b.x() - a.x()) / segLen, (b.y() - a.y()) / segLen);

        double d = step;
        while (d < segLen - 1e-9) {
            emitSample(QPointF(a.x() + dir.x() * d, a.y() + dir.y() * d),
                       chainAtVertex + d);
            d += step;
        }
        chainAtVertex += segLen;
        emitSample(b, chainAtVertex);   // exact vertex sample
    }

    // One bulk pass over the DEM (bilinear). Off-raster / NoData come back NaN,
    // which the chart renders as a gap in the ground line.
    const QVector<double> z = dtm.sampleBulk(rasterPts);
    for (int i = 0; i < out.samples.size() && i < z.size(); ++i)
        out.samples[i].ground = z[i] * vertFactor;   // NaN * f == NaN

    if (canvasToRaster)
        OGRCoordinateTransformation::DestroyCT(canvasToRaster);

    return out;   // hasResults = false, crossings empty — ground-only section.
}

} // namespace RasterProfileSampler
