/*!
 * \file speciesattributes.h
 * \brief Dynamic water-quality species as themeable result attributes
 *        (GUI plan D-G1 / subplan Y2a).
 *
 * \details A run carries N species — pollutants plus the reserved
 *          `__WATER_AGE__` and `__TEMPERATURE__` — and N is only known once
 *          an `.out` is open. The engine's output ABI already indexes them
 *          as `SWMM_OUT_*_POLLUT_BASE + speciesIndex`, so the *transient*
 *          lookup code is the engine's own convention.
 *
 *          What must NOT be an index is the **persisted** identity: D-G1
 *          requires themes and plots to reference a species by NAME so a
 *          model edit that reorders species cannot silently repoint a
 *          saved theme. Hence the attribute string is
 *          `"qual:<SpeciesName>"` — the token that flows through
 *          `FeatureSublayerStyle::attribute` into `.oswp` — and the index
 *          is resolved against the loaded run every time it is needed.
 *          An unresolvable name yields −1, which every existing call site
 *          already skips cleanly (the "warn on miss" foundation).
 *
 *          These are free functions in their own translation unit on
 *          purpose: `SWMMResultsLayer` is too heavy to link into a test
 *          (the same closure problem `tests/gui/CMakeLists.txt:1996`
 *          records for the options dialog), so the logic that can carry a
 *          defect lives where a test can reach it.
 *
 * \author  Caleb Buahin <caleb.buahin@gmail.com>
 * \copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * \license Apache-2.0
 */

#ifndef OPENSWMM_LAYERS_SPECIESATTRIBUTES_H
#define OPENSWMM_LAYERS_SPECIESATTRIBUTES_H

#include <QSet>
#include <QString>
#include <QStringList>

namespace OpenSWMMVis::Species
{

/*! The reserved species names the engine writes into the `.out` species
 *  list. They are transported like pollutants but are not concentrations,
 *  so pickers label and unit them differently (GUI plan §3.3). */
inline constexpr const char *kWaterAgeName    = "__WATER_AGE__";
inline constexpr const char *kTemperatureName = "__TEMPERATURE__";

/*! Prefix marking a themeable attribute as a species column. */
inline constexpr const char *kSpeciesAttrPrefix = "qual:";

/*! \brief `"TSS"` → `"qual:TSS"` — the token persisted in `.oswp`. */
[[nodiscard]] QString speciesAttributeName(const QString &species);

/*! \brief True if \p attr is a species attribute token. */
[[nodiscard]] bool isSpeciesAttribute(const QString &attr);

/*! \brief `"qual:TSS"` → `"TSS"`; empty for a non-species attribute.
 *  \note An empty species name after the prefix is NOT valid and returns
 *        empty too, so `"qual:"` cannot resolve to index 0. */
[[nodiscard]] QString speciesFromAttribute(const QString &attr);

/*! \brief Picker label for a species: friendly text for the reserved
 *         pair, the bare name for an ordinary pollutant. */
[[nodiscard]] QString speciesDisplayLabel(const QString &species);

/*! \brief Unit string for a species. The reserved pair carry fixed units
 *         the `.out` enum cannot express (hours / °C — engine A2b); an
 *         ordinary pollutant uses \p concentrationUnit as reported. */
[[nodiscard]] QString speciesUnitLabel(const QString &species,
                                       const QString &concentrationUnit);

/*! \brief True for the reserved species (age / temperature). */
[[nodiscard]] bool isReservedSpecies(const QString &species);

/*! \brief Resolve \p attr to an engine output variable code.
 *
 *  \param attr    a `"qual:<name>"` token.
 *  \param species the loaded run's species list, in `.out` order.
 *  \param pollutBase `SWMM_OUT_NODE_POLLUT_BASE`, `…_LINK_…` or
 *                    `…_SUBCATCH_…` for the element kind being themed.
 *  \return `pollutBase + index`, or −1 when \p attr is not a species
 *          token or names a species this run does not carry. */
[[nodiscard]] int speciesOutCode(const QString &attr,
                                 const QStringList &species,
                                 int pollutBase);

/*! \brief Y4 (amendment D-Y4): constituent choices for an [INFLOWS]
 *  editor — FLOW, every pollutant, then the reserved AGE species
 *  (§3.3's exclusion lifted for age ONLY; temperature keeps the
 *  dedicated-page treatment until heat's round and must NOT appear).
 *  Engine NAMES — combo labels come from inflowConstituentLabel(). */
[[nodiscard]] QStringList inflowConstituentNames(const QStringList &pollutants);

/*! \brief Combo label for an inflow constituent: the species display
 *  label for the reserved pair ("Water age (hours)" — the row's value
 *  column means HOURS, never mg/L), the bare name otherwise. */
[[nodiscard]] QString inflowConstituentLabel(const QString &name);

/*! \brief MASS is a pollutant-only inflow type: meaningless for FLOW
 *  and for the age species (the engine takes a MASS-typed age row as
 *  hours WITH a warning — the editor must not author what warns). */
[[nodiscard]] bool inflowMassAllowed(const QString &name);

/*! \brief D-G1 warn-on-miss (Y2b-3): a saved theme or series may name a
 *  species the currently-open run does not carry (saved against a
 *  3-species run, reopened against 1). Resolution already degrades to
 *  "no theme" (−1); this class supplies the WORDS — once per token, so
 *  per-frame resolution cannot spam the log. One instance per results
 *  layer; reset() when the run changes (the new run may carry it). */
class SpeciesMissWarner
{
public:
    /*! \brief Message for a failed species token, or empty when \p attr
     *  is not a species token or was already warned since reset(). */
    [[nodiscard]] QString noteMiss(const QString &attr,
                                   const QString &runName);
    void reset() { m_warned.clear(); }

private:
    QSet<QString> m_warned;
};

} // namespace OpenSWMMVis::Species

#endif // OPENSWMM_LAYERS_SPECIESATTRIBUTES_H
