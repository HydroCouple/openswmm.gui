/**
 * @file      swmmtimeseriespropertyadapter.h
 * @brief     Property-tree adapter for [TIMESERIES].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [TIMESERIES]. Scalar surface:
 * pointCount (read-only) + a summary string. Full grid editing lives in
 * the BQ TimeSeriesEditor; date/time/value triples are surfaced through
 * the same `(x, y)` engine call pair the curve adapter uses since SWMM
 * stores time series as ordered (datetime, value) tables.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMTIMESERIESPROPERTYADAPTER_H
#define SWMMTIMESERIESPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMTimeSeriesPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(QString pointSummary READ pointSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /**
     * @return Number of authored timestamp/value samples; zero for unresolved/read failure.
     */
    [[nodiscard]] int pointCount() const;
    /**
     * @return Empty for unresolved series, an empty marker for zero samples, or a localized sample-count summary. It does not materialize external source files.
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

#endif // SWMMTIMESERIESPROPERTYADAPTER_H
