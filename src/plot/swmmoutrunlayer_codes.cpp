/*!
 * \file   swmmoutrunlayer_codes.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pure PlotAttribute → engine SWMM_OUT_* code mapping for
 *         SwmmOutRunLayer::variableCodeFor.
 *
 * Split out from swmmoutrunlayer.cpp so the table can be linked into unit
 * tests without pulling in SWMMResultsLayer and its transitive deps (GDAL,
 * canvas layer base classes, etc.). Pure switch — no I/O, no state.
 */
#include "plot/swmmoutrunlayer.h"

#include <openswmm/engine/openswmm_output.h>

namespace openswmmvis::plot {

int SwmmOutRunLayer::variableCodeFor(PlotAttribute attr, ObjectRef::Kind kind)
{
    // 1D node variables
    if (kind == ObjectRef::Kind::Node) {
        switch (attr) {
        case PlotAttribute::NodeDepth:          return SWMM_OUT_NODE_DEPTH;
        case PlotAttribute::NodeHead:           return SWMM_OUT_NODE_HEAD;
        case PlotAttribute::NodeVolume:         return SWMM_OUT_NODE_VOLUME;
        case PlotAttribute::NodeLateralInflow:  return SWMM_OUT_NODE_LATERAL_INFLOW;
        case PlotAttribute::NodeTotalInflow:    return SWMM_OUT_NODE_TOTAL_INFLOW;
        case PlotAttribute::NodeOverflow:       return SWMM_OUT_NODE_OVERFLOW;
        default: return -1;
        }
    }
    // 1D link variables
    if (kind == ObjectRef::Kind::Link) {
        switch (attr) {
        case PlotAttribute::LinkFlow:     return SWMM_OUT_LINK_FLOW;
        case PlotAttribute::LinkDepth:    return SWMM_OUT_LINK_DEPTH;
        case PlotAttribute::LinkVelocity: return SWMM_OUT_LINK_VELOCITY;
        case PlotAttribute::LinkVolume:   return SWMM_OUT_LINK_VOLUME;
        case PlotAttribute::LinkCapacity: return SWMM_OUT_LINK_CAPACITY;
        default: return -1;
        }
    }
    // 1D subcatchment variables
    if (kind == ObjectRef::Kind::Subcatch) {
        switch (attr) {
        case PlotAttribute::SubcatchRainfall:  return SWMM_OUT_SUBCATCH_RAINFALL;
        case PlotAttribute::SubcatchSnowDepth: return SWMM_OUT_SUBCATCH_SNOW_DEPTH;
        case PlotAttribute::SubcatchEvap:      return SWMM_OUT_SUBCATCH_EVAP;
        case PlotAttribute::SubcatchInfil:     return SWMM_OUT_SUBCATCH_INFIL;
        case PlotAttribute::SubcatchRunoff:    return SWMM_OUT_SUBCATCH_RUNOFF;
        default: return -1;
        }
    }
    // System-wide variables (AT.2)
    if (kind == ObjectRef::Kind::System) {
        switch (attr) {
        case PlotAttribute::SystemTemperature: return SWMM_OUT_SYS_TEMPERATURE;
        case PlotAttribute::SystemRainfall:    return SWMM_OUT_SYS_RAINFALL;
        case PlotAttribute::SystemSnowDepth:   return SWMM_OUT_SYS_SNOW_DEPTH;
        case PlotAttribute::SystemEvap:        return SWMM_OUT_SYS_EVAP;
        case PlotAttribute::SystemInfil:       return SWMM_OUT_SYS_INFIL;
        case PlotAttribute::SystemRunoff:      return SWMM_OUT_SYS_RUNOFF;
        case PlotAttribute::SystemDwInflow:    return SWMM_OUT_SYS_DW_INFLOW;
        case PlotAttribute::SystemGwInflow:    return SWMM_OUT_SYS_GW_INFLOW;
        case PlotAttribute::SystemLatInflow:   return SWMM_OUT_SYS_LAT_INFLOW;
        case PlotAttribute::SystemFlooding:    return SWMM_OUT_SYS_FLOODING;
        case PlotAttribute::SystemOutflow:     return SWMM_OUT_SYS_OUTFLOW;
        case PlotAttribute::SystemStorage:     return SWMM_OUT_SYS_STORAGE;
        case PlotAttribute::SystemEvapTotal:   return SWMM_OUT_SYS_EVAP_TOTAL;
        case PlotAttribute::SystemPET:         return SWMM_OUT_SYS_PET;
        default: return -1;
        }
    }
    return -1;
}

} // namespace openswmmvis::plot
