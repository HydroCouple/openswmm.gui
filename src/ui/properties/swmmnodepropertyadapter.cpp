/*!
 * \file   swmmnodepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmnodepropertyadapter.h"

#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

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

    // Coordinates (Slice DB).
    if (property == QLatin1String("xCoord"))         return tr("X Coordinate");
    if (property == QLatin1String("yCoord"))         return tr("Y Coordinate");

    // Read-only summary block (Slice DB).
    if (property == QLatin1String("crownElev"))       return tr("Crown Elev. (%1)").arg(L);
    if (property == QLatin1String("fullVolume"))      return tr("Full Volume (%1³)").arg(L);
    if (property == QLatin1String("degree"))          return tr("Connected Links");
    if (property == QLatin1String("statMaxDepth"))    return tr("Max Depth, Sim. (%1)").arg(L);
    if (property == QLatin1String("statMaxOverflow")) return tr("Max Overflow (%1)").arg(u ? u->flowUnitLabel() : QStringLiteral("CFS"));
    if (property == QLatin1String("statVolFlooded"))  return tr("Vol. Flooded (%1³)").arg(L);
    if (property == QLatin1String("statTimeFlooded")) return tr("Time Flooded (hr)");

    // Outfall.
    if (property == QLatin1String("outfallType"))     return tr("Outfall Type");
    if (property == QLatin1String("outfallFlapGate")) return tr("Flap Gate");

    // Divider.
    if (property == QLatin1String("dividerType"))     return tr("Divider Type");

    // Compound editors (Slice DB.2).
    if (property == QLatin1String("inflows"))         return tr("External Inflows");
    if (property == QLatin1String("dwf"))             return tr("Dry Weather Flow");
    if (property == QLatin1String("rdii"))            return tr("RDII");
    if (property == QLatin1String("treatment"))       return tr("Pollutant Treatment");

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
GETTER(crownElev,        swmm_node_get_crown_elev)
GETTER(fullVolume,       swmm_node_get_full_volume)
GETTER(statMaxDepth,     swmm_node_get_stat_max_depth)
GETTER(statMaxOverflow,  swmm_node_get_stat_max_overflow)
GETTER(statVolFlooded,   swmm_node_get_stat_vol_flooded)
GETTER(statTimeFlooded,  swmm_node_get_stat_time_flooded)

int SWMMNodePropertyAdapter::degree() const {
    const int idx = nodeIdx();
    if (idx < 0) return 0;
    int v = 0;
    swmm_node_get_degree(m_engine, idx, &v);
    return v;
}

double SWMMNodePropertyAdapter::xCoord() const {
    const int idx = nodeIdx();
    if (idx < 0) return 0.0;
    double x = 0.0, y = 0.0;
    swmm_spatial_get_node_coord(m_engine, idx, &x, &y);
    return x;
}

double SWMMNodePropertyAdapter::yCoord() const {
    const int idx = nodeIdx();
    if (idx < 0) return 0.0;
    double x = 0.0, y = 0.0;
    swmm_spatial_get_node_coord(m_engine, idx, &x, &y);
    return y;
}

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

void SWMMNodePropertyAdapter::setXCoord(double v) {
    const int idx = nodeIdx();
    if (idx < 0) return;
    double cx = 0.0, cy = 0.0;
    swmm_spatial_get_node_coord(m_engine, idx, &cx, &cy);
    if (v == cx) return;
    // Defer the write to AttributePanel → SWMMModelLayer::applyNodeMove so
    // the cached scene coords + attached-link bboxes refresh in lockstep
    // with the engine. A bare `swmm_spatial_set_node_coord` here would let
    // the canvas drift until the next full geometry rebuild.
    emit coordChangeRequested(v, cy);
}

void SWMMNodePropertyAdapter::setYCoord(double v) {
    const int idx = nodeIdx();
    if (idx < 0) return;
    double cx = 0.0, cy = 0.0;
    swmm_spatial_get_node_coord(m_engine, idx, &cx, &cy);
    if (v == cy) return;
    emit coordChangeRequested(cx, v);
}

// ---------------------------------------------------------------------------
// Slice DB.2 — compound-editor refs
// ---------------------------------------------------------------------------
//
// Each *Ref() returns the payload the custom `NodeCompoundEditButton`
// editor needs to (a) render its summary in the cell and (b) open the
// matching NodeCompoundEditDialog page when clicked.
//
// `summary` is computed live from engine counts (RDII gets a per-node
// filter via swmm_rdii_get iteration; Inflows + DWF only show the
// global total until `swmm_inflow_get` / `swmm_dwf_get` land via the
// AG.0 batch). Treatment has no engine API — summary stays static.

NodeCompoundEditRef SWMMNodePropertyAdapter::inflowsRef() const {
    NodeCompoundEditRef r;
    r.engine   = m_engine;
    r.nodeName = m_name;
    r.kind     = NodeCompoundEditRef::Inflows;
    if (m_engine) {
        const int total = swmm_ext_inflow_count(m_engine);
        r.summary = (total > 0)
            ? tr("model total %1 — per-node filter pending").arg(total)
            : tr("(none)");
    }
    return r;
}

NodeCompoundEditRef SWMMNodePropertyAdapter::dwfRef() const {
    NodeCompoundEditRef r;
    r.engine   = m_engine;
    r.nodeName = m_name;
    r.kind     = NodeCompoundEditRef::Dwf;
    if (m_engine) {
        const int total = swmm_dwf_count(m_engine);
        r.summary = (total > 0)
            ? tr("model total %1 — per-node filter pending").arg(total)
            : tr("(none)");
    }
    return r;
}

NodeCompoundEditRef SWMMNodePropertyAdapter::rdiiRef() const {
    NodeCompoundEditRef r;
    r.engine   = m_engine;
    r.nodeName = m_name;
    r.kind     = NodeCompoundEditRef::Rdii;
    const int idx = nodeIdx();
    if (m_engine && idx >= 0) {
        const int total = swmm_rdii_count(m_engine);
        int matched = 0;
        char uhBuf[128];
        for (int i = 0; i < total; ++i) {
            int ni = -1; double area = 0.0;
            if (swmm_rdii_get(m_engine, i, &ni, uhBuf,
                               sizeof(uhBuf), &area) != SWMM_OK) continue;
            if (ni == idx) ++matched;
        }
        r.summary = (matched > 0)
            ? tr("%1 entries").arg(matched)
            : tr("(none)");
    }
    return r;
}

NodeCompoundEditRef SWMMNodePropertyAdapter::treatmentRef() const {
    NodeCompoundEditRef r;
    r.engine   = m_engine;
    r.nodeName = m_name;
    r.kind     = NodeCompoundEditRef::Treatment;
    r.summary  = tr("engine API pending");
    return r;
}
