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

    /*! \brief Map a PlotAttribute + ObjectRef::Kind to the engine's
     *  per-object var code (SWMM_OUT_NODE_*, SWMM_OUT_LINK_*,
     *  SWMM_OUT_SUBCATCH_*, SWMM_OUT_SYS_*). Returns -1 when the
     *  combination isn't valid (e.g. NodeDepth on a Link, or a Mesh2D
     *  attribute on any kind). Pure mapping — exposed for testing. */
    static int variableCodeFor(PlotAttribute attr, ObjectRef::Kind kind);

private:
    QPointer<SWMMResultsLayer> m_layer;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H
