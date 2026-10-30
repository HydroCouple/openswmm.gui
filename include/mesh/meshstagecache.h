/**
 * @file      meshstagecache.h
 * @brief     Silent sidecar disk cache for the expensive, rarely-changing boundary stage of mesh
 *            generation, so parameter-tweaking re-runs skip minutes of recompute.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Silent sidecar disk cache for the expensive, rarely-changing boundary
 * stage of mesh generation, so parameter-tweaking re-runs skip minutes of
 * recompute:
 *
 *   Stage A — prepared PSLG boundary: dissolved domains + prepared hole
 *             rings/seeds.  Keyed by the boundary source identity (path +
 *             mtime + size + layer name, or a hash of the subcatchment
 *             vertices), both CRS WKTs, and the ring-trimming parameters.
 *   Stage T — adaptive terrain error index (TerrainErrorField summaries).
 *             Keyed by the DEM identity, mesh CRS, meshing bbox and z scale;
 *             the file header is re-validated against the opened raster.
 *   Stage L — terrain break lines in mesh coordinates (adaptive mode).
 *             Keyed by the DEM identity, DEM window, mesh CRS and the
 *             extraction parameters.
 *
 * Entries live in `project-directory/.meshcache/` (sidecar convention, like .ovr
 * and .oswp), one file per entry, written atomically via QSaveFile.  Any
 * read anomaly (bad magic/version/short read) is a miss.  prune() keeps the
 * newest N entries per stage.  Keys embed a format-version salt, so bumping
 * kFormatVersion invalidates everything at once.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHSTAGECACHE_H
#define OPENSWMMVIS_MESH_MESHSTAGECACHE_H

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

namespace mesh
{

class MeshStageCache
{
public:
    /**
     * @brief Establish a best-effort sidecar cache directory beside a project input file.
     * @param[in] projectInpPath Borrowed project path, unchanged; an empty path leaves the cache
     * disabled. A nonempty path is resolved by QFileInfo and its parent/.meshcache is created
     * recursively if possible.
     * @note Owns only its directory string, not the project or cache contents. Failure is silent
     * and reflected by isUsable(); no project input contents are read or written.
     *
     * @details Cache dir = directory beside \p projectInpPath, named .meshcache (created
     * best-effort).
     */
    explicit MeshStageCache(const QString &projectInpPath);

    /**
     * @brief Check whether the stored cache directory is nonempty and presently exists as a
     * directory.
     * @return True for an existing directory, false otherwise; does not test read/write
     * permission or create it again. Performs filesystem metadata lookup without changing stored
     * state.
     */
    [[nodiscard]] bool isUsable() const;

    /*! Identity of a file-backed cache input. */
    struct FileIdentity {
        QString absPath;
        qint64 mtimeMs = 0;
        qint64 sizeBytes = 0;
    };

    // ── Stage A payload: prepared PSLG boundary ──────────────────────
    struct BoundaryPrep {
        QVector<QPolygonF> domains;          ///< simplified+densified exteriors
        QVector<QVector<QPointF>> holeRings; ///< prepared rings (ALL, in order)
        QVector<QPointF> holeSeeds;          ///< parallel: interior seed per ring
        QVector<bool> holeValid;             ///< parallel: false → skip ring
        qint32 skippedRings = 0;
    };

    /**
     * @brief Hash the complete stage-A source/settings identity with format and conditioning
     * salts.
     * @param[in] src Borrowed source-file identity; use empty fields for a subcatchment-derived
     * boundary.
     * @param[in] subcatchHash Borrowed subcatchment content hash, empty for file-backed sources.
     * @param[in] layerName Borrowed selected source layer name, unchanged.
     * @param[in] boundaryCRSWkt Borrowed source CRS WKT, hashed verbatim without normalization.
     * @param[in] meshCRSWkt Borrowed target CRS WKT, hashed verbatim.
     * @param[in] simplifyEps Opaque boundary preparation control hashed exactly; current dialog
     * passes its turn-trimming angle in degrees despite this historical parameter name.
     * @param[in] maxBoundaryEdgeLen Opaque second preparation control; current dialog passes
     * trimming deviation in mesh-coordinate units despite this historical parameter name.
     * @param[in] minCellSize Minimum conditioning cell size in mesh length units, zero when
     * unused.
     * @param[in] minSizeEnforce Whether minimum-size conditioning is active; includes a
     * conditioning-version salt only when true. Every supplied field contributes to the key
     * without validation.
     * @return Owned 64-character lowercase SHA-256 hex key using pinned Qt stream serialization.
     * No file access, geometry preparation, source ownership retention or project mutation.
     *
     * @details SHA-256 hex key for a Stage A entry.  Pass \p src for a file-backed boundary
     * (subcatchHash empty), or \p subcatchHash for the subcatchment source (src fields empty).
     * \p minSizeEnforce is part of the identity for the same reason \p minCellSize is: the
     * stored rings are CONDITIONED geometry, and enforcement mode changes what conditioning may
     * do to them.
     */
    static QByteArray boundaryKey(const FileIdentity &src, const QByteArray &subcatchHash,
                                  const QString &layerName, const QString &boundaryCRSWkt,
                                  const QString &meshCRSWkt, double simplifyEps,
                                  double maxBoundaryEdgeLen, double minCellSize = 0.0,
                                  bool minSizeEnforce = false);

    /**
     * @brief Read and validate a stage-A boundary-preparation entry into an owned result value.
     * @param[in] key Borrowed cache key from boundaryKey(), expected safe hexadecimal basename
     * text; no path-character sanitization is performed.
     * @param[out] out Optional borrowed destination, null refuses. Replaced only after complete
     * stream, magic/version/stage, EOF and parallel hole-array length validation; untouched on
     * every miss.
     * @return True on successful load, false for disabled cache, null output,
     * open/read/header/length failure or trailing data. Payload coordinates keep their original
     * mesh units. Geometry validity is not checked. Reads disk and allocates temporary/result
     * arrays; no input project update or retained output pointer.
     */
    bool loadBoundary(const QByteArray &key, BoundaryPrep *out) const;
    /**
     * @brief Write one stage-A payload through QSaveFile and prune older cache entries after
     * commit.
     * @param[in] key Borrowed safe hexadecimal key from boundaryKey(); not sanitized as a path
     * component.
     * @param[in] v Borrowed payload, unchanged; caller supplies consistent parallel hole arrays
     * and valid prepared geometry in mesh units, neither checked here.
     * @return QSaveFile commit result, false when disabled or opening fails. Existing
     * destination replacement uses QSaveFile behavior; no independent QDataStream status
     * validation is performed before commit. Successful storage invokes prune(), which may
     * remove entries of every stage. Does not modify the project input or retain source
     * references; no cross-process locking or transaction across pruning.
     */
    bool storeBoundary(const QByteArray &key, const BoundaryPrep &v) const;

    /**
     * @brief Capture lexical absolute path, modification timestamp and size for a file-backed
     * cache input.
     * @param[in] path Borrowed path resolved by QFileInfo, unchanged; existence/readability are
     * not required.
     * @return Owned identity with mtime in milliseconds since Unix epoch and size in bytes,
     * using QFileInfo fallbacks for missing/invalid files. Does not hash contents, canonicalize
     * symlinks or open the file; changes preserving path/mtime/size are not detected by this
     * identity alone.
     */
    static FileIdentity identityOf(const QString &path);

    // ── Stage T: terrain error index (file written by TerrainErrorField) ──
    /**
     * @brief Hash the stage-T DEM identity, target CRS, exact domain rectangle and elevation
     * scale.
     * @param[in] dem Borrowed file identity in bytes/milliseconds, unchanged.
     * @param[in] meshCRSWkt Borrowed target CRS WKT hashed verbatim.
     * @param[in] domain Borrowed rectangle in mesh CRS length units; x/y/width/height hashed
     * without normalization.
     * @param[in] zScale Multiplicative raster-to-mesh vertical-unit scale, hashed as supplied
     * without validation.
     * @return Owned lowercase SHA-256 hex key with format/stage salt. No raster reads or cache
     * file creation; callers must change the identity/settings when source semantics change.
     */
    static QByteArray terrainIndexKey(const FileIdentity &dem, const QString &meshCRSWkt,
                                      const QRectF &domain, double zScale);
    /**
     * @brief Form the stage-T binary entry pathname for a supplied key.
     * @param[in] key Borrowed safe hex key, not sanitized; normally terrainIndexKey() output.
     * @return Owned directory/T-key.bin path, or empty only when no directory was established.
     * Does not check present directory usability or file existence, create/open a file or prune
     * entries.
     *
     * @details Entry path for \p key; empty when the cache is unusable.
     */
    [[nodiscard]] QString terrainIndexPath(const QByteArray &key) const;

    // ── Stage L: terrain break lines (mesh CRS) ─────────────────────
    struct Breaklines {
        QVector<QVector<QPointF>> lines;
        qint32 medianLength = 0;
        qint64 dropped = 0;
        bool skipped = false;
    };
    /**
     * @brief Hash the stage-L DEM window and terrain-breakline extraction parameters.
     * @param[in] dem Borrowed file identity, unchanged.
     * @param[in] meshCRSWkt Borrowed mesh CRS WKT, hashed verbatim.
     * @param[in] demWindow Borrowed exact raster-window rectangle, in the DEM coordinate system
     * used by caller.
     * @param[in] tolerance Extraction vertical-error tolerance in the extractor's elevation
     * units.
     * @param[in] lowRatio Dimensionless low/high hysteresis ratio, hashed without range
     * validation.
     * @param[in] minPixels Minimum line-length pixel count setting, hashed as signed 32-bit
     * integer.
     * @param[in] maxPixels Extraction pixel budget, hashed as signed 64-bit integer.
     * @param[in] decimation Raster sampling decimation factor; value 1 preserves historical key
     * encoding, all other values append a signed 32-bit field. Caller supplies meaningful
     * positive factor.
     * @return Owned salted lowercase SHA-256 hex key; no file IO, normalization or extraction
     * occurs.
     */
    static QByteArray breaklineKey(const FileIdentity &dem, const QString &meshCRSWkt,
                                   const QRectF &demWindow, double tolerance, double lowRatio,
                                   int minPixels, qint64 maxPixels, int decimation = 1);
    /**
     * @brief Load a stage-L breakline payload after complete envelope and EOF validation.
     * @param[in] key Borrowed safe hexadecimal breaklineKey() result, not sanitized.
     * @param[out] out Optional borrowed destination; null refuses, otherwise replaced only on
     * success.
     * @return True for a readable matching magic/version/stage payload with no trailing bytes;
     * false for any miss/read failure with output untouched. Does not validate line geometry,
     * counts or coordinates. Allocates owned vectors, reads cache disk only and does not modify
     * project data or retain pointers.
     */
    bool loadBreaklines(const QByteArray &key, Breaklines *out) const;
    /**
     * @brief Commit a stage-L payload and prune cache stages after successful storage.
     * @param[in] key Borrowed safe hexadecimal breaklineKey() result, not sanitized as a path
     * component.
     * @param[in] v Borrowed breaklines and diagnostics, unchanged; validity and consistency are
     * caller's contract.
     * @return QSaveFile commit result, false for disabled cache or open failure; stream status
     * is not independently checked. Successful commit can prune other stage entries, without
     * locking against other cache users. Original source/project files are not modified; payload
     * references are not retained.
     */
    bool storeBreaklines(const QByteArray &key, const Breaklines &v) const;

    /**
     * @brief Best-effort remove older cache files in recognized stages, ordered by modification
     * time.
     * @param[in] keepPerStage Nonnegative number retained for A/B/L entries, default 8; T binary
     * indexes and D TIFF entries retain min(keepPerStage,2). Negative input is unsupported
     * because it indexes the list.
     * @note Returns silently when disabled and ignores individual removal failures. Only
     * matching A/B/L/T .bin and D .tif names are considered; no active-entry locking or
     * multi-stage transaction. Mutates disk cache contents, not this object's directory string
     * or the project input.
     *
     * @details Keep only the newest \p keepPerStage entries per stage (by mtime); terrain
     * indexes (stage T), which can reach gigabytes, keep two.
     */
    void prune(int keepPerStage = 8) const;

    /**
     * @brief Copy the cache directory established at construction.
     * @return Owned directory string, empty when disabled. Does not revalidate existence or
     * permissions; no filesystem mutation, allocation beyond normal QString value semantics or
     * ownership transfer.
     */
    [[nodiscard]] QString dir() const { return m_dir; }

private:
    static constexpr quint32 kMagic = 0x4D435348; // "MCSH"
    // Salted into every cache key AND written into every payload header —
    // bumping it invalidates all entries at once.
    // Version 3 (MESH_OVERHAUL_PLAN_2026-09-29.md): boundary rings are
    // trimmed by straightness instead of RDP+densify; the terrain stage
    // (B) no longer exists.
    static constexpr quint16 kFormatVersion = 3;
    // Bump when mesh::conditionMeshRegion changes its output (stage A only).
    static constexpr quint16 kConditioningVersion = 4;

    /**
     * @brief Concatenate a stage marker and key into the cache's binary-entry pathname.
     * @param[in] stage Single stage character, normally A/T/L, not validated.
     * @param[in] key Borrowed safe hexadecimal key interpreted as Latin-1 text, not sanitized.
     * @return Owned m_dir/stage-key.bin string even if m_dir is empty; callers must guard
     * disabled state. No disk access, existence check or directory creation.
     */
    [[nodiscard]] QString entryPath(char stage, const QByteArray &key) const;

    QString m_dir;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHSTAGECACHE_H
