/*!
 * \file   rastersymbollayers.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Typed accessors for Raster + TIN Symbol Layers (Slice Z.6).
 *
 *         The Rule Model packages raster + TIN paint passes as Symbol
 *         Layers — see RENDERING_RULE_MODEL_PLAN.md §6.4. Five typed
 *         specs ship here:
 *
 *           - RasterColorRampSymbolLayerSpec  → continuous-color ramp
 *               for DEMs and 2D depth fields.
 *           - HillshadeSymbolLayerSpec        → terrain hillshade overlay
 *               (azimuth / altitude / z-exaggeration / shadow floor).
 *           - ContourSymbolLayerSpec          → contour lines + optional
 *               filled isobands with labels.
 *           - MeshEdgeSymbolLayerSpec         → TIN / mesh wireframe.
 *           - MeshNodeSymbolLayerSpec         → TIN vertex markers
 *               (delegates to MarkerSymbolLayerSpec from Z.4).
 *
 *         Slice Z.6 ships the data-model side: typed specs +
 *         SymbolLayer round-trip + JSON. The actual paint integration
 *         with the existing §N rendering code (AU.6.4 hillshade, BJ.2
 *         contour generator, AZ.3 mesh edges) is the named Z.6a
 *         follow-up — those existing code paths consume the specs
 *         unchanged once a Rule-aware paint host calls them.
 */

#ifndef OPENSWMM_RENDER_RASTERSYMBOLLAYERS_H
#define OPENSWMM_RENDER_RASTERSYMBOLLAYERS_H

#include "render/colorramp.h"
#include "render/intervalbinner.h"
#include "render/markersymbollayer.h"
#include "render/symbollayer.h"

#include <QColor>
#include <QPen>

namespace OpenSWMM::Render
{

// ╭───────────────────────────────────────────────────────────────────╮
// │  Raster color ramp                                                 │
// ╰───────────────────────────────────────────────────────────────────╯

/*!
 * \struct RasterColorRampSymbolLayerSpec
 * \brief Continuous-color ramp paint pass for raster / 2D-mesh data.
 *
 *        When `binner.method() == Manual` with empty breaks (or in a
 *        future "unclassed" mode) the ramp samples continuously between
 *        clampMin and clampMax.  When the binner has breaks, values are
 *        bucketed first.
 *
 *        Canonical SymbolLayer props keys:
 *          - "ramp"        → RasterColorRamp JSON
 *          - "binner"      → IntervalBinner JSON
 *          - "clampMin"    → qreal
 *          - "clampMax"    → qreal
 *          - "noDataColor" → QColor
 *          - "opacity"     → qreal in [0,1]
 */
struct RasterColorRampSymbolLayerSpec
{
    RasterColorRamp ramp;
    IntervalBinner  binner;
    qreal           clampMin    = 0.0;
    qreal           clampMax    = 1.0;
    QColor          noDataColor = QColor(0, 0, 0, 0);
    qreal           opacity     = 1.0;

    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static RasterColorRampSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

// ╭───────────────────────────────────────────────────────────────────╮
// │  Hillshade                                                          │
// ╰───────────────────────────────────────────────────────────────────╯

/*!
 * \struct HillshadeSymbolLayerSpec
 * \brief Terrain-hillshade overlay (DEM / mesh bed elevation).
 *
 *        Canonical SymbolLayer props keys:
 *          - "azimuthDeg"      → qreal in [0, 360)
 *          - "altitudeDeg"     → qreal in [0, 90]
 *          - "zExaggeration"   → qreal (typical 0.5–10)
 *          - "shadowFloor"     → qreal in [0, 1] (minimum brightness)
 *          - "blendMode"       → QString ("Normal"/"Multiply"/…)
 *
 *        Default values reproduce the §N AU.6.4-lite shipped settings
 *        (azimuth 315° NW, altitude 45°) so a fresh Rule equals what
 *        the legacy Mesh tab currently paints.
 */
struct HillshadeSymbolLayerSpec
{
    qreal   azimuthDeg     = 315.0;
    qreal   altitudeDeg    = 45.0;
    qreal   zExaggeration  = 1.0;
    qreal   shadowFloor    = 0.2;
    QString blendMode      = QStringLiteral("Multiply");

    /*! Slice Z.6a — multiplier on shading intensity in [0, 1]. Distinct
     *  from shadowFloor: 0.0 disables shading entirely (everything fully
     *  lit); 1.0 applies the full normal-dot-light effect. Mirrors the
     *  legacy MeshFillStyle::hillshadeStrength field 1:1 so the paint
     *  integration preserves the existing default look. */
    qreal   strength       = 0.5;

    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static HillshadeSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

// ╭───────────────────────────────────────────────────────────────────╮
// │  Contour                                                            │
// ╰───────────────────────────────────────────────────────────────────╯

/*!
 * \enum ContourMode
 * \brief What a contour Symbol Layer paints.
 */
enum class ContourMode : int {
    Lines  = 0,   /*!< Iso-lines only. */
    Filled = 1,   /*!< Filled isobands only (no lines). */
    Both   = 2    /*!< Lines stacked on top of filled bands. */
};

[[nodiscard]] QString contourModeToString(ContourMode m);
[[nodiscard]] ContourMode contourModeFromString(const QString &s);

/*!
 * \struct ContourSymbolLayerSpec
 * \brief Iso-line / iso-band paint pass.
 *
 *        Canonical SymbolLayer props keys:
 *          - "mode"           → int (cast of ContourMode)
 *          - "binner"         → IntervalBinner JSON (drives the breaks)
 *          - "ramp"           → RasterColorRamp JSON (drives filled-band colours)
 *          - "lineColor"      → QColor (used when ramp-per-line is off)
 *          - "lineWidthPx"    → qreal
 *          - "labelEveryN"    → int (label every Nth line; 0 disables)
 *          - "labelFormat"    → QString (printf-style; default "%.1f")
 *
 *        LabelConfig (font / halo / placement) is owned by the Rule's
 *        decoration set per RENDERING_RULE_MODEL_PLAN.md §9 — kept
 *        outside this spec to avoid coupling Z.6 to the full label
 *        engine. The format string above is the minimum needed for
 *        per-iso-value text; richer styling lands in a follow-up.
 */
struct ContourSymbolLayerSpec
{
    ContourMode      mode         = ContourMode::Lines;
    IntervalBinner   binner;
    RasterColorRamp  ramp;
    QColor           lineColor    = QColor(40, 40, 40);
    qreal            lineWidthPx  = 0.75;
    int              labelEveryN  = 0;
    QString          labelFormat  = QStringLiteral("%.1f");

    /*! Slice Z.6a — when true (default), filled isobands sample the
     *  ramp continuously between low and high (smooth gradient). When
     *  false, each band picks a discrete viridis-step colour at its
     *  centroid (categorical look). Mirrors the legacy
     *  ContourBandStyle::smoothBands toggle so the paint integration
     *  preserves both visual modes. */
    bool             smoothBands  = true;

    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static ContourSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

// ╭───────────────────────────────────────────────────────────────────╮
// │  Mesh edge                                                          │
// ╰───────────────────────────────────────────────────────────────────╯

/*!
 * \struct MeshEdgeSymbolLayerSpec
 * \brief TIN / mesh wireframe paint pass.
 *
 *        Canonical SymbolLayer props keys:
 *          - "color"        → QColor
 *          - "width"        → qreal (px)
 *          - "penStyle"     → int (cast of Qt::PenStyle)
 *          - "lodMinZoom"   → int (mesh-zoom level below which edges
 *                              paint decimated — every Nth edge — to
 *                              keep fps stable on million-triangle
 *                              meshes; 0 disables decimation)
 */
struct MeshEdgeSymbolLayerSpec
{
    QColor       color      = QColor(80, 80, 80);
    qreal        width      = 0.5;
    Qt::PenStyle penStyle   = Qt::SolidLine;
    int          lodMinZoom = 0;

    /*! Slice Z.6a — slope-driven width emphasis. When `useSlopeDrivenWidth`
     *  is true, edges whose normalised slope exceeds `slopeBreak` paint
     *  at `wideWidthPx` with `wideColor` instead of the base width/color.
     *  Mirrors the legacy MeshEdgeStyle slope fields so the paint
     *  integration preserves the existing wireframe look. */
    bool         useSlopeDrivenWidth = false;
    qreal        slopeBreak          = 0.5;
    qreal        wideWidthPx         = 1.5;
    QColor       wideColor           = QColor(40, 40, 40);

    [[nodiscard]] QPen toQPen() const;
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static MeshEdgeSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

// ╭───────────────────────────────────────────────────────────────────╮
// │  Mesh node                                                          │
// ╰───────────────────────────────────────────────────────────────────╯

/*!
 * \struct MeshNodeSymbolLayerSpec
 * \brief TIN / mesh vertex marker paint pass.
 *
 *        Delegates to MarkerSymbolLayerSpec (Slice Z.4) for shape +
 *        size + fill + outline. Sets layer.kind to MeshNode rather
 *        than SimpleMarker so the paint host knows to walk vertex
 *        coordinates instead of feature centroids.
 *
 *        Canonical props keys: identical to MarkerSymbolLayerSpec
 *        (`shape`, `size`, `fillColor`, `outlineColor`, `outlineWidth`,
 *         `outlinePenStyle`, `rotationDeg`, `offsetX`, `offsetY`).
 */
struct MeshNodeSymbolLayerSpec
{
    MarkerSymbolLayerSpec marker;

    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static MeshNodeSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

// ╭───────────────────────────────────────────────────────────────────╮
// │  Velocity vector glyphs (Slice AN.1)                                │
// ╰───────────────────────────────────────────────────────────────────╯

/*!
 * \struct VelocityVectorSymbolLayerSpec
 * \brief 2D vector-field glyph paint pass (velocity arrows).
 *
 *        Mirrors VelocityVectorStyle 1:1 so the Rule path's edits
 *        round-trip back onto the existing legacy style via the
 *        SWMM2DResultsLayer back-propagation lambda
 *        (RENDERING_2D_RESULTS_STYLING_PLAN.md §4.4). Defaults match
 *        VelocityVectorStyle ctor defaults so a fresh Rule reproduces
 *        the legacy look.
 *
 *        Canonical SymbolLayer props keys:
 *          - "glyphLengthScalePxPerMps" → qreal (px per m/s)
 *          - "glyphLengthMinPx"         → qreal
 *          - "glyphLengthMaxPx"         → qreal
 *          - "glyphSpacingPx"           → qreal (grid spacing)
 *          - "headSizePx"               → qreal
 *          - "color"                    → QColor
 *          - "dryDepthCutoff"           → qreal (m; below this depth
 *                                          no arrow is drawn)
 */
struct VelocityVectorSymbolLayerSpec
{
    qreal  glyphLengthScalePxPerMps = 20.0;
    qreal  glyphLengthMinPx         = 4.0;
    qreal  glyphLengthMaxPx         = 40.0;
    qreal  glyphSpacingPx           = 30.0;
    qreal  headSizePx               = 5.0;
    QColor color                    = QColor(20, 20, 20, 220);
    qreal  dryDepthCutoff           = 0.01;

    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static VelocityVectorSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RASTERSYMBOLLAYERS_H
