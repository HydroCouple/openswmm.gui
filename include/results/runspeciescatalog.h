/**
 * @file      runspeciescatalog.h
 * @brief     V0 — the single list of species carried by one loaded run.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * §2 (`RunSpeciesCatalog`), D-V1 and the V0 gate. One entry per species NAME
 * — pollutants, the reserved `__WATER_AGE__` / `__TEMPERATURE__` columns and
 * MSX bulk / wall species — with where the run stores it:
 *   - `reportedIndex`: the `.out` species column (or -1: not in the `.out`,
 *     i.e. unavailable on 1D objects — MSX species before engine E1);
 *   - `meshKey` / `meshKeys`: the 2D result slices
 *     (`Mesh2DResultVariable::key()`) that carry it;
 *   - `sidecarIndex`: the per-cell transport sidecar column (V8; -1 today).
 * A species present in both the `.out` and the `.h5` is listed ONCE (D-V1).
 *
 * The name stays the identity (D-G1): pickers store the name, never an
 * index, and every consumer resolves name -> column against the catalog of
 * the run it is reading, so reordering `[POLLUTANTS]` cannot repoint a saved
 * style. The kind comes from the name (reserved pair) or from the model's
 * `[REACTIONS]` declarations when an engine is available
 * (`declaredFromEngine`); the units from the `.out` unit code, the 2D
 * `species_units` attribute or the declaration — unknown otherwise.
 *
 * Pure: built from plain inputs or a `SWMM_Output` handle, no layer types.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_RESULTS_RUNSPECIESCATALOG_H
#define OPENSWMMVIS_RESULTS_RUNSPECIESCATALOG_H

#include "io/mesh2dresultvariable.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_output.h>

#include <QString>
#include <QStringList>
#include <QVector>

namespace openswmmvis::results
{

/*! One species of a loaded run. */
struct RunSpecies {
    enum class Kind {
        Pollutant,    ///< ordinary `[POLLUTANTS]` row
        WaterAge,     ///< reserved `__WATER_AGE__` (hours)
        Temperature,  ///< reserved `__TEMPERATURE__` (degrees C)
        ReactionBulk, ///< MSX BULK species (`[REACTIONS]`)
        ReactionWall, ///< MSX WALL species (`[REACTIONS]`)
        Unclassified  ///< 2D-only name with no declaration available
    };

    QString name;
    Kind kind = Kind::Pollutant;
    QString units;          ///< display units; empty when unknown
    bool unitsKnown = false;
    int reportedIndex = -1; ///< `.out` species column; -1 = not in the `.out`
    int concentrationCode = -1; ///< `.out` unit code 0 mg/L, 1 ug/L, 2 count/L; -1 unknown
    QString meshKey;        ///< surface concentration slice key; empty when none
    QStringList meshKeys;   ///< every 2D slice of this species (surface, buildup, aquifer)
    int sidecarIndex = -1;  ///< per-cell transport sidecar column (V8); -1 = none

    /** @return True when the `.out` carries this species (1D objects). */
    bool in1D() const noexcept { return reportedIndex >= 0; }
    /** @return True when the 2D results carry at least one slice of it. */
    bool in2D() const noexcept { return !meshKeys.isEmpty(); }
    /** @return Short kind badge: "pollutant", "water age", "MSX bulk", … */
    QString kindBadge() const;
    /** @return Friendly label (reserved pair) or the bare name. */
    QString displayLabel() const;
    /*! \brief True for a 1D pollutant whose load (flow x concentration) is
     *  meaningful: an ordinary pollutant with a known `.out` unit code. */
    bool carriesLoad() const noexcept
    {
        return kind == Kind::Pollutant && in1D() && concentrationCode >= 0 &&
               concentrationCode <= 2;
    }
};

class RunSpeciesCatalog
{
public:
    /*! One `.out` species column, in file order. */
    struct OutputSpecies {
        QString name;
        int unitsCode = -1; ///< `swmm_output_get_pollut_units`
    };
    /*! One model declaration (MSX species from `[REACTIONS]`). */
    struct DeclaredSpecies {
        QString name;
        RunSpecies::Kind kind = RunSpecies::Kind::ReactionBulk;
        QString units;
    };

    /*!
     * \brief Merge the `.out` species, the 2D catalog's species slices and
     *        the model's declarations into one name-keyed list.
     * \param reported `.out` columns in file order (their index is the
     *        column); empty names are skipped.
     * \param meshVariables 2D catalog (`IMesh2DSource::faceVariables`);
     *        only entries with a species contribute.
     * \param declared Optional declarations used for kind (and units when
     *        no file states them).
     * \param has2D True when a 2D run was consulted at all, so a species
     *        missing from it is badged "1D only".
     */
    static RunSpeciesCatalog build(const QVector<OutputSpecies> &reported,
                                   const QVector<io::Mesh2DResultVariable> &meshVariables = {},
                                   const QVector<DeclaredSpecies> &declared = {},
                                   bool has2D = false);
    /*! \brief \ref build from an open `.out` reader (names + unit codes).
     *  A null handle contributes no 1D species. */
    static RunSpeciesCatalog fromOutput(SWMM_Output handle,
                                        const QVector<io::Mesh2DResultVariable> &meshVariables = {},
                                        const QVector<DeclaredSpecies> &declared = {},
                                        bool has2D = false);
    /*! \brief The model's MSX species declarations (BULK / WALL, declared
     *  units). Empty for a null engine or a model without `[REACTIONS]`. */
    static QVector<DeclaredSpecies> declaredFromEngine(SWMM_Engine engine);
    /*! \brief Normalise a stored unit spelling ("MG/L" -> "mg/L"). */
    static QString normalizedUnits(const QString &units);
    /*! \brief Display units for a `.out` unit code; empty when unknown. */
    static QString unitsForCode(int code);

    /** @return Entries: `.out` order first, then 2D-only species. */
    const QVector<RunSpecies> &entries() const noexcept { return m_entries; }
    /** @return Entry named \p name (exact match), or null. */
    const RunSpecies *find(const QString &name) const;
    /** @return Names carried by the `.out`, in column order. */
    QStringList reportedNames() const;
    /** @return True when 2D results were consulted. */
    bool has2D() const noexcept { return m_has2D; }
    /*! \brief Availability badge for \p s in this catalog: "2D only" when
     *  the `.out` lacks it, "1D only" when 2D results were consulted and lack
     *  it, empty when every consulted source carries it. */
    QString availabilityBadge(const RunSpecies &s) const;
    /*! \brief Picker label: "<label> [<kind> · <availability>] (<units>)";
     *  the availability part and the units are omitted when empty. */
    QString pickerLabel(const RunSpecies &s) const;
    /** @return Entry count. */
    int size() const noexcept { return int(m_entries.size()); }
    /** @return True without entries. */
    bool isEmpty() const noexcept { return m_entries.isEmpty(); }

private:
    QVector<RunSpecies> m_entries;
    bool m_has2D = false;
};

} // namespace openswmmvis::results

#endif // OPENSWMMVIS_RESULTS_RUNSPECIESCATALOG_H
