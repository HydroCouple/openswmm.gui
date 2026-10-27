/**
 * @file      resultdescriptor.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Y2b-1 (amendment D-Y4) — descriptor labels + the kind-keyed list.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/resultdescriptor.h"

#include "layers/speciesattributes.h"

namespace openswmmvis::plot
{

QString ResultDescriptor::label() const
{
    if (isObservation())
        return observationLabel.isEmpty() ? observationKey : observationLabel;
    if (isSpecies())
        return OpenSWMMVis::Species::speciesDisplayLabel(species);
    if (isMeshVariable())
        return variableLabel.isEmpty() ? variableKey : variableLabel;
    return labelFor(attr);
}

QString ResultDescriptor::unitLabel(UnitSystem u, const QString &concentrationUnit) const
{
    if (isObservation())
        return observationUnits;
    if (isSpecies())
        return OpenSWMMVis::Species::speciesUnitLabel(species, concentrationUnit);
    if (isMeshVariable())
        return variableUnits;
    return unitsFor(attr, u);
}

} // namespace openswmmvis::plot
