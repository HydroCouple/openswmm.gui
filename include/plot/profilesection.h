/*!
 * \file   profilesection.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The value type every longitudinal cross-section shares, regardless of
 *         which surface it was sampled from.
 *
 *         Originally the MeshProfileSampler::MeshProfile struct; extracted here
 *         so the DEM-raster sampler (RasterProfileSampler) can feed the same
 *         MeshProfilePlotWidget chart. MeshProfileSampler's names are aliases of
 *         these, so every existing mesh call site is untouched.
 *
 *         A raster (ground-only) section leaves depthNow / maxDepth at 0,
 *         triIdx at -1, `crossings` empty and `hasResults` false — the chart's
 *         water passes are already gated on hasResults, so it renders ground +
 *         soil fill only.
 */
#ifndef PROFILE_SECTION_H
#define PROFILE_SECTION_H

#include <QPointF>
#include <QVector>

namespace ProfileSection
{

/*! Hard cap on sample count so a very long polyline can't blow up the
 *  per-frame resample / per-cell envelope work. */
inline constexpr int kMaxSamples = 2000;

/*!
 * \struct Sample
 * \brief One point along the traced cross-section.
 */
struct Sample
{
    double chainage = 0.0;   /*!< cumulative scene-unit distance from start (== map units). */
    double ground   = 0.0;   /*!< terrain Z; NaN when the sample falls off the surface. */
    double depthNow = 0.0;   /*!< current-frame water depth (m); 0 dry / off-surface / raster. */
    double maxDepth = 0.0;   /*!< max water depth over the loaded run (m); 0 dry / raster. */
    int    triIdx   = -1;    /*!< containing results-layer triangle; -1 off-mesh / raster. */
    bool   cellHasSurface = false; /*!< a corner of triIdx carries a valid free-surface η
                                        this frame (signed depth ≠ 0). false = NO DATA
                                        (never-wet cell / off-mesh / raster) — the only
                                        dry gaps MeshProfileInterp may bridge across. */
    QPointF scenePt;         /*!< scene-space sample location (for per-frame re-sampling). */
};

/*!
 * \struct CellCrossing
 * \brief A point where the traced line crosses a mesh-cell (triangle) edge.
 *        Plotted as a dot on the ground line so the per-cell interpolation
 *        basis (constant-per-cell depth → stepped WSE) is visible: each dot
 *        marks a cell boundary; the span between two dots is one cell.
 *        Unused by raster sections (a DEM has no cells to cross).
 */
struct CellCrossing
{
    double  chainage = 0.0;  /*!< cumulative scene-unit distance from start. */
    double  ground   = 0.0;  /*!< terrain Z at the crossing (barycentric). */
    QPointF scenePt;         /*!< scene-space crossing location (for the map dot). */
};

/*!
 * \struct Section
 * \brief The assembled cross-section: ordered samples + a results flag.
 */
struct Section
{
    QVector<Sample> samples;
    QVector<CellCrossing> crossings;  /*!< cell-edge crossings along the path. */
    bool hasResults = false;   /*!< true when a results layer with ≥1 frame backed the sampling. */
};

} // namespace ProfileSection

#endif // PROFILE_SECTION_H
