/*!
 * \file   measurementunitmanager.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/measurementunitmanager.h"

#include <QLocale>
#include <cmath>

// ---------------------------------------------------------------------------
// Distance unit tables
// ---------------------------------------------------------------------------

static constexpr double kMetresToKm           =  0.001;
static constexpr double kMetresToFeet         =  3.280839895013123;
static constexpr double kMetresToUSFeet       = (3937.0 / 1200.0);
static constexpr double kMetresToMiles        =  1.0 / 1609.344;
static constexpr double kMetresToNautical     =  1.0 / 1852.0;
static constexpr double kMetresToYards        =  1.0936132983377078;
static constexpr double kMetresToInches       = 39.37007874015748;

double MeasurementUnitManager::metresTo(DistanceUnit unit)
{
    switch (unit)
    {
    case Metres:                return 1.0;
    case Kilometres:            return kMetresToKm;
    case Feet:                  return kMetresToFeet;
    case USFeet:                return kMetresToUSFeet;
    case Miles:                 return kMetresToMiles;
    case NauticalMiles:         return kMetresToNautical;
    case Yards:                 return kMetresToYards;
    case InternationalInches:   return kMetresToInches;
    }
    return 1.0;
}

double MeasurementUnitManager::toMetres(double value, DistanceUnit unit)
{
    return value / metresTo(unit);
}

double MeasurementUnitManager::convert(double value, DistanceUnit from, DistanceUnit to)
{
    return toMetres(value, from) * metresTo(to);
}

QString MeasurementUnitManager::distanceUnitSymbol(DistanceUnit unit)
{
    switch (unit)
    {
    case Metres:                return QStringLiteral("m");
    case Kilometres:            return QStringLiteral("km");
    case Feet:                  return QStringLiteral("ft");
    case USFeet:                return QStringLiteral("US ft");
    case Miles:                 return QStringLiteral("mi");
    case NauticalMiles:         return QStringLiteral("nmi");
    case Yards:                 return QStringLiteral("yd");
    case InternationalInches:   return QStringLiteral("in");
    }
    return {};
}

QString MeasurementUnitManager::distanceUnitName(DistanceUnit unit)
{
    switch (unit)
    {
    case Metres:                return QObject::tr("Metres");
    case Kilometres:            return QObject::tr("Kilometres");
    case Feet:                  return QObject::tr("Feet");
    case USFeet:                return QObject::tr("US survey feet");
    case Miles:                 return QObject::tr("Miles");
    case NauticalMiles:         return QObject::tr("Nautical miles");
    case Yards:                 return QObject::tr("Yards");
    case InternationalInches:   return QObject::tr("Inches");
    }
    return {};
}

QStringList MeasurementUnitManager::distanceUnitNames()
{
    return {
        distanceUnitName(Metres),
        distanceUnitName(Kilometres),
        distanceUnitName(Feet),
        distanceUnitName(USFeet),
        distanceUnitName(Miles),
        distanceUnitName(NauticalMiles),
        distanceUnitName(Yards),
        distanceUnitName(InternationalInches),
    };
}

// ---------------------------------------------------------------------------
// Area unit tables
// ---------------------------------------------------------------------------

static constexpr double kSqmToSqKm   = 1.0e-6;
static constexpr double kSqmToHa     = 1.0e-4;
static constexpr double kSqmToAcres  = 1.0 / 4046.8564224;
static constexpr double kSqmToSqFt   = 10.763910417;
static constexpr double kSqmToSqMi   = 1.0 / 2589988.110336;
static constexpr double kSqmToSqYd   = 1.19599005;

double MeasurementUnitManager::squareMetresTo(AreaUnit unit)
{
    switch (unit)
    {
    case SquareMetres:      return 1.0;
    case SquareKilometres:  return kSqmToSqKm;
    case Hectares:          return kSqmToHa;
    case Acres:             return kSqmToAcres;
    case SquareFeet:        return kSqmToSqFt;
    case SquareMiles:       return kSqmToSqMi;
    case SquareYards:       return kSqmToSqYd;
    }
    return 1.0;
}

double MeasurementUnitManager::toSquareMetres(double value, AreaUnit unit)
{
    return value / squareMetresTo(unit);
}

double MeasurementUnitManager::convertArea(double value, AreaUnit from, AreaUnit to)
{
    return toSquareMetres(value, from) * squareMetresTo(to);
}

QString MeasurementUnitManager::areaUnitSymbol(AreaUnit unit)
{
    switch (unit)
    {
    case SquareMetres:      return QStringLiteral("m\u00B2");
    case SquareKilometres:  return QStringLiteral("km\u00B2");
    case Hectares:          return QStringLiteral("ha");
    case Acres:             return QStringLiteral("ac");
    case SquareFeet:        return QStringLiteral("ft\u00B2");
    case SquareMiles:       return QStringLiteral("mi\u00B2");
    case SquareYards:       return QStringLiteral("yd\u00B2");
    }
    return {};
}

QString MeasurementUnitManager::areaUnitName(AreaUnit unit)
{
    switch (unit)
    {
    case SquareMetres:      return QObject::tr("Square metres");
    case SquareKilometres:  return QObject::tr("Square kilometres");
    case Hectares:          return QObject::tr("Hectares");
    case Acres:             return QObject::tr("Acres");
    case SquareFeet:        return QObject::tr("Square feet");
    case SquareMiles:       return QObject::tr("Square miles");
    case SquareYards:       return QObject::tr("Square yards");
    }
    return {};
}

QStringList MeasurementUnitManager::areaUnitNames()
{
    return {
        areaUnitName(SquareMetres),
        areaUnitName(SquareKilometres),
        areaUnitName(Hectares),
        areaUnitName(Acres),
        areaUnitName(SquareFeet),
        areaUnitName(SquareMiles),
        areaUnitName(SquareYards),
    };
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

QString MeasurementUnitManager::formatDistance(double metres,
                                               DistanceUnit preferredUnit,
                                               int decimals)
{
    DistanceUnit displayUnit = preferredUnit;

    if (preferredUnit == Metres)
    {
        // Auto-select: use km when >= 1000 m, nm never auto-selected
        if (std::abs(metres) >= 1000.0)
            displayUnit = Kilometres;
        else
            displayUnit = Metres;
    }

    const double value = metres * metresTo(displayUnit);
    return QLocale().toString(value, 'f', decimals)
           + QLatin1Char(' ')
           + distanceUnitSymbol(displayUnit);
}

QString MeasurementUnitManager::formatArea(double squareMetres,
                                           AreaUnit preferredUnit,
                                           int decimals)
{
    AreaUnit displayUnit = preferredUnit;

    if (preferredUnit == SquareMetres)
    {
        if (std::abs(squareMetres) >= 1.0e6)
            displayUnit = SquareKilometres;
        else if (std::abs(squareMetres) >= 10000.0)
            displayUnit = Hectares;
        else
            displayUnit = SquareMetres;
    }

    const double value = squareMetres * squareMetresTo(displayUnit);
    return QLocale().toString(value, 'f', decimals)
           + QLatin1Char(' ')
           + areaUnitSymbol(displayUnit);
}
