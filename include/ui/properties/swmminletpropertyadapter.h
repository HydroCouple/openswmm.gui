/*!
 * \file   swmminletpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [INLETS]. Thin adapter:
 * engine ships `swmm_inlet_add(name, type)` + `_set_params` (write-only
 * block). File engine gap `DA-ENG-04` for per-field accessors (type
 * string + length / width / clogging factor / cross slope etc.). BO
 * InletEditor (Slice 6.5) ships the full surface.
 */

#ifndef SWMMINLETPROPERTYADAPTER_H
#define SWMMINLETPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMInletPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMINLETPROPERTYADAPTER_H
