/*!
 * \file   swmmnodepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmnodepropertyadapter.h"

#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_nodes.h>

SWMMNodePropertyAdapter::SWMMNodePropertyAdapter(SWMM_Engine engine,
                                                   QString name,
                                                   QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_name(std::move(name))
{
    // Forward UnitSystem switches so the Property Browser can repaint
    // length / area / rate suffixes ("(ft)" ↔ "(m)") in place.
    if (auto *u = UnitSystem::instance())
    {
        connect(u, &UnitSystem::unitsChanged,
                this, [this]{ emit displayLabelsChanged(); });
    }
}

QString SWMMNodePropertyAdapter::displayLabelFor(const QString &property) const
{
    auto *u = UnitSystem::instance();
    const QString L  = u ? u->lengthLabel()                        : QStringLiteral("ft");
    const QString L2 = L + QStringLiteral("²");               // ft² / m²
    const QString R  = (u && u->isSI()) ? QStringLiteral("mm/hr")
                                        : QStringLiteral("in/hr");

    // Base — common to every node type.
    if (property == QLatin1String("name"))           return tr("Name");
    if (property == QLatin1String("nodeKind"))       return tr("Node Type");
    if (property == QLatin1String("invertElev"))     return tr("Invert Elev. (%1)").arg(L);

    // Junction / Storage / Divider depth columns.
    if (property == QLatin1String("maxDepth"))       return tr("Max Depth (%1)").arg(L);
    if (property == QLatin1String("initialDepth"))   return tr("Initial Depth (%1)").arg(L);
    if (property == QLatin1String("surchargeDepth")) return tr("Surcharge Depth (%1)").arg(L);
    if (property == QLatin1String("pondedArea"))     return tr("Ponded Area (%1)").arg(L2);
    if (property == QLatin1String("seepRate"))       return tr("Seepage Rate (%1)").arg(R);

    // Outfall.
    if (property == QLatin1String("outfallType"))     return tr("Outfall Type");
    if (property == QLatin1String("outfallFlapGate")) return tr("Flap Gate");

    // Divider.
    if (property == QLatin1String("dividerType"))     return tr("Divider Type");

    return {};
}

int SWMMNodePropertyAdapter::nodeIdx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_node_index(m_engine, m_name.toUtf8().constData());
}

SWMMNodePropertyAdapter::NodeKind SWMMNodePropertyAdapter::nodeKind() const
{
    const int idx = nodeIdx();
    if (idx < 0) return Junction;
    int t = 0;
    if (swmm_node_get_type(m_engine, idx, &t) != SWMM_OK) return Junction;
    return static_cast<NodeKind>(t);
}

SWMMNodePropertyAdapter::OutfallType SWMMNodePropertyAdapter::outfallType() const
{
    const int idx = nodeIdx();
    if (idx < 0) return FREE;
    int t = 0;
    if (swmm_node_get_outfall_type(m_engine, idx, &t) != SWMM_OK) return FREE;
    return static_cast<OutfallType>(t);
}

SWMMNodePropertyAdapter::FlapGate SWMMNodePropertyAdapter::outfallFlapGate() const
{
    const int idx = nodeIdx();
    if (idx < 0) return NO;
    int v = 0;
    if (swmm_node_get_outfall_flap_gate(m_engine, idx, &v) != SWMM_OK) return NO;
    return v ? YES : NO;
}

SWMMNodePropertyAdapter::DividerType SWMMNodePropertyAdapter::dividerType() const
{
    const int idx = nodeIdx();
    if (idx < 0) return CUTOFF;
    int t = 0;
    if (swmm_node_get_divider_type(m_engine, idx, &t) != SWMM_OK) return CUTOFF;
    return static_cast<DividerType>(t);
}

void SWMMNodePropertyAdapter::setName(const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == m_name) return;
    // The attribute panel listens and calls SWMMModelLayer::applyRename() which
    // does the engine rename + cache rebuild. We emit the old name so the
    // panel can locate and update the adapter's m_name on success.
    emit renameRequested(m_name, trimmed);
}

void SWMMNodePropertyAdapter::setOutfallType(OutfallType v)
{
    const int idx = nodeIdx();
    if (idx < 0) return;
    if (swmm_node_set_outfall_type(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

void SWMMNodePropertyAdapter::setOutfallFlapGate(FlapGate v)
{
    const int idx = nodeIdx();
    if (idx < 0) return;
    if (swmm_node_set_outfall_flap_gate(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

void SWMMNodePropertyAdapter::setDividerType(DividerType v)
{
    const int idx = nodeIdx();
    if (idx < 0) return;
    if (swmm_node_set_divider_type(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

#define GETTER(method, engineGet)                                   \
double SWMMNodePropertyAdapter::method() const {                    \
    const int idx = nodeIdx();                                      \
    if (idx < 0) return 0.0;                                        \
    double v = 0.0;                                                 \
    engineGet(m_engine, idx, &v);                                   \
    return v;                                                       \
}

GETTER(invertElev,     swmm_node_get_invert_elev)
GETTER(maxDepth,       swmm_node_get_max_depth)
GETTER(initialDepth,   swmm_node_get_initial_depth)
GETTER(surchargeDepth, swmm_node_get_surcharge_depth)
GETTER(pondedArea,     swmm_node_get_ponded_area)
GETTER(seepRate,       swmm_node_get_storage_seep_rate)

#define SETTER(method, engineSet)                                   \
void SWMMNodePropertyAdapter::method(double v) {                    \
    const int idx = nodeIdx();                                      \
    if (idx < 0) return;                                            \
    if (engineSet(m_engine, idx, v) == SWMM_OK)                     \
        emit changed();                                             \
}

SETTER(setInvertElev,     swmm_node_set_invert_elev)
SETTER(setMaxDepth,       swmm_node_set_max_depth)
SETTER(setInitialDepth,   swmm_node_set_initial_depth)
SETTER(setSurchargeDepth, swmm_node_set_surcharge_depth)
SETTER(setPondedArea,     swmm_node_set_pond_area)
SETTER(setSeepRate,       swmm_node_set_storage_seep_rate)
