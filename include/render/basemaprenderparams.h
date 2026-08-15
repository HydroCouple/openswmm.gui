/*!
 * \file   basemaprenderparams.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared visual-adjustment parameters for raster basemaps.
 *
 *         Slice X.22 — every basemap layer (XYZ / WMTS / WMS / WCS) holds
 *         one of these and applies it to its composed tile QImage just
 *         before drawImage().  The Rendering tab of LayerStyleDialog
 *         drives the values via setBasemapRenderParams() on each layer.
 */
#ifndef OPENSWMM_RENDER_BASEMAPRENDERPARAMS_H
#define OPENSWMM_RENDER_BASEMAPRENDERPARAMS_H

#include <QImage>
#include <QJsonObject>

namespace OpenSWMM::Render {

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
struct BasemapRenderParams
{
    /*! [-1, +1] — additive offset applied to each RGB channel after
     *  scaling to [0,1].  0 = no change, +1 = max bright, -1 = black. */
    double brightness = 0.0;

    /*! [0, 4] — multiplicative pivot-around-0.5 scale on each channel.
     *  1 = no change, >1 = more contrast, <1 = greyer. */
    double contrast   = 1.0;

    /*! [-1, +1] — saturation shift.  0 = unchanged, +1 = fully
     *  saturated mix, -1 = fully desaturated (greyscale). */
    double saturation = 0.0;

    /*! \enum Resampling
     *  \brief Sample mode the painter uses when scaling tiles. */
    enum Resampling { Bilinear = 0, Nearest = 1 };
    Resampling resampling = Bilinear;

    [[nodiscard]] bool isIdentity() const
    {
        return qFuzzyCompare(brightness, 0.0)
            && qFuzzyCompare(contrast,   1.0)
            && qFuzzyCompare(saturation, 0.0);
    }

    /*! Apply brightness / contrast / saturation to \p img in place.
     *  No-op when isIdentity() so callers can guard cheaply.  The
     *  `resampling` field is honoured separately via the painter's
     *  SmoothPixmapTransform render hint. */
    void applyTo(QImage &img) const;

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(const QJsonObject &j);
};

inline bool operator==(const BasemapRenderParams &a, const BasemapRenderParams &b)
{
    return qFuzzyCompare(a.brightness, b.brightness)
        && qFuzzyCompare(a.contrast,   b.contrast)
        && qFuzzyCompare(a.saturation, b.saturation)
        && a.resampling == b.resampling;
}
inline bool operator!=(const BasemapRenderParams &a, const BasemapRenderParams &b) { return !(a == b); }

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_BASEMAPRENDERPARAMS_H
