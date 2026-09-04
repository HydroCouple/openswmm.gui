/*!
 * \file   swmmsubcatchpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmsubcatchpropertyadapter.h"

#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"   // USER_FLAGS Phase 4 — ensureUserFlagsModel()
#include "layers/swmmresultslayer.h"        // stats dispatch (QA.2 mirror)
#include "output/outputstatsregistry.h"     // stats dispatch (QA.2 mirror)

#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_nodes.h>   // outlet node resolution
#include <openswmm/engine/openswmm_gages.h>   // rain gage resolution
#include <openswmm/engine/openswmm_pollutants.h>      // loadings summary
#include <openswmm/engine/openswmm_quality.h>        // landuse coverage summary
#include <openswmm/engine/openswmm_infrastructure.h>  // LID usage summary

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
    // Precipitation scaling — multiply the gage-derived precip for this
    // subcatchment only. Compose with the gage's own factors.
    if (property == QLatin1String("rainScaleFactor")) return tr("Rainfall Scale Factor");
    if (property == QLatin1String("snowScaleFactor")) return tr("Snow Scale Factor");
    // Phase 3 — gage / outlet / infiltration.
    if (property == QLatin1String("rainGage"))   return tr("Rain Gage");
    if (property == QLatin1String("outlet"))     return tr("Outlet");
    if (property == QLatin1String("infilModel")) return tr("Infil. Model");
    if (property == QLatin1String("hortonF0"))      return tr("Max. Infil. Rate (%1/hr)").arg(D);
    if (property == QLatin1String("hortonFmin"))    return tr("Min. Infil. Rate (%1/hr)").arg(D);
    if (property == QLatin1String("hortonDecay"))   return tr("Decay Constant (1/hr)");
    if (property == QLatin1String("hortonDryTime")) return tr("Drying Time (days)");
    if (property == QLatin1String("gaSuction"))      return tr("Suction Head (%1)").arg(D);
    if (property == QLatin1String("gaConductivity")) return tr("Conductivity (%1/hr)").arg(D);
    if (property == QLatin1String("gaInitDeficit"))  return tr("Initial Deficit (frac.)");
    if (property == QLatin1String("cnNumber"))       return tr("Curve Number");
    if (property == QLatin1String("landUse"))     return tr("Land Uses");
    if (property == QLatin1String("groundwater")) return tr("Groundwater");
    if (property == QLatin1String("lidUsage"))    return tr("LID Usage");
    if (property == QLatin1String("loadings"))    return tr("Initial Loadings");
    // USER_FLAGS Phase 4.
    if (property == QLatin1String("userFlags")) return tr("User Flags");
    // Read-only post-run summary block (Attribute Table dynamics parity).
    {
        const QString F = (u ? u->flowUnitLabel()
                             : QStringLiteral("CFS")).toLower();
        if (property == QLatin1String("statPrecip"))    return tr("Total Precipitation (%1³)").arg(L);
        if (property == QLatin1String("statRunoffVol")) return tr("Total Runoff Volume (%1³)").arg(L);
        if (property == QLatin1String("statMaxRunoff")) return tr("Peak Runoff (%1)").arg(F);
    }

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
G(pctZeroImperv, swmm_subcatch_get_zero_imperv_pct)
G(nImperv,   swmm_subcatch_get_n_imperv)
G(nPerv,     swmm_subcatch_get_n_perv)
G(dsImperv,  swmm_subcatch_get_ds_imperv)
G(dsPerv,    swmm_subcatch_get_ds_perv)

// Post-run statistics — dispatch on m_statsSourceId (see the node adapter's
// Slice QA.2 STAT_GETTER for the contract). Null id → the editing engine's
// ambient stats; non-null → the registry-resolved SWMMResultsLayer.
#define STAT_GETTER(METHOD, ENGINE_GET, LAYER_GET)                  \
double SWMMSubcatchPropertyAdapter::METHOD() const {                \
    if (m_statsSourceId.isNull() || !m_statsRegistry) {             \
        const int i = idx();                                        \
        if (i < 0) return 0.0;                                      \
        double v = 0.0;                                             \
        ENGINE_GET(m_engine, i, &v);                                \
        return v;                                                   \
    }                                                               \
    const auto id = m_statsRegistry->identityFor(m_statsSourceId);  \
    if (!id.layer) return 0.0; /* layer destroyed since combo set */ \
    return id.layer->LAYER_GET(m_name);                             \
}

STAT_GETTER(statPrecip,    swmm_subcatch_get_stat_precip,     subcatchStatPrecip)
STAT_GETTER(statRunoffVol, swmm_subcatch_get_stat_runoff_vol, subcatchStatRunoffVol)
STAT_GETTER(statMaxRunoff, swmm_subcatch_get_stat_max_runoff, subcatchStatMaxRunoff)

#undef STAT_GETTER

void SWMMSubcatchPropertyAdapter::setStatsRegistry(
        openswmmvis::OutputStatsRegistry *registry)
{
    m_statsRegistry = registry;
    // No emit changed() — see SWMMNodePropertyAdapter::setStatsRegistry.
}

void SWMMSubcatchPropertyAdapter::setStatsSource(const QUuid &id)
{
    if (m_statsSourceId == id) return;
    m_statsSourceId = id;
    emit changed();
}

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
// + PropertiesPanel.objectEdited fan-out is sufficient for two-way sync
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
S(setPctZeroImperv, swmm_subcatch_set_zero_imperv_pct)
S(setNImperv,   swmm_subcatch_set_n_imperv)
S(setNPerv,     swmm_subcatch_set_n_perv)
S(setDsImperv,  swmm_subcatch_set_ds_imperv)
S(setDsPerv,    swmm_subcatch_set_ds_perv)

// ---------------------------------------------------------------------------
// Precipitation scale factors.
//
// Hand-written rather than using G()/S() above: the G() macro falls back to 0.0
// when the engine read fails, but the neutral value for a multiplicative factor
// is 1.0 — a 0.0 fallback would silently show (and on a subsequent write, apply)
// "no precipitation". The setters also guard v > 0 because the Property Browser
// hands out an unbounded spin box; the engine would reject the value anyway, but
// silently, leaving the user with no feedback.
// ---------------------------------------------------------------------------
double SWMMSubcatchPropertyAdapter::rainScaleFactor() const
{
    const int i = idx();
    if (i < 0) return 1.0;
    double v = 1.0;
    swmm_subcatch_get_rain_scale_factor(m_engine, i, &v);
    return v;
}

void SWMMSubcatchPropertyAdapter::setRainScaleFactor(double v)
{
    if (v <= 0.0) return;
    const int i = idx();
    if (i < 0) return;
    if (swmm_subcatch_set_rain_scale_factor(m_engine, i, v) == SWMM_OK)
        emit changed();
}

double SWMMSubcatchPropertyAdapter::snowScaleFactor() const
{
    const int i = idx();
    if (i < 0) return 1.0;
    double v = 1.0;
    swmm_subcatch_get_snow_scale_factor(m_engine, i, &v);
    return v;
}

void SWMMSubcatchPropertyAdapter::setSnowScaleFactor(double v)
{
    if (v <= 0.0) return;
    const int i = idx();
    if (i < 0) return;
    if (swmm_subcatch_set_snow_scale_factor(m_engine, i, v) == SWMM_OK)
        emit changed();
}

// ===========================================================================
// Phase 3 — rain gage, outlet, infiltration model + params.
// ===========================================================================

// --- Rain gage (R3 picker over gage names) ---------------------------------
DataObjectRef SWMMSubcatchPropertyAdapter::rainGageRef() const
{
    DataObjectRef r;
    r.engine = m_engine;
    r.layer  = m_layer;
    r.kind   = DataObjectRef::RainGage;
    const int i = idx();
    if (i >= 0) {
        int g = -1;
        if (swmm_subcatch_get_gage(m_engine, i, &g) == SWMM_OK && g >= 0)
            if (const char *id = swmm_gage_id(m_engine, g))
                r.currentName = QString::fromUtf8(id);
    }
    return r;
}

void SWMMSubcatchPropertyAdapter::setRainGageRef(const DataObjectRef &r)
{
    const int i = idx();
    if (i < 0 || r.currentName.isEmpty()) return;   // gage is required; ignore clear
    const int g = swmm_gage_index(m_engine, r.currentName.toUtf8().constData());
    if (g < 0) return;
    if (swmm_subcatch_set_gage(m_engine, i, g) == SWMM_OK)
        emit changed();
}

// --- Outlet (combined node / subcatchment target) --------------------------
DataObjectRef SWMMSubcatchPropertyAdapter::outletRef() const
{
    DataObjectRef r;
    r.engine = m_engine;
    r.layer  = m_layer;
    r.kind   = DataObjectRef::SubcatchOutlet;
    const int i = idx();
    if (i >= 0) {
        // Cascade outlet (subcatchment) takes precedence: get_outlet_subcatch
        // returns -1 when the subcatch drains to a node instead.
        int sc = -1;
        if (swmm_subcatch_get_outlet_subcatch(m_engine, i, &sc) == SWMM_OK && sc >= 0) {
            if (const char *id = swmm_subcatch_id(m_engine, sc))
                r.currentName = QString::fromUtf8(id);
        } else {
            int nd = -1;
            if (swmm_subcatch_get_outlet(m_engine, i, &nd) == SWMM_OK && nd >= 0)
                if (const char *id = swmm_node_id(m_engine, nd))
                    r.currentName = QString::fromUtf8(id);
        }
    }
    return r;
}

void SWMMSubcatchPropertyAdapter::setOutletRef(const DataObjectRef &r)
{
    const int i = idx();
    if (i < 0 || r.currentName.isEmpty()) return;
    const QByteArray nm = r.currentName.toUtf8();
    // Nodes take precedence on a name collision. set_outlet and
    // set_outlet_subcatch are mutually exclusive in the engine.
    const int nd = swmm_node_index(m_engine, nm.constData());
    if (nd >= 0) {
        if (swmm_subcatch_set_outlet(m_engine, i, nd) == SWMM_OK)
            emit changed();
        return;
    }
    const int sc = swmm_subcatch_index(m_engine, nm.constData());
    if (sc >= 0 && sc != i) {
        if (swmm_subcatch_set_outlet_subcatch(m_engine, i, sc) == SWMM_OK)
            emit changed();
    }
}

// --- Infiltration model + parameters ---------------------------------------
SWMMSubcatchPropertyAdapter::InfilModel
SWMMSubcatchPropertyAdapter::infilModel() const
{
    const int i = idx();
    int m = 0;
    if (i >= 0) swmm_subcatch_get_infil_model(m_engine, i, &m);
    return static_cast<InfilModel>(m);
}

void SWMMSubcatchPropertyAdapter::setInfilModel(InfilModel m)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_subcatch_set_infil_model(m_engine, i, static_cast<int>(m)) == SWMM_OK)
        emit changed();
}

// Horton parameters (infil_p1..p4: f0, fmin, decay, dry_time).
double SWMMSubcatchPropertyAdapter::hortonF0() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double f0 = 0, fmin = 0, decay = 0, dry = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    return f0;
}
double SWMMSubcatchPropertyAdapter::hortonFmin() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double f0 = 0, fmin = 0, decay = 0, dry = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    return fmin;
}
double SWMMSubcatchPropertyAdapter::hortonDecay() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double f0 = 0, fmin = 0, decay = 0, dry = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    return decay;
}
double SWMMSubcatchPropertyAdapter::hortonDryTime() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double f0 = 0, fmin = 0, decay = 0, dry = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    return dry;
}

// Read-modify-write one Horton term, preserving the model code so that a
// Mod-Horton subcatchment isn't silently demoted to plain Horton by the
// engine's swmm_subcatch_set_infil_horton (which always stamps model = 0).
void SWMMSubcatchPropertyAdapter::setHortonF0(double v)
{
    const int i = idx(); if (i < 0) return;
    double f0 = 0, fmin = 0, decay = 0, dry = 0; int model = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_horton(m_engine, i, v, fmin, decay, dry) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}
void SWMMSubcatchPropertyAdapter::setHortonFmin(double v)
{
    const int i = idx(); if (i < 0) return;
    double f0 = 0, fmin = 0, decay = 0, dry = 0; int model = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_horton(m_engine, i, f0, v, decay, dry) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}
void SWMMSubcatchPropertyAdapter::setHortonDecay(double v)
{
    const int i = idx(); if (i < 0) return;
    double f0 = 0, fmin = 0, decay = 0, dry = 0; int model = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_horton(m_engine, i, f0, fmin, v, dry) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}
void SWMMSubcatchPropertyAdapter::setHortonDryTime(double v)
{
    const int i = idx(); if (i < 0) return;
    double f0 = 0, fmin = 0, decay = 0, dry = 0; int model = 0;
    swmm_subcatch_get_infil_horton(m_engine, i, &f0, &fmin, &decay, &dry);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_horton(m_engine, i, f0, fmin, decay, v) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}

// Green-Ampt parameters (infil_p1..p3: suction, conductivity, initial deficit).
double SWMMSubcatchPropertyAdapter::gaSuction() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double s = 0, k = 0, d = 0;
    swmm_subcatch_get_infil_green_ampt(m_engine, i, &s, &k, &d);
    return s;
}
double SWMMSubcatchPropertyAdapter::gaConductivity() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double s = 0, k = 0, d = 0;
    swmm_subcatch_get_infil_green_ampt(m_engine, i, &s, &k, &d);
    return k;
}
double SWMMSubcatchPropertyAdapter::gaInitDeficit() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double s = 0, k = 0, d = 0;
    swmm_subcatch_get_infil_green_ampt(m_engine, i, &s, &k, &d);
    return d;
}
void SWMMSubcatchPropertyAdapter::setGaSuction(double v)
{
    const int i = idx(); if (i < 0) return;
    double s = 0, k = 0, d = 0; int model = 0;
    swmm_subcatch_get_infil_green_ampt(m_engine, i, &s, &k, &d);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_green_ampt(m_engine, i, v, k, d) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);  // preserve Mod-GA
        emit changed();
    }
}
void SWMMSubcatchPropertyAdapter::setGaConductivity(double v)
{
    const int i = idx(); if (i < 0) return;
    double s = 0, k = 0, d = 0; int model = 0;
    swmm_subcatch_get_infil_green_ampt(m_engine, i, &s, &k, &d);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_green_ampt(m_engine, i, s, v, d) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}
void SWMMSubcatchPropertyAdapter::setGaInitDeficit(double v)
{
    const int i = idx(); if (i < 0) return;
    double s = 0, k = 0, d = 0; int model = 0;
    swmm_subcatch_get_infil_green_ampt(m_engine, i, &s, &k, &d);
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_green_ampt(m_engine, i, s, k, v) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}

// Curve Number (infil_p1) and its drying time (infil_p3). Both slots are
// written together, so each setter re-reads its sibling first; the model code
// is restored afterwards because the engine setter stamps CURVE_NUMBER.
double SWMMSubcatchPropertyAdapter::cnNumber() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double cn = 0;
    swmm_subcatch_get_infil_curve_number(m_engine, i, &cn, nullptr);
    return cn;
}
double SWMMSubcatchPropertyAdapter::cnDryTime() const
{
    const int i = idx(); if (i < 0) return 0.0;
    double dt = 0;
    swmm_subcatch_get_infil_curve_number(m_engine, i, nullptr, &dt);
    return dt;
}
void SWMMSubcatchPropertyAdapter::writeCurveNumber(double cn, double dryTime)
{
    const int i = idx(); if (i < 0) return;
    int model = 0;
    swmm_subcatch_get_infil_model(m_engine, i, &model);
    if (swmm_subcatch_set_infil_curve_number(m_engine, i, cn, dryTime) == SWMM_OK) {
        swmm_subcatch_set_infil_model(m_engine, i, model);
        emit changed();
    }
}
void SWMMSubcatchPropertyAdapter::setCnNumber(double v)
{
    writeCurveNumber(v, cnDryTime());
}
void SWMMSubcatchPropertyAdapter::setCnDryTime(double v)
{
    writeCurveNumber(cnNumber(), v);
}

// --- Compound refs (land use / groundwater / LID usage) --------------------
SubcatchCompoundEditRef SWMMSubcatchPropertyAdapter::landUseRef() const
{
    SubcatchCompoundEditRef r;
    r.engine = m_engine; r.layer = m_layer;
    r.subName = m_name; r.kind = SubcatchCompoundEditRef::LandUse;
    const int i = idx();
    int assigned = 0;
    if (i >= 0) {
        const int n = swmm_landuse_count(m_engine);
        for (int lu = 0; lu < n; ++lu) {
            double frac = 0.0;
            if (swmm_subcatch_get_coverage(m_engine, i, lu, &frac) == SWMM_OK && frac > 0.0)
                ++assigned;
        }
    }
    r.summary = assigned > 0 ? tr("%1 land use(s)").arg(assigned) : tr("(none)");
    return r;
}

SubcatchCompoundEditRef SWMMSubcatchPropertyAdapter::groundwaterRef() const
{
    SubcatchCompoundEditRef r;
    r.engine = m_engine; r.layer = m_layer;
    r.subName = m_name; r.kind = SubcatchCompoundEditRef::Groundwater;
    const int i = idx();
    int aq = -1;
    if (i >= 0) swmm_subcatch_get_aquifer(m_engine, i, &aq);
    r.summary = aq >= 0 ? tr("aquifer set") : tr("(none)");
    return r;
}

SubcatchCompoundEditRef SWMMSubcatchPropertyAdapter::lidUsageRef() const
{
    SubcatchCompoundEditRef r;
    r.engine = m_engine; r.layer = m_layer;
    r.subName = m_name; r.kind = SubcatchCompoundEditRef::LidUsage;
    const int i = idx();
    int mine = 0;
    if (i >= 0) {
        const int n = swmm_lid_usage_count(m_engine);
        for (int u = 0; u < n; ++u) {
            int sc = -1;
            if (swmm_lid_usage_get(m_engine, u, &sc, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, nullptr) == SWMM_OK
                && sc == i)
                ++mine;
        }
    }
    r.summary = mine > 0 ? tr("%1 LID(s)").arg(mine) : tr("(none)");
    return r;
}

SubcatchCompoundEditRef SWMMSubcatchPropertyAdapter::loadingsRef() const
{
    // [LOADINGS] initial pollutant buildup (iteration 4).
    SubcatchCompoundEditRef r;
    r.engine = m_engine; r.layer = m_layer;
    r.subName = m_name; r.kind = SubcatchCompoundEditRef::Loadings;
    const int i = idx();
    int assigned = 0;
    if (i >= 0) {
        const int n = swmm_pollutant_count(m_engine);
        for (int p = 0; p < n; ++p) {
            double w = 0.0;
            if (swmm_subcatch_get_initial_loading(m_engine, i, p, &w) == SWMM_OK
                && w > 0.0)
                ++assigned;
        }
    }
    r.summary = assigned > 0 ? tr("%1 pollutant(s)").arg(assigned)
                             : tr("(none)");
    return r;
}
