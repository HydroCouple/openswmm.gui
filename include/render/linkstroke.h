// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMM_RENDER_LINKSTROKE_H
#define OPENSWMM_RENDER_LINKSTROKE_H

#include <QPen>
#include <QPointF>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace OpenSWMM::Render {

// SVG mitres become bevels beyond this multiple of the half-width.
inline constexpr double kLinkMiterLimit = 4.0;
inline constexpr int kLinkCapSegments = 12;

inline void configureLinkStrokePen(QPen &pen)
{
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::SvgMiterJoin);
    pen.setMiterLimit(kLinkMiterLimit);
}

// Appends an open link as triangles. Most links fit in stack storage. Compute
// offsets in anchored doubles before the caller converts vertices to floats.
// Adjacent segments share a cross-section, so an ordinary bend is painted
// once even when the link is translucent. No full discs overlap its body.
template<class AppendVertex>
void appendLinkStrokeTriangles(const double *xy, std::size_t count,
                               double originX, double originY, double halfWidth,
                               AppendVertex appendVertex)
{
    if (!xy || count < 2 || !(halfWidth > 0) || !std::isfinite(halfWidth))
        return;
    QVarLengthArray<QPointF, 16> points;
    for (std::size_t i = 0; i < count; ++i) {
        const QPointF p(xy[2*i] - originX, xy[2*i+1] - originY);
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) return;
        if (points.isEmpty() || points.back() != p) points.append(p);
    }
    if (points.size() < 2) return;

    auto triangle = [&](QPointF a, QPointF b, QPointF c) {
        appendVertex(a); appendVertex(b); appendVertex(c);
    };
    auto normal = [&](qsizetype a, qsizetype b) {
        const QPointF d = points[b] - points[a];
        const double length = std::hypot(d.x(), d.y());
        return QPointF(-d.y()/length, d.x()/length);
    };
    auto cap = [&](QPointF center, QPointF startNormal) {
        constexpr double pi = 3.14159265358979323846;
        QPointF previous = center + startNormal * halfWidth;
        for (int i = 1; i <= kLinkCapSegments; ++i) {
            const double angle = pi * i / kLinkCapSegments;
            // Fix the final endpoint exactly on the adjoining body edge.
            const QPointF n = i == kLinkCapSegments ? -startNormal
                : QPointF(startNormal.x()*std::cos(angle) - startNormal.y()*std::sin(angle),
                          startNormal.x()*std::sin(angle) + startNormal.y()*std::cos(angle));
            const QPointF next = center + n * halfWidth;
            triangle(center, previous, next);
            previous = next;
        }
    };
    struct Join { QPointF leftIn, rightIn, leftOut, rightOut; };
    auto join = [&](qsizetype i) {
        const QPointF p = points[i];
        const QPointF before = normal(i-1, i);
        const QPointF after = normal(i, i+1);
        const double denominator = 1.0 + QPointF::dotProduct(before, after);
        Join result{p+before*halfWidth, p-before*halfWidth,
                    p+after*halfWidth, p-after*halfWidth};
        // A complete reversal has no finite mitre; the two bodies meet at p.
        if (denominator <= 1e-12) return result;
        const QPointF offset = (before + after) * (halfWidth / denominator);
        const double length = std::hypot(offset.x(), offset.y());
        if (length <= kLinkMiterLimit * halfWidth) {
            result.leftIn = result.leftOut = p + offset;
            result.rightIn = result.rightOut = p - offset;
        } else {
            // The outside uses a bevel. Bound the inside intersection too:
            // nearly reversing or very short edges must not create spikes.
            const double innerLimit = std::min(
                std::hypot((points[i]-points[i-1]).x(), (points[i]-points[i-1]).y()),
                std::hypot((points[i+1]-points[i]).x(), (points[i+1]-points[i]).y()));
            const QPointF inner = offset * (std::min(length, innerLimit) / length);
            const double turn = before.x()*after.y() - before.y()*after.x();
            if (turn > 0) {
                result.leftIn = result.leftOut = p + inner;
                triangle(result.leftIn, result.rightIn, result.rightOut);
            } else {
                result.rightIn = result.rightOut = p - inner;
                triangle(result.rightIn, result.leftOut, result.leftIn);
            }
        }
        return result;
    };

    const QPointF firstNormal = normal(0, 1);
    QPointF left = points.front() + firstNormal * halfWidth;
    QPointF right = points.front() - firstNormal * halfWidth;
    cap(points.front(), firstNormal);
    for (qsizetype i = 1; i < points.size(); ++i) {
        const QPointF lastNormal = normal(i-1, i);
        const Join next = i+1 < points.size() ? join(i)
            : Join{points[i]+lastNormal*halfWidth, points[i]-lastNormal*halfWidth,
                   points[i]+lastNormal*halfWidth, points[i]-lastNormal*halfWidth};
        triangle(left, right, next.leftIn);
        triangle(next.leftIn, right, next.rightIn);
        left = next.leftOut;
        right = next.rightOut;
    }
    cap(points.back(), -normal(points.size()-2, points.size()-1));
}

} // namespace OpenSWMM::Render
#endif
