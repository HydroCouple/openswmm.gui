/*!
 * \file   rainfallseriesmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/rainfallseriesmodel.h"

#include "core/swmmdatetime.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>

#include <algorithm>

namespace openswmmvis::plot {

namespace {

QString gageString(SWMM_Engine e, int idx,
                   int (*getter)(SWMM_Engine, int, char *, int))
{
    char buf[512] = {};
    if (getter(e, idx, buf, int(sizeof(buf))) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

} // namespace

RainfallSeriesModel::RainfallSeriesModel(QObject *parent)
    : QObject(parent)
{
}

int RainfallSeriesModel::reload(bool reloadRainFiles)
{
    m_gages.clear();
    auto *eng = static_cast<SWMM_Engine>(m_engine);
    if (!eng) {
        emit reloaded();
        return 0;
    }

    if (reloadRainFiles) {
        // FILE-gage data is read once at open; re-read after edits — but only
        // in an editable state (the engine refuses mid-run, and the previous
        // resolved series stays valid for display).
        int st = SWMM_STATE_NONE;
        if (swmm_engine_get_state(eng, &st) == SWMM_OK
            && (st == SWMM_STATE_OPENED || st == SWMM_STATE_BUILDING))
            swmm_gage_reload_rain_files(eng);
    }

    const int n = swmm_gage_count(eng);
    m_gages.reserve(n);
    for (int i = 0; i < n; ++i) {
        RainGageRainfall g;
        g.engineIndex = i;
        if (const char *cid = swmm_gage_id(eng, i))
            g.id = QString::fromUtf8(cid);

        swmm_gage_get_data_source(eng, i, &g.dataSource);
        swmm_gage_get_rain_type(eng, i, &g.rainType);
        swmm_gage_get_file_format(eng, i, &g.fileFormat);
        swmm_gage_get_rain_interval(eng, i, &g.intervalSec);
        swmm_gage_get_scale_factor(eng, i, &g.scaleFactor);
        g.timeseriesName = gageString(eng, i, &swmm_gage_get_timeseries);
        g.stationId      = gageString(eng, i, &swmm_gage_get_station_id);
        g.fileColumn     = gageString(eng, i, &swmm_gage_get_file_column);

        int count = 0;
        if (swmm_gage_get_rainfall_series_count(eng, i, &count) == SWMM_OK
            && count > 0) {
            g.timesOA.resize(count);
            g.intensity.resize(count);
            if (swmm_gage_get_rainfall_series(eng, i, g.timesOA.data(),
                                              g.intensity.data(),
                                              count) != SWMM_OK) {
                g.timesOA.clear();
                g.intensity.clear();
            }
        }
        g.fileFailed = (g.dataSource == 1 && g.timesOA.isEmpty());

        m_gages.push_back(std::move(g));
    }

    emit reloaded();
    return static_cast<int>(m_gages.size());
}

double RainfallSeriesModel::entryDurationSecs(const RainGageRainfall &g, int i)
{
    const int n = g.timesOA.size();
    if (i < 0 || i >= n) return 0.0;
    double dur = g.intervalSec;
    if (i + 1 < n) {
        const double gapSecs = (g.timesOA[i + 1] - g.timesOA[i]) * 86400.0;
        dur = std::min(dur, gapSecs);
    }
    return std::max(dur, 0.0);
}

RainGageStats RainfallSeriesModel::computeStats(const RainGageRainfall &g)
{
    RainGageStats s;
    const int n = g.timesOA.size();
    s.sampleCount = n;
    if (n == 0) return s;

    s.first = openswmmvis::core::swmmDateTimeToQDateTime(g.timesOA.first());
    s.last  = openswmmvis::core::swmmDateTimeToQDateTime(g.timesOA.last());

    for (int i = 0; i < n; ++i) {
        const double v = g.intensity[i];
        s.totalDepth += v * entryDurationSecs(g, i) / 3600.0;
        if (v > s.peakIntensity) {
            s.peakIntensity = v;
            s.peakTime =
                openswmmvis::core::swmmDateTimeToQDateTime(g.timesOA[i]);
        }
        if (i + 1 < n) {
            const double gapSecs = (g.timesOA[i + 1] - g.timesOA[i]) * 86400.0;
            // 1.5× tolerance absorbs float noise on regularly-spaced records
            // while still catching a skipped interval.
            if (gapSecs > 1.5 * g.intervalSec) {
                ++s.gapCount;
                s.longestGapSecs =
                    std::max(s.longestGapSecs,
                             static_cast<qint64>(gapSecs - g.intervalSec));
            }
        }
    }
    return s;
}

QVector<QPointF> RainfallSeriesModel::buildStepSeries(const RainGageRainfall &g,
                                                      Basis b)
{
    QVector<QPointF> pts;
    const int n = g.timesOA.size();
    if (n == 0) return pts;
    pts.reserve(n * 3);

    auto msecs = [](double oa) {
        return static_cast<double>(
            openswmmvis::core::swmmDateTimeToQDateTime(oa).toMSecsSinceEpoch());
    };

    double cumulative = 0.0;
    for (int i = 0; i < n; ++i) {
        const double durSec = entryDurationSecs(g, i);
        const double tBeg = msecs(g.timesOA[i]);
        const double tEnd = tBeg + durSec * 1000.0;

        double y = g.intensity[i];
        if (b == Basis::DepthPerInterval) y = y * durSec / 3600.0;
        else if (b == Basis::CumulativeDepth) {
            cumulative += g.intensity[i] * durSec / 3600.0;
            y = cumulative;
        }

        pts.push_back(QPointF(tBeg, y));
        pts.push_back(QPointF(tEnd, y));

        // Explicit zero across a gap to the next entry: the engine applies
        // no rainfall between an entry's interval end and the next entry.
        if (i + 1 < n && b != Basis::CumulativeDepth) {
            const double tNext = msecs(g.timesOA[i + 1]);
            if (tNext > tEnd + 1.0) {
                pts.push_back(QPointF(tEnd, 0.0));
                pts.push_back(QPointF(tNext, 0.0));
            }
        }
    }
    if (b != Basis::CumulativeDepth)
        pts.push_back(QPointF(pts.last().x(), 0.0));
    return pts;
}

QVector<QPointF> RainfallSeriesModel::buildCumulativeSeries(
    const RainGageRainfall &g)
{
    return buildStepSeries(g, Basis::CumulativeDepth);
}

} // namespace openswmmvis::plot
