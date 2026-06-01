/*!
 * \file   symbollayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One paint pass in a compositional SymbolStyle stack.
 *
 *         A SymbolLayer is one "pass" of drawing for a feature — e.g. a
 *         simple-marker pass, a simple-line pass, a hatch-fill pass.
 *         Multiple SymbolLayers stack inside a SymbolStyle (bottom-up
 *         paint order) so users can build composite symbols (e.g. an outer
 *         ring + an inner ring + a centre cross marker for a double-ringed
 *         manhole) without inventing one-off symbol classes.
 *
 *         Properties are stored as a QVariantMap to keep the type
 *         lightweight — concrete renderers know which keys their kind reads
 *         (a SimpleMarker layer expects "shape", "size", "color"; a
 *         SimpleLine layer expects "color", "width", "dashPattern"; etc.).
 *
 *         dataDefinedOverrides maps property names to expression strings
 *         that, when evaluated against a feature's attributes, override
 *         the corresponding entry in props at paint time. The expression
 *         language is BI.2's LabelExpression DSL (deferred — sub-phase
 *         8.13.6.1 only persists the strings; evaluation lands later).
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.3). Sub-phase 8.13.6.1 — interface + types only.
 */

#ifndef OPENSWMM_RENDER_SYMBOLLAYER_H
#define OPENSWMM_RENDER_SYMBOLLAYER_H

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariantMap>

namespace OpenSWMM::Render
{

/*!
 * \enum SymbolLayerKind
 * \brief The kind of paint pass this SymbolLayer represents.
 *
 *        v1 ships the items marked MUST in §J.3. The remaining variants
 *        round-trip through JSON so style files authored by future tools
 *        load correctly, even when the UI editor is deferred.
 */
enum class SymbolLayerKind
{
    SimpleMarker,       /*!< Point marker (circle, square, …). [MUST] */
    SimpleLine,         /*!< Stroked polyline. [MUST] */
    SimpleFill,         /*!< Filled polygon. [MUST] */
    MarkerLine,         /*!< Repeated marker along a polyline (flow arrows). [MUST] */
    HatchFill,          /*!< Parallel-line / cross-hatch polygon fill. */
    PatternFill,        /*!< Image-tile polygon fill. (UI editor deferred.) */
    SvgMarker,          /*!< SVG path file marker. (UI editor deferred.) */
    FontMarker,         /*!< Unicode-character marker from a font. (UI editor deferred.) */
    // ── Slice Z.6 — Raster + TIN paint passes ─────────────────────────
    RasterColorRamp,    /*!< Continuous-color raster ramp (DEM / 2D depth field). */
    Hillshade,          /*!< Terrain hillshade overlay (azimuth / altitude / z-exag). */
    Contour,            /*!< Contour lines + optional filled isobands. */
    MeshEdge,           /*!< TIN / mesh wireframe edges. */
    MeshNode,           /*!< TIN / mesh vertex markers. */
    // Slice AN.1 — 2D results velocity field. The Rule's SymbolLayer
    // carries glyph length / spacing / color / dryDepth cutoff; the
    // painter (SWMM2DVelocityArrowsItem) re-samples per frame via the
    // matching VelocityVectorSymbolLayerSpec. See
    // docs/RENDERING_2D_RESULTS_STYLING_PLAN.md.
    VectorGlyph         /*!< 2D vector-field glyph layer (velocity arrows). */
};

/*!
 * \brief Returns the JSON string token for a SymbolLayerKind.
 */
[[nodiscard]] QString symbolLayerKindToString(SymbolLayerKind kind);

/*!
 * \brief Parses the JSON string token back into a SymbolLayerKind.
 * \return SymbolLayerKind::SimpleMarker when the token is unknown — keeps
 *         round-trip from breaking on future / unknown kinds.
 */
[[nodiscard]] SymbolLayerKind symbolLayerKindFromString(const QString &s);

/*!
 * \struct SymbolLayer
 * \brief One paint pass in the SymbolStyle stack.
 */
struct SymbolLayer
{
    SymbolLayerKind        kind = SymbolLayerKind::SimpleMarker;
    QVariantMap            props;                  /*!< Kind-specific (shape, size, color, …). */
    QMap<QString, QString> dataDefinedOverrides;   /*!< property name → expression string. */

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(const QJsonObject &j);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLLAYER_H
