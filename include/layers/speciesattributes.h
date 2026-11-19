/**
 * @file      speciesattributes.h
 * @brief     Dynamic water-quality species as themeable result attributes
 *        (GUI plan D-G1 / subplan Y2a).
 *
 * @details A run carries N species — pollutants plus the reserved
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
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
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
inline constexpr const char *kWaterAgeName = "__WATER_AGE__";
inline constexpr const char *kTemperatureName = "__TEMPERATURE__";

/*! Prefix marking a themeable attribute as a species column. */
inline constexpr const char *kSpeciesAttrPrefix = "qual:";

/*! \brief `"TSS"` → `"qual:TSS"` — the token persisted in `.oswp`.
 * @param[in] species Exact case-sensitive name, copied without validation/escaping.
 * @return qual: plus name, or empty for an empty name. No source lookup occurs.
 */
[[nodiscard]] QString speciesAttributeName(const QString &species);

/*! \brief True if \p attr is a species attribute token.
 * @param[in] attr Attribute token.
 * @return True for an exact lowercase qual: prefix, including the empty-name token qual:; does not establish that resolution can succeed.
 */
[[nodiscard]] bool isSpeciesAttribute(const QString &attr);

/*! \brief `"qual:TSS"` → `"TSS"`; empty for a non-species attribute.
 *  \note An empty species name after the prefix is NOT valid and returns
 *        empty too, so `"qual:"` cannot resolve to index 0.
 * @param[in] attr Token tested with case-sensitive prefix matching.
 * @return Exact suffix after qual:, possibly empty, or empty for another prefix. No trimming/case normalization occurs.
 */
[[nodiscard]] QString speciesFromAttribute(const QString &attr);

/*! \brief Picker label for a species: friendly text for the reserved
 *         pair, the bare name for an ordinary pollutant.
 * @param[in] species Exact run species name.
 * @return Translated friendly label for the two exact reserved names, otherwise the name copied verbatim.
 */
[[nodiscard]] QString speciesDisplayLabel(const QString &species);

/*! \brief Unit string for a species. The reserved pair carry fixed units
 *         the `.out` enum cannot express (hours / °C — engine A2b); an
 *         ordinary pollutant uses \p concentrationUnit as reported.
 * @param[in] species Exact run species name.
 * @param[in] concentrationUnit Ordinary pollutant unit text passed through verbatim, possibly empty.
 * @return h for water age, degrees Celsius for temperature, otherwise concentrationUnit. No values are converted.
 */
[[nodiscard]] QString speciesUnitLabel(const QString &species, const QString &concentrationUnit);

/*! \brief True for the reserved species (age / temperature).
 * @param[in] species Name compared case-sensitively without trimming.
 * @return True only for the exact water-age/temperature reserved tokens.
 */
[[nodiscard]] bool isReservedSpecies(const QString &species);

/*! \brief Resolve \p attr to an engine output variable code.
 *
 *  \param attr    a `"qual:<name>"` token.
 *  \param species the loaded run's species list, in `.out` order.
 *  \param pollutBase `SWMM_OUT_NODE_POLLUT_BASE`, `…_LINK_…` or
 *                    `…_SUBCATCH_…` for the element kind being themed.
 *  \return `pollutBase + index`, or −1 when \p attr is not a species
 *          token or names a species this run does not carry.
 * @note Uses the first exact case-sensitive name match. pollutBase and addition range are caller preconditions; the function does not validate the base against an object kind or access an output handle.
 */
[[nodiscard]] int speciesOutCode(const QString &attr, const QStringList &species, int pollutBase);

/*! \brief Y4 (amendment D-Y4): constituent choices for an [INFLOWS]
 *  editor — FLOW, every pollutant, then the reserved AGE species
 *  (§3.3's exclusion lifted for age ONLY; temperature keeps the
 *  dedicated-page treatment until heat's round and must NOT appear).
 *  Engine NAMES — combo labels come from inflowConstituentLabel().
 * @param[in] pollutants Ordered ordinary pollutant names; empty names are skipped and duplicates/reserved-name collisions are not filtered.
 * @return FLOW, then supplied nonempty names, then the reserved age name. The caller must omit reserved temperature from pollutants to keep it out of choices.
 */
[[nodiscard]] QStringList inflowConstituentNames(const QStringList &pollutants);

/*! \brief Combo label for an inflow constituent: the species display
 *  label for the reserved pair ("Water age (hours)" — the row's value
 *  column means HOURS, never mg/L), the bare name otherwise.
 * @param[in] name Exact constituent token.
 * @return speciesDisplayLabel() for exact reserved names, otherwise verbatim name including FLOW.
 */
[[nodiscard]] QString inflowConstituentLabel(const QString &name);

/*! \brief MASS is a pollutant-only inflow type: meaningless for FLOW
 *  and for the age species (the engine takes a MASS-typed age row as
 *  hours WITH a warning — the editor must not author what warns).
 * @param[in] name Exact constituent token.
 * @return False for exact FLOW or either reserved name, true otherwise including empty/unknown tokens. This is a UI eligibility hint rather than engine identity validation.
 */
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
     *  is not a species token or was already warned since reset().
     * @param[in] attr Failed token whose exact qual: prefix should be recognized; even qual: with an empty name can be remembered.
     * @param[in] runName Display label interpolated into the diagnostic.
     * @return Translated explanatory message on first seen species-prefixed token, empty for another prefix/already seen token. Updates only the in-memory warned set; caller performs actual logging and must call this only after resolution failed.
     * @note Stateful instances are not synchronized; serialize noteMiss()/reset() calls.
     */
    [[nodiscard]] QString noteMiss(const QString &attr, const QString &runName);
    /**
     * @brief Forget every remembered miss so a new run can warn on its first failed token again.
     * @note No source lookup, signal or logging occurs.
     */
    void reset() { m_warned.clear(); }

private:
    QSet<QString> m_warned;
};

} // namespace OpenSWMMVis::Species

#endif // OPENSWMM_LAYERS_SPECIESATTRIBUTES_H
