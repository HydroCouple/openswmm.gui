/*!
 * \file speciesattributes.cpp
 * \brief Implementation of the dynamic species attribute helpers (Y2a).
 *
 * \see include/layers/speciesattributes.h
 */

#include "layers/speciesattributes.h"

#include <QCoreApplication>

namespace OpenSWMMVis::Species
{

QString speciesAttributeName(const QString &species)
{
    if (species.isEmpty()) return {};
    return QLatin1String(kSpeciesAttrPrefix) + species;
}

bool isSpeciesAttribute(const QString &attr)
{
    return attr.startsWith(QLatin1String(kSpeciesAttrPrefix));
}

QString speciesFromAttribute(const QString &attr)
{
    if (!isSpeciesAttribute(attr)) return {};
    // qsizetype in Qt6; the prefix is ASCII so size() is the char count.
    return attr.mid(static_cast<int>(qstrlen(kSpeciesAttrPrefix)));
}

bool isReservedSpecies(const QString &species)
{
    return species == QLatin1String(kWaterAgeName)
        || species == QLatin1String(kTemperatureName);
}

QString speciesDisplayLabel(const QString &species)
{
    if (species == QLatin1String(kWaterAgeName))
        return QCoreApplication::translate("Species", "Water age (hours)");
    if (species == QLatin1String(kTemperatureName))
        return QCoreApplication::translate("Species", "Temperature (°C)");
    return species;
}

QString speciesUnitLabel(const QString &species,
                         const QString &concentrationUnit)
{
    // The `.out` unit enum has no HOURS or DEGC slot — engine A2b keyed
    // those on the species NAME instead, and this is the consumer side of
    // that decision. Widening the enum would break every reader.
    if (species == QLatin1String(kWaterAgeName))    return QStringLiteral("h");
    if (species == QLatin1String(kTemperatureName)) return QStringLiteral("°C");
    return concentrationUnit;
}

int speciesOutCode(const QString &attr, const QStringList &species,
                   int pollutBase)
{
    const QString name = speciesFromAttribute(attr);
    if (name.isEmpty()) return -1;
    const int idx = species.indexOf(name);
    if (idx < 0) return -1;          // saved theme names a species this run
                                     // does not carry — caller skips.
    return pollutBase + idx;
}

} // namespace OpenSWMMVis::Species
