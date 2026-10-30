/**
 * @file      rastersymbollayers.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Typed accessors for Raster + TIN Symbol Layers (Slice Z.6).
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
struct RasterColorRampSymbolLayerSpec {
    RasterColorRamp ramp;
    IntervalBinner binner;
    qreal clampMin = 0.0;
    qreal clampMax = 1.0;
    QColor noDataColor = QColor(0, 0, 0, 0);
    qreal opacity = 1.0;

    /** @brief Build a fresh RasterColorRamp symbol layer from this specification.
     * Ramp and binner are stored as compact JSON strings. Clamp endpoints remain raw; opacity is clamped to [0,1].
     * @return Independent value with default layer metadata and this spec's properties. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    /** @brief Read this paint specification from a property map.
     * @param layer Borrowed layer; its kind and unrelated properties are ignored.
     * @return Independent specification. Missing properties retain fresh-spec defaults. Nonempty JSON objects are delegated to the ramp/binner readers; malformed, empty or nonobject JSON uses defaults. Present clamp endpoints use unchecked QVariant::toDouble(); opacity is clamped to [0,1]. Colors accept QColor or legacy strings through SymbolProps::readColor(). */
    [[nodiscard]] static RasterColorRampSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    /** @brief Set the layer kind to RasterColorRamp and write this spec's canonical properties.
     * @param[in,out] layer Destination; unrelated property keys and other layer metadata
     * are retained. Ramp and binner are stored as compact JSON strings. Clamp endpoints remain raw; opacity is clamped to [0,1]. No signals or paint work are triggered. */
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
struct HillshadeSymbolLayerSpec {
    qreal azimuthDeg = 315.0;
    qreal altitudeDeg = 45.0;
    qreal zExaggeration = 1.0;
    qreal shadowFloor = 0.2;
    QString blendMode = QStringLiteral("Multiply");

    /*! Slice Z.6a — multiplier on shading intensity in [0, 1]. Distinct
     *  from shadowFloor: 0.0 disables shading entirely (everything fully
     *  lit); 1.0 applies the full normal-dot-light effect. Mirrors the
     *  legacy MeshFillStyle::hillshadeStrength field 1:1 so the paint
     *  integration preserves the existing default look. */
    qreal strength = 0.5;

    /** @brief Build a fresh Hillshade symbol layer from this specification.
     * Angles in degrees and vertical exaggeration are stored without normalization; shadowFloor and strength are clamped to [0,1]. The blend token is stored verbatim.
     * @return Independent value with default layer metadata and this spec's properties. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    /** @brief Read this paint specification from a property map.
     * @param layer Borrowed layer; its kind and unrelated properties are ignored.
     * @return Independent specification. Missing properties retain fresh-spec defaults. Present numbers use unchecked QVariant::toDouble(); only shadowFloor and strength are clamped to [0,1]. A nonempty blend token is accepted verbatim. Azimuth/altitude ranges and nonnegative exaggeration are not enforced. */
    [[nodiscard]] static HillshadeSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    /** @brief Set the layer kind to Hillshade and write this spec's canonical properties.
     * @param[in,out] layer Destination; unrelated property keys and other layer metadata
     * are retained. Angles in degrees and vertical exaggeration are stored without normalization; shadowFloor and strength are clamped to [0,1]. The blend token is stored verbatim. No signals or paint work are triggered. */
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
    Lines = 0,  /*!< Iso-lines only. */
    Filled = 1, /*!< Filled isobands only (no lines). */
    Both = 2    /*!< Lines stacked on top of filled bands. */
};

/** @brief Encode the contour mode as a stable lowercase token.
 * @param m Mode to encode; an unrecognized integer produces "lines".
 * @return "lines", "filled" or "both". */
[[nodiscard]] QString contourModeToString(ContourMode m);
/** @brief Decode an exact lowercase contour token.
 * @param s Case-sensitive token; whitespace is not trimmed.
 * @return Matching enum, or Lines for any unknown token. */
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
struct ContourSymbolLayerSpec {
    ContourMode mode = ContourMode::Lines;
    IntervalBinner binner;
    RasterColorRamp ramp;
    QColor lineColor = QColor(40, 40, 40);
    qreal lineWidthPx = 0.75;
    int labelEveryN = 0;
    QString labelFormat = QStringLiteral("%.1f");

    /*! Slice Z.6a — when true (default), filled isobands sample the
     *  ramp continuously between low and high (smooth gradient). When
     *  false, each band picks a discrete viridis-step colour at its
     *  centroid (categorical look). Mirrors the legacy
     *  ContourBandStyle::smoothBands toggle so the paint integration
     *  preserves both visual modes. */
    bool smoothBands = true;

    /** @brief Build a fresh Contour symbol layer from this specification.
     * Mode is stored as its integer, ramp/binner as compact JSON strings, and remaining fields verbatim, including pixel width and label frequency/format.
     * @return Independent value with default layer metadata and this spec's properties. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    /** @brief Read this paint specification from a property map.
     * @param layer Borrowed layer; its kind and unrelated properties are ignored.
     * @return Independent specification. Missing properties retain fresh-spec defaults. Present numbers and booleans use unchecked QVariant conversions; the mode integer is not validated. Empty labelFormat retains the default; nonempty strings are not checked as printf formats. Ramp/binner use defaults for absent, malformed, empty or nonobject JSON; colors use the tolerant SymbolProps reader. Width, frequency and break ranges are not validated. */
    [[nodiscard]] static ContourSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    /** @brief Set the layer kind to Contour and write this spec's canonical properties.
     * @param[in,out] layer Destination; unrelated property keys and other layer metadata
     * are retained. Mode is stored as its integer, ramp/binner as compact JSON strings, and remaining fields verbatim, including pixel width and label frequency/format. No signals or paint work are triggered. */
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
struct MeshEdgeSymbolLayerSpec {
    QColor color = QColor(80, 80, 80);
    qreal width = 0.5;
    Qt::PenStyle penStyle = Qt::SolidLine;
    int lodMinZoom = 0;

    /*! Slice Z.6a — slope-driven width emphasis. When `useSlopeDrivenWidth`
     *  is true, edges whose normalised slope exceeds `slopeBreak` paint
     *  at `wideWidthPx` with `wideColor` instead of the base width/color.
     *  Mirrors the legacy MeshEdgeStyle slope fields so the paint
     *  integration preserves the existing wireframe look. */
    bool useSlopeDrivenWidth = false;
    qreal slopeBreak = 0.5;
    qreal wideWidthPx = 1.5;
    QColor wideColor = QColor(40, 40, 40);

    /** @brief Build the base wireframe pen from color, width and penStyle.
     * @return Noncosmetic QPen; width is passed directly to Qt. LOD and slope
     * emphasis fields are not applied, and invalid values follow Qt behavior. */
    [[nodiscard]] QPen toQPen() const;
    /** @brief Build a fresh MeshEdge symbol layer from this specification.
     * Colors, pixel widths, pen-style integer, LOD threshold and slope-emphasis fields are copied without clamping or enum validation.
     * @return Independent value with default layer metadata and this spec's properties. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    /** @brief Read this paint specification from a property map.
     * @param layer Borrowed layer; its kind and unrelated properties are ignored.
     * @return Independent specification. Missing properties retain fresh-spec defaults. Colors use the tolerant SymbolProps reader; other present values use unchecked QVariant conversions. Widths, LOD threshold, pen-style integer and normalized slope break are not validated. */
    [[nodiscard]] static MeshEdgeSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    /** @brief Set the layer kind to MeshEdge and write this spec's canonical properties.
     * @param[in,out] layer Destination; unrelated property keys and other layer metadata
     * are retained. Colors, pixel widths, pen-style integer, LOD threshold and slope-emphasis fields are copied without clamping or enum validation. No signals or paint work are triggered. */
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
struct MeshNodeSymbolLayerSpec {
    MarkerSymbolLayerSpec marker;

    /** @brief Build a fresh MeshNode symbol layer from this specification.
     * Delegates all marker properties to MarkerSymbolLayerSpec::writeToSymbolLayer(), then changes the kind to MeshNode.
     * @return Independent value with default layer metadata and this spec's properties. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    /** @brief Read this paint specification from a property map.
     * @param layer Borrowed layer; its kind and unrelated properties are ignored.
     * @return Independent specification. Delegates all properties to MarkerSymbolLayerSpec::fromSymbolLayer(), including its defaults, conversion and clamping rules. Does not require the input kind to be MeshNode. */
    [[nodiscard]] static MeshNodeSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    /** @brief Set the layer kind to MeshNode and write this spec's canonical properties.
     * @param[in,out] layer Destination; unrelated property keys and other layer metadata
     * are retained. Delegates all marker properties to MarkerSymbolLayerSpec::writeToSymbolLayer(), then changes the kind to MeshNode. No signals or paint work are triggered. */
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
struct VelocityVectorSymbolLayerSpec {
    qreal glyphLengthScalePxPerMps = 20.0;
    qreal glyphLengthMinPx = 4.0;
    qreal glyphLengthMaxPx = 40.0;
    qreal glyphSpacingPx = 30.0;
    qreal headSizePx = 5.0;
    QColor color = QColor(20, 20, 20, 220);
    qreal dryDepthCutoff = 0.01;

    /** @brief Build a fresh VectorGlyph symbol layer from this specification.
     * Copies length scale (pixels per m/s), lengths/spacing/head size (pixels), dry-depth cutoff (metres) and color verbatim; no clamping is applied.
     * @return Independent value with default layer metadata and this spec's properties. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    /** @brief Read this paint specification from a property map.
     * @param layer Borrowed layer; its kind and unrelated properties are ignored.
     * @return Independent specification. Missing properties retain fresh-spec defaults. Present numeric properties use unchecked QVariant::toDouble(); color uses the tolerant SymbolProps reader. Lengths, spacing and depth cutoff are not validated or reordered. */
    [[nodiscard]] static VelocityVectorSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);
    /** @brief Set the layer kind to VectorGlyph and write this spec's canonical properties.
     * @param[in,out] layer Destination; unrelated property keys and other layer metadata
     * are retained. Copies length scale (pixels per m/s), lengths/spacing/head size (pixels), dry-depth cutoff (metres) and color verbatim; no clamping is applied. No signals or paint work are triggered. */
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RASTERSYMBOLLAYERS_H
