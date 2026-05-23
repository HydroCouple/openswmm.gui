/*!
 * \file   swmmtransectpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [TRANSECTS]. Thin adapter:
 * engine ships `swmm_transect_add` + `_set_roughness` + `_add_station`
 * (write-only). File engine gap `DA-ENG-09` for per-field accessors
 * (roughness triple + station list). BQ TransectEditor (Slice 6.7)
 * ships the full station-elevation grid + bank stations.
 */

#ifndef SWMMTRANSECTPROPERTYADAPTER_H
#define SWMMTRANSECTPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMTransectPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMTRANSECTPROPERTYADAPTER_H
