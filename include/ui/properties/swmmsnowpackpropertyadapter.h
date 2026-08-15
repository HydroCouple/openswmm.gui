/*!
 * \file   swmmsnowpackpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [SNOWPACKS]. Thin adapter:
 * engine ships `swmm_snowpack_add` only. File engine gap `DA-ENG-07`
 * for per-field accessors (plowable / impervious / pervious snow melt
 * parameters). BP SnowpackEditor (Slice 6.6) carries the full surface.
 */

#ifndef SWMMSNOWPACKPROPERTYADAPTER_H
#define SWMMSNOWPACKPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMSnowpackPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMSNOWPACKPROPERTYADAPTER_H
