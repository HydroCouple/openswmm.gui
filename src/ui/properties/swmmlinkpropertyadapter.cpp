/*!
 * \file   swmmlinkpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmlinkpropertyadapter.h"

#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_tables.h>

SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter(SWMM_Engine engine,
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

QString SWMMLinkPropertyAdapter::displayLabelFor(const QString &property) const
{
    auto *u = UnitSystem::instance();
    const QString L = u ? u->lengthLabel() : QStringLiteral("ft");

    if (property == QLatin1String("name"))            return tr("Name");
    if (property == QLatin1String("linkKind"))        return tr("Link Type");
    if (property == QLatin1String("fromNode"))        return tr("From Node");
    if (property == QLatin1String("toNode"))          return tr("To Node");

    if (property == QLatin1String("length"))          return tr("Length (%1)").arg(L);
    if (property == QLatin1String("roughness"))       return tr("Manning's n");
    if (property == QLatin1String("offsetUp"))        return tr("Inlet Offset (%1)").arg(L);
    if (property == QLatin1String("offsetDn"))        return tr("Outlet Offset (%1)").arg(L);
    if (property == QLatin1String("offset"))          return tr("Offset (%1)").arg(L);
    if (property == QLatin1String("crestHeight"))     return tr("Crest Height (%1)").arg(L);
    if (property == QLatin1String("dischargeCoeff"))  return tr("Discharge Coeff.");
    if (property == QLatin1String("endContractions")) return tr("End Contractions");
    if (property == QLatin1String("flapGate"))        return tr("Flap Gate");

    if (property == QLatin1String("pumpCurve"))       return tr("Pump Curve");
    if (property == QLatin1String("initState"))       return tr("Initial Status");

    return {};
}

int SWMMLinkPropertyAdapter::linkIdx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_link_index(m_engine, m_name.toUtf8().constData());
}

SWMMLinkPropertyAdapter::LinkKind SWMMLinkPropertyAdapter::linkKind() const
{
    const int idx = linkIdx();
    if (idx < 0) return Conduit;
    int t = 0;
    if (swmm_link_get_type(m_engine, idx, &t) != SWMM_OK) return Conduit;
    return static_cast<LinkKind>(t);
}

QString SWMMLinkPropertyAdapter::fromNode() const
{
    const int idx = linkIdx();
    if (idx < 0) return {};
    int nodeIdx = -1;
    if (swmm_link_get_from_node(m_engine, idx, &nodeIdx) != SWMM_OK) return {};
    if (nodeIdx < 0) return {};
    const char *n = swmm_node_id(m_engine, nodeIdx);
    return n ? QString::fromUtf8(n) : QString();
}

QString SWMMLinkPropertyAdapter::toNode() const
{
    const int idx = linkIdx();
    if (idx < 0) return {};
    int nodeIdx = -1;
    if (swmm_link_get_to_node(m_engine, idx, &nodeIdx) != SWMM_OK) return {};
    if (nodeIdx < 0) return {};
    const char *n = swmm_node_id(m_engine, nodeIdx);
    return n ? QString::fromUtf8(n) : QString();
}

#define GETTER_D(method, engineGet)                                 \
double SWMMLinkPropertyAdapter::method() const {                    \
    const int idx = linkIdx();                                      \
    if (idx < 0) return 0.0;                                        \
    double v = 0.0;                                                 \
    engineGet(m_engine, idx, &v);                                   \
    return v;                                                       \
}
GETTER_D(length,           swmm_link_get_length)
GETTER_D(roughness,        swmm_link_get_roughness)
GETTER_D(offsetUp,         swmm_link_get_offset_up)
GETTER_D(offsetDn,         swmm_link_get_offset_dn)
GETTER_D(crestHeight,      swmm_link_get_crest_height)
GETTER_D(dischargeCoeff,   swmm_link_get_discharge_coeff)
GETTER_D(endContractions,  swmm_link_get_end_contractions)

SWMMLinkPropertyAdapter::FlapGate SWMMLinkPropertyAdapter::flapGate() const
{
    const int idx = linkIdx();
    if (idx < 0) return NO;
    int v = 0;
    if (swmm_link_get_flap_gate(m_engine, idx, &v) != SWMM_OK) return NO;
    return v ? YES : NO;
}

SWMMLinkPropertyAdapter::PumpInitState SWMMLinkPropertyAdapter::pumpInitState() const
{
    const int idx = linkIdx();
    if (idx < 0) return OFF;
    int v = 0;
    if (swmm_link_get_pump_init_state(m_engine, idx, &v) != SWMM_OK) return OFF;
    return v ? ON : OFF;
}

QString SWMMLinkPropertyAdapter::pumpCurveName() const
{
    const int idx = linkIdx();
    if (idx < 0) return {};
    int curveIdx = -1;
    if (swmm_link_get_pump_curve(m_engine, idx, &curveIdx) != SWMM_OK) return {};
    if (curveIdx < 0) return {};
    const char *n = swmm_table_id(m_engine, curveIdx);
    return n ? QString::fromUtf8(n) : QString();
}

#define SETTER_D(method, engineSet)                                 \
void SWMMLinkPropertyAdapter::method(double v) {                    \
    const int idx = linkIdx();                                      \
    if (idx < 0) return;                                            \
    if (engineSet(m_engine, idx, v) == SWMM_OK) emit changed();     \
}
SETTER_D(setLength,           swmm_link_set_length)
SETTER_D(setRoughness,        swmm_link_set_roughness)
SETTER_D(setOffsetUp,         swmm_link_set_offset_up)
SETTER_D(setOffsetDn,         swmm_link_set_offset_dn)
SETTER_D(setCrestHeight,      swmm_link_set_crest_height)
SETTER_D(setDischargeCoeff,   swmm_link_set_discharge_coeff)
SETTER_D(setEndContractions,  swmm_link_set_end_contractions)

void SWMMLinkPropertyAdapter::setName(const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == m_name) return;
    emit renameRequested(m_name, trimmed);
}

void SWMMLinkPropertyAdapter::setFlapGate(FlapGate v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_flap_gate(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setPumpInitState(PumpInitState v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_pump_init_state(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}
