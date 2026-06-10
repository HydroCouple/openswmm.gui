/*!
 * \file   swmmsubcatchpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmsubcatchpropertyadapter.h"

#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"   // USER_FLAGS Phase 4 — ensureUserFlagsModel()

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
    if (property == QLatin1String("tag"))       return tr("Tag");
    if (property == QLatin1String("area"))      return tr("Area (%1)").arg(A);
    if (property == QLatin1String("width"))     return tr("Width (%1)").arg(L);
    if (property == QLatin1String("slope"))     return tr("Slope (%)");
    if (property == QLatin1String("impervPct")) return tr("% Imperv");
    if (property == QLatin1String("nImperv"))   return tr("N-Imperv");
    if (property == QLatin1String("nPerv"))     return tr("N-Perv");
    if (property == QLatin1String("dsImperv"))  return tr("Dstore-Imperv (%1)").arg(D);
    if (property == QLatin1String("dsPerv"))    return tr("Dstore-Perv (%1)").arg(D);
    // USER_FLAGS Phase 4.
    if (property == QLatin1String("userFlags")) return tr("User Flags");

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

void SWMMSubcatchPropertyAdapter::setName(const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == m_name) return;
    emit renameRequested(m_name, trimmed);
}

// Slice TA — `[TAGS]` accessor. Matches SWMMNodePropertyAdapter::tag /
// setTag (and SWMMLinkPropertyAdapter Slice SA) line-for-line: direct
// engine read/write, no model-layer route. Tag changes don't affect map
// symbology or attribute-table layout, so the existing `changed()` signal
// + AttributePanel.objectEdited fan-out is sufficient for two-way sync
// with the Attribute Table.
QString SWMMSubcatchPropertyAdapter::tag() const
{
    const int i = idx();
    if (i < 0) return {};
    char buf[256] = {0};
    if (swmm_subcatch_get_tag(m_engine, i, buf, sizeof(buf)) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

void SWMMSubcatchPropertyAdapter::setTag(const QString &t)
{
    const int i = idx();
    if (i < 0) return;
    const QByteArray bytes = t.toUtf8();
    if (swmm_subcatch_set_tag(m_engine, i, bytes.constData()) == SWMM_OK)
        emit changed();
}

UserFlagsEditRef SWMMSubcatchPropertyAdapter::userFlagsRef() const
{
    UserFlagsEditRef r;
    r.objectType = QStringLiteral("SUBCATCHMENT");
    r.objectName = m_name;
    r.model      = m_layer ? m_layer->ensureUserFlagsModel() : nullptr;
    r.summary    = userFlagsSummaryFor(r.model, r.objectType, r.objectName);
    return r;
}

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
