/*!
 * \file   mesh2drunlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice CF.3 — IRunLayer adapter wrapping a SWMM2DResultsLayer.
 *
 * Lets the Comparison Plot Dialog treat a 2D mesh results layer as a
 * first-class RunSource, identical in surface to a 1D `.out` adapter.
 * The model holds a `shared_ptr<IRunLayer>`; the adapter holds a
 * `QPointer<SWMM2DResultsLayer>` and resolves it on every call so a
 * project-close cleanly invalidates outstanding plot series.
 *
 * Series resolution per attribute:
 *   - Mesh2DDepth      → IMesh2DSource::readDepthsAt over all time idx
 *   - Mesh2DHGL        → depth + z_bed (mean of triangle vertex z's,
 *                        cached on the layer)
 *   - Mesh2DVelocityX  → RT0 reconstruction from edge fluxes
 *   - Mesh2DVelocityY  → "
 *   - Mesh2DVelocityMag→ sqrt(Vx² + Vy²)
 *   - Mesh2DRainfall   → /Mesh2_face_rainfall (m/s → mm/hr)
 *   - Mesh2DRainVolume → /Mesh2_face_rain_cum (cumulative m³ per cell)
 *
 * Time axis: simulated wall-clock times are pulled from the source's
 * `simTimeAt(timeIdx)`. The adapter converts back to SWMM Julian days
 * for the model's common timeline.
 */
#ifndef OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H
#define OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H

#include "plot/irunlayer.h"

#include <QPointer>

class SWMM2DResultsLayer;

namespace openswmmvis::plot {

class Mesh2DRunLayer final : public IRunLayer
{
public:
    explicit Mesh2DRunLayer(SWMM2DResultsLayer *layer);
    ~Mesh2DRunLayer() override = default;

    SWMM2DResultsLayer *layer() const { return m_layer.data(); }

    // IRunLayer
    QString    scenarioName()      const override;
    UnitSystem unitSystem()        const override;
    double     startDateJulian()   const override;
    int        periodCount()       const override;
    int        reportStepSeconds() const override;
    QString    persistenceKey()    const override;

    void getSeriesAt(const ObjectRef& ref,
                     PlotAttribute attr,
                     SeriesData& out) const override;

    bool supportsAttribute(PlotAttribute attr) const override;

private:
    /*! \brief Cached bed elevation per triangle = mean of vertex z's. */
    void ensureZBedCache_() const;

    /*! \brief Cached per-vertex incident-triangle adjacency + per-vertex bed
     *  elevation, built once from the source geometry. Used to interpolate a
     *  depth/HGL time series at a mesh vertex (mean of the triangles touching
     *  it). */
    void ensureVertexAdjCache_() const;

    /*! \brief Reconstruct (Vx, Vy) for one cell from three edge fluxes
     *  and time-invariant edge geometry using the closed-form RT0
     *  least-squares solve. Returns false (NaN-filled) for dry cells. */
    bool reconstructVelocityAtCell_(int triIdx,
                                    const std::vector<float>& flux,
                                    const std::vector<float>& edge_len,
                                    const std::vector<float>& edge_nx,
                                    const std::vector<float>& edge_ny,
                                    double depth,
                                    double dryDepth,
                                    double& vx_out,
                                    double& vy_out) const;

    QPointer<SWMM2DResultsLayer> m_layer;
    mutable std::vector<float>   m_zBed;     ///< [triCount], cached on first use.
    mutable bool                 m_zBedReady = false;

    mutable std::vector<std::vector<int>> m_vertexTris;  ///< [vtxCount] incident triangle indices.
    mutable std::vector<float>            m_vertexZ;     ///< [vtxCount] bed elevation at the vertex.
    mutable bool                          m_vertexAdjReady = false;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H
