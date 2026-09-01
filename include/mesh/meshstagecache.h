/*!
 * \file   meshstagecache.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Silent sidecar disk cache for the two expensive, rarely-changing stages of
 * mesh generation, so parameter-tweaking re-runs skip minutes of recompute:
 *
 *   Stage A — prepared PSLG boundary: dissolved domains + prepared hole
 *             rings/seeds.  Keyed by the boundary source identity (path +
 *             mtime + size + layer name, or a hash of the subcatchment
 *             vertices), both CRS WKTs, and the two ring-prep parameters.
 *   Stage B — terrain candidate points (DTM CRS, PRE-filter): the output of
 *             DTMThinner::generatePoints()/readPixels().  Keyed by the DEM
 *             identity + band + the thinning-relevant options + the sampled
 *             bbox.  Poisson/boundary-filter/mesh-CRS parameters are
 *             deliberately NOT in the key — those passes are cheap and rerun
 *             on both paths, so tweaking them still hits the cache.
 *
 * Entries live in <project dir>/.meshcache/ (sidecar convention, like .ovr
 * and .oswp), one file per entry, written atomically via QSaveFile.  Any
 * read anomaly (bad magic/version/short read) is a miss.  prune() keeps the
 * newest N entries per stage.  Keys embed a format-version salt, so bumping
 * kFormatVersion invalidates everything at once.
 */
#ifndef OPENSWMMVIS_MESH_MESHSTAGECACHE_H
#define OPENSWMMVIS_MESH_MESHSTAGECACHE_H

#include "mesh/dtmthinner.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

// Opt-in perf/diagnostic chatter for the mesh pipeline:
// QT_LOGGING_RULES="openswmm.mesh.perf.debug=true" (cache lines are qCInfo).
Q_DECLARE_LOGGING_CATEGORY(lcMeshPerf)

namespace mesh {

class MeshStageCache
{
public:
    /*! Cache dir = <dir of \p projectInpPath>/.meshcache (created best-effort). */
    explicit MeshStageCache(const QString &projectInpPath);

    [[nodiscard]] bool isUsable() const;

    /*! Identity of a file-backed cache input. */
    struct FileIdentity
    {
        QString absPath;
        qint64  mtimeMs   = 0;
        qint64  sizeBytes = 0;
    };

    // ── Stage A payload: prepared PSLG boundary ──────────────────────
    struct BoundaryPrep
    {
        QVector<QPolygonF>        domains;     ///< simplified+densified exteriors
        QVector<QVector<QPointF>> holeRings;   ///< prepared rings (ALL, in order)
        QVector<QPointF>          holeSeeds;   ///< parallel: interior seed per ring
        QVector<bool>             holeValid;   ///< parallel: false → skip ring
        qint32                    skippedRings = 0;
    };

    // ── Stage B payload: terrain candidates (DTM CRS, pre-filter) ────
    struct TerrainPoints
    {
        QVector<QPointF> xyDtm;
        QVector<double>  z;
    };

    /*! SHA-256 hex key for a Stage A entry.  Pass \p src for a file-backed
     *  boundary (subcatchHash empty), or \p subcatchHash for the
     *  subcatchment source (src fields empty).  \p minSizeEnforce is part of
     *  the identity for the same reason \p minCellSize is: the stored rings
     *  are CONDITIONED geometry, and enforcement mode changes what
     *  conditioning may do to them. */
    static QByteArray boundaryKey(const FileIdentity &src,
                                  const QByteArray   &subcatchHash,
                                  const QString      &layerName,
                                  const QString      &boundaryCRSWkt,
                                  const QString      &meshCRSWkt,
                                  double simplifyEps,
                                  double maxBoundaryEdgeLen,
                                  double minCellSize = 0.0,
                                  bool   minSizeEnforce = false);

    /*! SHA-256 hex key for a Stage B entry.  Only the thinning-relevant
     *  DTMThinnerOptions fields participate (gridSpacing, threshold,
     *  useAverageDot, maxPoints, maxIterations). */
    static QByteArray terrainKey(const FileIdentity &dem, int band,
                                 const DTMThinnerOptions &opts,
                                 bool doThinning,
                                 const QRectF &dtmBbox);

    bool loadBoundary(const QByteArray &key, BoundaryPrep *out) const;
    bool storeBoundary(const QByteArray &key, const BoundaryPrep &v) const;
    bool loadTerrain(const QByteArray &key, TerrainPoints *out) const;
    bool storeTerrain(const QByteArray &key, const TerrainPoints &v) const;

    /*! Keep only the newest \p keepPerStage entries per stage (by mtime). */
    void prune(int keepPerStage = 8) const;

    [[nodiscard]] QString dir() const { return m_dir; }

private:
    static constexpr quint32 kMagic         = 0x4D435348;  // "MCSH"
    // Salted into every cache key AND written into every payload header —
    // bumping it invalidates all entries at once.  MUST be bumped when
    // DTMThinner's banded-thinning geometry constants change
    // (kBytesPerGridPoint, kMaxGridBytesDefault, kMaxThinningHalo in
    // dtmthinner.{h,cpp}): they determine multi-band tiling and therefore
    // the terrain-point OUTPUT for banded configurations.
    static constexpr quint16 kFormatVersion = 1;

    [[nodiscard]] QString entryPath(char stage, const QByteArray &key) const;

    QString m_dir;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHSTAGECACHE_H
