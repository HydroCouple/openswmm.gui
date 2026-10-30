/**
 * @file timeseriesstore.h
 * @brief Atomic GeoPackage import and bounded thread-local observation readers.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "timeseries/timeseriesdataset.h"
#include <atomic>
#include <memory>
#include <functional>

namespace OpenSWMM::TimeSeries
{
/** @brief Source-neutral storage; every call owns/closes GDAL handles on its calling thread. */
class TimeSeriesStore
{
public:
    /** @brief Import a local versioned JSON descriptor and its long-form CSV sources atomically.
     * @param[in] descriptorPath Local JSON schema openswmm.timeseries-import/1.
     * @param[in] destination New GeoPackage path; existing destinations are never overwritten.
     * @param[out] error Cleared on entry; explains parse/storage/cancellation failure.
     * @param[in] cancel Optional cancellation flag checked between records and before publication.
     * @param[in] cancelled Optional worker-local cancellation query; never retained after return.
     * @return True after closing and atomically publishing a complete package; false removes staging.
     * @details Run on a worker. Sources have path, columns (semantic name to CSV header),
     * defaults (semantic constants), and optional metadata. Semantic columns: siteId,
     * parameter, sourceSeriesId, statistic, time, value, unit, observationId, approval,
     * qualifier, properties, geometry. Source defaults also accept provider, collection,
     * title, locationId and timeBasis (instant/calendar-date). All files must be local.
     * Locations contain id/title/attributes and optional RFC7946 Point geometry.
     * Null CSV values remain missing. Conflicting duplicate times or units fail import.
     * A series without sourceSeriesId uses the source's explicit default or 'series'. */
    static bool importDescriptor(const QString &descriptorPath, const QString &destination,
                                 QString *error = nullptr, std::atomic_bool *cancel = nullptr,
                                 std::function<bool()> cancelled = {});

    /** @brief Read schema-validated metadata, without loading samples.
     * @param[in] path Local native package, restricted to the GeoPackage driver.
     * @param[out] error Cleared on entry; nonempty on invalid/missing/unsupported data.
     * @return Immutable metadata or null; no handles escape the calling thread.
     * @note Native layers open the optional spatial table named 'ts_locations'. */
    static std::shared_ptr<const TimeSeriesDataset> open(const QString &path,
                                                         QString *error = nullptr);

    /** @brief Read a bounded ordered sample page using the persistent series/time index.
     * @param[in] path Local package.
     * @param[in] seriesId Stable SeriesInfo::id; unknown identities return an empty page.
     * @param[in] query Range and exclusive cursor, with 1..10000 returned rows.
     * @param[out] error Cleared on entry; distinguishes a failed read from an empty page.
     * @param[in] cancel Optional cooperative cancellation flag.
     * @param[in] cancelled Optional worker-local cancellation query; never retained after return.
     * @return Owning page; no database handle or borrowed metadata escapes.
     * @note Call from a worker for plotting/conversion; each invocation uses its own handle. */
    static ObservationPage readPage(const QString &path, const QString &seriesId,
                                    const ObservationQuery &query = {}, QString *error = nullptr,
                                    std::atomic_bool *cancel = nullptr,
                                    std::function<bool()> cancelled = {});
};
}
