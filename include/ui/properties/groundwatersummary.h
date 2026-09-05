/*!
 * \file   groundwatersummary.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One-line summary of a subcatchment's [GROUNDWATER] / [GWF]
 *         configuration, shared by the property browser's Groundwater
 *         row, the attribute table's Groundwater column and the
 *         GroundwaterExchangeDialog so every surface reads the same text.
 */

#ifndef GROUNDWATERSUMMARY_H
#define GROUNDWATERSUMMARY_H

#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

/*! "(none)" when no aquifer is assigned; otherwise "AQ1 → J12" (or
 *  "AQ1 → (no node)"), with " (custom)" appended when either [GWF]
 *  expression (LATERAL / DEEP) is non-empty. `subIdx < 0` → "(none)". */
QString groundwaterSummary(SWMM_Engine engine, int subIdx);

#endif // GROUNDWATERSUMMARY_H
