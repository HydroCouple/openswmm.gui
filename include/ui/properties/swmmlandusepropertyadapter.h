/*!
 * \file   swmmlandusepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [LANDUSES]. Scalar coverage
 * is sweepInterval + sweepRemoval; build-up / wash-off curve grids
 * land in the BP structured editor.
 */

#ifndef SWMMLANDUSEPROPERTYADAPTER_H
#define SWMMLANDUSEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMLandUsePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double sweepInterval READ sweepInterval WRITE setSweepInterval NOTIFY changed)
    Q_PROPERTY(double sweepRemoval  READ sweepRemoval  WRITE setSweepRemoval  NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] double sweepInterval() const;
    [[nodiscard]] double sweepRemoval()  const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setSweepInterval(double v);
    void setSweepRemoval(double v);

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMLANDUSEPROPERTYADAPTER_H
