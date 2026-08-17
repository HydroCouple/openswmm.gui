/*!
 * \file   swmmraingagepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmraingagepropertyadapter.h"

#include "layers/swmmmodellayer.h"   // USER_FLAGS Phase 4 — ensureUserFlagsModel()
#include "ui/util/externalcolumnfile.h"  // column reconciliation after a path edit

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
    if (property == QLatin1String("snowFactor"))       return tr("Snow Catch Factor (SCF)");
    if (property == QLatin1String("scaleFactor"))      return tr("Rainfall Scale Factor");
    if (property == QLatin1String("dataSource"))       return tr("Data Source");
    if (property == QLatin1String("seriesName"))       return tr("Series Name");
    if (property == QLatin1String("currentRainfall"))  return tr("Current Rainfall");
    if (property == QLatin1String("filePath"))         return tr("Rain File (path)");
    if (property == QLatin1String("resolvedFilePath")) return tr("Rain File (resolved)");
    if (property == QLatin1String("fileColumn"))       return tr("Rain File Column");
    if (property == QLatin1String("fileFormat"))       return tr("Rain File Format");
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

double SWMMRainGagePropertyAdapter::scaleFactor() const
{
    const int i = idx();
    if (i < 0) return 1.0;
    double v = 1.0;
    swmm_gage_get_scale_factor(m_engine, i, &v);
    return v;
}

void SWMMRainGagePropertyAdapter::setScaleFactor(double v)
{
    // The engine rejects values <= 0. Guard here as well: the Property Browser
    // supplies a plain spin box with no min/max (unlike the attribute table,
    // whose ColumnSpec carries a range), so without this an out-of-range entry
    // would be silently discarded by the engine with no feedback.
    if (v <= 0.0) return;
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_scale_factor(m_engine, i, v) == SWMM_OK) emit changed();
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
                            p.toUtf8().constData()) != SWMM_OK)
        return;

    // The column belongs to the file, so a new file has to reconcile it: a
    // stale name from the previous file would fail the run, and leaving a
    // multi-column file unbound made the gage write the standard
    // `FILE "path" Station Units` grammar for a file that has no station
    // column (risk R3). Same rule as the timeseries dialog uses after Browse.
    // Prefer the engine's resolved absolute, but fall back to the token just
    // written: resolution happens at load/post-parse, so a path set from the
    // browser has no .absolute yet and the headers would be read from nothing.
    // Same precedence the attribute table's column options use.
    const QString onDisk = resolvedFilePath().isEmpty() ? filePath()
                                                        : resolvedFilePath();
    const QString want =
        openswmmvis::ui::reconcileColumnSelector(onDisk, fileColumn());
    if (want != fileColumn())
        swmm_gage_set_file_column(m_engine, idx(), want.toUtf8().constData());

    emit changed();
}

// ----------------------------------------------------------------------------
// Multi-column rain file (spec §4 task 4) — column selector + format probe.
// The engine stores the column separately from the path (GageData.col_name)
// and its writer composes the `FILE "path:col"` token; setting a non-empty
// column flips the gage's file format to USER_CSV (see openswmm_gages.h).
// ----------------------------------------------------------------------------

QString SWMMRainGagePropertyAdapter::fileColumn() const
{
    const int i = idx();
    if (i < 0) return {};
    char buf[256] = {};
    if (swmm_gage_get_file_column(m_engine, i, buf, sizeof(buf)) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

void SWMMRainGagePropertyAdapter::setFileColumn(const QString &c)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_file_column(m_engine, i, c.toUtf8().constData()) == SWMM_OK)
        emit changed();
}

int SWMMRainGagePropertyAdapter::fileFormat() const
{
    const int i = idx();
    if (i < 0) return -1;
    int v = -1;
    swmm_gage_get_file_format(m_engine, i, &v);
    return v;
}

SWMMRainGagePropertyAdapter::RainFileFormat
SWMMRainGagePropertyAdapter::fileFormatEnum() const
{
    const int v = fileFormat();
    if (v == MultiColumn) return MultiColumn;
    if (v == AutoDetect)  return AutoDetect;
    // Codes 0..4 are the NWS/DSI/HLY formats: never written by the engine, but
    // if one ever appears it is station-based, which is the only distinction
    // this row makes.
    return StandardRainFile;
}

void SWMMRainGagePropertyAdapter::setFileFormat(int v)
{
    const int i = idx();
    if (i < 0) return;
    // The engine clears whichever row selector does not apply (station id for
    // USER_CSV, column name for a station-based format), so both the Station ID
    // and Rain File Column rows have to re-read — which the panel does off
    // changed().
    if (swmm_gage_set_file_format(m_engine, i, v) == SWMM_OK)
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
