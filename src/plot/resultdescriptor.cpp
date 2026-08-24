/*!
 * \file   resultdescriptor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Y2b-1 (amendment D-Y4) — descriptor labels + the kind-keyed list.
 */
#include "plot/resultdescriptor.h"

#include "layers/speciesattributes.h"

namespace openswmmvis::plot {

QString ResultDescriptor::label() const
{
    if (isSpecies())
        return OpenSWMMVis::Species::speciesDisplayLabel(species);
    return labelFor(attr);
}

QString ResultDescriptor::unitLabel(UnitSystem u,
                                    const QString &concentrationUnit) const
{
    if (isSpecies())
        return OpenSWMMVis::Species::speciesUnitLabel(species,
                                                      concentrationUnit);
    return unitsFor(attr, u);
}

} // namespace openswmmvis::plot
