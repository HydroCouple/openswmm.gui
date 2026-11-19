/**
 * @file timeseriesassignment.h
 * @brief Explicit, worker-safe conversion of observations into model-owned forcing samples.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#pragma once
#include "timeseries/timeseriesdataset.h"
#include <QJsonObject>
#include <atomic>

namespace OpenSWMM::TimeSeries
{
/** @brief Native model input receiving a prepared copy; source data is never modified. */
enum class AssignmentTarget { SeriesOnly, NodeFlow, OutfallStage, RainGage };
/** @brief Explicit choices collected before preparation; no engine or QObject pointers. */
struct AssignmentOptions {
    QString seriesId; ///< Source stable series ID.
    QString modelSeriesName; ///< New model identifier; replacement is deliberately unsupported.
    AssignmentTarget target = AssignmentTarget::SeriesOnly; ///< Create-only or explicitly chosen input binding.
    QString targetObject; ///< Existing model node/outfall/gage name when binding.
    QString targetUnit; ///< Actual intended model units, never a display-only relabel.
    bool clockConfirmed = false; ///< User explicitly selected the model clock interpretation.
    int utcOffsetSeconds = 0; ///< Model wall clock = UTC instant plus this fixed offset.
    bool dailyConstantConfirmed = false; ///< Hold daily statistic over each model calendar day.
    bool allowQualified = false; ///< Explicitly accept provisional/qualified source values.
    bool allowIrregularInterpolation = false; ///< Explicit linear interpolation of irregular finite samples.
    qint64 expectedIntervalSeconds = 0; ///< Positive declared continuous cadence; reject larger gaps.
    bool unitInterpretationConfirmed = false; ///< Explicit approval of source/native unit interpretation.
    bool datumConfirmed = false; ///< Outfall absolute-stage conversion was explicitly reviewed.
    double datumOffset = 0; ///< Added after conversion, in target stage units.
    int rainfallFormat = 0; ///< Engine intensity=0, volume=1, cumulative=2.
    int rainfallIntervalSeconds = 0; ///< Required positive rain-gage recording interval.
    qint64 fromKey = std::numeric_limits<qint64>::min(); ///< Inclusive source-basis bound.
    qint64 toKey = std::numeric_limits<qint64>::max(); ///< Inclusive source-basis bound.
};
/** @brief Fully validated copy ready for one checked GUI-thread transaction. */
struct PreparedAssignment {
    AssignmentOptions options; ///< Exact reviewed transformation and binding choices used for these samples.
    QVector<QDateTime> times; ///< Strictly increasing model wall-clock timestamps, tagged UTC.
    QVector<double> values; ///< Finite target-unit values parallel to times.
    QJsonObject provenance; ///< Source fingerprint, selected interpretation and transformation.
    QString error; ///< Nonempty means no model mutation is permitted.
    /** @return True when preparation succeeded with nonempty parallel sample vectors. */
    bool ok() const { return error.isEmpty() && !times.isEmpty() && times.size() == values.size(); }
};
/** @brief Resolve a known physical-unit conversion without changing source values.
 * @param[in] source Native source unit spelling.
 * @param[in] target Required target unit spelling.
 * @param[out] scale Multiplier applied before offset on success.
 * @param[out] offset Additive conversion term (temperature), zero otherwise.
 * @return True for compatible known units or identical nonempty unit text; false otherwise. */
bool conversion(const QString &source, const QString &target, double &scale, double &offset);
/** @brief Read and prepare one explicit contiguous source range on a worker.
 * @param[in] dataset Immutable metadata, no handles shared across threads.
 * @param[in] options User-reviewed unit/clock/gap/target choices.
 * @param[in] cancel Optional cooperative cancellation checked between samples/pages.
 * @return Complete copied values/provenance or a diagnostic; source/model files are never modified.
 * @details Missing samples, conflicting times, nonfinite values and post-rounding collisions fail.
 * At most one million prepared points are allowed per operation. Daily holding emits day-start and
 * 23:59:59 values, making the final one-second transition between days explicit in provenance. */
PreparedAssignment prepareAssignment(const TimeSeriesDataset &dataset, const AssignmentOptions &options,
                                      std::atomic_bool *cancel = nullptr);
}
