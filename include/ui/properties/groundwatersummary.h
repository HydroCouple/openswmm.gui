/**
 * @file      groundwatersummary.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One-line summary of a subcatchment's [GROUNDWATER] / [GWF]
 *         configuration, shared by the property browser's Groundwater
 *         row, the attribute table's Groundwater column and the
 *         GroundwaterExchangeDialog so every surface reads the same text.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef GROUNDWATERSUMMARY_H
#define GROUNDWATERSUMMARY_H

#include <QString>

#include <openswmm/engine/openswmm_callbacks.h> // SWMM_Engine typedef

/*! "(none)" when no aquifer is assigned; otherwise "AQ1 → J12" (or
 *  "AQ1 → (no node)"), with " (custom)" appended when either [GWF]
 *  expression (LATERAL / DEEP) is non-empty. `subIdx < 0` → "(none)".
 * @brief Describe the receiving aquifer/node and presence of custom groundwater expressions.
 * @param[in] engine Borrowed engine; nullptr is treated as unassigned.
 * @param[in] subIdx Zero-based engine subcatchment index; a negative index is unassigned.
 * @return Display summary; missing aquifer/read failures resolve to the none label without a separate error result.
 */
QString groundwaterSummary(SWMM_Engine engine, int subIdx);

#endif // GROUNDWATERSUMMARY_H
