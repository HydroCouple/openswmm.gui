/*!
 * \file   swmmoutrunlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — IRunLayer adapter wrapping a SWMMResultsLayer (1D .out).
 *
 * One `SwmmOutRunLayer` is the bridge between the Comparison Plot Dialog's
 * model layer (which thinks in terms of IRunLayer + ObjectRef) and the
 * existing on-canvas `SWMMResultsLayer` that already holds the open .out
 * handle.
 *
 * The adapter is non-owning — it holds a raw `SWMMResultsLayer*` and
 * resolves it on every call. When the underlying layer is destroyed
 * (e.g. project window closed), the adapter detects the dangling pointer
 * via a QPointer guard and returns ok=false from `getSeriesAt`.
 *
 * No standalone .out support yet; *Tools → Comparison Plot → Load Run…*
 * (a follow-up) will spin up a parallel "headless" adapter that owns its
 * own `SWMM_Output` handle.
 */
#ifndef OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H
#define OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H

#include "plot/irunlayer.h"

#include <QPointer>

class SWMMResultsLayer;

namespace openswmmvis::plot {

class SwmmOutRunLayer final : public IRunLayer
{
public:
    /*! \brief Wrap an existing on-canvas SWMMResultsLayer (non-owning). */
    explicit SwmmOutRunLayer(SWMMResultsLayer *layer);
    ~SwmmOutRunLayer() override = default;

    /*! \brief Underlying layer (may be null if it was destroyed). */
    SWMMResultsLayer *layer() const;

    // IRunLayer
    QString    scenarioName()     const override;
    UnitSystem unitSystem()       const override;
    double     startDateJulian()  const override;
    int        periodCount()      const override;
    int        reportStepSeconds() const override;
    QString    persistenceKey()   const override;

    void getSeriesAt(const ObjectRef& ref,
                     PlotAttribute attr,
                     SeriesData& out) const override;

    bool supportsAttribute(PlotAttribute attr) const override;

    /*! \brief Y2b-1: fixed set + the open run's species BY NAME (D-G1).
     *  Legacy `.out` (no quality) serves the fixed set only. */
    QVector<ResultDescriptor> resultDescriptorsForKind(
        ObjectRef::Kind kind) const override;

    /*! \brief Y2b-2: species series resolve NAME → index → per-kind
     *  `SWMM_OUT_*_POLLUT_BASE + index` against the run's live species
     *  list on every call (D-G1 — a stored index would repoint when a
     *  model edit reorders species). Fixed attributes take the enum
     *  path unchanged. */
    void getSeriesAt(const ObjectRef& ref,
                     const ResultDescriptor& descriptor,
                     SeriesData& out) const override;

    /*! \brief Map a species NAME to the engine's per-object var code:
     *  `SWMM_OUT_{NODE,LINK,SUBCATCH}_POLLUT_BASE + indexOf(name)`.
     *  Returns -1 when the run does not carry the species or the kind
     *  has no species columns. Pure mapping — exposed for testing, and
     *  reorder-safe by construction (the index comes from \p runSpecies
     *  at call time, never from storage). */
    static int speciesVariableCodeFor(const QString& species,
                                      const QStringList& runSpecies,
                                      ObjectRef::Kind kind);

    /*! \brief Map a PlotAttribute + ObjectRef::Kind to the engine's
     *  per-object var code (SWMM_OUT_NODE_*, SWMM_OUT_LINK_*,
     *  SWMM_OUT_SUBCATCH_*, SWMM_OUT_SYS_*). Returns -1 when the
     *  combination isn't valid (e.g. NodeDepth on a Link, or a Mesh2D
     *  attribute on any kind). Pure mapping — exposed for testing. */
    static int variableCodeFor(PlotAttribute attr, ObjectRef::Kind kind);

private:
    /*! Shared tail of both getSeriesAt overloads: object resolution +
     *  bulk fetch + time axis for an already-resolved var code. */
    void fetchSeriesByCode_(const ObjectRef& ref, int varCode,
                            SeriesData& out) const;

    QPointer<SWMMResultsLayer> m_layer;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H
