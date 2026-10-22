/**
 * @file      resultdescriptor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Y2b-1 (amendment D-Y4) — one plottable result, fixed or species.
 *
 * The plot surface's `PlotAttribute` is a closed enum: it cannot say
 * "TSS at this node" because a run's species list is only known once its
 * `.out` is open, and D-G1 forbids extending the enum by `POLLUT_BASE +
 * index` (a saved integer would silently repoint when a model edit
 * reorders species). A `ResultDescriptor` is the union the pickers need:
 * either a fixed attribute (every existing enumerator, unchanged) or a
 * species carried BY NAME — the same name-keyed identity Y2a gave map
 * theming (`speciesattributes.h`).
 *
 * Y2b-1 is plumbing only: `IRunLayer::resultDescriptorsForKind` serves
 * the list (fixed set for every layer; fixed + this run's species for a
 * `.out`-backed layer). Pickers consume it in Y2b-2; tabular/statistics
 * and `.oswp` persistence follow in Y2b-3.
 *
 * The list builder (`resultDescriptorsForKind`) is INLINE in
 * irunlayer.h: IRunLayer's virtual defaults call it, and IRunLayer is a
 * header-only interface — hundreds of tests stub it without linking any
 * plot TU. Only the label/unit authorities live in resultdescriptor.cpp
 * (they pull in the Y2a species helpers).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_RESULTDESCRIPTOR_H
#define OPENSWMMVIS_PLOT_RESULTDESCRIPTOR_H

#include "plot/plotattribute.h"

#include <QString>
#include <QVector>

namespace openswmmvis::plot
{

/*! \brief One plottable result: a fixed attribute, a species by name, OR a
 *  2D face variable from the source's own catalog (`IMesh2DSource::
 *  faceVariables`) keyed by `Mesh2DResultVariable::key()` — groundwater
 *  terms, infiltration, 2D species concentrations. Its label and units come
 *  from the results file's metadata, so they travel with the descriptor. */
struct ResultDescriptor {
    /*! Fixed attribute; `Unknown` when this descriptor is a species. */
    PlotAttribute attr = PlotAttribute::Unknown;

    /*! Species name (`TSS`, `__WATER_AGE__`, …) when `attr == Unknown`;
     *  empty for a fixed attribute. The NAME is the identity (D-G1). */
    QString species;

    /*! 2D face variable key (`Mesh2DResultVariable::key()`) when this
     *  descriptor is a 2D catalog variable; empty otherwise. The key is the
     *  identity; label and units are display metadata. */
    QString variableKey;
    QString variableLabel;
    QString variableUnits;

    /**
     * @brief Recognize the name-keyed species branch.
     * @return True when attr is Unknown and species is nonempty; variableKey is ignored in this test.
     */
    [[nodiscard]] bool isSpecies() const noexcept
    {
        return attr == PlotAttribute::Unknown && !species.isEmpty();
    }

    /**
     * @brief Recognize the metadata-keyed mesh-variable branch.
     * @return True only when attr is Unknown, species is empty and variableKey is nonempty.
     */
    [[nodiscard]] bool isMeshVariable() const noexcept
    {
        return attr == PlotAttribute::Unknown && species.isEmpty() && !variableKey.isEmpty();
    }

    /**
     * @brief Test structural presence of a fixed identifier or dynamic key.
     * @return True for any attr other than Unknown or either nonempty key; does not validate enum membership, source availability or mutually exclusive fields.
     */
    [[nodiscard]] bool isValid() const noexcept
    {
        return attr != PlotAttribute::Unknown || !species.isEmpty() || !variableKey.isEmpty();
    }

    /*! \brief Picker label: `labelFor(attr)` for a fixed attribute;
     *  the species display label (friendly text for the reserved pair,
     *  the bare name otherwise) for a species.
     * @return Species-friendly label first, then mesh label (falling back to its key), otherwise labelFor(attr). No source lookup occurs.
     */
    [[nodiscard]] QString label() const;

    /*! \brief Unit string. Fixed attributes use `unitsFor(attr, u)`;
     *  species use the reserved pair's fixed units (h / °C) or
     *  \p concentrationUnit for an ordinary pollutant.
     * @param[in] u Fixed-attribute unit family; ignored by species and mesh branches.
     * @param[in] concentrationUnit Caller-provided ordinary pollutant units, default mg/L; reserved species retain their fixed h/degree-C units.
     * @return Mesh metadata units verbatim, species units, or unitsFor(attr,u), following the same branch priority as label().
     */
    [[nodiscard]] QString
    unitLabel(UnitSystem u, const QString &concentrationUnit = QStringLiteral("mg/L")) const;

    /**
     * @brief Construct a fixed-result identity without consulting a source.
     * @param[in] a Fixed enum; Unknown produces an invalid empty descriptor.
     * @return Independent descriptor with dynamic names/metadata empty.
     */
    static ResultDescriptor forAttribute(PlotAttribute a)
    {
        ResultDescriptor d;
        d.attr = a;
        return d;
    }

    /**
     * @brief Construct an identity using the exact species name.
     * @param[in] name Case-sensitive stored name, copied verbatim; empty produces an invalid descriptor.
     * @return Species descriptor; no registry validation or numeric index binding occurs.
     */
    static ResultDescriptor forSpecies(const QString &name)
    {
        ResultDescriptor d;
        d.species = name;
        return d;
    }

    /**
     * @brief Carry a mesh-catalog key together with optional display metadata.
     * @param[in] key Exact source catalog key, copied verbatim; empty remains invalid even with metadata.
     * @param[in] label Display label; an empty label falls back to key when rendered.
     * @param[in] units Display units copied verbatim.
     * @return Independent descriptor; catalog membership is not checked.
     */
    static ResultDescriptor forMeshVariable(const QString &key, const QString &label,
                                            const QString &units)
    {
        ResultDescriptor d;
        d.variableKey = key;
        d.variableLabel = label;
        d.variableUnits = units;
        return d;
    }

    /**
     * @brief Compare result identity independently of presentation metadata.
     * @param[in] o Other descriptor.
     * @return Equality of attr, species and variableKey with exact string case; variableLabel/variableUnits are ignored.
     */
    bool operator==(const ResultDescriptor &o) const noexcept
    {
        return attr == o.attr && species == o.species && variableKey == o.variableKey;
    }
    /**
     * @brief Test whether stored result identities differ.
     * @param[in] o Other descriptor.
     * @return Negation of operator==; presentation labels/units do not affect identity.
     */
    bool operator!=(const ResultDescriptor &o) const noexcept { return !(*this == o); }
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_RESULTDESCRIPTOR_H
