// GPL-3.0-or-later

/**
 * @file      smoothfillraster.h
 * @brief     Rasterization of smoothly interpolated mesh scalar fields.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <algorithm>
#include <array>
#include <cmath>

namespace OpenSWMM::Render::SmoothFillRaster
{
struct Vertex {
    QPointF point;
    QRgb color;
}; // device pixels, premultiplied RGBA

// Gouraud interpolation matches the native colored-triangle material. Shared
// edges have one owner, including the diagonal of a clipped polygon's fan.
/** @brief Composite one Gouraud-shaded triangle using a shared-edge ownership rule.
 * @param[in,out] image Writable QImage::Format_ARGB32_Premultiplied destination,
 * addressed as QRgb scanlines. The format is a caller requirement, not checked or
 * converted here. Only covered in-bounds pixels change; no painter is required.
 * @param v Three copied device-pixel positions and premultiplied RGBA colors.
 * Nonfinite coordinates, nonfinite/zero area or wholly outside bounds yield no
 * output. Clockwise vertices are reordered locally; inputs are not mutated.
 * Samples pixel centers, interpolates premultiplied channels barycentrically and
 * applies integer source-over with rounding. The top/left edge rule assigns a
 * shared edge to one adjacent triangle; there is no antialiasing or depth test.
 * Exclusive image access and correctly premultiplied colors are required. */
inline void triangle(QImage &image, std::array<Vertex, 3> v)
{
    const auto edge = [](QPointF a, QPointF b, QPointF p) {
        return (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
    };
    for (const auto &p : v)
        if (!std::isfinite(p.point.x()) || !std::isfinite(p.point.y()))
            return;
    double area = edge(v[0].point, v[1].point, v[2].point);
    if (!std::isfinite(area) || area == 0)
        return;
    if (area < 0) {
        std::swap(v[1], v[2]);
        area = -area;
    }
    const double minX = std::min({v[0].point.x(), v[1].point.x(), v[2].point.x()});
    const double maxX = std::max({v[0].point.x(), v[1].point.x(), v[2].point.x()});
    const double minY = std::min({v[0].point.y(), v[1].point.y(), v[2].point.y()});
    const double maxY = std::max({v[0].point.y(), v[1].point.y(), v[2].point.y()});
    if (maxX < .5 || maxY < .5 || minX > image.width() - .5 || minY > image.height() - .5)
        return;
    const int x0 = int(std::ceil(std::max(.5, minX) - .5));
    const int x1 = int(std::floor(std::min(image.width() - .5, maxX) - .5));
    const int y0 = int(std::ceil(std::max(.5, minY) - .5));
    const int y1 = int(std::floor(std::min(image.height() - .5, maxY) - .5));
    std::array<bool, 3> inclusive;
    for (int k = 0; k < 3; ++k) {
        const auto a = v[(k + 1) % 3].point, b = v[(k + 2) % 3].point;
        inclusive[k] = b.y() < a.y() || (b.y() == a.y() && b.x() > a.x());
    }
    for (int y = y0; y <= y1; ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = x0; x <= x1; ++x) {
            std::array<double, 3> w;
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                w[k] = edge(v[(k + 1) % 3].point, v[(k + 2) % 3].point, {x + .5, y + .5});
                if (w[k] < 0 || (w[k] == 0 && !inclusive[k])) {
                    inside = false;
                    break;
                }
                w[k] /= area;
            }
            if (!inside)
                continue;
            const auto channel = [&](int c0, int c1, int c2) {
                return std::clamp(int(std::lround(w[0] * c0 + w[1] * c1 + w[2] * c2)), 0, 255);
            };
            const int a = channel(qAlpha(v[0].color), qAlpha(v[1].color), qAlpha(v[2].color));
            const int r = channel(qRed(v[0].color), qRed(v[1].color), qRed(v[2].color));
            const int g = channel(qGreen(v[0].color), qGreen(v[1].color), qGreen(v[2].color));
            const int b = channel(qBlue(v[0].color), qBlue(v[1].color), qBlue(v[2].color));
            const QRgb old = row[x];
            const auto over = [&](int c, int dest) {
                return std::min(255, c + (dest * (255 - a) + 127) / 255);
            };
            row[x] = qRgba(over(r, qRed(old)), over(g, qGreen(old)), over(b, qBlue(old)),
                           over(a, qAlpha(old)));
        }
    }
}
} // namespace OpenSWMM::Render::SmoothFillRaster
