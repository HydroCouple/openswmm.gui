/*!
 * \file   aquiferdiagram.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-zone groundwater (aquifer) illustration with parameter callouts.
 *
 * Phase G2 (workplans/AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md).
 *
 * Takes a plain input struct rather than an AquiferProvider* so the drawing
 * code carries no dependency on aquifer/ — the dialog fills the struct from
 * its widgets. Keeps the test link chain to sectiondiagram.cpp alone, exactly
 * as lidlayerdiagram does.
 *
 * Only the twelve [AQUIFERS] values are drawn to scale. The ground surface and
 * the lateral-flow arrow to the receiving node belong to the subcatchment that
 * uses the aquifer, so they are drawn schematically and labelled
 * "per subcatchment" rather than given invented numbers (plan D4).
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_AQUIFERDIAGRAM_H
#define OPENSWMMVIS_SECTIONVIEW_AQUIFERDIAGRAM_H

#include <QString>
#include <QStringList>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview {

/*! Everything the diagram needs about the aquifer being edited.
 *
 *  The twelve values are in input-file units (the same columns as the
 *  [AQUIFERS] line); the diagram never converts. */
struct AquiferDiagramInput
{
    QString name;

    double porosity      = 0.0;
    double wiltingPoint  = 0.0;
    double fieldCapacity = 0.0;
    double conductivity  = 0.0;   //!< Ksat, rate units.
    double conductSlope  = 0.0;   //!< Kslope.
    double tensionSlope  = 0.0;   //!< Tslope.
    double upperEvapFrac = 0.0;   //!< ETu.
    double lowerEvapDepth = 0.0;  //!< ETs, length units.
    double lowerLossCoeff = 0.0;  //!< Seep, rate units.
    double bottomElev    = 0.0;   //!< Ebot, length units.
    double waterTableElev = 0.0;  //!< Egw, length units.
    double upperMoisture = 0.0;   //!< Umc.

    QString evapPattern;          //!< Upper-zone ET pattern name; empty = none.
    QString lengthLabel = QStringLiteral("ft");
    QString rateLabel   = QStringLiteral("in/hr");

    /*! SWMM_AquiferParam code (0..11, the AquiferProvider::Param order) of the
     *  form field that has focus; its callout is emphasised. -1 = none. */
    int activeParam = -1;

    /*! Soft-validation texts (plan D6); each is drawn as a warning callout. */
    QStringList warnings;
};

/*! Prefix put on the emphasised callout's text (the leader model carries no
 *  accent flag; dimension lines use their own `accent`). */
[[nodiscard]] QString aquiferActiveCalloutPrefix();

/*! Prefix put on each warning callout's text. */
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
 */
[[nodiscard]] SectionDiagramModel buildAquiferDiagram(const AquiferDiagramInput &in);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_AQUIFERDIAGRAM_H
