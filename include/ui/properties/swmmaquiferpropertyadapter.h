/*!
 * \file   swmmaquiferpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [AQUIFERS]. Thin adapter:
 * the engine ships `swmm_aquifer_add` only — no per-field getters today.
 * File engine gap `DA-ENG-06` for the aquifer accessor surface
 * (porosity, wilting point, field capacity, ksat, etc.). Until then the
 * adapter exposes the name (rename) and the BO AquiferEditor takes the
 * full scalar surface.
 */

#ifndef SWMMAQUIFERPROPERTYADAPTER_H
#define SWMMAQUIFERPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMAquiferPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMAQUIFERPROPERTYADAPTER_H
