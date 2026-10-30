/**
 * @file      vjsourcesummary.h
 * @brief     Human-readable summary of the lateral sources attached to a node,
 *         for the virtual-junction delete / re-fuse prompts.
 *
 * Point laterals are allowed at a virtual junction (engine plan
 * VJ_LATERAL_INFLOW_PLAN_2026-09-04). Re-fusing one runs the engine's
 * node-delete cascade: its [INFLOWS] / [DWF] / RDII rows are erased and any
 * subcatchment that drained to it loses its outlet. The prompts say so, and
 * this pure engine query builds that sentence so it can be unit-tested
 * without the map stack.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
 *  no C-API accessor and are not reported.)
 * @param[in] engine Borrowed stable engine handle; null returns empty.
 * @param[in] nodeIdx Nonnegative target node index; any node kind is accepted and upper bounds are not checked directly.
 * @return Translated descriptive sentences for matching external-inflow/DWF/RDII row counts and quoted surface-outlet subcatchment names, or empty for no matches. Unreadable rows are skipped.
 * @note Pure query only: does not delete/refuse anything, scan groundwater routing or report every possible dependent object. Caller serializes engine access; text is for UI prompts, not a complete dependency inventory.
 */
QString lateralSourceSummary(SWMM_Engine engine, int nodeIdx);

} // namespace OpenSWMMVis::VirtualJunction

#endif // OPENSWMM_LAYERS_VJSOURCESUMMARY_H
