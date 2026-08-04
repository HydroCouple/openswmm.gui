#include "map/objectdefaultsapplier.h"

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>

namespace {

using OD = PreferencesManager::ObjectDefaults;

OD activeDefaults()
{
    return PreferencesManager::instance()
        ->objectDefaults(UnitSystem::instance()->isSI());
}

//! Keyword → engine code maps. Unknown keywords fall back to the first
//! (safest) entry so a hand-edited QSettings value cannot brick creation.
int outfallTypeCode(const QString &kw)
{
    if (kw == QLatin1String("NORMAL")) return 1;
    if (kw == QLatin1String("FIXED"))  return 2;
    return 0;   // FREE
}

int dividerTypeCode(const QString &kw)
{
    if (kw == QLatin1String("CUTOFF"))  return 0;   // SWMM_DIVIDER_CUTOFF
    if (kw == QLatin1String("TABULAR")) return 2;
    if (kw == QLatin1String("WEIR"))    return 3;
    return 1;   // OVERFLOW
}

int xsectShapeCode(const QString &kw)
{
    if (kw == QLatin1String("RECT_OPEN"))   return 3;   // SWMM_XSECT_RECT_OPEN
    if (kw == QLatin1String("RECT_CLOSED")) return 2;
    if (kw == QLatin1String("TRIANGULAR"))  return 5;
    if (kw == QLatin1String("TRAPEZOIDAL")) return 4;
    return 0;   // CIRCULAR
}

int orificeTypeCode(const QString &kw)
{
    return kw == QLatin1String("BOTTOM") ? 1 : 0;   // SIDE
}

int weirTypeCode(const QString &kw)
{
    if (kw == QLatin1String("SIDEFLOW"))    return 1;
    if (kw == QLatin1String("VNOTCH"))      return 2;
    if (kw == QLatin1String("TRAPEZOIDAL")) return 3;
    if (kw == QLatin1String("ROADWAY"))     return 4;
    return 0;   // TRANSVERSE
}

int outletRatingCode(const QString &kw)
{
    if (kw == QLatin1String("FUNCTIONAL_HEAD")) return 0;
    if (kw == QLatin1String("TABULAR_HEAD"))    return 2;
    if (kw == QLatin1String("TABULAR_DEPTH"))   return 3;
    return 1;   // FUNCTIONAL_DEPTH
}

} // anonymous

namespace ObjectDefaultsApplier {

void applyNodeDefaults(SWMM_Engine engine, int idx, int nodeType)
{
    if (!engine || idx < 0) return;
    const OD d = activeDefaults();

    switch (nodeType) {
    case 0:   // SWMM_NODE_JUNCTION
        swmm_node_set_max_depth(engine, idx, d.junctionMaxDepth);
        swmm_node_set_initial_depth(engine, idx, d.junctionInitDepth);
        swmm_node_set_surcharge_depth(engine, idx, d.junctionSurDepth);
        swmm_node_set_pond_area(engine, idx, d.junctionPondedArea);
        break;
    case 1:   // SWMM_NODE_OUTFALL
        swmm_node_set_outfall_type(engine, idx, outfallTypeCode(d.outfallType));
        swmm_node_set_outfall_flap_gate(engine, idx, d.outfallFlapGate ? 1 : 0);
        break;
    case 2:   // SWMM_NODE_STORAGE
        swmm_node_set_max_depth(engine, idx, d.storageMaxDepth);
        swmm_node_set_storage_functional(engine, idx,
                                         d.storageFuncCoeff,
                                         d.storageFuncExponent,
                                         d.storageFuncConstant);
        swmm_node_set_storage_seep_rate(engine, idx, d.storageSeepRate);
        break;
    case 3:   // SWMM_NODE_DIVIDER
        swmm_node_set_divider_type(engine, idx, dividerTypeCode(d.dividerType));
        break;
    default:
        break;
    }
}

void applyLinkDefaults(SWMM_Engine engine, int idx, int linkType, bool skipLength)
{
    if (!engine || idx < 0) return;
    const OD d = activeDefaults();

    switch (linkType) {
    case 0:   // SWMM_LINK_CONDUIT
        swmm_link_set_xsect(engine, idx, xsectShapeCode(d.conduitShape),
                            d.conduitGeom1, d.conduitGeom2,
                            d.conduitGeom3, d.conduitGeom4);
        swmm_link_set_roughness(engine, idx, d.conduitRoughness);
        if (!skipLength)
            swmm_link_set_length(engine, idx, d.conduitLength);
        swmm_link_set_barrels(engine, idx, d.conduitBarrels);
        swmm_link_set_loss_coeff(engine, idx,
                                 d.conduitLossInlet, d.conduitLossOutlet, 0.0);
        swmm_link_set_flap_gate(engine, idx, d.conduitFlapGate ? 1 : 0);
        break;
    case 1:   // SWMM_LINK_PUMP (ideal pump — no curve to default)
        swmm_link_set_pump_init_state(engine, idx, d.pumpInitStateOn ? 1 : 0);
        swmm_link_set_pump_startup_depth(engine, idx, d.pumpStartupDepth);
        swmm_link_set_pump_shutoff_depth(engine, idx, d.pumpShutoffDepth);
        break;
    case 2:   // SWMM_LINK_ORIFICE
        swmm_link_set_orifice_type(engine, idx, orificeTypeCode(d.orificeType));
        swmm_link_set_xsect(engine, idx, /*CIRCULAR*/ 0,
                            d.orificeGeom1, 0.0, 0.0, 0.0);
        swmm_link_set_discharge_coeff(engine, idx, d.orificeCd);
        swmm_link_set_flap_gate(engine, idx, d.orificeFlapGate ? 1 : 0);
        swmm_link_set_orifice_open_close_rate(engine, idx, d.orificeOpenCloseRate);
        break;
    case 3:   // SWMM_LINK_WEIR (RECT_OPEN: geom1 = height, geom2 = length)
        swmm_link_set_weir_type(engine, idx, weirTypeCode(d.weirType));
        swmm_link_set_xsect(engine, idx, /*RECT_OPEN*/ 3,
                            d.weirGeom1, d.weirGeom2, 0.0, 0.0);
        swmm_link_set_discharge_coeff(engine, idx, d.weirCd);
        swmm_link_set_end_contractions(engine, idx,
                                       double(d.weirEndContractions));
        swmm_link_set_flap_gate(engine, idx, d.weirFlapGate ? 1 : 0);
        break;
    case 4:   // SWMM_LINK_OUTLET (rating Q = C*h^n)
        swmm_link_set_outlet_rating_type(engine, idx,
                                         outletRatingCode(d.outletRatingType));
        swmm_link_set_discharge_coeff(engine, idx, d.outletCoeff);
        swmm_link_set_outlet_expon(engine, idx, d.outletExponent);
        swmm_link_set_flap_gate(engine, idx, d.outletFlapGate ? 1 : 0);
        break;
    default:
        break;
    }
}

void applySubcatchDefaults(SWMM_Engine engine, int idx, bool skipArea)
{
    if (!engine || idx < 0) return;
    const OD d = activeDefaults();

    if (!skipArea)
        swmm_subcatch_set_area(engine, idx, d.subcatchArea);
    swmm_subcatch_set_width(engine, idx, d.subcatchWidth);
    swmm_subcatch_set_slope(engine, idx, d.subcatchSlopePct);
    swmm_subcatch_set_imperv_pct(engine, idx, d.subcatchImpervPct);
    swmm_subcatch_set_n_imperv(engine, idx, d.subcatchNImperv);
    swmm_subcatch_set_n_perv(engine, idx, d.subcatchNPerv);
    swmm_subcatch_set_ds_imperv(engine, idx, d.subcatchDsImperv);
    swmm_subcatch_set_ds_perv(engine, idx, d.subcatchDsPerv);
    swmm_subcatch_set_zero_imperv_pct(engine, idx, d.subcatchPctZeroImperv);

    // Write only the parameter family matching the PROJECT's INFILTRATION
    // option. Keying off the subcatchment's own infil_model would pin every
    // new subcatchment to Horton: swmm_subcatch_add zero-initialises that
    // field (SubcatchData::grow_to), so a fresh row always reads HORTON no
    // matter what the project selected.
    char buf[64] = {};
    QString opt = QStringLiteral("HORTON");
    if (swmm_options_get(engine, "INFILTRATION", buf, int(sizeof(buf))) == 0)
        opt = QString::fromUtf8(buf).trimmed().toUpper();

    // The family setters stamp the canonical model (0 / 2 / 4); re-stamp the
    // MOD_ variants (1 / 3) afterwards so the project's choice is preserved.
    const bool modified = opt.startsWith(QLatin1String("MOD"));
    if (opt.contains(QLatin1String("CURVE"))) {
        swmm_subcatch_set_infil_curve_number(engine, idx, d.cnCurveNumber,
                                             d.cnDryTime);
    } else if (opt.contains(QLatin1String("GREEN"))) {
        swmm_subcatch_set_infil_green_ampt(engine, idx,
                                           d.gaSuction, d.gaKsat, d.gaImd);
        if (modified) swmm_subcatch_set_infil_model(engine, idx, 3);
    } else {
        swmm_subcatch_set_infil_horton(engine, idx,
                                       d.hortonMaxRate, d.hortonMinRate,
                                       d.hortonDecay, d.hortonDryTime);
        if (modified) swmm_subcatch_set_infil_model(engine, idx, 1);
    }
}

void applyGageDefaults(SWMM_Engine engine, int idx)
{
    if (!engine || idx < 0) return;
    const OD d = activeDefaults();

    // INTENSITY=0, VOLUME=1, CUMULATIVE=2 (SWMM_GageRainType)
    int fmt = 0;
    if (d.gageRainFormat == QLatin1String("VOLUME"))     fmt = 1;
    else if (d.gageRainFormat == QLatin1String("CUMULATIVE")) fmt = 2;
    swmm_gage_set_rain_type(engine, idx, fmt);
    swmm_gage_set_rain_interval(engine, idx, d.gageIntervalMin * 60.0);
    swmm_gage_set_snow_factor(engine, idx, d.gageSnowCatch);
}

} // namespace ObjectDefaultsApplier
