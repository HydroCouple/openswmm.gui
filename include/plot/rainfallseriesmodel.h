/**
 * @file      rainfallseriesmodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Data model for the Rainfall Visualization dialog.
 *
 * Assembles every rain gage's RESOLVED rainfall series from the engine —
 * source-agnostic: inline `[TIMESERIES]` gages, standard SWMM rain files
 * (STAN_PRCP) and multi-column CSV/TSF files all come back through
 * `swmm_gage_get_rainfall_series`, which returns exactly the values the
 * runtime applies (rain-type transform, units factor and scale factor
 * already folded in — the plot cannot drift from the run).
 *
 * Engine-linked but widget-free, so the pure series/stats math is
 * unit-testable without a QApplication.
 *
 * Notes carried from the engine contract (openswmm_gages.h):
 *  - times are absolute SWMM DateTimes (days since 1899-12-30);
 *  - values are rainfall INTENSITY in the project's rain units per hour;
 *  - each entry applies from its own time until the recording interval
 *    elapses or the next entry begins, whichever comes first; zero between;
 *  - FILE-gage series include the full loaded record, independent of run dates;
 *  - a FILE gage whose file failed to load reports count 0;
 *  - `swmm_gage_reload_rain_files` must run after gage/source/date edits
 *    (editable engine states only) before re-reading.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_RAINFALLSERIESMODEL_H
#define OPENSWMMVIS_PLOT_RAINFALLSERIESMODEL_H

#include <QDateTime>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace openswmmvis::plot
{

/*! \brief One rain gage's metadata + resolved rainfall series. */
struct RainGageRainfall {
    QString id;
    int engineIndex = -1;
    int dataSource = 0;  ///< 0 = TIMESERIES, 1 = FILE.
    int rainType = 0;    ///< 0 INTENSITY / 1 VOLUME / 2 CUMULATIVE (authoring info).
    int fileFormat = -1; ///< 5 STAN_PRCP / 6 USER_CSV / -1 n.a.
    double intervalSec = 3600.0;
    double scaleFactor = 1.0;
    QString timeseriesName;    ///< TIMESERIES source only.
    QString stationId;         ///< STAN_PRCP files only.
    QString fileColumn;        ///< USER_CSV files only.
    QVector<double> timesOA;   ///< Absolute SWMM DateTimes, ascending.
    QVector<double> intensity; ///< Resolved intensity, project rain units/hr.
    bool fileFailed = false;   ///< FILE source with an empty resolved series.

    /**
     * @brief Test whether timestamp storage is nonempty.
     * @return True based on timesOA alone; does not validate matching intensity length, finite values or fileFailed.
     */
    bool hasData() const noexcept { return !timesOA.isEmpty(); }
};

/*! \brief Per-gage summary statistics (computed from the resolved series). */
struct RainGageStats {
    int sampleCount = 0;
    double totalDepth = 0.0;    ///< Project rain units.
    double peakIntensity = 0.0; ///< Project rain units / hr.
    QDateTime peakTime;
    QDateTime first;
    QDateTime last;
    int gapCount = 0; ///< Spacings > 1.5 × recording interval.
    qint64 longestGapSecs = 0;
};

class RainfallSeriesModel : public QObject
{
    Q_OBJECT

public:
    /*! \brief Display basis for chart series built from a gage. */
    enum class Basis {
        Intensity,        ///< As resolved: units/hr, step function.
        DepthPerInterval, ///< Depth accrued over each entry's interval.
        CumulativeDepth   ///< Running total depth.
    };
    Q_ENUM(Basis)

    /**
     * @brief Create an empty rainfall cache with no bound engine.
     * @param[in] parent Optional QObject owner. No engine/model is allocated.
     */
    explicit RainfallSeriesModel(QObject *parent = nullptr);

    /*! \brief Bind the engine handle (void* keeps the header MOC-clean;
     *  registry precedent). Does not load — call reload().
     * @param[in] engineHandle Borrowed SWMM_Engine erased to void*, or null to detach. Must outlive each use and obey engine access-state/thread rules.
     * @note Replaces only the pointer; does not clear cached gages, load data or emit reloaded().
     */
    void setEngine(void *engineHandle) { m_engine = engineHandle; }
    /**
     * @brief Inspect the unguarded engine binding.
     * @return Borrowed erased handle, possibly null; caller controls its lifetime.
     */
    void *engine() const noexcept { return m_engine; }

    /*! \brief Re-assemble every gage's series from the engine.
     *  \param reloadRainFiles  Also re-read FILE-gage data from disk first
     *         (skipped silently unless the engine is in an editable state).
     *  \returns Number of gages assembled (0 when no engine is bound).
     * @note Clears the prior cache first and always emits reloaded(), including an unbound engine. FILE reload is attempted only in BUILDING/OPENED and its status is ignored. Gage metadata getter failures leave field defaults; series count/data failures leave that gage empty. Empty FILE series sets fileFailed even when emptiness has a cause other than a read failure. No per-gage error list or atomic rollback is supplied.
     */
    int reload(bool reloadRainFiles);

    /**
     * @brief Access the last assembled metadata/data snapshot.
     * @return Borrowed cache reference; reload() clears/replaces contents and invalidates element references. Binding a different engine alone leaves this cache untouched.
     */
    const QVector<RainGageRainfall> &gages() const noexcept { return m_gages; }

    // ── Pure series/stats math (unit-testable without an engine) ────────────

    /**
     * @brief Integrate resolved intensities over their effective application durations.
     * @param[in] g Ascending finite SWMM-date times and parallel finite intensities; intervalSec should be finite nonnegative. Input consistency is a caller precondition.
     * @return Sample count, first/last entry instants, depth in project rain units and peak intensity in units/hour. Peak starts at zero and retains the first strictly higher sample, so all-nonpositive records have no peakTime. Gaps exceed 1.5 times the recording interval; longestGapSecs is the largest uncovered spacing minus that interval, truncated to whole seconds. Empty input returns zero statistics and invalid dates.
     */
    static RainGageStats computeStats(const RainGageRainfall &g);

    /*! \brief Chart points for the engine's application
     *  contract: per entry `(t_i, v_i) (t_end, v_i)` with
     *  `t_end = min(t_i + interval, t_{i+1})`, plus explicit zero segments
     *  across gaps. Cumulative depth rises linearly during each interval
     *  and remains constant across gaps. X is msecsSinceEpoch.
     * @param[in] g Ascending finite times and parallel intensities, with valid recording interval. No malformed/nonfinite sample filtering is performed.
     * @param[in] b Intensity uses project units/hour, DepthPerInterval uses intensity times effective duration/3600, and CumulativeDepth accumulates project rain units. Unknown enum values follow intensity behavior.
     * @return Independent epoch-millisecond polyline. Intensity/interval-depth use horizontal application segments and zero gaps longer than one millisecond, ending at zero; cumulative segments rise during application and connect gaps horizontally. Empty times return empty.
     */
    static QVector<QPointF> buildStepSeries(const RainGageRainfall &g, Basis b);

    /*! \brief Cumulative-depth polyline (X msecsSinceEpoch, Y total depth).
     * @param[in] g Same validated input preconditions as buildStepSeries().
     * @return buildStepSeries(g,Basis::CumulativeDepth), with epoch milliseconds X and accumulated project rain units Y; empty input returns empty.
     */
    static QVector<QPointF> buildCumulativeSeries(const RainGageRainfall &g);

    /*! \brief Effective accrual duration of entry \a i in seconds:
     *  min(recording interval, spacing to the next entry).
     * @param[in] g Timestamp array and recording interval; parallel intensities are not inspected.
     * @param[in] i Entry index; out-of-range returns zero.
     * @return Nonnegative minimum of recording interval and next timestamp spacing in seconds; the last entry uses its full interval. No finite-value or ordering validation occurs.
     */
    static double entryDurationSecs(const RainGageRainfall &g, int i);

signals:
    /*! \brief reload() finished; consumers rebuild charts/tables.
     * @note Emitted after the old cache is replaced, including an empty/unbound result; it does not guarantee every FILE source loaded successfully.
     */
    void reloaded();

private:
    void *m_engine = nullptr;
    QVector<RainGageRainfall> m_gages;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_RAINFALLSERIESMODEL_H
