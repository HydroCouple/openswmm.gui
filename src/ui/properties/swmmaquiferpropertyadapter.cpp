/*!
 * \file   swmmaquiferpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmaquiferpropertyadapter.h"

#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_subcatchments.h>

int SWMMAquiferPropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_aquifer_index(m_engine, m_name.toUtf8().constData());
}

double SWMMAquiferPropertyAdapter::param(int code) const
{
    const int i = idx();
    if (i < 0) return 0.0;
    double v = 0.0;
    swmm_aquifer_get_param(m_engine, i, code, &v);
    return v;
}

void SWMMAquiferPropertyAdapter::setParam(int code, double v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_aquifer_set_param(m_engine, i, code, v) == SWMM_OK)
        emit changed();
}

QString SWMMAquiferPropertyAdapter::displayLabelFor(const QString &property) const
{
    auto *u = UnitSystem::instance();
    const QString L = u ? u->lengthLabel() : QStringLiteral("ft");
    const QString R = (u && u->isSI()) ? QStringLiteral("mm/hr")
                                       : QStringLiteral("in/hr");
    // Tension slope is entered in rain-depth units (in or mm), not ft/m.
    const QString D = (u && u->isSI()) ? QStringLiteral("mm")
                                       : QStringLiteral("in");

    if (property == QLatin1String("name"))           return tr("Name");
    if (property == QLatin1String("porosity"))       return tr("Porosity");
    if (property == QLatin1String("wiltingPoint"))   return tr("Wilting Point");
    if (property == QLatin1String("fieldCapacity"))  return tr("Field Capacity");
    if (property == QLatin1String("conductivity"))   return tr("Conductivity (%1)").arg(R);
    if (property == QLatin1String("conductSlope"))   return tr("Conductivity Slope");
    if (property == QLatin1String("tensionSlope"))   return tr("Tension Slope (%1)").arg(D);
    if (property == QLatin1String("upperEvapFrac"))  return tr("Upper Evap. Fraction");
    if (property == QLatin1String("lowerEvapDepth")) return tr("Lower Evap. Depth (%1)").arg(L);
    if (property == QLatin1String("lowerLossCoeff")) return tr("Lower GW Loss Rate (%1)").arg(R);
    if (property == QLatin1String("bottomElev"))     return tr("Bottom Elev. (%1)").arg(L);
    if (property == QLatin1String("waterTableElev")) return tr("Water Table Elev. (%1)").arg(L);
    if (property == QLatin1String("upperMoisture"))  return tr("Upper Moisture");
    if (property == QLatin1String("evapPattern"))    return tr("Upper Evap. Pattern");
    return {};
}

#define G(method, code) \
double SWMMAquiferPropertyAdapter::method() const { return param(code); }
G(porosity,       SWMM_AQUIFER_POROSITY)
G(wiltingPoint,   SWMM_AQUIFER_WILTING_POINT)
G(fieldCapacity,  SWMM_AQUIFER_FIELD_CAPACITY)
G(conductivity,   SWMM_AQUIFER_CONDUCTIVITY)
G(conductSlope,   SWMM_AQUIFER_CONDUCT_SLOPE)
G(tensionSlope,   SWMM_AQUIFER_TENSION_SLOPE)
G(upperEvapFrac,  SWMM_AQUIFER_UPPER_EVAP_FRAC)
G(lowerEvapDepth, SWMM_AQUIFER_LOWER_EVAP_DEPTH)
G(lowerLossCoeff, SWMM_AQUIFER_LOWER_LOSS_COEFF)
G(bottomElev,     SWMM_AQUIFER_BOTTOM_ELEV)
G(waterTableElev, SWMM_AQUIFER_WATER_TABLE_ELEV)
G(upperMoisture,  SWMM_AQUIFER_UPPER_MOISTURE)
#undef G

#define S(method, code) \
void SWMMAquiferPropertyAdapter::method(double v) { setParam(code, v); }
S(setPorosity,       SWMM_AQUIFER_POROSITY)
S(setWiltingPoint,   SWMM_AQUIFER_WILTING_POINT)
S(setFieldCapacity,  SWMM_AQUIFER_FIELD_CAPACITY)
S(setConductivity,   SWMM_AQUIFER_CONDUCTIVITY)
S(setConductSlope,   SWMM_AQUIFER_CONDUCT_SLOPE)
S(setTensionSlope,   SWMM_AQUIFER_TENSION_SLOPE)
S(setUpperEvapFrac,  SWMM_AQUIFER_UPPER_EVAP_FRAC)
S(setLowerEvapDepth, SWMM_AQUIFER_LOWER_EVAP_DEPTH)
S(setLowerLossCoeff, SWMM_AQUIFER_LOWER_LOSS_COEFF)
S(setBottomElev,     SWMM_AQUIFER_BOTTOM_ELEV)
S(setWaterTableElev, SWMM_AQUIFER_WATER_TABLE_ELEV)
S(setUpperMoisture,  SWMM_AQUIFER_UPPER_MOISTURE)
#undef S

QString SWMMAquiferPropertyAdapter::evapPattern() const
{
    const int i = idx();
    if (i < 0) return {};
    char buf[256] = {};
    if (swmm_aquifer_get_evap_pattern(m_engine, i, buf, sizeof buf) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

DataObjectRef SWMMAquiferPropertyAdapter::evapPatternRef() const
{
    DataObjectRef r;
    r.engine      = m_engine;
    r.layer       = m_layer;
    r.kind        = DataObjectRef::Pattern;
    r.typeLock    = 0;   // MONTHLY — the only pattern type [AQUIFERS] accepts.
    r.currentName = evapPattern();
    return r;
}

void SWMMAquiferPropertyAdapter::setEvapPattern(const QString &pattern)
{
    const int i = idx();
    if (i < 0) return;
    const QByteArray utf8 = pattern.trimmed().toUtf8();
    if (swmm_aquifer_set_evap_pattern(m_engine, i,
                                      utf8.isEmpty() ? nullptr : utf8.constData())
        == SWMM_OK)
        emit changed();
}

void SWMMAquiferPropertyAdapter::setEvapPatternRef(const DataObjectRef &ref)
{
    setEvapPattern(ref.currentName);
}
