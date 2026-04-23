/*!
 * \file   crsmanager.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#include "map/crsmanager.h"
#include "map/spatialreferencesystem.h"

#include <ogr_srs_api.h>
#include <QSettings>

namespace {
const char *osrCRSTypeToString(OSRCRSType eType)
{
    switch (eType) {
    case OSR_CRS_TYPE_GEOGRAPHIC_2D: return "Geographic 2D";
    case OSR_CRS_TYPE_GEOGRAPHIC_3D: return "Geographic 3D";
    case OSR_CRS_TYPE_GEOCENTRIC:    return "Geocentric";
    case OSR_CRS_TYPE_PROJECTED:     return "Projected";
    case OSR_CRS_TYPE_VERTICAL:      return "Vertical";
    case OSR_CRS_TYPE_COMPOUND:      return "Compound";
    case OSR_CRS_TYPE_OTHER:         return "Other";
    default:                         return "Unknown";
    }
}
} // anonymous namespace

#include <ogr_spatialref.h>
#include <gdal_priv.h>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

CRSManager &CRSManager::instance()
{
    static CRSManager s_instance;
    return s_instance;
}

CRSManager::CRSManager(QObject *parent)
    : QObject(parent)
{
    GDALAllRegister();
    initBuiltins();
    loadSettings();
}

CRSManager::~CRSManager()
{
    saveSettings();
    delete m_wgs84;
    delete m_webMercator;
}

void CRSManager::initBuiltins()
{
    m_wgs84       = new SpatialReferenceSystem("EPSG", 4326, this);
    m_webMercator = new SpatialReferenceSystem("EPSG", 3857, this);
}

// ---------------------------------------------------------------------------
// Database queries
// ---------------------------------------------------------------------------

QList<CRSInfo> CRSManager::queryDatabase(const QString &keyword,
                                          const QString &authority,
                                          bool includeDeprecated) const
{
    QList<CRSInfo> results;

    OGRSpatialReference ogrSRS;
    // Use GDAL's OGRSpatialReference::GetCRSInfoListFromDatabase
    // to enumerate all available CRSes.
    int count = 0;
    const char *authFilter = authority.isEmpty() ? nullptr
                                                 : authority.toUtf8().constData();

    OSRCRSInfo **list = OSRGetCRSInfoListFromDatabase(authFilter, nullptr, &count);

    if (!list) return results;

    const QString kwLower = keyword.toLower();

    for (int i = 0; i < count; ++i)
    {
        const OSRCRSInfo *info = list[i];
        if (!info) continue;
        if (!includeDeprecated && info->bDeprecated) continue;

        CRSInfo entry;
        entry.authName   = QString::fromUtf8(info->pszAuthName ? info->pszAuthName : "");
        entry.code       = info->pszCode ? QString::fromUtf8(info->pszCode).toInt() : -1;
        entry.name       = QString::fromUtf8(info->pszName ? info->pszName : "");
        entry.type       = QString::fromUtf8(osrCRSTypeToString(info->eType));
        entry.areaName   = QString::fromUtf8(info->pszAreaName ? info->pszAreaName : "");
        entry.deprecated = info->bDeprecated;

        if (!kwLower.isEmpty())
        {
            const bool nameMatch = entry.name.toLower().contains(kwLower);
            const bool codeMatch = QString::number(entry.code).contains(kwLower);
            if (!nameMatch && !codeMatch) continue;
        }

        results.append(entry);
    }

    OSRDestroyCRSInfoList(list);

    return results;
}

int CRSManager::totalCRSCount() const
{
    int count = 0;
    OSRCRSInfo **list = OSRGetCRSInfoListFromDatabase(nullptr, nullptr, &count);
    if (list) OSRDestroyCRSInfoList(list);
    return count;
}

QStringList CRSManager::availableAuthorities() const
{
    int count = 0;
    OSRCRSInfo **list = OSRGetCRSInfoListFromDatabase(nullptr, nullptr, &count);

    QSet<QString> seen;
    for (int i = 0; i < count && list; ++i)
    {
        if (list[i] && list[i]->pszAuthName)
            seen.insert(QString::fromUtf8(list[i]->pszAuthName));
    }

    if (list) OSRDestroyCRSInfoList(list);

    QStringList out(seen.begin(), seen.end());
    out.sort();
    return out;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

SpatialReferenceSystem *CRSManager::createFromAuthCode(const QString &authName,
                                                        int code,
                                                        QObject *parent) const
{
    return SpatialReferenceSystem::fromAuthCode(authName, code, parent);
}

SpatialReferenceSystem *CRSManager::createFromWktOrProj(const QString &wktOrProj,
                                                         QObject *parent) const
{
    return SpatialReferenceSystem::fromWktOrProj(wktOrProj, parent);
}

bool CRSManager::areSameCRS(const SpatialReferenceSystem &a,
                             const SpatialReferenceSystem &b) const
{
    return a.equals(b);
}

// ---------------------------------------------------------------------------
// Built-ins
// ---------------------------------------------------------------------------

SpatialReferenceSystem *CRSManager::wgs84() const  { return m_wgs84; }
SpatialReferenceSystem *CRSManager::webMercator() const { return m_webMercator; }

// ---------------------------------------------------------------------------
// Recent / favourites
// ---------------------------------------------------------------------------

QList<QPair<QString, int>> CRSManager::recentCRSes() const { return m_recentCRSes; }

void CRSManager::addRecentCRS(const QString &authName, int code)
{
    const QPair<QString, int> entry{authName, code};
    m_recentCRSes.removeAll(entry);
    m_recentCRSes.prepend(entry);
    while (m_recentCRSes.size() > MaxRecentCRSes)
        m_recentCRSes.removeLast();
    emit recentCRSesChanged();
}

void CRSManager::clearRecentCRSes()
{
    m_recentCRSes.clear();
    emit recentCRSesChanged();
}

void CRSManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup("CRSManager/RecentCRSes");
    const int count = settings.beginReadArray("crs");
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        const QString auth = settings.value("authName").toString();
        const int     code = settings.value("code", -1).toInt();
        if (!auth.isEmpty() && code > 0)
            m_recentCRSes.append({auth, code});
    }
    settings.endArray();
    settings.endGroup();
}

void CRSManager::saveSettings() const
{
    QSettings settings;
    settings.beginGroup("CRSManager/RecentCRSes");
    settings.beginWriteArray("crs", m_recentCRSes.size());
    for (int i = 0; i < m_recentCRSes.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("authName", m_recentCRSes[i].first);
        settings.setValue("code",     m_recentCRSes[i].second);
    }
    settings.endArray();
    settings.endGroup();
}
