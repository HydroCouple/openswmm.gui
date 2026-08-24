/*!
 * \file   resultdescriptor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Y2b-1 (amendment D-Y4) — one plottable result, fixed or species.
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
 * The list-building logic lives in a free function in its own TU so a
 * test can reach it without linking `SWMMResultsLayer` (the closure
 * problem recorded at `tests/gui/CMakeLists.txt` for the options dialog;
 * the Y2a extraction precedent).
 */
#ifndef OPENSWMMVIS_PLOT_RESULTDESCRIPTOR_H
#define OPENSWMMVIS_PLOT_RESULTDESCRIPTOR_H

#include "plot/plotattribute.h"

#include <QString>
#include <QVector>

namespace openswmmvis::plot {

/*! \brief One plottable result: a fixed attribute OR a species by name. */
struct ResultDescriptor {
    /*! Fixed attribute; `Unknown` when this descriptor is a species. */
    PlotAttribute attr = PlotAttribute::Unknown;

    /*! Species name (`TSS`, `__WATER_AGE__`, …) when `attr == Unknown`;
     *  empty for a fixed attribute. The NAME is the identity (D-G1). */
    QString species;

    [[nodiscard]] bool isSpecies() const noexcept
    { return attr == PlotAttribute::Unknown && !species.isEmpty(); }

    [[nodiscard]] bool isValid() const noexcept
    { return attr != PlotAttribute::Unknown || !species.isEmpty(); }

    /*! \brief Picker label: `labelFor(attr)` for a fixed attribute;
     *  the species display label (friendly text for the reserved pair,
     *  the bare name otherwise) for a species. */
    [[nodiscard]] QString label() const;

    /*! \brief Unit string. Fixed attributes use `unitsFor(attr, u)`;
     *  species use the reserved pair's fixed units (h / °C) or
     *  \p concentrationUnit for an ordinary pollutant. */
    [[nodiscard]] QString unitLabel(UnitSystem u,
                                    const QString &concentrationUnit =
                                        QStringLiteral("mg/L")) const;

    static ResultDescriptor forAttribute(PlotAttribute a)
    { ResultDescriptor d; d.attr = a; return d; }

    static ResultDescriptor forSpecies(const QString &name)
    { ResultDescriptor d; d.species = name; return d; }

    bool operator==(const ResultDescriptor &o) const noexcept
    { return attr == o.attr && species == o.species; }
    bool operator!=(const ResultDescriptor &o) const noexcept
    { return !(*this == o); }
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_RESULTDESCRIPTOR_H
