/*!
 * \file   subcatchcompoundeditref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 3 — property-cell value identifying one compound subcatchment
 * attribute (land-use coverage / groundwater / LID usage). Mirror of
 * NodeCompoundEditRef; the matching SubcatchCompoundEditButton opens
 * SubcatchCompoundEditDialog, whose pages apply edits to the engine
 * directly (apply-as-you-go).
 */

#ifndef SUBCATCHCOMPOUNDEDITREF_H
#define SUBCATCHCOMPOUNDEDITREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

class SWMMModelLayer;

/*! Identifies one editable compound attribute on a single subcatchment. */
struct SubcatchCompoundEditRef
{
    enum Kind {
        LandUse     = 0,  ///< [COVERAGES] per-landuse coverage percents
        Groundwater = 1,  ///< [GROUNDWATER] aquifer + flow routing params
        LidUsage    = 2,  ///< [LID_USAGE] LID controls assigned to the subcatch
        Loadings    = 3,  ///< [LOADINGS] initial pollutant buildup (iter. 4)
    };

    SWMM_Engine     engine   = nullptr;  ///< Engine handle (borrow, not owned)
    QString         subName;             ///< Owning subcatchment id
    Kind            kind     = LandUse;
    QString         summary;             ///< Short text shown in the cell
    SWMMModelLayer *layer    = nullptr;  ///< Owning layer (borrow) for pickers

    bool operator==(const SubcatchCompoundEditRef &other) const noexcept
    {
        return engine == other.engine && subName == other.subName
               && kind == other.kind && summary == other.summary
               && layer == other.layer;
    }
};

Q_DECLARE_METATYPE(SubcatchCompoundEditRef)

/*! Install the `SubcatchCompoundEditRef → QString` converter (idempotent). */
void registerSubcatchCompoundEditRefConverter();

#endif // SUBCATCHCOMPOUNDEDITREF_H
