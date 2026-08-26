/*!
 * \file   swmmlinkpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmlinkpropertyadapter.h"

#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"   // USER_FLAGS Phase 4 — ensureUserFlagsModel()
#include "ui/properties/xsectshapegeom.h"  // xsectGeomApplies (inline geom edits)

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
    if (property == QLatin1String("tag"))             return tr("Tag");

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
    if (property == QLatin1String("orificeType"))     return tr("Type");
    if (property == QLatin1String("weirType"))        return tr("Type");
    if (property == QLatin1String("ratingType"))      return tr("Rating Curve");
    if (property == QLatin1String("coefficient"))     return tr("Coefficient");
    if (property == QLatin1String("expon"))           return tr("Exponent");
    if (property == QLatin1String("outletCurve"))     return tr("Rating Curve");
    if (property == QLatin1String("startupDepth"))    return tr("Startup Depth (%1)").arg(L);
    if (property == QLatin1String("shutoffDepth"))    return tr("Shutoff Depth (%1)").arg(L);
    if (property == QLatin1String("openCloseRate"))   return tr("Open/Close Rate (1/s)");

    // Slice SB — scalar parity labels. `UnitSystem::flowUnitLabel()`
    // returns "CFS"/"CMS"/etc.; lowercase it for visual parity with the
    // length-unit suffix style ("(ft)") used elsewhere in the labels.
    {
        const QString F = (u ? u->flowUnitLabel()
                             : QStringLiteral("CFS")).toLower();
        if (property == QLatin1String("initialFlow"))   return tr("Initial Flow (%1)").arg(F);
        if (property == QLatin1String("maxFlow"))       return tr("Maximum Flow (%1)").arg(F);
    }
    if (property == QLatin1String("lossInlet"))       return tr("Entry Loss Coeff.");
    if (property == QLatin1String("lossOutlet"))      return tr("Exit Loss Coeff.");
    if (property == QLatin1String("lossAvg"))         return tr("Avg. Loss Coeff.");
    if (property == QLatin1String("seepRate"))        return tr("Seepage Rate (%1/hr)").arg(L);
    if (property == QLatin1String("barrels"))         return tr("Barrels");
    // Generic inline cross-section geom labels. The shape-specific meaning
    // (Diameter / Max Depth / …) is surfaced as a per-row tooltip by
    // PropertiesPanel via openswmmvis::xsectGeomLabel().
    if (property == QLatin1String("geom1"))           return tr("Geom 1 (%1)").arg(L);
    if (property == QLatin1String("geom2"))           return tr("Geom 2 (%1)").arg(L);
    if (property == QLatin1String("geom3"))           return tr("Geom 3 (%1)").arg(L);
    if (property == QLatin1String("geom4"))           return tr("Geom 4 (%1)").arg(L);
    // Slice SC.1 — compound-edit row labels.
    if (property == QLatin1String("xsection"))        return tr("Cross Section");
    if (property == QLatin1String("culvertCode"))     return tr("Culvert Code");
    if (property == QLatin1String("inletUsage"))      return tr("Inlets");
    // USER_FLAGS Phase 4.
    if (property == QLatin1String("userFlags"))       return tr("User Flags");
    // Initial-quality UI round.
    if (property == QLatin1String("initialQuality"))  return tr("Initial Quality");

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
// Slice SB — scalar parity getters. initialFlow / maxFlow rely on the
// new BN-LINK-01a/b engine getters; the others have always existed.
GETTER_D(initialFlow,      swmm_link_get_initial_flow)
GETTER_D(maxFlow,          swmm_link_get_max_flow)
GETTER_D(seepRate,         swmm_link_get_seep_rate)

// Loss coefficients share one engine call (`swmm_link_get_loss_coeff`
// returns the triple by reference). Each per-coefficient accessor pulls
// the same triple and discards the other two — slightly redundant work
// per cell read, but the engine call is O(1) and the alternative would
// require caching state on the adapter (which §S.3 / CLAUDE.md §2 say
// to avoid: every getter round-trips to the engine).
double SWMMLinkPropertyAdapter::lossInlet() const {
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double inlet = 0.0, outlet = 0.0, avg = 0.0;
    swmm_link_get_loss_coeff(m_engine, idx, &inlet, &outlet, &avg);
    return inlet;
}
double SWMMLinkPropertyAdapter::lossOutlet() const {
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double inlet = 0.0, outlet = 0.0, avg = 0.0;
    swmm_link_get_loss_coeff(m_engine, idx, &inlet, &outlet, &avg);
    return outlet;
}
double SWMMLinkPropertyAdapter::lossAvg() const {
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double inlet = 0.0, outlet = 0.0, avg = 0.0;
    swmm_link_get_loss_coeff(m_engine, idx, &inlet, &outlet, &avg);
    return avg;
}

int SWMMLinkPropertyAdapter::barrels() const {
    const int idx = linkIdx();
    if (idx < 0) return 1;
    int n = 1;
    if (swmm_link_get_barrels(m_engine, idx, &n) != SWMM_OK) return 1;
    return n;
}

// Direct inline cross-section geometry. Each getter pulls the whole xsect
// tuple and returns the requested slot (cheap O(1) engine call; same
// no-cache contract as the loss-coeff getters). `xsectShapeId` is the
// shape used by PropertiesPanel / the table to grey out inapplicable geoms.
int SWMMLinkPropertyAdapter::xsectShapeId() const {
    const int idx = linkIdx();
    if (idx < 0) return 0;
    int shape = 0;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    if (swmm_link_get_xsect(m_engine, idx, &shape, &g1, &g2, &g3, &g4) != SWMM_OK)
        return 0;
    return shape;
}
namespace {
double xsectGeomSlot(SWMM_Engine e, int idx, int ordinal) {
    if (idx < 0) return 0.0;
    int shape = 0;
    double g[4] = {0, 0, 0, 0};
    if (swmm_link_get_xsect(e, idx, &shape, &g[0], &g[1], &g[2], &g[3]) != SWMM_OK)
        return 0.0;
    return (ordinal >= 1 && ordinal <= 4) ? g[ordinal - 1] : 0.0;
}
} // namespace
double SWMMLinkPropertyAdapter::xsectGeom1() const { return xsectGeomSlot(m_engine, linkIdx(), 1); }
double SWMMLinkPropertyAdapter::xsectGeom2() const { return xsectGeomSlot(m_engine, linkIdx(), 2); }
double SWMMLinkPropertyAdapter::xsectGeom3() const { return xsectGeomSlot(m_engine, linkIdx(), 3); }
double SWMMLinkPropertyAdapter::xsectGeom4() const { return xsectGeomSlot(m_engine, linkIdx(), 4); }

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

// Slice SD partial — orifice TYPE. Engine round-trip via BN-LINK-02.
// Defaults to SIDE on read failure (e.g., link removed since adapter
// construction) — matches the FlapGate/PumpInitState fallback style.
SWMMLinkPropertyAdapter::OrificeType SWMMLinkPropertyAdapter::orificeType() const
{
    const int idx = linkIdx();
    if (idx < 0) return SIDE;
    int v = 0;
    if (swmm_link_get_orifice_type(m_engine, idx, &v) != SWMM_OK) return SIDE;
    return v ? BOTTOM : SIDE;
}

// Slice SD partial — weir TYPE. Engine round-trip via BN-LINK-03.
SWMMLinkPropertyAdapter::WeirType SWMMLinkPropertyAdapter::weirType() const
{
    const int idx = linkIdx();
    if (idx < 0) return TRANSVERSE;
    int v = 0;
    if (swmm_link_get_weir_type(m_engine, idx, &v) != SWMM_OK) return TRANSVERSE;
    if (v < TRANSVERSE || v > ROADWAY) return TRANSVERSE;
    return static_cast<WeirType>(v);
}

// Slice SD partial — outlet rating type + exponent. Engine round-trip
// via BN-LINK-04. Coefficient and tabular curve picker reuse existing
// scalar accessors (dischargeCoeff + pumpCurveRef).
SWMMLinkPropertyAdapter::OutletRatingType
SWMMLinkPropertyAdapter::outletRatingType() const
{
    const int idx = linkIdx();
    if (idx < 0) return FUNCTIONAL_HEAD;
    int v = 0;
    if (swmm_link_get_outlet_rating_type(m_engine, idx, &v) != SWMM_OK)
        return FUNCTIONAL_HEAD;
    if (v < FUNCTIONAL_HEAD || v > TABULAR_DEPTH) return FUNCTIONAL_HEAD;
    return static_cast<OutletRatingType>(v);
}

double SWMMLinkPropertyAdapter::outletExpon() const
{
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double v = 0.0;
    swmm_link_get_outlet_expon(m_engine, idx, &v);
    return v;
}

// Slice SD partial — pump startup / shutoff depth (BN-LINK-05) and
// orifice open/close rate (BN-LINK-06) accessors. Engine validates link
// kind; the adapter falls back to 0.0 on read failure (e.g., the cell
// is queried via the wrong-kind subclass — shouldn't happen in normal
// dispatch but keeps the contract safe).
double SWMMLinkPropertyAdapter::pumpStartupDepth() const
{
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double v = 0.0;
    swmm_link_get_pump_startup_depth(m_engine, idx, &v);
    return v;
}

double SWMMLinkPropertyAdapter::pumpShutoffDepth() const
{
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double v = 0.0;
    swmm_link_get_pump_shutoff_depth(m_engine, idx, &v);
    return v;
}

double SWMMLinkPropertyAdapter::orificeOpenCloseRate() const
{
    const int idx = linkIdx();
    if (idx < 0) return 0.0;
    double v = 0.0;
    swmm_link_get_orifice_open_close_rate(m_engine, idx, &v);
    return v;
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

// Slice BM.0-Browse-Edit (2026-05-25)
DataObjectRef SWMMLinkPropertyAdapter::pumpCurveRef() const
{
    DataObjectRef r;
    r.engine      = m_engine;
    r.layer       = m_layer;
    r.kind        = DataObjectRef::AnyCurve;
    r.currentName = pumpCurveName();
    return r;
}

void SWMMLinkPropertyAdapter::setPumpCurveRef(const DataObjectRef &ref)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    int curveIdx = -1;
    if (!ref.currentName.isEmpty()) {
        curveIdx = swmm_table_index(m_engine, ref.currentName.toUtf8().constData());
        if (curveIdx < 0) return;   // unknown curve — ignore the write
    }
    if (swmm_link_set_pump_curve(m_engine, idx, curveIdx) == SWMM_OK)
        emit changed();
}

// Slice SC.1 — compound-edit ref builders. Each constructs a fresh ref
// every call (no caching) so the summary reflects whatever the engine
// holds today; the cell delegate uses this to render the in-row text.
LinkCompoundEditRef SWMMLinkPropertyAdapter::xsectionRef() const
{
    LinkCompoundEditRef r;
    r.engine   = m_engine;
    r.linkName = m_name;
    r.kind     = LinkCompoundEditRef::XSection;
    r.layer    = m_layer;

    const int idx = linkIdx();
    if (idx >= 0) {
        int shape = 0;
        double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        if (swmm_link_get_xsect(m_engine, idx, &shape, &g1, &g2, &g3, &g4) == SWMM_OK) {
            QString name = openswmmvis::xsectShapeName(shape);
            if (name.isEmpty()) name = QStringLiteral("UNKNOWN");
            if (g2 > 0.0)
                r.summary = QStringLiteral("%1 (%2 × %3)")
                                .arg(name)
                                .arg(g1, 0, 'g', 4).arg(g2, 0, 'g', 4);
            else
                r.summary = QStringLiteral("%1 (%2)")
                                .arg(name)
                                .arg(g1, 0, 'g', 4);
        }
    }
    return r;
}

CulvertCodeRef SWMMLinkPropertyAdapter::culvertCodeRef() const
{
    // ATTRIBUTE_EDITOR_WIRING Phase 0 — inline combobox value (display
    // label comes from the CulvertCodeRef → QString converter, which
    // resolves the descriptive HDS-5 label via culvertCodeLabel()).
    CulvertCodeRef r;
    r.engine   = m_engine;
    r.linkName = m_name;
    r.layer    = m_layer;

    const int idx = linkIdx();
    if (idx >= 0) {
        int code = 0;
        if (swmm_link_get_culvert_code(m_engine, idx, &code) == SWMM_OK)
            r.code = code;
    }
    return r;
}

LinkCompoundEditRef SWMMLinkPropertyAdapter::inletUsageRef() const
{
    LinkCompoundEditRef r;
    r.engine   = m_engine;
    r.linkName = m_name;
    r.kind     = LinkCompoundEditRef::InletUsage;
    r.layer    = m_layer;
    // No engine accessor for inlet-usage count today (BN-LINK-11 gap).
    // Show a placeholder until Slice BO 6.5.8 wires the deep editor.
    r.summary  = tr("(engine API pending — Slice BO 6.5.8)");
    return r;
}

UserFlagsEditRef SWMMLinkPropertyAdapter::userFlagsRef() const {
    UserFlagsEditRef r;
    r.objectType = QStringLiteral("LINK");
    r.objectName = m_name;
    r.model      = m_layer ? m_layer->ensureUserFlagsModel() : nullptr;
    r.summary    = userFlagsSummaryFor(r.model, r.objectType, r.objectName);
    return r;
}

InitialQualityEditRef SWMMLinkPropertyAdapter::initialQualityRef() const {
    InitialQualityEditRef r;
    r.engine      = m_engine;
    r.isLink      = 1;
    r.elementName = m_name;
    r.summary     = initialQualitySummaryFor(m_engine, 1, m_name);
    return r;
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
// Slice SB — scalar setters. Init / max flow + seepage round-trip
// through their dedicated single-double setters.
SETTER_D(setInitialFlow,      swmm_link_set_initial_flow)
SETTER_D(setMaxFlow,          swmm_link_set_max_flow)
SETTER_D(setSeepRate,         swmm_link_set_seep_rate)

// Slice SB — loss-coefficient setters. The engine API only exposes a
// single triple-write (`swmm_link_set_loss_coeff(inlet, outlet, avg)`),
// so each per-coefficient slot reads the existing triple first then
// rewrites with the one slot replaced. From a caller's view, edits look
// independent (three Property Browser rows = three separate writes).
// The atomicity contract: each Q_PROPERTY assignment IS one engine
// triple-write, so concurrent edits in batch group-edit (Slice BU)
// can't interleave a half-applied triple.
void SWMMLinkPropertyAdapter::setLossInlet(double v) {
    const int idx = linkIdx();
    if (idx < 0) return;
    double inlet = 0.0, outlet = 0.0, avg = 0.0;
    swmm_link_get_loss_coeff(m_engine, idx, &inlet, &outlet, &avg);
    if (swmm_link_set_loss_coeff(m_engine, idx, v, outlet, avg) == SWMM_OK)
        emit changed();
}
void SWMMLinkPropertyAdapter::setLossOutlet(double v) {
    const int idx = linkIdx();
    if (idx < 0) return;
    double inlet = 0.0, outlet = 0.0, avg = 0.0;
    swmm_link_get_loss_coeff(m_engine, idx, &inlet, &outlet, &avg);
    if (swmm_link_set_loss_coeff(m_engine, idx, inlet, v, avg) == SWMM_OK)
        emit changed();
}
void SWMMLinkPropertyAdapter::setLossAvg(double v) {
    const int idx = linkIdx();
    if (idx < 0) return;
    double inlet = 0.0, outlet = 0.0, avg = 0.0;
    swmm_link_get_loss_coeff(m_engine, idx, &inlet, &outlet, &avg);
    if (swmm_link_set_loss_coeff(m_engine, idx, inlet, outlet, v) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setBarrels(int v) {
    const int idx = linkIdx();
    if (idx < 0 || v < 1) return;   // engine contract: barrels >= 1
    if (swmm_link_set_barrels(m_engine, idx, v) == SWMM_OK)
        emit changed();
}

// Inline geom setters — read-modify-write the xsect tuple so shape and the
// other three geoms are preserved (the engine has no per-geom API). Mirror
// of the loss-coeff slots. Every geom is writable whatever the shape —
// the stored numbers survive a shape change, which is what users expect
// when they set a width before switching CIRCULAR → RECT_CLOSED. Only the
// picker-owned index slots (IRREGULAR / STREET geom1, CUSTOM geom2) are
// refused: a raw number there re-points the section at another transect /
// shape curve. Kept in lock-step with the Attribute Table's xsectGeomSet.
void SWMMLinkPropertyAdapter::writeXsectGeom(int ordinal, double v) {
    const int idx = linkIdx();
    if (idx < 0) return;
    int shape = 0;
    double g[4] = {0, 0, 0, 0};
    if (swmm_link_get_xsect(m_engine, idx, &shape, &g[0], &g[1], &g[2], &g[3]) != SWMM_OK)
        return;
    if (openswmmvis::xsectGeomIsPickerIndex(shape, ordinal)) return;
    g[ordinal - 1] = v;
    if (swmm_link_set_xsect(m_engine, idx, shape, g[0], g[1], g[2], g[3]) == SWMM_OK)
        emit changed();
}
void SWMMLinkPropertyAdapter::setXsectGeom1(double v) { writeXsectGeom(1, v); }
void SWMMLinkPropertyAdapter::setXsectGeom2(double v) { writeXsectGeom(2, v); }
void SWMMLinkPropertyAdapter::setXsectGeom3(double v) { writeXsectGeom(3, v); }
void SWMMLinkPropertyAdapter::setXsectGeom4(double v) { writeXsectGeom(4, v); }

void SWMMLinkPropertyAdapter::setName(const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == m_name) return;
    emit renameRequested(m_name, trimmed);
}

// Slice SA — `[TAGS]` accessor. Matches SWMMNodePropertyAdapter::tag /
// setTag line-for-line: direct engine read/write, no model-layer route.
// Tag changes don't affect map symbology or attribute-table layout, so
// the existing `changed()` signal + PropertiesPanel.objectEdited fan-out
// is sufficient for two-way sync with the Attribute Table.
QString SWMMLinkPropertyAdapter::tag() const
{
    const int idx = linkIdx();
    if (idx < 0) return {};
    char buf[256] = {0};
    if (swmm_link_get_tag(m_engine, idx, buf, sizeof(buf)) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

void SWMMLinkPropertyAdapter::setTag(const QString &t)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    const QByteArray bytes = t.toUtf8();
    if (swmm_link_set_tag(m_engine, idx, bytes.constData()) == SWMM_OK)
        emit changed();
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

// Slice SD partial — orifice TYPE setter. Engine validates the link
// kind itself (rejects non-orifice with SWMM_ERR_BADPARAM); we just
// gate on the SWMM_OK return.
void SWMMLinkPropertyAdapter::setOrificeType(OrificeType v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_orifice_type(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setWeirType(WeirType v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_weir_type(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setOutletRatingType(OutletRatingType v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_outlet_rating_type(m_engine, idx, static_cast<int>(v)) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setOutletExpon(double v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_outlet_expon(m_engine, idx, v) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setPumpStartupDepth(double v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_pump_startup_depth(m_engine, idx, v) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setPumpShutoffDepth(double v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_pump_shutoff_depth(m_engine, idx, v) == SWMM_OK)
        emit changed();
}

void SWMMLinkPropertyAdapter::setOrificeOpenCloseRate(double v)
{
    const int idx = linkIdx();
    if (idx < 0) return;
    if (swmm_link_set_orifice_open_close_rate(m_engine, idx, v) == SWMM_OK)
        emit changed();
}
