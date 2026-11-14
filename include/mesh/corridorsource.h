/**
 * @file      corridorsource.h
 * @brief     Selected-feature recipes for mesh corridor generation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_MESH_CORRIDORSOURCE_H
#define OPENSWMMVIS_MESH_CORRIDORSOURCE_H

#include "mesh/meshpatch.h"
#include <QStringList>
#include <functional>

namespace mesh
{

// A selected-feature recipe. Width and spacing are in meshCRSWkt linear units.
// Runtime layer UUIDs are deliberately excluded: they are not persisted by GIS layers.
struct CorridorSource {
    QString path, layerName, sourceCRSWkt, meshCRSWkt;
    QVector<qint64> featureIds; // Explicit selection; empty never means all features.
    bool bankPair =
        false; // Exactly two LineStrings in this layer; width comes from their geometry.
    QString widthField;
    double width = 10.0, along = 0.0;
    int across = 2;
    QString tag;
    QString geometryDigest; // Empty captures the first successful read.
    QStringList sourceFiles;
};

struct CorridorSourceStamp {
    QString path, canonicalPath;
    qint64 size = -1, modifiedMs = -1;
};

struct CorridorReadResult {
    QVector<PatchMesh> patches;
    QVector<CorridorSource> resolvedSources;
    QVector<CorridorSourceStamp> sourceStamps;
    QString error;
    /**
     * @brief Check whether this corridor-read result carries an explicit error string.
     * @return True when error is empty, false otherwise. Empty/no-source results can be
     * successful; this does not independently validate patch geometry, source freshness or
     * whether any cells exist. Constant read-only query with no allocation, source IO or
     * ownership transfer.
     */
    bool ok() const { return error.isEmpty(); }
};

// Worker-safe: opens owned datasets and returns values only. Cancellation returns
// an error and no partial patches or resolved sources. No live GIS layer is read.
/**
 * @brief Read explicitly selected local GIS features into owned corridor patches and
 * reproducible recipes.
 * @param[in] sources Borrowed stable recipes, unchanged; feature selection must be nonempty and
 * persistent nonnegative IDs. Bank pairs require exactly two distinct single open LineStrings
 * and no width field; ordinary sources allow LineString/MultiLineString.
 * Layer/source/recorded-mesh CRS and finite positive width, nonnegative along spacing, across>=1
 * are required, with feasible subdivision/storage arithmetic. Fixed/field widths and spacing are
 * already in target mesh linear units, not automatically source-unit values; source geometry
 * alone is reprojected. Existing geometryDigest must match selected original WKB, source CRS,
 * role and optional width-field values; empty digest captures the first successful read.
 * @param[in] targetCRSWkt Borrowed current target CRS text, unchanged. Nonempty sources require
 * a valid projected/local planar CRS with positive finite linear units matching every recipe's
 * recorded mesh CRS. An empty source list succeeds without validating CRS unless the initial
 * cancellation poll refuses.
 * @param[in] cancel Optional synchronous predicate, polled between
 * sources/features/vertices/patch work and before final validation; true returns a cancellation
 * error. Must not mutate inputs, exceptions propagate, callback is not retained and no
 * mid-patch-helper cancellation is guaranteed.
 * @return Owned result. Any checked source/selection/type/CRS/transform/digest/patch/freshness
 * failure returns an error with no partial patches, resolved recipes or stamps. Success
 * sorts/deduplicates IDs, captures canonical local dependency list/digests and stamps
 * path/target/size/mtime, and returns helper-built patches. Opens independent owned GDAL
 * datasets/features/transforms, never reads live GUI layers or writes sources/project/engine.
 * All dependencies must be existing local files, not remote/virtual datasets. Freshness checks
 * are best-effort metadata snapshots, not content locks; recheck sourceStamps immediately before
 * adopting a worker result. Result values own their storage and retain no caller references.
 */
CorridorReadResult readCorridorSources(const QVector<CorridorSource> &sources,
                                       const QString &targetCRSWkt,
                                       const std::function<bool()> &cancel = {});

// Recheck captured local dependencies before adopting a completed worker result.
/**
 * @brief Recheck captured local dependency identities before accepting a worker result.
 * @param[in] stamps Borrowed expected absolute/canonical path, size-byte and
 * modified-millisecond records, unchanged; empty list passes.
 * @param[out] error Optional borrowed diagnostic, cleared at entry, filled with first
 * changed/unavailable source message on failure; remains empty on success.
 * @return True only when every path still names a local file with matching canonical
 * target/size/mtime, false otherwise. Does not hash content, lock files or detect edits
 * preserving/reverting metadata; caller must minimize adoption race. Reads filesystem metadata
 * only, no source mutation or retained pointers.
 */
bool corridorSourceFilesUnchanged(const QVector<CorridorSourceStamp> &stamps,
                                  QString *error = nullptr);

} // namespace mesh
#endif
