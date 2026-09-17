/*!
 * \file   plotattribute.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — canonical enumeration of plottable result attributes.
 *
 * A `PlotAttribute` names what is being plotted (Depth, Flow, …) decoupled
 * from the object kind that produced it (node/link/subcatch/mesh-cell).
 * Series with the same attribute land on the same chart row in the
 * Comparison Plot Dialog, which lets two runs of the same junction's
 * depth be compared even when the engine indexing differs.
 *
 * CF.3 amendment (2026-05-21): five `Mesh2D*` attributes anchor per-cell
 * 2D mesh series in their own chart rows. They're separate enumerators
 * from the 1D node/link equivalents on purpose — node depth (free-surface
 * above invert in a manhole) and mesh-cell depth (water column over a
 * triangle) share units but are physically different quantities; sharing
 * a row would mis-axis them.
 */
#ifndef OPENSWMMVIS_PLOT_PLOTATTRIBUTE_H
#define OPENSWMMVIS_PLOT_PLOTATTRIBUTE_H

#include <QString>
#include <QVector>

namespace openswmmvis::plot {

/*! \brief Unit system carried per .out file. Sourced from
 *  `swmm_output_get_flow_units`: 0=CFS / 1=GPM / 2=MGD → US, 3=CMS / 4=LPS
 *  / 5=MLD → SI. */
enum class UnitSystem {
    US,   ///< ft, ft/s, ft³/s, ft³
    SI    ///< m,  m/s,  m³/s,  m³
};

/*! \brief What a series plots. One row per attribute in the Nx2 grid. */
enum class PlotAttribute {
    Unknown          = 0,

    // ----- 1D node variables (engine SWMM_OUT_NODE_*) -----------------------
    NodeDepth        = 1,   ///< m / ft   — above invert
    NodeHead         = 2,   ///< m / ft   — hydraulic head
    NodeVolume       = 3,   ///< m³ / ft³
    NodeLateralInflow= 4,   ///< m³/s / ft³/s
    NodeTotalInflow  = 5,   ///< m³/s / ft³/s
    NodeOverflow     = 6,   ///< m³/s / ft³/s

    // ----- 1D link variables (engine SWMM_OUT_LINK_*) -----------------------
    LinkFlow         = 7,   ///< m³/s / ft³/s
    LinkDepth        = 8,   ///< m / ft
    LinkVelocity     = 9,   ///< m/s / ft/s
    LinkVolume       = 10,  ///< m³ / ft³
    LinkCapacity     = 11,  ///< dimensionless 0..1

    // ----- 1D subcatchment variables (engine SWMM_OUT_SUBCATCH_*) -----------
    SubcatchRainfall  = 12, ///< mm/hr / in/hr
    SubcatchSnowDepth = 13, ///< mm / in
    SubcatchEvap      = 14, ///< mm/d / in/d
    SubcatchInfil     = 15, ///< mm/hr / in/hr
    SubcatchRunoff    = 16, ///< m³/s / ft³/s

    // ----- 2D mesh-cell variables (CF.3) -----------------------------------
    // Separate from Node*/Link* on purpose; see file header.
    Mesh2DDepth       = 17, ///< m / ft   — water column over triangle
    Mesh2DHGL         = 18, ///< m / ft   — depth + z_bed (GUI-computed)
    Mesh2DVelocityMag = 19, ///< m/s / ft/s — sqrt(Vx² + Vy²)
    Mesh2DVelocityX   = 20, ///< m/s / ft/s — projected map CRS, +east
    Mesh2DVelocityY   = 21, ///< m/s / ft/s — projected map CRS, +north

    // ----- System-wide variables (engine SWMM_OUT_SYS_*) -------------------
    // Slice AT.2 — one global series per attribute per run. ObjectRef name
    // is unused; ObjectRef::Kind::System is the discriminator.
    SystemTemperature = 22, ///< °C / °F   — SYS_TEMPERATURE
    SystemRainfall    = 23, ///< mm/hr / in/hr — SYS_RAINFALL  (avg over subcatchments)
    SystemSnowDepth   = 24, ///< mm / in   — SYS_SNOW_DEPTH (avg)
    SystemEvap        = 25, ///< mm/d / in/d — SYS_EVAP       (avg rate)
    SystemInfil       = 26, ///< mm/hr / in/hr — SYS_INFIL    (avg rate)
    SystemRunoff      = 27, ///< m³/s / ft³/s — SYS_RUNOFF    (total)
    SystemDwInflow    = 28, ///< m³/s / ft³/s — SYS_DW_INFLOW (total)
    SystemGwInflow    = 29, ///< m³/s / ft³/s — SYS_GW_INFLOW (total)
    SystemLatInflow   = 30, ///< m³/s / ft³/s — SYS_LAT_INFLOW(total)
    SystemFlooding    = 31, ///< m³/s / ft³/s — SYS_FLOODING  (total)
    SystemOutflow     = 32, ///< m³/s / ft³/s — SYS_OUTFLOW   (total)
    SystemStorage     = 33, ///< m³ / ft³  — SYS_STORAGE     (total volume)
    SystemEvapTotal   = 34, ///< mm/d / in/d — SYS_EVAP_TOTAL (total rate)
    SystemPET         = 35, ///< mm/d / in/d — SYS_PET        (potential ET)

    // ----- 2D mesh-edge variables ------------------------------------------
    // Both derive from the engine's single Mesh2_edge_flux dataset (volumetric
    // F_e, m³/s). Flow is that value directly; unit-width flux is F_e ÷ edge length.
    Mesh2DEdgeFlux    = 36, ///< m²/s / ft²/s — unit-width flux q = F_e / edge length
    Mesh2DEdgeFlow    = 37, ///< m³/s / ft³/s — volumetric flow Q across a mesh edge (= F_e)

    // ----- 2D mesh-cell rainfall (engine Mesh2_face_rainfall / _rain_cum) --
    Mesh2DRainfall    = 38, ///< mm/hr / in/hr — rainfall intensity applied to the cell
    Mesh2DRainVolume  = 39, ///< m³ / ft³ — cumulative rainfall volume applied to the cell
};

/*! \brief Short human label, e.g. "Depth", "Flow", "Velocity |V|". */
QString labelFor(PlotAttribute a);

/*! \brief Unit string for the given attribute + unit system, e.g. "m", "ft³/s". */
QString unitsFor(PlotAttribute a, UnitSystem u);

/*! \brief Concatenation of `labelFor(a)` + "(" + unitsFor(a,u) + ")". */
QString labelWithUnits(PlotAttribute a, UnitSystem u);

/*! \brief Map an engine `swmm_output_get_flow_units` return value to UnitSystem. */
UnitSystem unitSystemFromFlowUnits(int flowUnits);

// ---------------------------------------------------------------------------
// Canonical plottable-attribute lists. Single source of truth for every UI
// that enumerates attributes (AttributePickerMenu, PlotVariablePickerDialog,
// the Comparison Plot "Add Series" menus, and the "All attributes" fan-out).
// Order is presentation order. A kind-keyed dispatcher lives in irunlayer.h
// (`attributesForKind`) — the nested ObjectRef::Kind can't be named here.
// ---------------------------------------------------------------------------

/*! \brief The 6 per-node attributes, presentation order. */
const QVector<PlotAttribute> &nodePlotAttributes();

/*! \brief The 5 per-link attributes, presentation order. */
const QVector<PlotAttribute> &linkPlotAttributes();

/*! \brief The 5 per-subcatchment attributes, presentation order. */
const QVector<PlotAttribute> &subcatchPlotAttributes();

/*! \brief The 14 system-wide attributes, presentation order (matches the
 *  long-standing system picker menu: Rainfall first, Temperature last). */
const QVector<PlotAttribute> &systemPlotAttributes();

/*! \brief The 7 per-2D-cell attributes, presentation order. */
const QVector<PlotAttribute> &mesh2DCellPlotAttributes();

/*! \brief The 2 per-2D-edge attributes (flow, unit-width flux). */
const QVector<PlotAttribute> &mesh2DEdgePlotAttributes();

/*! \brief The 2 per-2D-vertex attributes (depth, HGL — interpolated). */
const QVector<PlotAttribute> &mesh2DVertexPlotAttributes();

/*! \brief True iff the attribute is a 2D mesh-cell quantity (CF.3). */
inline bool isMesh2DAttribute(PlotAttribute a) noexcept
{
    return a == PlotAttribute::Mesh2DDepth      ||
           a == PlotAttribute::Mesh2DHGL        ||
           a == PlotAttribute::Mesh2DVelocityMag||
           a == PlotAttribute::Mesh2DVelocityX  ||
           a == PlotAttribute::Mesh2DVelocityY  ||
           a == PlotAttribute::Mesh2DRainfall   ||
           a == PlotAttribute::Mesh2DRainVolume;
}

/*! \brief True iff the attribute is a system-wide quantity (Slice AT.2).
 *  System attributes resolve via `swmm_output_get_system_series` and
 *  carry no per-object name (ObjectRef::Kind::System suffices). */
inline bool isSystemAttribute(PlotAttribute a) noexcept
{
    return a == PlotAttribute::SystemTemperature ||
           a == PlotAttribute::SystemRainfall    ||
           a == PlotAttribute::SystemSnowDepth   ||
           a == PlotAttribute::SystemEvap        ||
           a == PlotAttribute::SystemInfil       ||
           a == PlotAttribute::SystemRunoff      ||
           a == PlotAttribute::SystemDwInflow    ||
           a == PlotAttribute::SystemGwInflow    ||
           a == PlotAttribute::SystemLatInflow   ||
           a == PlotAttribute::SystemFlooding    ||
           a == PlotAttribute::SystemOutflow     ||
           a == PlotAttribute::SystemStorage     ||
           a == PlotAttribute::SystemEvapTotal   ||
           a == PlotAttribute::SystemPET;
}

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_PLOTATTRIBUTE_H
