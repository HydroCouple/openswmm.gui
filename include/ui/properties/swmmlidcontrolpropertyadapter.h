/*!
 * \file   swmmlidcontrolpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [LID_CONTROLS]. Thin adapter:
 * engine ships `swmm_lid_add` + `swmm_lid_set_{surface,soil,storage,drain}`
 * block setters but no getters. File engine gap `DA-ENG-08` for per-
 * layer accessors. BO LIDControlEditor (Slice 6.5) ships the full
 * 6-layer scalar surface (surface / soil / storage / drain / drainmat /
 * pavement).
 */

#ifndef SWMMLIDCONTROLPROPERTYADAPTER_H
#define SWMMLIDCONTROLPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMLIDControlPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMLIDCONTROLPROPERTYADAPTER_H
