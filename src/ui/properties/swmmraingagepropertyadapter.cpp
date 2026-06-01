/*!
 * \file   swmmraingagepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>

int SWMMRainGagePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_gage_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMRainGagePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))             return tr("Name");
    if (property == QLatin1String("rainType"))         return tr("Rain Type");
    if (property == QLatin1String("dataSource"))       return tr("Data Source");
    if (property == QLatin1String("currentRainfall"))  return tr("Current Rainfall");
    if (property == QLatin1String("filePath"))         return tr("Rain File (path)");
    if (property == QLatin1String("resolvedFilePath")) return tr("Rain File (resolved)");
    return {};
}

int SWMMRainGagePropertyAdapter::rainType() const
{
    const int i = idx();
    if (i < 0) return 0;
    int v = 0;
    swmm_gage_get_rain_type(m_engine, i, &v);
    return v;
}

int SWMMRainGagePropertyAdapter::dataSource() const
{
    const int i = idx();
    if (i < 0) return 0;
    int v = 0;
    swmm_gage_get_data_source(m_engine, i, &v);
    return v;
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
