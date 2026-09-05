/*!
 * \file   vjsourcesummary.h
 * \brief  Human-readable summary of the lateral sources attached to a node,
 *         for the virtual-junction delete / re-fuse prompts.
 *
 * Point laterals are allowed at a virtual junction (engine plan
 * VJ_LATERAL_INFLOW_PLAN_2026-09-04). Re-fusing one runs the engine's
 * node-delete cascade: its [INFLOWS] / [DWF] / RDII rows are erased and any
 * subcatchment that drained to it loses its outlet. The prompts say so, and
 * this pure engine query builds that sentence so it can be unit-tested
 * without the map stack.
 */

#ifndef OPENSWMM_LAYERS_VJSOURCESUMMARY_H
#define OPENSWMM_LAYERS_VJSOURCESUMMARY_H

#include <QString>

#include <openswmm/engine/openswmm_engine.h>

namespace OpenSWMMVis::VirtualJunction
{

/*! Sentence naming what re-fusing or deleting node \a nodeIdx would drop:
 *  the counts of its own [INFLOWS] / [DWF] / RDII rows and the subcatchments
 *  whose outlet it is. Empty when nothing targets the node. (LID drains have
 *  no C-API accessor and are not reported.) */
QString lateralSourceSummary(SWMM_Engine engine, int nodeIdx);

} // namespace OpenSWMMVis::VirtualJunction

#endif // OPENSWMM_LAYERS_VJSOURCESUMMARY_H
