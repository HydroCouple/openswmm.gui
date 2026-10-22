/**
 * @file      basemaprenderparams.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared visual-adjustment parameters for raster basemaps.
 *
 *         Slice X.22 — every basemap layer (XYZ / WMTS / WMS / WCS) holds
 *         one of these and applies it to its composed tile QImage just
 *         before drawImage().  The Rendering tab of LayerStyleDialog
 *         drives the values via setBasemapRenderParams() on each layer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_BASEMAPRENDERPARAMS_H
#define OPENSWMM_RENDER_BASEMAPRENDERPARAMS_H

#include <QImage>
#include <QJsonObject>

namespace OpenSWMM::Render
{

/*!
 * \struct BasemapRenderParams
 * \brief Brightness / contrast / saturation / resampling for a basemap.
 *
 *        Values centred on zero / one so a default-constructed instance
 *        is a no-op pass-through.  `applyTo()` mutates the supplied
 *        QImage in place; for ARGB32_Premultiplied inputs (the format
 *        every basemap layer's composed tile uses) the transform is
 *        linear per pixel.
 */
struct BasemapRenderParams {
    /*! [-1, +1] — additive offset applied to each RGB channel after
     *  scaling to [0,1].  0 = no change, +1 = max bright, -1 = black. */
    double brightness = 0.0;

    /*! [0, 4] — multiplicative pivot-around-0.5 scale on each channel.
     *  1 = no change, >1 = more contrast, <1 = greyer. */
    double contrast = 1.0;

    /*! [-1, +1] — saturation shift.  0 = unchanged, +1 = fully
     *  saturated mix, -1 = fully desaturated (greyscale). */
    double saturation = 0.0;

    /*! \enum Resampling
     *  \brief Sample mode the painter uses when scaling tiles. */
    enum Resampling { Bilinear = 0, Nearest = 1 };
    Resampling resampling = Bilinear;

    /**
     * @brief Test whether the color adjustment is an approximate no-op.
     * @return True when brightness/saturation compare to zero and contrast to one
     *         using qFuzzyCompare(). Resampling is deliberately not part of this test.
     */
    [[nodiscard]] bool isIdentity() const
    {
        return qFuzzyCompare(brightness, 0.0) && qFuzzyCompare(contrast, 1.0) &&
               qFuzzyCompare(saturation, 0.0);
    }

    /**
     * @brief Apply brightness, contrast, and saturation to an image in place.
     * @details Apply brightness / contrast / saturation to \p img in place.
     *  No-op when isIdentity() so callers can guard cheaply.  The
     *  `resampling` field is honoured separately via the painter's
     *  SmoothPixmapTransform render hint.
     * @param[in,out] img Image to modify. A null image or identity adjustment is left
     *                   unchanged. Other formats are converted to ARGB32; ARGB32 and
     *                   ARGB32_Premultiplied retain their formats and alpha channels.
     * @details Finite brightness, contrast, and saturation are clamped to [-1,1], [0,4],
     *          and [-1,1] respectively. Premultiplied channels are unpremultiplied for
     *          adjustment and premultiplied again afterward. Fully transparent pixels
     *          are skipped. QImage detachment/allocation may occur; the caller retains img.
     * @pre Adjustment values must be finite. The routine does not validate NaN/infinity.
     */
    void applyTo(QImage &img) const;

    /**
     * @brief Serialize non-default adjustment and resampling values.
     * @return A JSON object omitting fuzzy-default numeric values and default Bilinear
     *         resampling. No range validation or clamping occurs during serialization.
     */
    [[nodiscard]] QJsonObject toJson() const;

    /**
     * @brief Replace settings from project JSON, defaulting missing or wrong-typed fields.
     * @param[in] j Settings object; unknown keys are ignored without modifying the object.
     * @post Brightness defaults to 0, contrast to 1, saturation to 0, and resampling to
     *       Bilinear. Numeric values and integer enum values are loaded without range checks.
     */
    void fromJson(const QJsonObject &j);
};

/**
 * @brief Compare color settings approximately and resampling modes exactly.
 * @param[in] a Left settings value.
 * @param[in] b Right settings value.
 * @return True when all numeric fields satisfy qFuzzyCompare() and resampling matches.
 */
inline bool operator==(const BasemapRenderParams &a, const BasemapRenderParams &b)
{
    return qFuzzyCompare(a.brightness, b.brightness) && qFuzzyCompare(a.contrast, b.contrast) &&
           qFuzzyCompare(a.saturation, b.saturation) && a.resampling == b.resampling;
}
/**
 * @brief Negate the basemap adjustment equality comparison.
 * @param[in] a Left settings value.
 * @param[in] b Right settings value.
 * @return True when at least one field differs under operator==.
 */
inline bool operator!=(const BasemapRenderParams &a, const BasemapRenderParams &b)
{
    return !(a == b);
}

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_BASEMAPRENDERPARAMS_H
