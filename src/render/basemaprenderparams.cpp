/*!
 * \file   basemaprenderparams.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/basemaprenderparams.h"

#include <QRgb>

#include <algorithm>

namespace OpenSWMM::Render {

void BasemapRenderParams::applyTo(QImage &img) const
{
    if (isIdentity() || img.isNull()) return;

    // Force an ARGB32 layout if we got something odd (Format_ARGB32 is
    // straight-alpha; ARGB32_Premultiplied complicates the per-channel
    // contrast scaling so we round-trip through straight-alpha).
    if (img.format() != QImage::Format_ARGB32
        && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);

    const bool premul = (img.format() == QImage::Format_ARGB32_Premultiplied);

    const double bright = std::clamp(brightness, -1.0, 1.0);
    const double con    = std::clamp(contrast,    0.0, 4.0);
    const double sat    = std::clamp(saturation, -1.0, 1.0);

    const int H = img.height();
    const int W = img.width();
    for (int y = 0; y < H; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < W; ++x) {
            const QRgb px = row[x];
            int a = qAlpha(px);
            if (a == 0) continue;

            double r = qRed(px)   / 255.0;
            double g = qGreen(px) / 255.0;
            double b = qBlue(px)  / 255.0;
            if (premul && a > 0) {
                const double aN = a / 255.0;
                r /= aN; g /= aN; b /= aN;
            }

            // Brightness: additive offset.
            r += bright; g += bright; b += bright;

            // Contrast: pivot around 0.5.
            r = 0.5 + (r - 0.5) * con;
            g = 0.5 + (g - 0.5) * con;
            b = 0.5 + (b - 0.5) * con;

            // Saturation: mix with luma (Rec. 709).
            if (!qFuzzyCompare(sat, 0.0)) {
                const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                if (sat < 0.0) {
                    const double k = 1.0 + sat;       // sat == -1 → all luma
                    r = luma + (r - luma) * k;
                    g = luma + (g - luma) * k;
                    b = luma + (b - luma) * k;
                } else {
                    // Push channels away from luma; clamp via std::clamp below.
                    const double k = 1.0 + sat;
                    r = luma + (r - luma) * k;
                    g = luma + (g - luma) * k;
                    b = luma + (b - luma) * k;
                }
            }

            r = std::clamp(r, 0.0, 1.0);
            g = std::clamp(g, 0.0, 1.0);
            b = std::clamp(b, 0.0, 1.0);

            int R = int(r * 255.0 + 0.5);
            int G = int(g * 255.0 + 0.5);
            int B = int(b * 255.0 + 0.5);
            if (premul) {
                const double aN = a / 255.0;
                R = int(R * aN + 0.5);
                G = int(G * aN + 0.5);
                B = int(B * aN + 0.5);
            }
            row[x] = qRgba(R, G, B, a);
        }
    }
}

QJsonObject BasemapRenderParams::toJson() const
{
    QJsonObject j;
    if (!qFuzzyCompare(brightness, 0.0)) j[QStringLiteral("brightness")] = brightness;
    if (!qFuzzyCompare(contrast,   1.0)) j[QStringLiteral("contrast")]   = contrast;
    if (!qFuzzyCompare(saturation, 0.0)) j[QStringLiteral("saturation")] = saturation;
    if (resampling != Bilinear)          j[QStringLiteral("resampling")] = int(resampling);
    return j;
}

void BasemapRenderParams::fromJson(const QJsonObject &j)
{
    brightness = j.value(QStringLiteral("brightness")).toDouble(0.0);
    contrast   = j.value(QStringLiteral("contrast")).toDouble(1.0);
    saturation = j.value(QStringLiteral("saturation")).toDouble(0.0);
    resampling = static_cast<Resampling>(j.value(QStringLiteral("resampling")).toInt(int(Bilinear)));
}

} // namespace OpenSWMM::Render
