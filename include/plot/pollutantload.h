/**
 * @file      pollutantload.h
 * @brief     R4 — pollutant load (flow x concentration) and cumulative mass
 *            series for 1D nodes and links, with the unit conversion the
 *            `.out` flow and concentration codes require.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/BELLINGE_GW_QUALITY_PHASE4B_AGENT_BRIEF_2026-10-30.md item 3.
 * Two descriptors per transported pollutant, offered beside its
 * concentration:
 *   - load rate  = flow x concentration AT REPORT STEPS (node: total inflow;
 *     link: flow signed in the link direction, so a link load is NET of
 *     reversing flow), in kg/d (SI flow units), lb/d (US) or count/d
 *     (COUNT/L pollutants);
 *   - cumulative = the time integral of those report-step samples (kg, lb
 *     or count) — each held over the interval ending at it
 *     (plot/seriesintegration.h), never a sum of samples.
 * Neither is the engine's own total: the `.rpt` load summaries integrate
 * every ROUTING step (and the Link Pollutant Load Summary sums |flow|), so
 * they can differ substantially when the report step is long (Bellinge,
 * 15-min reports: 1.86 vs 2.91 kg at one outfall). Labels and the
 * descriptor note (`ResultDescriptor::variableNote`) say so.
 * Conversions are exact (1 ft3 = 28.316846592 L, 1 gal = 3.785411784 L,
 * 1 lb = 453592.37 mg). The `.rpt` load summaries use SWMM's rounded
 * constants (28.317 L/ft3, 0.02832 m3/ft3, 2.203e-6 lb/mg), so they can sit
 * up to 0.011 % (SI) / 0.07 % (US outfall lb) below these values. For a link
 * whose flow never reverses the cumulative equals the `.rpt` "Link
 * Pollutant Load Summary" (which books |flow|); a reversing link nets out.
 *
 * Identity: the descriptors are keyed (`ResultDescriptor::variableKey`)
 * `pollutant_load:<species>` / `pollutant_load_cum:<species>` with the
 * species percent-encoded, so they persist by NAME like the species itself
 * (D-G1) and get their own comparison-plot rows.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_POLLUTANTLOAD_H
#define OPENSWMMVIS_PLOT_POLLUTANTLOAD_H

#include "plot/resultdescriptor.h"

#include <QString>

#include <vector>

namespace openswmmvis::plot::pollutantload
{

/*! Mass basis of one (flow units, concentration units) pair. */
struct LoadUnits {
    bool valid = false;
    QString rateUnits; ///< "kg/d", "lb/d" or "count/d"
    QString massUnits; ///< "kg", "lb" or "count"
    /*! Mass units per (flow unit x concentration unit x second). */
    double massPerFlowConcSecond = 0.0;
};

/*!
 * \brief Liters per second carried by one unit of the `.out` flow code.
 * \param flowUnitsCode 0 CFS, 1 GPM, 2 MGD, 3 CMS, 4 LPS, 5 MLD.
 * \return Exact factor, or NaN for an unknown code.
 */
double litresPerSecond(int flowUnitsCode);

/*!
 * \brief Name of a `.out` flow-units code ("CFS", "GPM", "MGD", "CMS", "LPS",
 *        "MLD"); empty for an unknown code. Flow columns of the `.out` are in
 *        these units (not always m3/s or ft3/s).
 */
QString flowUnitsName(int flowUnitsCode);

/*!
 * \brief Load units for a flow code and a `.out` concentration code.
 * \param flowUnitsCode `swmm_output_get_flow_units` value.
 * \param concentrationCode `swmm_output_get_pollut_units` value: 0 mg/L,
 *        1 ug/L, 2 count/L.
 * \return Invalid (valid == false) for unknown codes. Mass is kg for SI flow
 *         codes (CMS, LPS, MLD) and lb for US codes; counts stay counts.
 */
LoadUnits loadUnits(int flowUnitsCode, int concentrationCode);

/** @return Key of the load-rate descriptor for \p species. */
QString rateKey(const QString &species);
/** @return Key of the cumulative-load descriptor for \p species. */
QString cumulativeKey(const QString &species);
/*!
 * \brief Recognize a load key.
 * \param[out] species Decoded species name on success.
 * \param[out] cumulative True for the cumulative key.
 * \return False for any other key (outputs untouched).
 */
bool parseKey(const QString &key, QString *species, bool *cumulative);
/** @return True when \p d is a load-rate or cumulative-load descriptor. */
bool isLoadDescriptor(const ResultDescriptor &d);

/*!
 * \brief How a load series is computed and how it differs from the `.rpt`.
 * \param link True for a link (net of reversing flow), false for a node.
 */
QString loadNote(bool link);
/*!
 * \brief Load-rate descriptor for \p species, labeled with \p units:
 *        "<sp> load (flow × concentration at report steps)", "net load" on
 *        a link; its note is \ref loadNote. The key is the same for both.
 */
ResultDescriptor rateDescriptor(const QString &species, const LoadUnits &units, bool link);
/*!
 * \brief Cumulative-load descriptor: "<sp> cumulative load (integrated from
 *        report-step samples)", "net cumulative load" on a link.
 */
ResultDescriptor cumulativeDescriptor(const QString &species, const LoadUnits &units, bool link);

/*!
 * \brief Load rate per sample: flow x concentration in \p units.rateUnits.
 * \return NaN where either input is missing; empty when units are invalid.
 */
std::vector<double> rate(const std::vector<double> &flow, const std::vector<double> &conc,
                         const LoadUnits &units);

/*!
 * \brief Cumulative load (mass) at each sample: the time integral of
 *        flow x concentration from the simulation start.
 * \param timesJulian Sample times (SWMM DateTime days).
 * \param startJulian Simulation start; NaN when unknown.
 * \param reportStepSeconds Report step; 0 when unknown.
 * \return NaN from the first missing sample on; empty when units are invalid.
 */
std::vector<double> cumulative(const std::vector<double> &timesJulian,
                               const std::vector<double> &flow, const std::vector<double> &conc,
                               double startJulian, int reportStepSeconds, const LoadUnits &units);

} // namespace openswmmvis::plot::pollutantload

#endif // OPENSWMMVIS_PLOT_POLLUTANTLOAD_H
