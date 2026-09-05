/*!
 * \file   gwsourcesummary.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "layers/gwsourcesummary.h"

#include <openswmm/engine/openswmm_subcatchments.h>

namespace OpenSWMMVis::Groundwater
{

QStringList groundwaterSourceSubcatchments(SWMM_Engine engine, int nodeIdx)
{
    QStringList out;
    if (!engine || nodeIdx < 0) return out;

    const int ns = swmm_subcatch_count(engine);
    for (int s = 0; s < ns; ++s) {
        int node = -1, aquifer = -1;
        if (swmm_subcatch_get_gw_node(engine, s, &node) != SWMM_OK || node != nodeIdx)
            continue;
        if (swmm_subcatch_get_aquifer(engine, s, &aquifer) != SWMM_OK || aquifer < 0)
            continue;
        if (const char *id = swmm_subcatch_id(engine, s))
            out << QString::fromUtf8(id);
    }
    return out;
}

} // namespace OpenSWMMVis::Groundwater
