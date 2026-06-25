/*!
 * \file   swmmraingagepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmraingagepropertyadapter.h"

#include "layers/swmmmodellayer.h"   // USER_FLAGS Phase 4 — ensureUserFlagsModel()

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>  // DA.2 parity — series id <-> index

int SWMMRainGagePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_gage_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMRainGagePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))             return tr("Name");
    if (property == QLatin1String("rainType"))         return tr("Rain Type");
    if (property == QLatin1String("rainInterval"))     return tr("Recording Interval (s)");
    if (property == QLatin1String("snowFactor"))       return tr("Snow Catch Factor");
    if (property == QLatin1String("dataSource"))       return tr("Data Source");
    if (property == QLatin1String("seriesName"))       return tr("Series Name");
    if (property == QLatin1String("currentRainfall"))  return tr("Current Rainfall");
    if (property == QLatin1String("filePath"))         return tr("Rain File (path)");
    if (property == QLatin1String("resolvedFilePath")) return tr("Rain File (resolved)");
    if (property == QLatin1String("stationId"))        return tr("Station ID");
    if (property == QLatin1String("rainUnits"))        return tr("Rain Units");
    // USER_FLAGS Phase 4.
    if (property == QLatin1String("userFlags"))        return tr("User Flags");
    return {};
}

SWMMRainGagePropertyAdapter::RainType SWMMRainGagePropertyAdapter::rainType() const
{
    const int i = idx();
    if (i < 0) return Intensity;
    int v = 0;
    swmm_gage_get_rain_type(m_engine, i, &v);
    return static_cast<RainType>(v);
}

SWMMRainGagePropertyAdapter::DataSource SWMMRainGagePropertyAdapter::dataSource() const
{
    const int i = idx();
    if (i < 0) return Timeseries;
    int v = 0;
    swmm_gage_get_data_source(m_engine, i, &v);
    return static_cast<DataSource>(v);
}

double SWMMRainGagePropertyAdapter::currentRainfall() const
{
    const int i = idx();
    if (i < 0) return 0.0;
    double v = 0.0;
    swmm_gage_get_rainfall(m_engine, i, &v);
    return v;
}

void SWMMRainGagePropertyAdapter::setRainType(int v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_rain_type(m_engine, i, v) == SWMM_OK) emit changed();
}

void SWMMRainGagePropertyAdapter::setDataSource(int v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_data_source(m_engine, i, v) == SWMM_OK) emit changed();
}

// ----------------------------------------------------------------------------
// DA.2 parity (DA-ENG-04 closed) — recording interval, snow-catch factor,
// series-name picker, station id, rain units. Reads round-trip to the engine
// via the getters added alongside this slice (openswmm_gages.h).
// ----------------------------------------------------------------------------

RainIntervalRef SWMMRainGagePropertyAdapter::rainIntervalRef() const
{
    RainIntervalRef r;
    r.engine   = m_engine;
    r.gageName = m_name;
    r.layer    = m_layer;
    const int i = idx();
    if (i >= 0) {
        double v = 0.0;
        swmm_gage_get_rain_interval(m_engine, i, &v);
        r.seconds = static_cast<int>(v + 0.5);
    }
    return r;
}

void SWMMRainGagePropertyAdapter::setRainIntervalRef(const RainIntervalRef &r)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_rain_interval(m_engine, i,
                                    static_cast<double>(r.seconds)) == SWMM_OK)
        emit changed();
}

double SWMMRainGagePropertyAdapter::snowFactor() const
{
    const int i = idx();
    if (i < 0) return 1.0;
    double v = 1.0;
    swmm_gage_get_snow_factor(m_engine, i, &v);
    return v;
}

void SWMMRainGagePropertyAdapter::setSnowFactor(double v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_snow_factor(m_engine, i, v) == SWMM_OK) emit changed();
}

DataObjectRef SWMMRainGagePropertyAdapter::seriesNameRef() const
{
    DataObjectRef r;
    r.engine = m_engine;
    r.layer  = m_layer;
    r.kind   = DataObjectRef::TimeSeries;
    const int i = idx();
    if (i >= 0) {
        char buf[256] = {};
        if (swmm_gage_get_timeseries(m_engine, i, buf, sizeof(buf)) == SWMM_OK)
            r.currentName = QString::fromUtf8(buf);
    }
    return r;
}

void SWMMRainGagePropertyAdapter::setSeriesNameRef(const DataObjectRef &r)
{
    const int i = idx();
    if (i < 0 || r.currentName.isEmpty()) return;
    if (swmm_gage_set_timeseries(m_engine, i, r.currentName.toUtf8().constData()) == SWMM_OK)
        emit changed();
}

QString SWMMRainGagePropertyAdapter::stationId() const
{
    const int i = idx();
    if (i < 0) return {};
    char buf[256] = {};
    if (swmm_gage_get_station_id(m_engine, i, buf, sizeof(buf)) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

void SWMMRainGagePropertyAdapter::setStationId(const QString &s)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_station_id(m_engine, i, s.toUtf8().constData()) == SWMM_OK)
        emit changed();
}

SWMMRainGagePropertyAdapter::RainUnits SWMMRainGagePropertyAdapter::rainUnits() const
{
    const int i = idx();
    if (i < 0) return Inches;
    int v = 0;
    swmm_gage_get_rain_units(m_engine, i, &v);
    return static_cast<RainUnits>(v);
}

void SWMMRainGagePropertyAdapter::setRainUnits(int v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_rain_units(m_engine, i, v) == SWMM_OK) emit changed();
}

// ----------------------------------------------------------------------------
// Slice IO-11e — external rain-file path. Reads / writes go through the
// typed C-API from Slice IO-9 so the engine slot's .original / .absolute
// pair stays consistent (FilePathPair::operator= clears the cached
// resolution on a new write).
// ----------------------------------------------------------------------------

QString SWMMRainGagePropertyAdapter::filePath() const
{
    if (!m_engine || m_name.isEmpty()) return {};
    char abs[1024] = {};
    char orig[1024] = {};
    if (swmm_file_path_get(m_engine, SWMM_FILE_RAINGAGE_DATA,
                            m_name.toUtf8().constData(),
                            abs, sizeof(abs),
                            orig, sizeof(orig)) != SWMM_OK)
        return {};
    return QString::fromUtf8(orig);
}

QString SWMMRainGagePropertyAdapter::resolvedFilePath() const
{
    if (!m_engine || m_name.isEmpty()) return {};
    char abs[1024] = {};
    char orig[1024] = {};
    if (swmm_file_path_get(m_engine, SWMM_FILE_RAINGAGE_DATA,
                            m_name.toUtf8().constData(),
                            abs, sizeof(abs),
                            orig, sizeof(orig)) != SWMM_OK)
        return {};
    return QString::fromUtf8(abs);
}

void SWMMRainGagePropertyAdapter::setFilePath(const QString &p)
{
    if (!m_engine || m_name.isEmpty()) return;
    if (swmm_file_path_set(m_engine, SWMM_FILE_RAINGAGE_DATA,
                            m_name.toUtf8().constData(),
                            p.toUtf8().constData()) == SWMM_OK)
        emit changed();
}

UserFlagsEditRef SWMMRainGagePropertyAdapter::userFlagsRef() const
{
    UserFlagsEditRef r;
    r.objectType = QStringLiteral("GAGE");
    r.objectName = name();
    r.model      = modelLayer() ? modelLayer()->ensureUserFlagsModel()
                                : nullptr;
    r.summary    = userFlagsSummaryFor(r.model, r.objectType, r.objectName);
    return r;
}
