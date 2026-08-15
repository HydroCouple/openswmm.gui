/*!
 * \file   swmmstreetpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [STREETS]. Thin adapter:
 * engine ships `swmm_street_add` + `_set_params` (write-only block).
 * File engine gap `DA-ENG-10` for per-field accessors (max depth,
 * sidewalls, gutter, sidewalk geometry). BN StreetEditor (Slice 6.4.6)
 * ships the full surface.
 */

#ifndef SWMMSTREETPROPERTYADAPTER_H
#define SWMMSTREETPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMStreetPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMSTREETPROPERTYADAPTER_H
