/**
 * @file timeseriesassignment.cpp
 * @brief Validated unit, temporal and quality preparation for explicit model assignment.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "timeseries/timeseriesassignment.h"
#include "timeseries/timeseriesstore.h"
#include <QJsonArray>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace OpenSWMM::TimeSeries
{
namespace
{
/** @brief Linear conversion to a canonical physical dimension, including temperature offsets. */
struct Unit { QString dimension; double scale = 1, offset = 0; };
/** @brief Normalize only known unit spelling conventions; never infer a physical quantity. */
QString normalized(QString s)
{
    return s.trimmed().toLower().replace(QChar(0x00b3), "3").replace(QChar(0x00b0), "")
        .replace('^', "").replace(' ', "");
}
/** @brief Resolve supported physical units; unknown spellings return an empty dimension. */
Unit unit(const QString &text)
{
    const QString s = normalized(text);
    if (s == "m3/s" || s == "cms") return {"flow", 1};
    if (s == "ft3/s" || s == "cfs" || s == "ft3s") return {"flow", .028316846592};
    if (s == "l/s" || s == "lps") return {"flow", .001};
    if (s == "gpm") return {"flow", .003785411784 / 60.};
    if (s == "mgd") return {"flow", 3785.411784 / 86400.};
    if (s == "mld") return {"flow", 1000. / 86400.};
    if (s == "m") return {"length", 1};
    if (s == "ft" || s == "feet") return {"length", .3048};
    if (s == "mm") return {"length", .001};
    if (s == "in" || s == "inch") return {"length", .0254};
    if (s == "mm/hr" || s == "mm/h") return {"rainrate", .001 / 3600.};
    if (s == "in/hr" || s == "in/h") return {"rainrate", .0254 / 3600.};
    if (s == "c" || s == "degc") return {"temperature", 1, 0};
    if (s == "f" || s == "degf") return {"temperature", 5. / 9., -32. * 5. / 9.};
    if (s == "k") return {"temperature", 1, -273.15};
    return {};
}
/** @brief Detect source qualifiers/provisional states requiring explicit acceptance. */
bool qualified(const Observation &v)
{
    const auto a = v.approval.trimmed().toLower();
    const auto q = v.qualifier.trimmed();
    return (!a.isEmpty() && a != "approved" && a != "a") ||
           (!q.isEmpty() && q != "[]" && q != "null" && q != "{}");
}
}
bool conversion(const QString &source, const QString &target, double &scale, double &offset)
{
    scale = 1; offset = 0;
    if (!source.trimmed().isEmpty() && normalized(source) == normalized(target)) return true;
    const auto a = unit(source), b = unit(target);
    if (a.dimension.isEmpty() || a.dimension != b.dimension) return false;
    scale = a.scale / b.scale;
    offset = (a.offset - b.offset) / b.scale;
    return true;
}
PreparedAssignment prepareAssignment(const TimeSeriesDataset &dataset, const AssignmentOptions &o,
                                      std::atomic_bool *cancel)
{
    PreparedAssignment out; out.options = o;
    auto fail = [&](QString e) { out.error = std::move(e); out.times.clear(); out.values.clear(); return out; };
    const SeriesInfo *s = nullptr;
    for (const auto &v : dataset.series) if (v.id == o.seriesId) { s = &v; break; }
    if (!s) return fail("The source series is unavailable.");
    if (cancel && cancel->load()) return fail("Preparation cancelled.");
    QString sourceError;
    const auto current = TimeSeriesStore::open(dataset.path,&sourceError);
    if (!current) return fail(sourceError);
    if (current->id != dataset.id || current->fingerprint != dataset.fingerprint)
        return fail("The source collection changed. Reload it before preparing a model copy.");
    if (o.modelSeriesName.trimmed().isEmpty()) return fail("Enter a new model time-series name.");
    if (!o.clockConfirmed || std::abs(o.utcOffsetSeconds) > 14 * 3600)
        return fail("Choose and confirm the model clock basis.");
    if (!o.unitInterpretationConfirmed) return fail("Confirm the intended model units.");
    double scale = 1, offset = 0;
    if (!conversion(s->unit, o.targetUnit, scale, offset)) return fail("Source and target units are unknown or incompatible.");
    const auto targetUnit = unit(o.targetUnit);
    if (o.target == AssignmentTarget::NodeFlow && targetUnit.dimension != "flow")
        return fail("A node FLOW inflow requires discharge units.");
    if (o.target == AssignmentTarget::OutfallStage &&
        (targetUnit.dimension != "length" || !o.datumConfirmed || !std::isfinite(o.datumOffset)))
        return fail("Review the source gage datum and specify an absolute-stage offset in model length units.");
    if (o.target == AssignmentTarget::RainGage &&
        (o.rainfallFormat < 0 || o.rainfallFormat > 2 || o.rainfallIntervalSeconds <= 0 ||
         targetUnit.dimension != (o.rainfallFormat == 0 ? "rainrate" : "length")))
        return fail("Rain-gage binding requires compatible rainfall units, format and a positive interval.");
    if (o.target != AssignmentTarget::SeriesOnly && o.targetObject.trimmed().isEmpty())
        return fail("Choose the existing receiving model object.");
    if (s->timeBasis == TimeBasis::CalendarDate && !o.dailyConstantConfirmed)
        return fail("Choose how each daily statistic will be represented in the model.");
    if (s->timeBasis == TimeBasis::CalendarDate && o.target == AssignmentTarget::RainGage)
        return fail("Daily-statistic holding is not a rainfall recording-interval conversion. Use instantaneous interval records.");
    ObservationQuery q; q.limit = 10000; q.fromKey = o.fromKey; q.toKey = o.toKey;
    q.expectedDatasetId = dataset.id;
    q.expectedFingerprint = dataset.fingerprint;
    qint64 previousKey = 0, cadence = 0; bool first = true;
    auto append = [&](QDateTime t, double v) {
        if (!t.isValid() || !std::isfinite(v) || (!out.times.isEmpty() && t <= out.times.last())) return false;
        out.times.append(t); out.values.append(v); return true;
    };
    do {
        QString error;
        const auto page = TimeSeriesStore::readPage(dataset.path, s->id, q, &error, cancel);
        if (!error.isEmpty()) return fail(error);
        for (const auto &v : page.observations) {
            if (cancel && cancel->load()) return fail("Preparation cancelled.");
            if (!v.hasValue || !std::isfinite(v.value)) return fail("Missing observations remain in the selected period. Choose a contiguous complete range.");
            if (qualified(v) && !o.allowQualified) return fail("Provisional or qualified observations require explicit acceptance.");
            if (!first) {
                const auto delta = v.timeKey - previousKey;
                if (s->timeBasis == TimeBasis::CalendarDate && delta != 1)
                    return fail("The selected daily record has a gap; choose a contiguous range.");
                if (s->timeBasis == TimeBasis::Instant) {
                    if (!cadence) cadence = delta;
                    const qint64 expected = o.expectedIntervalSeconds > 0 ? o.expectedIntervalSeconds * 1000 : cadence;
                    if (delta <= 0 || (delta != expected && !o.allowIrregularInterpolation))
                        return fail("The selected observations are irregular or contain a gap. Review the cadence and interpolation policy.");
                    if (o.expectedIntervalSeconds > 0 && delta > expected)
                        return fail("The selected record has a gap longer than the declared interval.");
                }
            }
            first = false; previousKey = v.timeKey;
            double value = v.value * scale + offset;
            if (o.target == AssignmentTarget::OutfallStage) value += o.datumOffset;
            if (o.target == AssignmentTarget::RainGage && value < 0) return fail("Rainfall cannot contain negative values.");
            QDateTime t;
            if (s->timeBasis == TimeBasis::Instant) {
                const qint64 seconds = qint64(std::llround(v.timeKey / 1000.0)) + o.utcOffsetSeconds;
                t = QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
            } else t = QDateTime(QDate::fromJulianDay(v.timeKey), QTime(0, 0), QTimeZone::UTC);
            if (!append(t, value)) return fail("Invalid or colliding model timestamps remain after second-resolution conversion.");
            if (s->timeBasis == TimeBasis::CalendarDate && !append(t.addSecs(86399), value))
                return fail("The daily model interval could not be represented.");
            if (out.times.size() > 1000000) return fail("Select a shorter period: one assignment supports at most one million prepared samples.");
        }
        if (!page.hasMore) break;
        if (page.nextKey <= q.afterKey) return fail("Observation paging did not advance.");
        q.afterKey = page.nextKey;
    } while (true);
    if (out.times.isEmpty()) return fail("The selected period has no observations.");
    out.provenance = {{"datasetId", dataset.id}, {"fingerprint", dataset.fingerprint},
                      {"sourcePath", dataset.path}, {"seriesId", s->id}, {"sourceSeriesId", s->sourceSeriesId},
                      {"parameter", s->parameter}, {"statistic", s->statistic},
                      {"sourceUnit", s->unit}, {"targetUnit", o.targetUnit}, {"scale", scale},
                      {"unitOffset", offset}, {"datumOffset", o.datumOffset},
                      {"modelSeries", o.modelSeriesName}, {"targetKind", int(o.target)},
                      {"targetObject", o.targetObject}, {"clockOffsetSeconds", o.utcOffsetSeconds},
                      {"allowQualified", o.allowQualified}, {"allowIrregularInterpolation", o.allowIrregularInterpolation},
                      {"firstModelTime", out.times.first().toString(Qt::ISODate)},
                      {"lastModelTime", out.times.last().toString(Qt::ISODate)}, {"points", out.times.size()},
                      {"dailyInterpretation", s->timeBasis == TimeBasis::CalendarDate ? "calendar-day hold with one-second boundary transition" : "none"}};
    return out;
}
}
