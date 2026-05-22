/*!
 * \file   spatialreferencesystem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#include "map/spatialreferencesystem.h"

#include <ogr_spatialref.h>
#include <ogr_api.h>

// ---------------------------------------------------------------------------
// Constructors / destructor
// ---------------------------------------------------------------------------

SpatialReferenceSystem::SpatialReferenceSystem(const QString &authName,
                                               int code,
                                               QObject *parent)
    : QObject(parent)
{
    initFromAuthCode(authName, code);
}

SpatialReferenceSystem::SpatialReferenceSystem(const QString &wktOrProj,
                                               QObject *parent)
    : QObject(parent)
{
    initFromWktOrProj(wktOrProj);
}

SpatialReferenceSystem::SpatialReferenceSystem(const SpatialReferenceSystem &other,
                                               QObject *parent)
    : QObject(parent),
      m_authName(other.m_authName),
      m_code(other.m_code),
      m_description(other.m_description)
{
    if (other.m_ogrSRS)
        m_ogrSRS = other.m_ogrSRS->Clone();
}

SpatialReferenceSystem::~SpatialReferenceSystem()
{
    delete m_ogrSRS;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SpatialReferenceSystem::initFromAuthCode(const QString &authName, int code)
{
    m_ogrSRS = new OGRSpatialReference();
    m_ogrSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    const QString importStr = QStringLiteral("%1:%2").arg(authName).arg(code);
    OGRErr err = m_ogrSRS->SetFromUserInput(importStr.toUtf8().constData());
    if (err != OGRERR_NONE)
    {
        delete m_ogrSRS;
        m_ogrSRS = nullptr;
        return;
    }

    m_authName = authName.toUpper();
    m_code     = code;

    const char *name = m_ogrSRS->GetName();
    if (name) m_description = QString::fromUtf8(name);
}

void SpatialReferenceSystem::initFromWktOrProj(const QString &wktOrProj)
{
    m_ogrSRS = new OGRSpatialReference();
    m_ogrSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRErr err = m_ogrSRS->SetFromUserInput(wktOrProj.toUtf8().constData());
    if (err != OGRERR_NONE)
    {
        delete m_ogrSRS;
        m_ogrSRS = nullptr;
        return;
    }

    const char *auth = m_ogrSRS->GetAuthorityName(nullptr);
    const char *code = m_ogrSRS->GetAuthorityCode(nullptr);
    m_authName = auth ? QString::fromUtf8(auth).toUpper() : QString();
    m_code     = code ? QString::fromUtf8(code).toInt() : -1;

    const char *name = m_ogrSRS->GetName();
    if (name) m_description = QString::fromUtf8(name);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

QString SpatialReferenceSystem::authName() const { return m_authName; }
int     SpatialReferenceSystem::code()     const { return m_code; }

QString SpatialReferenceSystem::description() const
{
    if (!m_description.isEmpty()) return m_description;
    if (m_ogrSRS)
    {
        const char *n = m_ogrSRS->GetName();
        if (n) return QString::fromUtf8(n);
    }
    return toAuthority();
}

void SpatialReferenceSystem::setDescription(const QString &description)
{
    if (m_description != description)
    {
        m_description = description;
        emit propertyChanged("description");
    }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

QString SpatialReferenceSystem::toWkt() const
{
    if (!m_ogrSRS) return {};
    char *wkt = nullptr;
    m_ogrSRS->exportToWkt(&wkt);
    QString result = wkt ? QString::fromUtf8(wkt) : QString();
    CPLFree(wkt);
    return result;
}

QString SpatialReferenceSystem::toProj4() const
{
    if (!m_ogrSRS) return {};
    char *proj = nullptr;
    m_ogrSRS->exportToProj4(&proj);
    QString result = proj ? QString::fromUtf8(proj) : QString();
    CPLFree(proj);
    return result;
}

QString SpatialReferenceSystem::toAuthority() const
{
    if (m_authName.isEmpty() || m_code < 0)
        return (m_ogrSRS && m_ogrSRS->IsLocal()) ? QStringLiteral("Local") : QString();
    return QStringLiteral("%1:%2").arg(m_authName).arg(m_code);
}

// ---------------------------------------------------------------------------
// Type predicates
// ---------------------------------------------------------------------------

bool SpatialReferenceSystem::isLocal() const
{
    return m_ogrSRS ? m_ogrSRS->IsLocal() != 0 : false;
}

bool SpatialReferenceSystem::isGeographic() const
{
    return m_ogrSRS ? m_ogrSRS->IsGeographic() != 0 : false;
}

bool SpatialReferenceSystem::isProjected() const
{
    return m_ogrSRS ? m_ogrSRS->IsProjected() != 0 : false;
}

bool SpatialReferenceSystem::equals(const SpatialReferenceSystem &other) const
{
    if (!m_ogrSRS || !other.m_ogrSRS) return false;
    return m_ogrSRS->IsSame(other.m_ogrSRS) != 0;
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

QString SpatialReferenceSystem::linearUnitsName() const
{
    if (!m_ogrSRS) return {};
    const char *unit = nullptr;
    m_ogrSRS->GetLinearUnits(&unit);
    return unit ? QString::fromUtf8(unit) : QString();
}

double SpatialReferenceSystem::linearUnitsToMetres() const
{
    if (!m_ogrSRS) return 1.0;
    return m_ogrSRS->GetLinearUnits(nullptr);
}

QString SpatialReferenceSystem::angularUnitsName() const
{
    if (!m_ogrSRS) return {};
    const char *unit = nullptr;
    m_ogrSRS->GetAngularUnits(&unit);
    return unit ? QString::fromUtf8(unit) : QString();
}

// ---------------------------------------------------------------------------
// GDAL interop
// ---------------------------------------------------------------------------

OGRSpatialReference *SpatialReferenceSystem::ogrSpatialReference() const
{
    return m_ogrSRS;
}

OGRCoordinateTransformation *
SpatialReferenceSystem::createTransformationTo(const SpatialReferenceSystem &target) const
{
    if (!m_ogrSRS || !target.m_ogrSRS) return nullptr;
    return OGRCreateCoordinateTransformation(m_ogrSRS, target.m_ogrSRS);
}

// ---------------------------------------------------------------------------
// Static factory methods
// ---------------------------------------------------------------------------

SpatialReferenceSystem *SpatialReferenceSystem::fromWktOrProj(const QString &wktOrProj,
                                                               QObject *parent)
{
    auto *srs = new SpatialReferenceSystem(wktOrProj, parent);
    if (!srs->ogrSpatialReference()) { delete srs; return nullptr; }
    return srs;
}

SpatialReferenceSystem *SpatialReferenceSystem::fromAuthCode(const QString &authName,
                                                              int code,
                                                              QObject *parent)
{
    auto *srs = new SpatialReferenceSystem(authName, code, parent);
    if (!srs->ogrSpatialReference()) { delete srs; return nullptr; }
    return srs;
}

SpatialReferenceSystem::SpatialReferenceSystem(QObject *parent, Qt::Initialization)
    : QObject(parent)
{
}

SpatialReferenceSystem *SpatialReferenceSystem::untitled(QObject *parent)
{
    auto *srs = new SpatialReferenceSystem(parent, Qt::Uninitialized);
    srs->m_ogrSRS = new OGRSpatialReference();
    srs->m_ogrSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    srs->m_ogrSRS->SetLocalCS("Untitled");
    srs->m_ogrSRS->SetLinearUnits(SRS_UL_METER, 1.0);
    srs->m_description = QStringLiteral("Untitled (Local)");
    return srs;
}

SpatialReferenceSystem *SpatialReferenceSystem::localFromMapUnits(const QString &units,
                                                                    QObject *parent)
{
    auto *srs = new SpatialReferenceSystem(parent, Qt::Uninitialized);
    srs->m_ogrSRS = new OGRSpatialReference();
    srs->m_ogrSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (units.compare(QLatin1String("FEET"), Qt::CaseInsensitive) == 0 ||
        units.compare(QLatin1String("FOOT"), Qt::CaseInsensitive) == 0) {
        srs->m_ogrSRS->SetLocalCS("Local (ft)");
        srs->m_ogrSRS->SetLinearUnits(SRS_UL_US_FOOT, 0.3048006096012192);
        srs->m_description = QStringLiteral("Local (ft)");
    } else {
        srs->m_ogrSRS->SetLocalCS("Local (m)");
        srs->m_ogrSRS->SetLinearUnits(SRS_UL_METER, 1.0);
        srs->m_description = QStringLiteral("Local (m)");
    }
    return srs;
}