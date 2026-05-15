/*!
 * \file   swmmsubcatchpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmsubcatchpropertyadapter.h"

#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_subcatchments.h>

SWMMSubcatchPropertyAdapter::SWMMSubcatchPropertyAdapter(SWMM_Engine engine,
                                                           QString name,
                                                           QObject *parent)
    : QObject(parent), m_engine(engine), m_name(std::move(name))
{
    if (auto *u = UnitSystem::instance())
    {
        connect(u, &UnitSystem::unitsChanged,
                this, [this]{ emit displayLabelsChanged(); });
    }
}

QString SWMMSubcatchPropertyAdapter::displayLabelFor(const QString &property) const
{
    auto *u = UnitSystem::instance();
    const QString L = u ? u->lengthLabel()                : QStringLiteral("ft");
    const QString A = u ? u->areaLabel()                  : QStringLiteral("ac");
    const QString D = (u && u->isSI()) ? QStringLiteral("mm")
                                       : QStringLiteral("in");

    if (property == QLatin1String("name"))      return tr("Name");
    if (property == QLatin1String("area"))      return tr("Area (%1)").arg(A);
    if (property == QLatin1String("width"))     return tr("Width (%1)").arg(L);
    if (property == QLatin1String("slope"))     return tr("Slope (%)");
    if (property == QLatin1String("impervPct")) return tr("% Imperv");
    if (property == QLatin1String("nImperv"))   return tr("N-Imperv");
    if (property == QLatin1String("nPerv"))     return tr("N-Perv");
    if (property == QLatin1String("dsImperv"))  return tr("Dstore-Imperv (%1)").arg(D);
    if (property == QLatin1String("dsPerv"))    return tr("Dstore-Perv (%1)").arg(D);

    return {};
}

int SWMMSubcatchPropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_subcatch_index(m_engine, m_name.toUtf8().constData());
}

#define G(method, engineGet)                                        \
double SWMMSubcatchPropertyAdapter::method() const {                \
    const int i = idx();                                            \
    if (i < 0) return 0.0;                                          \
    double v = 0.0;                                                 \
    engineGet(m_engine, i, &v);                                     \
    return v;                                                       \
}
G(area,      swmm_subcatch_get_area)
G(width,     swmm_subcatch_get_width)
G(slope,     swmm_subcatch_get_slope)
G(impervPct, swmm_subcatch_get_imperv_pct)
G(nImperv,   swmm_subcatch_get_n_imperv)
G(nPerv,     swmm_subcatch_get_n_perv)
G(dsImperv,  swmm_subcatch_get_ds_imperv)
G(dsPerv,    swmm_subcatch_get_ds_perv)

#define S(method, engineSet)                                        \
void SWMMSubcatchPropertyAdapter::method(double v) {                \
    const int i = idx();                                            \
    if (i < 0) return;                                              \
    if (engineSet(m_engine, i, v) == SWMM_OK) emit changed();       \
}
S(setArea,      swmm_subcatch_set_area)
S(setWidth,     swmm_subcatch_set_width)
S(setSlope,     swmm_subcatch_set_slope)
S(setImpervPct, swmm_subcatch_set_imperv_pct)
S(setNImperv,   swmm_subcatch_set_n_imperv)
S(setNPerv,     swmm_subcatch_set_n_perv)
S(setDsImperv,  swmm_subcatch_set_ds_imperv)
S(setDsPerv,    swmm_subcatch_set_ds_perv)
