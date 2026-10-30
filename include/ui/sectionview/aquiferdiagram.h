/**
 * @file      aquiferdiagram.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Two-zone groundwater (aquifer) illustration with parameter callouts.
 *
 * Phase G2 (workplans/AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md).
 *
 * Takes a plain input struct rather than an AquiferProvider* so the drawing
 * code carries no dependency on aquifer/ — the dialog fills the struct from
 * its widgets. Keeps the test link chain to sectiondiagram.cpp alone, exactly
 * as lidlayerdiagram does.
 *
 * The twelve [AQUIFERS] values are reported in callouts; zone geometry is a
 * schematic with minimum visible heights. The ground surface and
 * the lateral-flow arrow to the receiving node belong to the subcatchment that
 * uses the aquifer, so they are drawn schematically and labelled
 * "per subcatchment" rather than given invented numbers (plan D4).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_AQUIFERDIAGRAM_H
#define OPENSWMMVIS_SECTIONVIEW_AQUIFERDIAGRAM_H

#include <QString>
#include <QStringList>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview
{

/*! Everything the diagram needs about the aquifer being edited.
 *
 *  The twelve values are in input-file units (the same columns as the
 *  [AQUIFERS] line); the diagram never converts. */
struct AquiferDiagramInput {
    QString name;

    double porosity = 0.0;
    double wiltingPoint = 0.0;
    double fieldCapacity = 0.0;
    double conductivity = 0.0;   //!< Ksat, rate units.
    double conductSlope = 0.0;   //!< Kslope.
    double tensionSlope = 0.0;   //!< Tslope.
    double upperEvapFrac = 0.0;  //!< ETu.
    double lowerEvapDepth = 0.0; //!< ETs, length units.
    double lowerLossCoeff = 0.0; //!< Seep, rate units.
    double bottomElev = 0.0;     //!< Ebot, length units.
    double waterTableElev = 0.0; //!< Egw, length units.
    double upperMoisture = 0.0;  //!< Umc.

    QString evapPattern; //!< Upper-zone ET pattern name; empty = none.
    QString lengthLabel = QStringLiteral("ft");
    QString rateLabel = QStringLiteral("in/hr");

    /*! SWMM_AquiferParam code (0..11, the AquiferProvider::Param order) of the
     *  form field that has focus; its callout is emphasised. -1 = none. */
    int activeParam = -1;

    /*! Soft-validation texts (plan D6); each is drawn as a warning callout. */
    QStringList warnings;
};

/*! Prefix put on the emphasised callout's text (the leader model carries no
 *  accent flag; dimension lines use their own `accent`).
 * @return Owned QString containing ▶ plus space, prepended to focused parameter callout. No
 *   model/palette access.
 */
[[nodiscard]] QString aquiferActiveCalloutPrefix();

/*! Prefix put on each warning callout's text.
 * @return Owned QString containing ⚠ plus space for caller-supplied validation messages. No
 *   validation performed.
 */
[[nodiscard]] QString aquiferWarningCalloutPrefix();

/*!
 * \brief Build the two-zone groundwater illustration for one aquifer.
 *
 * The saturated zone is drawn to the thickness Egw − Ebot when Egw > Ebot;
 * otherwise it collapses to a hatched "unknown" slab at the bottom. The
 * unsaturated zone above it has no engine-known thickness (the surface is a
 * subcatchment property), so it is drawn tall enough to hold the lower
 * evaporation depth ETs and never thinner than a fraction of the saturated
 * zone.
 * @param[in] in Borrowed aquifer inputs, finite coherent elevations/depths expected. Numeric values
 *   remain input units; length/rate labels copied verbatim. activeParam 0..11 highlights matching
 *   callout, other values none.
 * @return Independent two-zone schematic model with parameter leaders, ET/percolation arrows,
 *   receiving-node glyph and caller warnings. Nonempty even for default/invalid physical values;
 *   this builder does not validate engine feasibility or generate warnings automatically.
 * @note Saturated thickness label uses waterTable-bottom when positive; drawn slab has minimum
 *   visibility floor and unknown slab fallback. Unsaturated
 *   height=max(lowerEvapDepth,0.35*satHeight), fallback 1 when nonpositive, nominal half-width 1
 *   and nonuniform scaling. Surface/node/flux arrows are schematic, not model-owned surface
 *   elevation, path or computed flows. Lower evaporation reach clipped for drawing; values in
 *   labels unchanged. No aquifer/provider/engine mutation or unit conversion.
 */
[[nodiscard]] SectionDiagramModel buildAquiferDiagram(const AquiferDiagramInput &in);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_AQUIFERDIAGRAM_H
