/**
 * @file      datadefined.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BI Phase 8.13.7-α / 8.13.43-α — typed-struct carve-out of
 *         the BI.2 string-expression data-defined override mechanism.
 *
 *         `DataDefinedScalar` maps one numeric feature attribute (e.g.
 *         `maxDepth`, `geom1`, `area`) onto a scalar output range
 *         (typically a symbol size or line width in pixels). It carries:
 *
 *           - `attribute`         the SWMM attribute key to read
 *           - `valueMin / Max`    the attribute-value range to map from
 *           - `outMin / Max`      the output range (e.g. 2..14 px)
 *           - `curve`             interpolation shape (Linear / Sqrt / Log)
 *
 *         `evaluate(value)` clamps to [valueMin, valueMax], applies the
 *         curve, and linearly maps to [outMin, outMax]. NaN / non-finite
 *         inputs return `outMin` defensively.
 *
 *         Storage convention: each `SymbolLayer::props` may carry an
 *         optional `"sizeData"` (markers) or `"widthData"` (lines)
 *         payload holding a `DataDefinedScalar` JSON. Renderers that
 *         honour data-defined output read the struct, evaluate it
 *         against the feature's attribute map, and write the resolved
 *         number into `props["size"] / props["width"]` before returning
 *         the SymbolStyle from `symbolFor`. Painters that already read
 *         those keys then see the per-feature override transparently —
 *         no painter changes needed for static rendering.
 *
 *         Cross-slice: GUI_IMPLEMENTATION_PLAN.md §L.BI Phase 8.13.7-α
 *         + Phase 8.13.43-α + §L.BI BI-MK.1 size/width controls.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_DATADEFINED_H
#define OPENSWMM_RENDER_DATADEFINED_H

#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

/*! \enum DDCurve
 *  \brief Interpolation shape for `DataDefinedScalar`. */
enum class DDCurve : int {
    Linear = 0,
    Sqrt = 1, /*!< Compresses high-end differences — useful for area-like attrs. */
    Log =
        2, /*!< Compresses extreme values — useful for flow / runoff that span orders of magnitude. */
};

/*! \struct DataDefinedScalar
 *  \brief Maps one numeric attribute to a scalar output range. */
struct DataDefinedScalar {
    QString attribute;     /*!< SWMM attribute key (e.g. "maxDepth"). */
    double valueMin = 0.0; /*!< Lower end of attribute-value range. */
    double valueMax = 1.0; /*!< Upper end of attribute-value range. */
    double outMin = 2.0;   /*!< Lower end of output range (pixels). */
    double outMax = 12.0;  /*!< Upper end of output range (pixels). */
    DDCurve curve = DDCurve::Linear;

    /*! Maps `value` through the curve into [outMin, outMax].
     *
     *  - NaN / non-finite → returns `outMin` (defensive default).
     *  - value ≤ valueMin → returns `outMin`.
     *  - value ≥ valueMax → returns `outMax`.
     *  - When `valueMin == valueMax` → returns `(outMin + outMax) * 0.5`
     *    so the output is well-defined and centred.
     *
     *  Linear curve: `outMin + t * (outMax - outMin)` where `t = (v - valueMin) / (valueMax - valueMin)`.
     *  Sqrt curve:   uses `sqrt(t)` as the interpolation parameter.
     *  Log curve:    uses `log10(1 + 9 * t) / 1` — maps [0,1] to [0,1] but compresses the high end.
     * @param value Numeric attribute value in the same units as valueMin/valueMax.
     * @return Interpolated scalar in outMin/outMax units (normally pixels).
     * Reversed input bounds reverse the mapping; reversed output bounds are legal.
     * Callers must provide finite configuration endpoints; evaluate() does not call
     * isValid(). A nonfinite input returns outMin before checking a constant range.
     */
    [[nodiscard]] double evaluate(double value) const;

    /** @brief Check the attribute key and finiteness of all four range endpoints.
     * @return True when attribute is nonempty and both input/output endpoints are
     * finite. Constant or reversed ranges, whitespace-only keys, and unrecognized
     * curve enum values are accepted; attribute existence is not checked. */
    [[nodiscard]] bool isValid() const;

    /** @brief Serialize all mapping fields without validating the configuration.
     * @return Owned object with the attribute, input/output endpoints, and integer curve tag. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Read a mapping using constructor defaults for absent or mistyped values.
     * @param j Mapping JSON; unrecognized curve integers select Linear.
     * @return Owned configuration without validating endpoint order, finiteness,
     * or whether the attribute exists. Use isValid() before applying it. */
    static DataDefinedScalar fromJson(const QJsonObject &j);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_DATADEFINED_H
