/*!
 * \file   swmmcurvepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [CURVES]. Scalar surface today:
 * curveType (12-value enum) + pointCount (read-only — count of X/Y rows).
 * The full X/Y grid lands as a `ButtonCellDelegate` "Edit…" trigger in
 * the BQ structured editor; for now, the adapter surfaces a summary
 * string `pointSummary`.
 */

#ifndef SWMMCURVEPROPERTYADAPTER_H
#define SWMMCURVEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMCurvePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(int     curveType    READ curveType    NOTIFY changed)
    Q_PROPERTY(int     pointCount   READ pointCount   NOTIFY changed)
    Q_PROPERTY(QString pointSummary READ pointSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /*! 0=STORAGE, 1=DIVERSION, 2=TIDAL, 3=RATING, 4=CONTROL, 5=SHAPE,
     *  6..11=PUMP1..PUMP5 (engine canonical encoding). */
    [[nodiscard]] int     curveType()    const;
    [[nodiscard]] int     pointCount()   const;
    /*! Short human-readable description of the X/Y grid for the
     *  Property Browser cell (e.g. "5 rows, X: 0–120, Y: 0–25"). */
    [[nodiscard]] QString pointSummary() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMCURVEPROPERTYADAPTER_H
