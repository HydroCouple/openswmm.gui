/**
 * @file      labelconfig.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared label-configuration value-type used by every layer that
 *         paints text labels.
 *
 *         Slice X.18 — the Labels tab in LayerStyleDialog edits one of
 *         these for the active layer.  Both `SWMMModelLayer` and
 *         `GISVectorLayer` hold a `LabelConfig` and emit a change signal
 *         on mutation so the canvas + legend stay in sync via the
 *         standard MVC channel.
 *
 *         Schema is JSON-round-trippable and persisted into the .oswp via
 *         `projectserializer`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LABELCONFIG_H
#define OPENSWMM_RENDER_LABELCONFIG_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QString>
#include <QVariant>

namespace OpenSWMM::Render
{

/*!
 * \struct LabelConfig
 * \brief Per-layer label rendering parameters.
 *
 *        The "Standard" set per slice X.18:
 *          - font family + size + bold/italic (via QFont)
 *          - text colour
 *          - halo (colour + radius in pixels) for legibility on busy maps
 *          - placement hint (auto / above / below / left / right / centre)
 *          - field name (label expression — empty means "object name" for
 *            SWMMModelLayer, or the layer's labelField property for
 *            GISVectorLayer)
 *          - visibility scale window (minScale / maxScale; 0 = unbounded)
 *
 *        Halo + placement + scale-range are the QGIS-standard additions
 *        that turn a single-line "show labels" toggle into a useful
 *        cartographic tool.
 */
struct LabelConfig {
    /*! Master on/off.  Equivalent to the legacy `showLabels` bool. */
    bool enabled = false;

    /*! Attribute / field to render as the label.  Empty string means
     *  "use the object's primary name" — the layer paint path falls
     *  back to whatever it considers canonical (e.g. SWMM element
     *  name, GIS feature's FID). */
    QString fieldName;

    /*! Label expression / template (L-1 results labels). When non-empty it
     *  wins over fieldName: the paint path substitutes `{token}` placeholders
     *  with field values — `{name}` for the element name and any
     *  result/attribute field name (e.g. `{depth}`, `{flow}`) for that
     *  feature's current value. Literal text between tokens is kept verbatim,
     *  so "{name}: {depth} m" → "J1: 2.84 m". Unknown tokens resolve to empty. */
    QString expression;

    /*! Font — family / weight / italic flags ride here. */
    QFont font;

    /*! Point size for `font`.  Stored separately to make JSON
     *  round-trip independent of platform font metrics. */
    qreal fontSizePt = 9.0;

    /*! Text fill colour. */
    QColor color = QColor(20, 20, 20);

    /*! Halo (outline around glyphs) — when `haloEnabled`, the painter
     *  strokes the glyph path with `haloColor` at `haloRadiusPx` before
     *  filling.  Cheap and dramatically improves legibility over busy
     *  raster basemaps / mesh shading. */
    bool haloEnabled = false;
    QColor haloColor = QColor(255, 255, 255);
    qreal haloRadiusPx = 1.5;

    /*! \enum Placement
     *  \brief Where the label sits relative to the feature anchor. */
    enum Placement {
        AutoPlacement = 0, /*!< Painter picks (typically above-right).   */
        Above,             /*!< Above the anchor.                        */
        Below,             /*!< Below the anchor.                        */
        Left,              /*!< Left of the anchor, right-aligned text.  */
        Right,             /*!< Right of the anchor, left-aligned text.  */
        Centre,            /*!< Anchor centred horizontally and vert.    */
    };
    Placement placement = AutoPlacement;

    /*! Scale-dependent visibility window.  Scale here means the
     *  canvas scale denominator (`1:scale`).  `0` means unbounded.
     *  - `minScale > 0` hides labels when the user zooms further OUT
     *    than the threshold (scale denominator larger).
     *  - `maxScale > 0` hides labels when the user zooms further IN
     *    than the threshold (scale denominator smaller). */
    double minScale = 0.0;
    double maxScale = 0.0;

    /*! Slice X.24 — background frame drawn behind the glyphs.  When
     *  `backgroundEnabled`, the painter fills a rounded-rect padded by
     *  `backgroundPaddingPx` around the text bounding box in
     *  `backgroundColor` before drawing the glyphs.  Paired well with
     *  the halo for legibility on busy maps. */
    bool backgroundEnabled = false;
    QColor backgroundColor = QColor(255, 255, 255, 200);
    qreal backgroundPaddingPx = 2.0;
    qreal backgroundRadiusPx = 3.0;

    /*! Slice X.24 — per-feature priority field.  When set, the paint
     *  path can sort features by the value of this attribute and skip
     *  lower-priority labels first when collision avoidance is enabled
     *  (collision avoidance itself is a follow-up; the field is
     *  persisted so styles authored today survive when the feature
     *  lands). */
    QString priorityField;

    /*! Feedback C4 — numeric formatting for field values and numeric
     *  `{tokens}` in expressions. -1 = automatic (6 significant digits,
     *  so a slope of 0.004123456789012 reads "0.00412346" instead of the
     *  17 digits QVariant::toString() emits); 0–10 = that many fixed
     *  decimals in the UI. Only Float/Double variants use this formatting;
     *  integer, string and other variants use QVariant::toString(). */
    int decimals = -1;

    /** @brief Format floating-point variants while retaining other QVariant text forms.
     * @param v Value inspected without mutation. Float/Double variants are handled
     * numerically; integers, booleans and numeric-looking strings use toString().
     * @param decimals Negative selects six significant digits; nonnegative selects
     * that many fixed decimal places without a precision cap in this function.
     * @return Owned text with no unit conversion/suffix. Nonfinite floating values
     * produce empty text; other types follow QVariant conversion, including invalid/null values. */
    [[nodiscard]] static QString formatValue(const QVariant &v, int decimals);

    /** @brief Serialize label fields, supported font attributes and paint settings.
     * @return Owned object with ARGB colors, point-size/pixel units retained,
     * and positive scale bounds only. Font persistence records family/bold/italic
     * rather than every QFont attribute. Optional/default fields can be omitted;
     * no validation of sizes, enum values, fields or expressions occurs. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Apply present label properties and reset absent scale/precision metadata.
     * @param j Label object. Most absent keys retain current values; minScale and
     * maxScale always become their converted values or zero, and decimals becomes
     * its converted value or -1. Invalid color strings are ignored; numeric ranges,
     * placement integers, field keys and expressions are not validated. No signals
     * are emitted because this is a value type. */
    void fromJson(const QJsonObject &j);

    /** @brief Test only whether the requested font size is positive.
     * @return fontSizePt > 0, without checking finiteness, font availability,
     * halo/background dimensions, scale bounds or expression validity. */
    [[nodiscard]] bool isValid() const { return fontSizePt > 0.0; }

    /*! \brief Build a QFont with our `fontSizePt` applied.  Helpers
     *         cache this so the paint loop doesn't reconstruct it per
     *         feature.
     * @return Owned font copied from font with setPointSizeF(fontSizePt) applied.
     * Callers supply a valid positive point size; Qt handles invalid requests,
     * with no local validity check or mutation of this configuration. */
    [[nodiscard]] QFont effectiveFont() const
    {
        QFont f = font;
        f.setPointSizeF(fontSizePt);
        return f;
    }
};

/** @brief Compare every stored label configuration field.
 * @param a First value, inspected without mutation.
 * @param b Second value, inspected without mutation.
 * @return True when strings, enums, fonts, colors and flags compare equal and
 * numeric dimensions/scales satisfy unshifted qFuzzyCompare. NaN numeric fields
 * do not compare equal; no expression or field-name equivalence is inferred. */
inline bool operator==(const LabelConfig &a, const LabelConfig &b)
{
    return a.enabled == b.enabled && a.fieldName == b.fieldName && a.expression == b.expression &&
           a.font == b.font && qFuzzyCompare(a.fontSizePt, b.fontSizePt) && a.color == b.color &&
           a.haloEnabled == b.haloEnabled && a.haloColor == b.haloColor &&
           qFuzzyCompare(a.haloRadiusPx, b.haloRadiusPx) && a.placement == b.placement &&
           qFuzzyCompare(a.minScale, b.minScale) && qFuzzyCompare(a.maxScale, b.maxScale) &&
           a.backgroundEnabled == b.backgroundEnabled && a.backgroundColor == b.backgroundColor &&
           qFuzzyCompare(a.backgroundPaddingPx, b.backgroundPaddingPx) &&
           qFuzzyCompare(a.backgroundRadiusPx, b.backgroundRadiusPx) &&
           a.priorityField == b.priorityField && a.decimals == b.decimals;
}
/** @brief Test whether label configurations differ.
 * @param a First value, inspected without mutation.
 * @param b Second value, inspected without mutation.
 * @return Logical negation of operator==(), including its numeric comparison semantics. */
inline bool operator!=(const LabelConfig &a, const LabelConfig &b)
{
    return !(a == b);
}

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LABELCONFIG_H
