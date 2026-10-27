/**
 * @file timeseriesdataset.h
 * @brief Owning metadata and bounded sample values for native observation datasets.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <limits>

namespace OpenSWMM::TimeSeries
{
/** @brief Time interpretation; dates never implicitly become UTC instants. */
enum class TimeBasis { Instant, CalendarDate };

/** @brief One immutable monitoring location, independent of project feature links. */
struct LocationInfo {
    QString id;               ///< Stable source location identifier; never an OGR FID.
    QString title;            ///< Human-readable caption.
    bool hasGeometry = false; ///< False denotes an unlocated record.
    QPointF point;            ///< Longitude/latitude degrees when geometry exists.
    QJsonObject attributes;   ///< Original site properties and source CRS/datum descriptions.
};

/** @brief One homogeneous source series; all identifiers retain leading zeros. */
struct SeriesInfo {
    QString id;             ///< Stable deterministic series UUID within the dataset.
    QString title;          ///< Display name.
    QString provider;       ///< Source provider identifier.
    QString collection;     ///< Source collection/temporal support identifier.
    QString siteId;         ///< Original station identifier, possibly empty.
    QString parameter;      ///< Source measurement identifier, possibly empty.
    QString sourceSeriesId; ///< Original source series identifier.
    QString statistic;      ///< Distinguishes mean, maximum and other statistics.
    QString unit;           ///< Native unit text; empty means unknown.
    QString locationId;     ///< Optional LocationInfo::id; no geometry is inferred.
    TimeBasis timeBasis = TimeBasis::Instant; ///< Instant or calendar-date semantics.
    qint64 count = 0;                         ///< Total samples including nulls.
    qint64 validCount = 0;                    ///< Finite nonnull samples.
    qint64 firstKey = 0;    ///< UTC epoch milliseconds or Julian day, according to timeBasis.
    qint64 lastKey = 0;     ///< Inclusive final normalized key.
    QJsonObject attributes; ///< Additional source series metadata.
};

/** @brief One source sample with its original metadata and explicit null state. */
struct Observation {
    QString id;            ///< Original observation identifier, possibly empty.
    QString sourceTime;    ///< Original timestamp/date spelling.
    qint64 timeKey = 0;    ///< UTC epoch milliseconds or Julian day.
    bool hasValue = false; ///< False retains a missing value; never interpreted as zero.
    double value = std::numeric_limits<double>::quiet_NaN(); ///< Native value when hasValue.
    QString approval;                                        ///< Original approval state.
    QString qualifier;                                       ///< Original qualifier text/JSON.
    QJsonObject properties;                                  ///< Original source sample properties.
    QJsonObject geometry; ///< Original sample geometry, when supplied.
};

/** @brief Immutable collection metadata; reading it does not load observation rows. */
struct TimeSeriesDataset {
    QString path;                    ///< Absolute native GeoPackage path.
    QString id;                      ///< Dataset UUID validated on open.
    QString title;                   ///< Display caption.
    QString fingerprint;             ///< SHA-256 of descriptor and source bytes at import.
    QJsonObject provenance;          ///< Retrieval metadata without credentials.
    QVector<SeriesInfo> series;      ///< Bounded metadata list (maximum 100000 series).
    QVector<LocationInfo> locations; ///< Optional owned site geometries/metadata.
};

/** @brief Keyset/range sample query; all bounds use the series' native time basis. */
struct ObservationQuery {
    qint64 afterKey = std::numeric_limits<qint64>::min(); ///< Exclusive page cursor.
    qint64 fromKey = std::numeric_limits<qint64>::min();  ///< Inclusive range start.
    qint64 toKey = std::numeric_limits<qint64>::max();    ///< Inclusive range end.
    int limit = 1000;                                     ///< Requested page size; capped at 10000.
    QString expectedDatasetId;   ///< Optional captured identity; mismatches refuse the read.
    QString expectedFingerprint; ///< Optional captured source revision; mismatches refuse the read.
};

/** @brief Owning bounded page; a successful empty page is not an error. */
struct ObservationPage {
    QVector<Observation> observations;                   ///< Ascending normalized keys.
    qint64 nextKey = std::numeric_limits<qint64>::min(); ///< Last returned key or query cursor.
    bool hasMore = false; ///< Another row exists within the requested range.
};
}
