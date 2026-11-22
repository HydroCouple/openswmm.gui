/**
 * @file      swmmunits.h
 * @brief     The engine's unit factors and physical constants, defined once for the GUI.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMUNITS_H
#define SWMMUNITS_H

/**
 * @brief Unit factors and physical constants the GUI shares with the OpenSWMM engine.
 *
 * The engine's public C API exposes no conversion factors, so the GUI carries the
 * values it needs here, once, in the two families the engine's Constants.hpp and
 * UnitConversion.hpp keep apart:
 *
 * - legacy-faithful: the truncated literals legacy SWMM computes with (swmm5.c
 *   Ucf/Qcf, consts.h). Anything the GUI converts for or from the engine's own
 *   numbers (flow units, hot start files, result volumes) uses these, so GUI
 *   arithmetic lands on the engine's values.
 * - exact: full-precision factors for the GUI's own geometry, map scales and SI
 *   mesh work, where no legacy number is being matched.
 *
 * test_swmmunits pins the legacy family to the swmm5.c / consts.h values.
 */
namespace openswmmvis::core::units
{
// ---- legacy-faithful (engine Constants.hpp / UnitConversion.hpp) --------------

/// Display flow units → cfs, in the engine's FlowUnits order
/// (CFS, GPM, MGD, CMS, LPS, MLD): swmm5.c Qcf[].
inline constexpr double kQcf[6] = {1.0, 448.831, 0.64632, 0.02832, 28.317, 2.4466};

/// swmm5.c Ucf rows the GUI reads, indexed [US, SI]: divide a display value by
/// the factor for the engine's internal ft / ft² / ft³ / ft/s.
inline constexpr double kUcfRainfall[2] = {43200.0, 1097280.0};    ///< in/hr, mm/hr → ft/s
inline constexpr double kUcfLandArea[2] = {2.2956e-5, 0.92903e-5}; ///< ac, ha → ft²
inline constexpr double kUcfVolume[2] = {1.0, 0.02832};            ///< ft³, m³ → ft³

inline constexpr double kFtToM = 0.3048;        ///< ft → m (exact by definition; consts.h MperFT)
inline constexpr double kFt3ToL = 28.317;       ///< ft³ → L: consts.h LperFT3
inline constexpr double kFt2PerAcre = 43560.0;  ///< ft² per acre: swmm5.c Ucf[GWFLOW][US]
inline constexpr double kM2PerHectare = 10000.0; ///< m² per hectare
inline constexpr double kSecPerDay = 86400.0;   ///< consts.h SECperDAY
inline constexpr int kSecPerDayInt = 86400;     ///< for whole-second modulo and division
inline constexpr double kMsecPerDay = 8.64e7;   ///< consts.h MSECperDAY
inline constexpr long long kMsecPerDayInt = 86400000; ///< for whole-millisecond arithmetic
inline constexpr double kKelvinOffset = 273.15; ///< Kelvin reading of 0 °C

// ---- exact (GUI geometry, map scales, SI mesh engine) ---------------------------

inline constexpr double kPiExact = 3.14159265358979323846;
inline constexpr double kGravitySI = 9.80665;         ///< m/s², the 2D solver's gravity
inline constexpr double kMToFt = 1.0 / kFtToM;        ///< 3.280839895013123
inline constexpr double kFt3ToM3Exact = 0.028316846592; ///< 0.3048³
inline constexpr double kFt3ToLExact = 28.316846592;    ///< 1000 × 0.3048³
inline constexpr double kSqMPerAcre = 4046.8564224;     ///< 43560 × 0.3048²
} // namespace openswmmvis::core::units

#endif // SWMMUNITS_H
