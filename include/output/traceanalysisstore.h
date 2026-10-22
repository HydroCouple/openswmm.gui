// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      traceanalysisstore.h
 * @brief     Persistent storage of flow balance and travel-time analyses.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "output/tracedata.h"
#include <atomic>
namespace openswmmvis::trace
{
class AnalysisStore
{
public:
    // Every call owns/closes its dataset on the calling thread. No GDAL handle
    // is transferred to a worker or retained by a map layer.
    /**
     * @brief Create a schema-two GeoPackage of immutable hydraulic averages and run provenance.
     * @param[in] path Package destination; existing files are accepted only when run ID, content fingerprint, and model fingerprint match.
     * @param[in] data Valid snapshot and aligned SI native node/link averages.
     * @param[out] error Optional failure diagnostic; not cleared on entry or success. An existing-package identity mismatch can return false without a new diagnostic.
     * @param[in] cancel Optional atomic cancellation flag checked during row writes and before commit.
     * @return True for a compatible existing package or after staging/transaction commit and rename; false removes newly staged files on caught failure.
     * @details Owns/closes GDAL handles on the caller thread. Creates parent directories as needed; does not replace an incompatible existing package.
     */
    static bool prepare(const QString &path, const Dataset &data, QString *error,
                        std::atomic_bool *cancel = nullptr);
    /**
     * @brief Persist or replace one estimate and its geometry/summary/diagnostic tables.
     * @param[in] path Existing writable package with matching run ID and fingerprint.
     * @param[in] r Result with non-null aligned dataset, valid in-range seed, and UUID-like lowercase hex analysis ID.
     * @param[out] error Optional caught-failure diagnostic, unchanged on success.
     * @param[in] cancel Optional cancellation flag checked during row writes and before commit.
     * @return True after transaction commit; false attempts rollback on failure/cancellation.
     * @details Existing rows/tables for the analysis ID are replaced; failure preserves the in-memory result for caller retry. GDAL handles never leave the calling thread.
     */
    static bool save(const QString &path, const Result &r, QString *error,
                     std::atomic_bool *cancel = nullptr);
    /**
     * @brief Restore a package topology and complete index-aligned SI averages into owning memory.
     * @param[in] path Read-only GeoPackage path.
     * @param[out] error Optional caught-failure diagnostic, unchanged on success.
     * @return Shared owning dataset, or null for unavailable/schema-invalid/incomplete or mismatched row data. Geometry comes from saved snapshot JSON, not feature geometry.
     */
    static std::shared_ptr<Dataset> readDataset(const QString &path, QString *error);
    /*! True when the package was built from a running or cancelled output. Reads
     *  only the run metadata row.
     * @param[in] path Package read independently on the caller thread.
     * @return Metadata partial flag, or false for any open/read/parse failure; false does not establish package validity.
     */
    static bool isPartial(const QString &path);
    /**
     * @brief Enumerate saved estimate recipes, styles, and summaries without reading every spatial result.
     * @param[in] path Package opened read-only and closed before return.
     * @param[out] error Optional failure diagnostic, unchanged on success.
     * @return Owning recipe objects with IDs/style/summary; can contain rows read before a later caught failure.
     */
    static QVector<QJsonObject> analyses(const QString &path, QString *error);
    /**
     * @brief Restore a saved estimate, first loading its complete dataset averages.
     * @param[in] path Package path.
     * @param[in] id Saved analysis identifier.
     * @param[out] error Optional failure diagnostic, unchanged on success.
     * @return Owning shared result/dataset or null on read/validation failure.
     */
    static std::shared_ptr<Result> read(const QString &path, const QString &id, QString *error);
    // Same, reusing averages already in memory instead of reading them again.
    /**
     * @brief Restore saved estimate fields using already loaded averages.
     * @param[in] path Package opened independently read-only.
     * @param[in] id Analysis ID whose recipe and node/link tables are read.
     * @param[in] data Non-null matching immutable dataset; caller ensures it belongs to this package/run.
     * @param[out] error Optional failure diagnostic, unchanged on success.
     * @return Owning shared result referencing data, or null for missing/invalid recipe or incomplete/mismatched trace rows.
     */
    static std::shared_ptr<Result> read(const QString &path, const QString &id,
                                        std::shared_ptr<const Dataset> data, QString *error);
    /**
     * @brief Replace a saved estimate default-style JSON value.
     * @param[in] path Existing package opened for update on the caller thread.
     * @param[in] id Existing analysis ID matched exactly.
     * @param[in] style Style metadata copied without semantic validation.
     * @param[out] error Optional failure diagnostic, unchanged on success.
     * @return True after GDAL SetFeature() succeeds; false for absent ID or update/open failure. No explicit transaction or close-flush error check is performed.
     */
    static bool saveStyle(const QString &path, const QString &id, const QJsonObject &style,
                          QString *error);
};
} // namespace openswmmvis::trace
