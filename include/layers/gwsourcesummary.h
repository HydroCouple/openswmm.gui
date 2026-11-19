/**
 * @file      gwsourcesummary.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Which subcatchments discharge groundwater to a node — the node-side
 *         navigational summary of [GROUNDWATER] (AQUIFER_GROUNDWATER_EXCHANGE
 *         plan G5). Pure engine query, like vjsourcesummary, so the property
 *         browser row, the attribute-table cell and the delete prompts all
 *         agree and it can be unit-tested without the map stack.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_LAYERS_GWSOURCESUMMARY_H
#define OPENSWMM_LAYERS_GWSOURCESUMMARY_H

#include <QStringList>

#include <openswmm/engine/openswmm_engine.h>

namespace OpenSWMMVis::Groundwater
{

/*! Ids of the subcatchments whose [GROUNDWATER] receiving node is \a nodeIdx
 *  and that have an aquifer assigned (a receiving node without an aquifer
 *  produces no flow), in subcatchment order. Empty for a bad handle/index.
 * @param[in] engine Borrowed stable engine handle; null returns empty. No engine mutation occurs.
 * @param[in] nodeIdx Nonnegative target node index. Upper bounds are not checked directly; matching groundwater getters establish any matches.
 * @return Owned UTF-8-decoded IDs in subcatchment order, only for matching receiving node with an assigned aquifer. Getter failures/missing IDs are skipped without diagnostics. Caller serializes engine access.
 */
QStringList groundwaterSourceSubcatchments(SWMM_Engine engine, int nodeIdx);

} // namespace OpenSWMMVis::Groundwater

#endif // OPENSWMM_LAYERS_GWSOURCESUMMARY_H
