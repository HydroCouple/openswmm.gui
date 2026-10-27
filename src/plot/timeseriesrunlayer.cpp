/**
 * @file timeseriesrunlayer.cpp
 * @brief Worker-owned native observation reads and bounded comparison-plot caches.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "plot/timeseriesrunlayer.h"
#include "timeseries/timeseriesstore.h"
#include "core/swmmdatetime.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QTimeZone>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::plot
{
using namespace OpenSWMM::TimeSeries;
namespace
{
/** @brief Resolve one stable identity in immutable metadata; no observation IO. */
const SeriesInfo *findSeries(const TimeSeriesDataset &d, const QString &id)
{
    for (const auto &s : d.series)
        if (s.id == id) return &s;
    return nullptr;
}
/** @brief Represent an instant or explicitly labelled calendar date on the plot coordinate axis. */
QDateTime displayTime(const SeriesInfo &s, qint64 key)
{
    return s.timeBasis == TimeBasis::Instant
               ? QDateTime::fromMSecsSinceEpoch(key, QTimeZone::UTC)
               : QDateTime(QDate::fromJulianDay(key), QTime(0, 0), QTimeZone::UTC);
}
/** @brief Convert a display bound into the source series time basis. */
qint64 sourceKey(const SeriesInfo &s, const QDateTime &t)
{
    return s.timeBasis == TimeBasis::Instant ? t.toMSecsSinceEpoch() : t.date().toJulianDay();
}
/** @brief One extrema candidate retained during streaming display reduction. */
struct Sample { qint64 key; double value; };
/** @brief Retain first/last/min/max samples in one display bucket. */
struct Bucket {
    bool used = false;
    Sample first{}, last{}, minimum{}, maximum{};
    /** @brief Update extrema and endpoints without storing intermediate samples. */
    void add(Sample s)
    {
        if (!used) { first = last = minimum = maximum = s; used = true; return; }
        last = s;
        if (s.value < minimum.value) minimum = s;
        if (s.value > maximum.value) maximum = s;
    }
};
}
/** @brief Shared worker state; done publishes all result writes with release/acquire ordering. */
struct TimeSeriesRunLayer::Work {
    Request request; ///< Immutable query copied by the GUI thread before launching this worker.
    SeriesData data; ///< Worker-written result; read on the GUI thread only after acquiring done.
    std::atomic_bool cancel{false}; ///< Cooperative cancellation checked between database pages and samples.
    std::atomic_bool done{false}; ///< Release/acquire publication barrier for the completed result.
};
TimeSeriesRunLayer::TimeSeriesRunLayer(std::shared_ptr<const TimeSeriesDataset> dataset)
    : m_dataset(std::move(dataset))
{
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &TimeSeriesRunLayer::poll);
}
TimeSeriesRunLayer::~TimeSeriesRunLayer()
{
    if (m_work) m_work->cancel = true;
    if (m_thread.joinable()) m_thread.join();
}
QString TimeSeriesRunLayer::scenarioName() const { return m_dataset ? m_dataset->title : QString(); }
QString TimeSeriesRunLayer::persistenceKey() const
{
    return m_dataset ? QStringLiteral("timeseries://") + m_dataset->id : QString();
}
const SeriesInfo *TimeSeriesRunLayer::series(const QString &id) const
{ return m_dataset ? findSeries(*m_dataset, id) : nullptr; }
double TimeSeriesRunLayer::startDateJulian() const
{
    if (!m_dataset || m_dataset->series.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    const auto &s = m_dataset->series.first();
    return core::qDateTimeToSwmmDateTime(displayTime(s, s.firstKey));
}
int TimeSeriesRunLayer::periodCount() const
{
    qint64 count = 0;
    if (m_dataset) for (const auto &s : m_dataset->series) count = std::max(count, s.count);
    return int(std::min<qint64>(count, std::numeric_limits<int>::max()));
}
ResultDescriptor TimeSeriesRunLayer::descriptorFor(const QString &id) const
{
    const auto *s = series(id);
    if (!s) return {};
    QString quantity = s->parameter == "00060" ? tr("Discharge")
                       : s->parameter == "00065" ? tr("Gage height")
                       : s->parameter == "00010" ? tr("Water temperature") : s->title;
    if (!s->statistic.isEmpty()) quantity += QStringLiteral(" — ") + s->statistic;
    quantity += s->timeBasis == TimeBasis::CalendarDate ? tr(" (calendar dates)")
                : m_modelClockOffset ? tr(" (model clock: UTC%1%2h)").arg(*m_modelClockOffset >= 0 ? "+" : "").arg(*m_modelClockOffset/3600.0)
                                     : tr(" (UTC)");
    const QJsonArray key{s->provider, s->parameter.isEmpty() ? s->id : s->parameter,
                         s->statistic, s->unit, int(s->timeBasis), m_modelClockOffset ? QString::number(*m_modelClockOffset) : QStringLiteral("unmapped")};
    return ResultDescriptor::forObservation(QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact)),
                                             quantity, s->unit.isEmpty() ? tr("unknown units") : s->unit);
}
void TimeSeriesRunLayer::getSeriesAt(const ObjectRef &, PlotAttribute, SeriesData &out) const
{
    out = {};
    out.errorMessage = tr("Choose a native observation series.");
}
void TimeSeriesRunLayer::getSeriesAt(const ObjectRef &ref, const ResultDescriptor &d, SeriesData &out) const
{
    out = {};
    if (ref.kind != ObjectRef::Kind::Observed || !series(ref.name) || d != descriptorFor(ref.name)) {
        out.errorMessage = tr("This observation series is unavailable.");
        return;
    }
    if (!m_cache.contains(ref.name)) {
        const_cast<TimeSeriesRunLayer *>(this)->requestWindow({ref.name});
        out.errorMessage = tr("Loading observations…");
        return;
    }
    out = m_cache.value(ref.name);
}
void TimeSeriesRunLayer::setModelClockOffset(int seconds)
{
    if (seconds < -50400 || seconds > 50400 || (m_modelClockOffset && *m_modelClockOffset == seconds)) return;
    if (m_work) m_work->cancel = true;
    m_pending.clear(); m_ranges.clear(); m_cache.clear();
    m_modelClockOffset = seconds;
}
void TimeSeriesRunLayer::requestWindow(const QStringList &ids, QDateTime from, QDateTime to)
{
    if (from.isValid() && to.isValid() && from > to) std::swap(from, to);
    for (const auto &id : ids) {
        if (!series(id)) continue;
        const QPair<QDateTime, QDateTime> range{from, to};
        if (m_ranges.contains(id) && m_ranges.value(id) == range) continue;
        m_ranges.insert(id, range);
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                      [&](const Request &r) { return r.id == id; }), m_pending.end());
        if (m_work && m_work->request.id == id) m_work->cancel = true;
        m_pending.push_back({id, from, to, m_modelClockOffset.value_or(0)});
    }
    startNext();
}
void TimeSeriesRunLayer::startNext()
{
    if (m_work || m_pending.empty() || !m_dataset) return;
    m_work = std::make_shared<Work>();
    m_work->request = m_pending.front(); m_pending.pop_front();
    const auto work = m_work;
    const auto dataset = m_dataset;
    m_thread = std::thread([work, dataset] {
        auto from = work->request.from, to = work->request.to;
        const auto *meta = findSeries(*dataset,work->request.id);
        const int offset = meta && meta->timeBasis == TimeBasis::Instant ? work->request.clockOffset : 0;
        if (from.isValid()) from = from.addSecs(-offset);
        if (to.isValid()) to = to.addSecs(-offset);
        work->data = readWindow(*dataset, work->request.id, from, to, &work->cancel);
        if (offset) for (auto &t : work->data.timesJulian) t += offset / 86400.0;
        work->data.statisticsFrom = work->request.from;
        work->data.statisticsTo = work->request.to;
        if (offset) work->data.displayNote += tr(" UTC instants mapped to the chosen model clock.");
        work->done.store(true, std::memory_order_release);
    });
    m_timer.start();
}
void TimeSeriesRunLayer::poll()
{
    if (!m_work || !m_work->done.load(std::memory_order_acquire)) return;
    m_thread.join();
    auto work = std::move(m_work);
    m_work.reset();
    m_timer.stop();
    if (!work->cancel && m_ranges.value(work->request.id) == qMakePair(work->request.from, work->request.to)) {
        m_cache.insert(work->request.id, std::move(work->data));
        emit dataReady();
    }
    startNext();
}
SeriesData TimeSeriesRunLayer::readWindow(const TimeSeriesDataset &dataset, const QString &id,
                                         QDateTime from, QDateTime to, std::atomic_bool *cancel)
{
    SeriesData out;
    out.allowPairing = false;
    const auto *s = findSeries(dataset, id);
    if (!s) { out.errorMessage = tr("Unknown observation series."); return out; }
    if (cancel && cancel->load()) { out.errorMessage = tr("Cancelled."); return out; }
    QString sourceError;
    const auto current = TimeSeriesStore::open(dataset.path,&sourceError);
    if (!current) { out.errorMessage = sourceError; return out; }
    if (current->id != dataset.id || current->fingerprint != dataset.fingerprint) {
        out.errorMessage = tr("The source collection changed. Reload it before plotting."); return out;
    }
    ObservationQuery query;
    query.expectedDatasetId = dataset.id;
    query.expectedFingerprint = dataset.fingerprint;
    query.fromKey = from.isValid() ? sourceKey(*s, from) : s->firstKey;
    query.toKey = to.isValid() ? sourceKey(*s, to) : s->lastKey;
    query.limit = 10000;
    constexpr int bucketCount = 3000;
    std::vector<Bucket> buckets(bucketCount);
    std::vector<double> statisticsValues;
    // At most one selected series is read at a time. Exact order statistics have a bounded
    // workspace; larger native imports still plot/export without approximate summaries.
    constexpr std::size_t maxStatisticsSamples = 1000000;
    bool statisticsBounded = true, irregular = s->timeBasis == TimeBasis::CalendarDate;
    qint64 previous = 0, step = 0, records = 0;
    bool havePrevious = false;
    do {
        QString error;
        const auto page = TimeSeriesStore::readPage(dataset.path, id, query, &error, cancel);
        if (!error.isEmpty()) { out.errorMessage = error; return out; }
        for (const auto &v : page.observations) {
            if (cancel && cancel->load()) { out.errorMessage = tr("Cancelled."); return out; }
            ++records;
            if (havePrevious) {
                const qint64 delta = v.timeKey - previous;
                if (!step) step = delta;
                else if (delta != step) irregular = true;
            }
            previous = v.timeKey; havePrevious = true;
            if (!v.hasValue || !std::isfinite(v.value)) { irregular = true; continue; }
            if (statisticsBounded) {
                if (statisticsValues.size() < maxStatisticsSamples) statisticsValues.push_back(v.value);
                else { statisticsBounded = false; statisticsValues.clear(); statisticsValues.shrink_to_fit(); }
            }
            const long double span = std::max<long double>(1, static_cast<long double>(query.toKey) - query.fromKey);
            const int bucket = std::clamp(int((static_cast<long double>(v.timeKey) - query.fromKey) / span * bucketCount), 0, bucketCount - 1);
            buckets[bucket].add({v.timeKey, v.value});
        }
        if (!page.hasMore) break;
        if (page.nextKey <= query.afterKey) { out.errorMessage = tr("Observation cursor did not advance."); return out; }
        query.afterKey = page.nextKey;
    } while (true);
    for (const auto &bucket : buckets) {
        if (!bucket.used) continue;
        std::vector<Sample> points{bucket.first, bucket.minimum, bucket.maximum, bucket.last};
        std::sort(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
        qint64 last = std::numeric_limits<qint64>::min();
        for (const auto &v : points) {
            if (v.key == last) continue;
            last = v.key;
            // Retain millisecond plotting resolution; model authoring's whole-second rounding
            // is deliberately confined to the explicit assignment preparation path.
            const auto dt = displayTime(*s, v.key);
            out.timesJulian.push_back(25569.0 + dt.toMSecsSinceEpoch() / 86400000.0);
            out.values.push_back(v.value);
        }
    }
    out.pointsOnly = irregular;
    out.displayOnly = records > qint64(out.values.size());
    out.statisticsFrom = from;
    out.statisticsTo = to;
    if (statisticsBounded) out.fullStatistics = computeStatistics(statisticsValues);
    out.displayNote = s->timeBasis == TimeBasis::CalendarDate
                          ? tr("Calendar-date statistics; markers do not represent midnight UTC measurements.")
                          : irregular ? tr("Irregular or missing observations: samples are shown without interpolated lines.")
                                      : tr("Observation time: UTC.");
    if (out.displayOnly) out.displayNote += tr(" Display reduced while preserving extrema; export retains every sample.");
    if (!statisticsBounded) out.displayNote += tr(" Exact summary exceeds the one-million-sample workspace; select a shorter window.");
    out.periodCount = int(std::min<qint64>(records, std::numeric_limits<int>::max()));
    out.ok = true;
    return out;
}
bool TimeSeriesRunLayer::exportCsv(const QVector<QPair<std::shared_ptr<const TimeSeriesDataset>, QString>> &sources,
                                  const QString &path, QString *error, std::atomic_bool *cancel)
{
    if (error) error->clear();
    // A Save As chooser can select the package itself despite its suggested CSV filter.
    // Refuse that destination before opening an output so an export cannot destroy its source.
    for (const auto &source : sources) {
        const QFileInfo destination(path);
        const QFileInfo input(source.first ? source.first->path : QString());
        if (source.first && (destination.absoluteFilePath() == input.absoluteFilePath() ||
            (!destination.canonicalFilePath().isEmpty() && destination.canonicalFilePath() == input.canonicalFilePath()))) {
            if (error) *error = tr("Choose a CSV destination different from the source collection.");
            return false;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { if (error) *error = file.errorString(); return false; }
    QTextStream stream(&file);
    stream << "dataset_id,series_id,series_name,time,value,unit,approval,qualifier,properties_json\n";
    const auto quoted = [](QString text) { return QStringLiteral("\"") + text.replace('"', "\"\"") + '"'; };
    for (const auto &source : sources) {
        if (!source.first) { if (error) *error = tr("The source dataset is unavailable."); return false; }
        QString sourceError;
        const auto current = TimeSeriesStore::open(source.first->path,&sourceError);
        if (!current) { if (error) *error = sourceError; return false; }
        if (current->id != source.first->id || current->fingerprint != source.first->fingerprint) {
            if (error) *error = tr("The source collection changed. Reload it before exporting."); return false;
        }
        const auto *series = findSeries(*source.first, source.second);
        if (!series) { if (error) *error = tr("The source series is unavailable."); return false; }
        ObservationQuery query; query.limit = 10000;
        query.expectedDatasetId = source.first->id;
        query.expectedFingerprint = source.first->fingerprint;
        do {
            QString detail;
            const auto page = TimeSeriesStore::readPage(source.first->path, source.second, query, &detail, cancel);
            if (!detail.isEmpty()) { if (error) *error = detail; return false; }
            for (const auto &v : page.observations) {
                if (cancel && cancel->load()) { if (error) *error = tr("Export cancelled."); return false; }
                stream << quoted(source.first->id) << ',' << quoted(series->id) << ',' << quoted(series->title)
                       << ',' << quoted(v.sourceTime) << ',' << (v.hasValue ? QString::number(v.value, 'g', 17) : QString())
                       << ',' << quoted(series->unit) << ',' << quoted(v.approval) << ',' << quoted(v.qualifier)
                       << ',' << quoted(QString::fromUtf8(QJsonDocument(v.properties).toJson(QJsonDocument::Compact))) << '\n';
            }
            if (stream.status() != QTextStream::Ok) { if (error) *error = tr("The CSV could not be written completely."); return false; }
            if (!page.hasMore) break;
            if (page.nextKey <= query.afterKey) { if (error) *error = tr("Observation paging did not advance."); return false; }
            query.afterKey = page.nextKey;
        } while (true);
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || (cancel && cancel->load()) || !file.commit()) {
        if (error) *error = cancel && cancel->load() ? tr("Export cancelled.") : file.errorString();
        return false;
    }
    return true;
}

}
