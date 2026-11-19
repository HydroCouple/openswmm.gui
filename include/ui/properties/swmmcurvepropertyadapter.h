/**
 * @file      swmmcurvepropertyadapter.h
 * @brief     Property-tree adapter for [CURVES].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [CURVES]. Scalar surface today:
 * curveType (12-value enum) + pointCount (read-only — count of X/Y rows).
 * The full X/Y grid lands as a `ButtonCellDelegate` "Edit…" trigger in
 * the BQ structured editor; for now, the adapter surfaces a summary
 * string `pointSummary`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMCURVEPROPERTYADAPTER_H
#define SWMMCURVEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMCurvePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(int curveType READ curveType NOTIFY changed)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(QString pointSummary READ pointSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /*! 0=STORAGE, 1=DIVERSION, 2=TIDAL, 3=RATING, 4=CONTROL, 5=SHAPE,
     *  6..11=PUMP1..PUMP5 (engine canonical encoding).
     * @return SWMM table/curve type integer; zero when unresolved/read fails, which is also the engine TIMESERIES type.
     */
    [[nodiscard]] int curveType() const;
    /**
     * @return Number of authored X/Y rows, or zero when unresolved/read fails.
     */
    [[nodiscard]] int pointCount() const;
    /*! Short human-readable description of the X/Y grid for the
     *  Property Browser cell (e.g. "5 rows, X: 0–120, Y: 0–25").
     * @return Empty for an unresolved curve, an empty marker for zero rows, or row count plus min/max X/Y from readable points. Units depend on curve type; no model writes occur.
     */
    [[nodiscard]] QString pointSummary() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

private:
    /**
     * @brief Resolve the stored object name against the current engine collection.
     * @return Zero-based live index, or -1 for a missing engine, empty name or unresolved ID; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
};

#endif // SWMMCURVEPROPERTYADAPTER_H
