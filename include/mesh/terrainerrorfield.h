// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMM_MESH_TERRAINERRORFIELD_H
#define OPENSWMM_MESH_TERRAINERRORFIELD_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <functional>
#include <memory>

namespace mesh {

/*! Tiled DEM reference for error-driven refinement. Tests original pixel
 * centres against the actual triangle plane. Affine residual bounds allow
 * whole planar blocks to pass without visiting their pixels. Raster samples
 * live in a bounded LRU cache, not a dense point cloud. Not thread-safe: each
 * worker must own its reader, transforms and cache. */
class TerrainErrorField
{
public:
    struct Query {
        double maxError = 0.0;       ///< Largest actually visited residual.
        double upperBound = 0.0;     ///< Includes certified unvisited blocks.
        QPointF point;              ///< Location of maxError.
        quint64 samples = 0;        ///< Valid samples actually tested inside T.
        quint64 noDataSamples = 0;  ///< Missing DEM samples inside T; never certified as terrain.
        bool valid = true;          ///< False for unknown vertex z or I/O failure.
    };
    TerrainErrorField();
    ~TerrainErrorField();
    TerrainErrorField(const TerrainErrorField &) = delete;
    TerrainErrorField &operator=(const TerrainErrorField &) = delete;

    /*! \p indexFile, when set, persists the summary hierarchy: a valid file
     *  for the same raster window, origin and scale is loaded instead of
     *  rebuilding; otherwise the built index is saved there (best effort,
     *  skipped when the volume lacks twice its size). */
    bool open(const QString &path, const QString &meshCRS, const QRectF &domain,
              double zScale = 1.0, int cacheMiB = 64,
              const std::function<bool(double)> &progress = {},
              const QString &indexFile = {});
    bool indexLoaded() const;   ///< open() reused a saved index.
    bool indexSaved() const;    ///< open() wrote a new index file.
    // Test seam; coordinates are pixel centres (col+.5,row+.5).
    bool buildFromGrid(const float *z, int cols, int rows,
                       const std::function<bool(double)> &progress = {});
    double sampleAt(double x, double y) const;
    Query queryTriangle(const QPointF *xy, const double *z, double tolerance,
                        bool exhaustive = false) const;
    /*! Refinement query: the same maxError, point and validity as
     *  queryTriangle(xy, z, tolerance), found by best-first branch and bound
     *  so blocks that cannot hold the worst residual are not scanned.
     *  upperBound, samples and noDataSamples are not computed. */
    Query queryWorst(const QPointF *xy, const double *z, double tolerance) const;
    QString errorMsg() const;
    quint64 referenceSamples() const;
    qint64 summaryBytes() const;
    double verticalQuantum() const; ///< Whole-unit quantization, in output z units; 0 if unproven.
    void setCancellation(std::function<bool()> cancelled);
    // Composite references (e.g. authored channel bathymetry) override pixel
    // elevations only inside the supplied bounds. Ordinary terrain keeps its
    // fast planar hierarchy. sampleAt() still samples the original raster.
    void setQueryOverride(const QRectF &bounds, std::function<double(double,double,double)> value,
                          std::function<bool(const QRectF &)> intersects = {});

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
#endif
