/*!
 * \file   swmmtimeseriespropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [TIMESERIES]. Scalar surface:
 * pointCount (read-only) + a summary string. Full grid editing lives in
 * the BQ TimeSeriesEditor; date/time/value triples are surfaced through
 * the same `(x, y)` engine call pair the curve adapter uses since SWMM
 * stores time series as ordered (datetime, value) tables.
 */

#ifndef SWMMTIMESERIESPROPERTYADAPTER_H
#define SWMMTIMESERIESPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMTimeSeriesPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(int     pointCount   READ pointCount   NOTIFY changed)
    Q_PROPERTY(QString pointSummary READ pointSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] int     pointCount()   const;
    [[nodiscard]] QString pointSummary() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMTIMESERIESPROPERTYADAPTER_H
