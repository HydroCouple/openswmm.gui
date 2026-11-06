/**
 * @file timeseriesrunlayer.h
 * @brief Cancellable, bounded observation queries for the existing comparison plot.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#pragma once
#include "plot/irunlayer.h"
#include "timeseries/timeseriesdataset.h"
#include <QObject>
#include <QHash>
#include <QTimer>
#include <deque>
#include <memory>
#include <thread>

namespace openswmmvis::plot
{
/** @brief Immutable dataset adapter; GUI calls never perform observation database reads.
 * @details Workers own their database handles. Published display buffers are capped at 12000
 * points per series; exact summaries are computed separately from all selected samples. Raw
 * source times remain in the dataset. Calendar dates use an explicitly labelled calendar axis,
 * without suggesting midnight UTC observations. No model clock or fit pairing is inferred. */
class TimeSeriesRunLayer final : public QObject, public IRunLayer
{
    Q_OBJECT
public:
    /** @param[in] dataset Immutable validated collection; no plugin lifetime is retained. */
    explicit TimeSeriesRunLayer(std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> dataset);
    /** @brief Cancel queued work and join the current bounded-reader worker. */
    ~TimeSeriesRunLayer() override;
    /** @return Collection display title, empty without metadata. */
    QString scenarioName() const override;
    /** @return Legacy family placeholder; native descriptor units always override it. */
    UnitSystem unitSystem() const override { return UnitSystem::SI; }
    /** @return First series display start, or NaN for no series. */
    double startDateJulian() const override;
    /** @return Largest series sample count, clamped to the legacy interface integer range. */
    int periodCount() const override;
    /** @return Zero: no uniform sampling interval is inferred. */
    int reportStepSeconds() const override { return 0; }
    /** @return Native dataset identity URI; never a model/run filename. */
    QString persistenceKey() const override;
    /** @return False: native observations use descriptors rather than fixed result attributes. */
    bool supportsAttribute(PlotAttribute) const override { return false; }
    /** @brief Fixed attributes are deliberately unsupported for native observations.
     * @param[in,out] out Receives the unsupported-attribute diagnostic. */
    void getSeriesAt(const ObjectRef &, PlotAttribute, SeriesData &out) const override;
    /** @brief Return a cached display window and schedule missing work without synchronous IO.
     * @param[in] ref Observed reference whose name is a stable series UUID.
     * @param[in] descriptor Exact descriptor returned by descriptorFor().
     * @param[in,out] out Receives cached values or loading/error state. */
    void getSeriesAt(const ObjectRef &ref, const ResultDescriptor &descriptor, SeriesData &out) const override;
    /** @brief Request a bounded display window for selected series.
     * @param[in] ids Stable series identifiers.
     * @param[in] from Optional UTC display coordinate; invalid requests complete coverage.
     * @param[in] to Optional inclusive display end; calendar series interpret its date fields. */
    void requestWindow(const QStringList &ids, QDateTime from = {}, QDateTime to = {});
    /** @brief Explicitly map UTC observation instants to a fixed-offset model wall clock.
     * @param[in] seconds Signed UTC offset in [-50400,50400]. Calendar dates retain their date.
     * @note Cancels stale queries and discards display caches; source files are unchanged. */
    void setModelClockOffset(int seconds);
    /** @return Whether a user supplied a model-clock mapping for comparison with results. */
    bool hasModelClockMapping() const { return m_modelClockOffset.has_value(); }
    /** @param[in] id Stable source series identity.
     * @return Observation descriptor preserving units/statistic/time basis. */
    ResultDescriptor descriptorFor(const QString &id) const;
    /** @param[in] id Stable source series identity.
     * @return Borrowed immutable metadata, null for an unknown ID. */
    const OpenSWMM::TimeSeries::SeriesInfo *series(const QString &id) const;
    /** @return Shared immutable collection used by full-resolution export. */
    auto dataset() const { return m_dataset; }
    /** @brief Build one worker-owned display window; also used by deterministic tests.
     * @param[in] dataset Source metadata; sample IO occurs on the calling thread.
     * @param[in] id Stable series identity.
     * @param[in] from Inclusive display-coordinate range start, invalid for whole coverage.
     * @param[in] to Inclusive range end.
     * @param[in] cancel Cooperative cancellation flag.
     * @return Bounded display data; never suitable for model assignment or fit metrics. */
    static SeriesData readWindow(const OpenSWMM::TimeSeries::TimeSeriesDataset &dataset,
                                 const QString &id, QDateTime from, QDateTime to,
                                 std::atomic_bool *cancel = nullptr);
    /** @brief Stream full-resolution observations to a new CSV; no display buffers are used.
     * @param[in] sources Dataset/series pairs, independent of plot windows and decimation.
     * @param[in] path Destination CSV; atomically replaced only after successful complete write.
     * @param[out] error Failure or cancellation detail.
     * @param[in] cancel Cooperative cancellation flag.
     * @return True after atomic publication. Run on a worker. */
    static bool exportCsv(const QVector<QPair<std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset>, QString>> &sources,
                          const QString &path, QString *error, std::atomic_bool *cancel = nullptr);
signals:
    /** @brief A display/statistics cache was published on the GUI thread; refresh views. */
    void dataReady();
private:
    struct Work; ///< Detached request/result storage shared only with the active worker.
    /** @brief Copied bounded query and explicit display-clock mapping. */
    struct Request {
        QString id; ///< Stable source series identity copied before worker launch.
        QDateTime from; ///< Inclusive display start; invalid means full source coverage.
        QDateTime to; ///< Inclusive display end, interpreted in the source time basis.
        int clockOffset = 0; ///< Explicit fixed UTC offset applied only to instantaneous series.
    };
    /** @brief Launch the next queued query if no worker is active; GUI thread only. */
    void startNext();
    /** @brief Publish a completed nonstale result, then advance the queue. */
    void poll();
    std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> m_dataset; ///< Immutable source metadata.
    QHash<QString, SeriesData> m_cache; ///< GUI-thread bounded display snapshots.
    std::optional<int> m_modelClockOffset; ///< Explicit plot-only model-clock interpretation.
    QHash<QString, QPair<QDateTime, QDateTime>> m_ranges; ///< Latest accepted request per identity.
    std::deque<Request> m_pending; ///< At most one queued request per source series.
    std::shared_ptr<Work> m_work; ///< Current worker-owned completion state.
    std::thread m_thread; ///< Joined before replacing state or destruction.
    QTimer m_timer; ///< GUI completion polling; workers never invoke UI callbacks.
};
}
