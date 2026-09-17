/*!
 * \file   rainfallseriesmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Data model for the Rainfall Visualization dialog.
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
 *  - FILE-gage series are windowed to the simulation window ±1 day;
 *  - a FILE gage whose file failed to load reports count 0;
 *  - `swmm_gage_reload_rain_files` must run after gage/source/date edits
 *    (editable engine states only) before re-reading.
 */
#ifndef OPENSWMMVIS_PLOT_RAINFALLSERIESMODEL_H
#define OPENSWMMVIS_PLOT_RAINFALLSERIESMODEL_H

#include <QDateTime>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace openswmmvis::plot {

/*! \brief One rain gage's metadata + resolved rainfall series. */
struct RainGageRainfall {
    QString id;
    int     engineIndex = -1;
    int     dataSource  = 0;    ///< 0 = TIMESERIES, 1 = FILE.
    int     rainType    = 0;    ///< 0 INTENSITY / 1 VOLUME / 2 CUMULATIVE (authoring info).
    int     fileFormat  = -1;   ///< 5 STAN_PRCP / 6 USER_CSV / -1 n.a.
    double  intervalSec = 3600.0;
    double  scaleFactor = 1.0;
    QString timeseriesName;     ///< TIMESERIES source only.
    QString stationId;          ///< STAN_PRCP files only.
    QString fileColumn;         ///< USER_CSV files only.
    QVector<double> timesOA;    ///< Absolute SWMM DateTimes, ascending.
    QVector<double> intensity;  ///< Resolved intensity, project rain units/hr.
    bool    fileFailed = false; ///< FILE source with an empty resolved series.

    bool hasData() const noexcept { return !timesOA.isEmpty(); }
};

/*! \brief Per-gage summary statistics (computed from the resolved series). */
struct RainGageStats {
    int       sampleCount   = 0;
    double    totalDepth    = 0.0;  ///< Project rain units.
    double    peakIntensity = 0.0;  ///< Project rain units / hr.
    QDateTime peakTime;
    QDateTime first;
    QDateTime last;
    int       gapCount      = 0;    ///< Spacings > 1.5 × recording interval.
    qint64    longestGapSecs = 0;
};

class RainfallSeriesModel : public QObject
{
    Q_OBJECT

public:
    /*! \brief Display basis for chart series built from a gage. */
    enum class Basis {
        Intensity,          ///< As resolved: units/hr, step function.
        DepthPerInterval,   ///< Depth accrued over each entry's interval.
        CumulativeDepth     ///< Running total depth.
    };
    Q_ENUM(Basis)

    explicit RainfallSeriesModel(QObject *parent = nullptr);

    /*! \brief Bind the engine handle (void* keeps the header MOC-clean;
     *  registry precedent). Does not load — call reload(). */
    void setEngine(void *engineHandle) { m_engine = engineHandle; }
    void *engine() const noexcept { return m_engine; }

    /*! \brief Re-assemble every gage's series from the engine.
     *  \param reloadRainFiles  Also re-read FILE-gage data from disk first
     *         (skipped silently unless the engine is in an editable state).
     *  \returns Number of gages assembled (0 when no engine is bound). */
    int reload(bool reloadRainFiles);

    const QVector<RainGageRainfall> &gages() const noexcept { return m_gages; }

    // ── Pure series/stats math (unit-testable without an engine) ────────────

    static RainGageStats computeStats(const RainGageRainfall &g);

    /*! \brief Step-function chart points for the engine's application
     *  contract: per entry `(t_i, v_i) (t_end, v_i)` with
     *  `t_end = min(t_i + interval, t_{i+1})`, plus explicit zero segments
     *  across gaps. X is msecsSinceEpoch (QDateTimeAxis convention). */
    static QVector<QPointF> buildStepSeries(const RainGageRainfall &g, Basis b);

    /*! \brief Cumulative-depth polyline (X msecsSinceEpoch, Y total depth). */
    static QVector<QPointF> buildCumulativeSeries(const RainGageRainfall &g);

    /*! \brief Effective accrual duration of entry \a i in seconds:
     *  min(recording interval, spacing to the next entry). */
    static double entryDurationSecs(const RainGageRainfall &g, int i);

signals:
    /*! \brief reload() finished; consumers rebuild charts/tables. */
    void reloaded();

private:
    void  *m_engine = nullptr;
    QVector<RainGageRainfall> m_gages;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_RAINFALLSERIESMODEL_H
