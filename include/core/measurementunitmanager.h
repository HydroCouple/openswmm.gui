/*!
 * \file   measurementunitmanager.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
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
 *          (metres for distance, square metres for area).
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
    enum DistanceUnit
    {
        Metres,
        Kilometres,
        Feet,
        USFeet,           /*!< US survey foot (1 200/3 937 m). */
        Miles,
        NauticalMiles,
        Yards,
        InternationalInches,
    };

    /*!
     * \brief Returns the conversion factor from metres to the target unit.
     * \param unit  Target distance unit.
     */
    [[nodiscard]] static double metresTo(DistanceUnit unit);

    /*!
     * \brief Converts \p value from the source unit to metres.
     */
    [[nodiscard]] static double toMetres(double value, DistanceUnit unit);

    /*!
     * \brief Converts \p value between two distance units.
     */
    [[nodiscard]] static double convert(double value, DistanceUnit from, DistanceUnit to);

    /*!
     * \brief Returns the abbreviated unit symbol (e.g. "m", "km", "ft").
     */
    [[nodiscard]] static QString distanceUnitSymbol(DistanceUnit unit);

    /*!
     * \brief Returns the full unit name (e.g. "Metres", "US survey feet").
     */
    [[nodiscard]] static QString distanceUnitName(DistanceUnit unit);

    /*!
     * \brief Returns a list of all supported distance unit names.
     */
    [[nodiscard]] static QStringList distanceUnitNames();

    // ----- Area units ----------------------------------------------------

    /*!
     * \enum AreaUnit
     * \brief Supported area units.
     */
    enum AreaUnit
    {
        SquareMetres,
        SquareKilometres,
        Hectares,
        Acres,
        SquareFeet,
        SquareMiles,
        SquareYards,
    };

    /*!
     * \brief Returns the conversion factor from square metres to the target unit.
     */
    [[nodiscard]] static double squareMetresTo(AreaUnit unit);

    /*!
     * \brief Converts \p value from the source unit to square metres.
     */
    [[nodiscard]] static double toSquareMetres(double value, AreaUnit unit);

    /*!
     * \brief Converts \p value between two area units.
     */
    [[nodiscard]] static double convertArea(double value, AreaUnit from, AreaUnit to);

    /*!
     * \brief Returns the abbreviated unit symbol (e.g. "m²", "ha", "ac").
     */
    [[nodiscard]] static QString areaUnitSymbol(AreaUnit unit);

    /*!
     * \brief Returns the full unit name (e.g. "Square metres", "Hectares").
     */
    [[nodiscard]] static QString areaUnitName(AreaUnit unit);

    /*!
     * \brief Returns a list of all supported area unit names.
     */
    [[nodiscard]] static QStringList areaUnitNames();

    // ----- Formatting helpers --------------------------------------------

    /*!
     * \brief Formats a distance value with the appropriate unit symbol.
     * \details Automatically scales to a more readable unit when the value
     *          exceeds a threshold (e.g. >= 1000 m → km).
     * \param metres        Distance in metres.
     * \param preferredUnit Preferred output unit; pass Metres to auto-select.
     * \param decimals      Number of decimal places.
     */
    [[nodiscard]] static QString formatDistance(double metres,
                                                DistanceUnit preferredUnit = Metres,
                                                int decimals = 3);

    /*!
     * \brief Formats an area value with the appropriate unit symbol.
     * \param squareMetres  Area in square metres.
     * \param preferredUnit Preferred output unit; pass SquareMetres to auto-select.
     * \param decimals      Number of decimal places.
     */
    [[nodiscard]] static QString formatArea(double squareMetres,
                                            AreaUnit preferredUnit = SquareMetres,
                                            int decimals = 3);
};

#endif // MEASUREMENTUNITMANAGER_H
