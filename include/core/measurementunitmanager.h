/**
 * @file      measurementunitmanager.h
 * @brief     Utility class for converting between measurement units used in GIS operations.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MEASUREMENTUNITMANAGER_H
#define MEASUREMENTUNITMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

/*!
 * \class MeasurementUnitManager
 * \brief Utility class for converting between measurement units used in GIS operations.
 * \details Provides conversion factors and display symbols for distance and area
 *          units.  Used by OpenSWMMVisMapToolMeasure and the status bar to format
 *          measurement results.  All conversions pass through SI base units
 *          (meters for distance, square meters for area).
 */
class MeasurementUnitManager : public QObject
{
    Q_OBJECT

public:
    // ----- Distance units ------------------------------------------------

    /*!
     * \enum DistanceUnit
     * \brief Supported distance units.
     */
    enum DistanceUnit {
        Metres,
        Kilometres,
        Feet,
        USFeet, /*!< US survey foot (1 200/3 937 m). */
        Miles,
        NauticalMiles,
        Yards,
        InternationalInches,
    };

    /*!
     * \brief Returns the conversion factor from meters to the target unit.
     * @param[in] unit Target distance unit; an unrecognized enum falls back to meters.
     * @return Target units per meter (multiply an SI distance by this factor).
     */
    [[nodiscard]] static double metresTo(DistanceUnit unit);

    /*!
     * \brief Converts \p value from the source unit to meters.
     * @param[in] value Signed distance in @p unit; no range or finiteness check is applied.
     * @param[in] unit Source unit; an unrecognized enum uses meters.
     * @return Distance in meters, preserving the sign of @p value.
     */
    [[nodiscard]] static double toMetres(double value, DistanceUnit unit);

    /*!
     * \brief Converts \p value between two distance units.
     * @param[in] value Signed source distance; no range or finiteness check is applied.
     * @param[in] from Source unit; an unrecognized enum uses meters.
     * @param[in] to Destination unit; an unrecognized enum uses meters.
     * @return Distance in @p to, converted through meters.
     */
    [[nodiscard]] static double convert(double value, DistanceUnit from, DistanceUnit to);

    /*!
     * \brief Returns the abbreviated unit symbol (e.g. "m", "km", "ft").
     * @param[in] unit Unit whose untranslated display abbreviation is requested.
     * @return Symbol, distinguishing international feet (ft) from US survey feet (US ft); empty for an unknown enum.
     */
    [[nodiscard]] static QString distanceUnitSymbol(DistanceUnit unit);

    /*!
     * \brief Returns the full unit name (e.g. "Meters", "US survey feet").
     * @param[in] unit Unit whose translated name is requested.
     * @return Translated name, or an empty string for an unknown enum.
     */
    [[nodiscard]] static QString distanceUnitName(DistanceUnit unit);

    /*!
     * \brief Returns a list of all supported distance unit names.
     * @return Translated names in DistanceUnit enumeration order, suitable for an index-based selector.
     */
    [[nodiscard]] static QStringList distanceUnitNames();

    // ----- Area units ----------------------------------------------------

    /*!
     * \enum AreaUnit
     * \brief Supported area units.
     */
    enum AreaUnit {
        SquareMetres,
        SquareKilometres,
        Hectares,
        Acres,
        SquareFeet,
        SquareMiles,
        SquareYards,
    };

    /*!
     * \brief Returns the conversion factor from square meters to the target unit.
     * @param[in] unit Target area unit; an unrecognized enum falls back to square meters.
     * @return Target units per square meter.
     */
    [[nodiscard]] static double squareMetresTo(AreaUnit unit);

    /*!
     * \brief Converts \p value from the source unit to square meters.
     * @param[in] value Signed area expressed in @p unit; no validation is applied.
     * @param[in] unit Source area unit; an unrecognized enum uses square meters.
     * @return Area in square meters.
     */
    [[nodiscard]] static double toSquareMetres(double value, AreaUnit unit);

    /*!
     * \brief Converts \p value between two area units.
     * @param[in] value Signed area expressed in @p from; no validation is applied.
     * @param[in] from Source area unit; an unrecognized enum uses square meters.
     * @param[in] to Destination area unit; an unrecognized enum uses square meters.
     * @return Area expressed in @p to.
     */
    [[nodiscard]] static double convertArea(double value, AreaUnit from, AreaUnit to);

    /*!
     * \brief Returns the abbreviated unit symbol (e.g. "m²", "ha", "ac").
     * @param[in] unit Unit whose untranslated display abbreviation is requested.
     * @return Unit symbol, or an empty string for an unknown enum.
     */
    [[nodiscard]] static QString areaUnitSymbol(AreaUnit unit);

    /*!
     * \brief Returns the full unit name (e.g. "Square meters", "Hectares").
     * @param[in] unit Unit whose translated name is requested.
     * @return Translated name, or an empty string for an unknown enum.
     */
    [[nodiscard]] static QString areaUnitName(AreaUnit unit);

    /*!
     * \brief Returns a list of all supported area unit names.
     * @return Translated names in AreaUnit enumeration order.
     */
    [[nodiscard]] static QStringList areaUnitNames();

    // ----- Formatting helpers --------------------------------------------

    /*!
     * \brief Formats a distance value with the appropriate unit symbol.
     * \details Automatically scales to a more readable unit when the value
     *          exceeds a threshold (e.g. >= 1000 m → km).
     * \param metres        Distance in meters.
     * \param preferredUnit Preferred output unit; pass Metres to auto-select.
     * \param decimals      Number of decimal places.
     * @return Current-locale fixed-point number, a space, and the unit symbol.
     * @details Auto-selection uses the absolute distance: at least 1000 m selects kilometers; smaller magnitudes remain meters. Other preferred units are used directly. Precision is passed unchanged to QLocale.
     */
    [[nodiscard]] static QString formatDistance(double metres, DistanceUnit preferredUnit = Metres,
                                                int decimals = 3);

    /*!
     * \brief Formats an area value with the appropriate unit symbol.
     * \param squareMetres  Area in square meters.
     * \param preferredUnit Preferred output unit; pass SquareMetres to auto-select.
     * \param decimals      Number of decimal places.

     * @return Current-locale fixed-point number, a space, and the unit symbol.
     * @details With SquareMetres preference, absolute areas at least 1,000,000 m² use square kilometers, at least 10,000 m² use hectares, and smaller areas use square meters. Precision is passed unchanged to QLocale.
     */
    [[nodiscard]] static QString
    formatArea(double squareMetres, AreaUnit preferredUnit = SquareMetres, int decimals = 3);
};

#endif // MEASUREMENTUNITMANAGER_H
