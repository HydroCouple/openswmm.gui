/*!
 * \file   sectiondiagram.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/sectiondiagram.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace openswmmvis::sectionview {

namespace {

// ── Layout constants (device-independent pixels) ────────────────────────────
constexpr double kMarginX      = 14.0;
constexpr double kMarginTop    = 8.0;
constexpr double kMarginBottom = 8.0;
constexpr double kTitleH       = 18.0;
constexpr double kSubtitleH    = 14.0;
constexpr double kFooterH      = 16.0;
//! Extra room reserved around the drawing for dimension lines + leaders.
constexpr double kAnnotationPad = 46.0;
constexpr double kArrowSize     = 6.5;
constexpr double kPlanInsetSize = 96.0;

//! Below these widths/heights annotations are progressively dropped.
constexpr double kMinWidthForLeaders = 300.0;
constexpr double kMinWidthForDims    = 220.0;
constexpr double kMinHeightForFooter = 150.0;

/*! Blend \p over onto \p base by \p t (0..1) — keeps derived fills tied to the
 *  palette so dark themes stay legible without a second colour table. */
QColor mix(const QColor &base, const QColor &over, qreal t)
{
    return QColor::fromRgbF(base.redF()   * (1 - t) + over.redF()   * t,
                            base.greenF() * (1 - t) + over.greenF() * t,
                            base.blueF()  * (1 - t) + over.blueF()  * t);
}

void drawArrowHead(QPainter &p, const QPointF &tip, double angleRad,
                   const QColor &color)
{
    const double c = std::cos(angleRad), s = std::sin(angleRad);
    auto rot = [&](double dx, double dy) {
        return QPointF(tip.x() + dx * c - dy * s, tip.y() + dx * s + dy * c);
    };
    QPolygonF head;
    head << tip
         << rot(-kArrowSize,  kArrowSize * 0.38)
         << rot(-kArrowSize, -kArrowSize * 0.38);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(head);
}

/*!
 * Deterministic jitter in [-1, 1] from an integer key.
 *
 * A texture must look identical on every repaint — one that reshuffles when the
 * panel resizes reads as noise rather than as material — so this is a hash, not
 * a random generator, and carries no state.
 */
double jitter(int key)
{
    quint32 h = static_cast<quint32>(key) * 2654435761u;   // Knuth
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (static_cast<double>(h & 0xFFFFu) / 32767.5) - 1.0;
}

/*!
 * Fill \p shape with a material pattern.
 *
 * Patterns are generated in SCREEN space over the polygon's bounding box and
 * clipped to it, so the grain stays a constant visual size as the user zooms —
 * the same convention as a hatch on a drawing sheet, and it avoids a stipple
 * collapsing into a solid block when zoomed out.
 */
void paintTexture(QPainter &p, const QPolygonF &shape, DiagramTexture texture,
                  const QColor &ink)
{
    if (texture == DiagramTexture::None || shape.size() < 3) return;

    const QRectF b = shape.boundingRect();
    if (b.width() < 2.0 || b.height() < 2.0) return;

    p.save();
    QPainterPath clip;
    clip.addPolygon(shape);
    clip.closeSubpath();
    p.setClipPath(clip, Qt::IntersectClip);

    QColor c = ink;
    c.setAlphaF(0.55);

    switch (texture) {
    case DiagramTexture::Stipple:
    case DiagramTexture::Sand: {
        const double step = (texture == DiagramTexture::Sand) ? 5.0 : 8.0;
        const double r    = (texture == DiagramTexture::Sand) ? 0.6 : 0.9;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        int k = 0;
        for (double y = b.top() + 2.0; y < b.bottom(); y += step)
            for (double x = b.left() + 2.0; x < b.right(); x += step, ++k)
                p.drawEllipse(QPointF(x + jitter(k) * step * 0.35,
                                      y + jitter(k + 7919) * step * 0.35), r, r);
        break;
    }
    case DiagramTexture::Gravel: {
        // Open outlines, not filled blobs: gravel is mostly void, and the void
        // is the point of a storage layer.
        p.setBrush(Qt::NoBrush);
        QPen pen(c);
        pen.setWidthF(0.9);
        p.setPen(pen);
        const double step = 13.0;
        int k = 0;
        for (double y = b.top() + 5.0; y < b.bottom(); y += step)
            for (double x = b.left() + 5.0; x < b.right(); x += step, ++k) {
                const double rr = 2.6 + jitter(k) * 0.9;
                p.drawEllipse(QPointF(x + jitter(k + 104729) * 3.0,
                                      y + jitter(k + 15485863) * 3.0), rr, rr);
            }
        break;
    }
    case DiagramTexture::Aggregate: {
        p.setBrush(Qt::NoBrush);
        QPen pen(c);
        pen.setWidthF(0.9);
        p.setPen(pen);
        const double step = 11.0;
        int k = 0;
        for (double y = b.top() + 4.0; y < b.bottom(); y += step)
            for (double x = b.left() + 4.0; x < b.right(); x += step, ++k) {
                const double rr = 2.2 + jitter(k) * 0.8;
                const QPointF c0(x + jitter(k + 31) * 2.5, y + jitter(k + 97) * 2.5);
                // Angular chip: a jittered triangle reads as crushed stone
                // where a circle reads as rounded river gravel.
                QPolygonF chip;
                for (int v = 0; v < 3; ++v) {
                    const double a = (v * 2.0 * M_PI / 3.0) + jitter(k + v * 13) * 0.6;
                    chip << c0 + QPointF(rr * std::cos(a), rr * std::sin(a));
                }
                p.drawPolygon(chip);
            }
        break;
    }
    case DiagramTexture::Hatch: {
        QPen pen(c);
        pen.setWidthF(0.8);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const double step = 8.0;
        for (double x = b.left() - b.height(); x < b.right(); x += step)
            p.drawLine(QPointF(x, b.bottom()),
                       QPointF(x + b.height(), b.top()));
        break;
    }
    case DiagramTexture::Lattice: {
        QPen pen(c);
        pen.setWidthF(0.8);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const double step = 7.0;
        for (double x = b.left(); x < b.right(); x += step)
            p.drawLine(QPointF(x, b.top()), QPointF(x, b.bottom()));
        for (double y = b.top(); y < b.bottom(); y += step)
            p.drawLine(QPointF(b.left(), y), QPointF(b.right(), y));
        break;
    }
    case DiagramTexture::Brick: {
        QPen pen(c);
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const double bw = 26.0, bh = std::max(6.0, b.height() * 0.5);
        int row = 0;
        for (double y = b.top(); y < b.bottom(); y += bh, ++row) {
            p.drawLine(QPointF(b.left(), y), QPointF(b.right(), y));
            const double off = (row % 2) ? bw * 0.5 : 0.0;
            for (double x = b.left() + off; x < b.right(); x += bw)
                p.drawLine(QPointF(x, y), QPointF(x, std::min(y + bh, b.bottom())));
        }
        break;
    }
    case DiagramTexture::None:
        break;
    }

    p.restore();
}

/*! One shrub (a stem with two pairs of leaves) or one grass tuft. */
void paintPlant(QPainter &p, const QPointF &base, double heightPx, bool grass,
                const QColor &green, int key)
{
    if (heightPx < 3.0) return;

    QPen stem(green);
    stem.setWidthF(std::clamp(heightPx * 0.09, 0.8, 2.2));
    stem.setCapStyle(Qt::RoundCap);
    p.setPen(stem);
    p.setBrush(Qt::NoBrush);

    if (grass) {
        // Three splayed blades — turf / swale bottom.
        for (int i = -1; i <= 1; ++i) {
            const double lean = (i * 0.30) + jitter(key + i) * 0.12;
            const double h    = heightPx * (0.75 + 0.25 * (i == 0 ? 1.0 : 0.6));
            QPainterPath blade(base);
            blade.quadTo(base + QPointF(lean * h * 0.4, -h * 0.6),
                         base + QPointF(lean * h, -h));
            p.drawPath(blade);
        }
        return;
    }

    // Shrub: upright stem plus two leaf pairs.
    const QPointF top = base + QPointF(jitter(key) * heightPx * 0.08, -heightPx);
    p.drawLine(base, top);
    for (double f : { 0.45, 0.75 }) {
        const QPointF at = base + (top - base) * f;
        const double  L  = heightPx * 0.30;
        p.drawLine(at, at + QPointF(-L, -L * 0.75));
        p.drawLine(at, at + QPointF( L, -L * 0.75));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Colour roles
// ---------------------------------------------------------------------------

QColor diagramFillColor(DiagramRole role, const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Base);
    const QColor hi   = palette.color(QPalette::Highlight);
    const QColor text = palette.color(QPalette::WindowText);

    switch (role) {
    case DiagramRole::Conduit:    return mix(base, hi,   0.18);
    case DiagramRole::Structure:  return mix(base, text, 0.12);
    case DiagramRole::Soil:       return mix(base, QColor(150, 120,  70), 0.30);
    case DiagramRole::Media:      return mix(base, QColor(160, 130,  80), 0.42);
    case DiagramRole::Gravel:     return mix(base, text, 0.18);
    case DiagramRole::Vegetation: return mix(base, QColor( 90, 160,  90), 0.32);
    case DiagramRole::Water:      return mix(base, hi,   0.36);
    case DiagramRole::Muted:      return mix(base, text, 0.07);
    case DiagramRole::Accent:     return mix(base, hi,   0.30);
    }
    return base;
}

QColor diagramStrokeColor(DiagramRole role, const QPalette &palette)
{
    const QColor text = palette.color(QPalette::WindowText);
    switch (role) {
    case DiagramRole::Muted:  return palette.color(QPalette::Mid);
    case DiagramRole::Accent: return palette.color(QPalette::Highlight).darker(120);
    default:                  return text;
    }
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

QRectF SectionDiagramModel::computeBounds() const
{
    // NB: a zero-size QRectF *is* null, so `isNull()` cannot be used as the
    // "nothing accumulated yet" flag — an explicit one is required.
    bool   have = false;
    QRectF r;
    auto add = [&r, &have](const QPointF &p) {
        if (!have) { r = QRectF(p, QSizeF(0.0, 0.0)); have = true; return; }
        r.setLeft  (std::min(r.left(),   p.x()));
        r.setRight (std::max(r.right(),  p.x()));
        r.setTop   (std::min(r.top(),    p.y()));
        r.setBottom(std::max(r.bottom(), p.y()));
    };

    for (const auto &poly : polys)      for (const QPointF &p : poly.pts) add(p);
    for (const auto &pl   : polylines)  for (const QPointF &p : pl.pts)   add(p);
    for (const auto &g    : grounds)  { add({g.x0, g.y}); add({g.x1, g.y}); }
    for (const auto &d    : dims)     { add(d.from); add(d.to); }
    for (const auto &l    : leaders)    add(l.anchor);

    if (!have) return {};

    // Guard against a zero-extent axis (a single horizontal ground line, a
    // DUMMY section) so the fit below never divides by zero.
    if (r.width()  <= 0.0) r.adjust(-0.5, 0.0, 0.5, 0.0);
    if (r.height() <= 0.0) r.adjust(0.0, -0.5, 0.0, 0.5);
    return r;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void paintSectionDiagram(QPainter &p, const QRectF &target,
                         const SectionDiagramModel &model,
                         const QPalette &palette,
                         const DiagramViewport &viewport,
                         QRectF *fitRectOut)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor inkColor    = palette.color(QPalette::WindowText);
    const QColor mutedColor  = palette.color(QPalette::Mid);
    const QColor dimColor    = palette.color(QPalette::Highlight).darker(115);
    const QColor accentColor = palette.color(QPalette::Highlight);

    const QFont baseFont = p.font();
    const QFontMetricsF fm(baseFont);
    QRectF area = target.adjusted(kMarginX, kMarginTop, -kMarginX, -kMarginBottom);

    // ── Header ─────────────────────────────────────────────────────────────
    if (!model.title.isEmpty()) {
        QFont bold = baseFont;
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(inkColor);
        p.drawText(QRectF(area.left(), area.top(), area.width(), kTitleH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(model.title, Qt::ElideRight, area.width()));
        p.setFont(baseFont);
        area.setTop(area.top() + kTitleH);
    }
    if (!model.subtitle.isEmpty()) {
        p.setPen(mutedColor);
        p.drawText(QRectF(area.left(), area.top(), area.width(), kSubtitleH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(model.subtitle, Qt::ElideRight, area.width()));
        area.setTop(area.top() + kSubtitleH);
    }

    // ── Empty state ────────────────────────────────────────────────────────
    if (model.isEmpty()) {
        p.setPen(mutedColor);
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap,
                   model.emptyText.isEmpty() ? QStringLiteral("—")
                                             : model.emptyText);
        p.restore();
        return;
    }

    // ── Decluttering budget ────────────────────────────────────────────────
    const bool showLeaders = target.width()  >= kMinWidthForLeaders;
    const bool showDims    = target.width()  >= kMinWidthForDims;
    const bool showFooter  = !model.footer.isEmpty()
                             && target.height() >= kMinHeightForFooter;

    if (showFooter) area.setBottom(area.bottom() - kFooterH);

    // ── Plan inset carve-out ───────────────────────────────────────────────
    QRectF planRect;
    if (!model.plan.isEmpty()
        && area.width() > 2.4 * kPlanInsetSize
        && area.height() > kPlanInsetSize + 20.0)
    {
        planRect = QRectF(area.right() - kPlanInsetSize, area.top(),
                          kPlanInsetSize, kPlanInsetSize);
        area.setRight(planRect.left() - 10.0);
    }

    // ── Fit model bounds into the drawing area ─────────────────────────────
    const QRectF b = model.bounds.isNull() ? model.computeBounds() : model.bounds;
    if (b.isNull() || b.width() <= 0.0 || b.height() <= 0.0
        || area.width() <= 4.0 || area.height() <= 4.0) {
        p.restore();
        return;
    }

    const double pad = (showDims || showLeaders) ? kAnnotationPad : 10.0;

    // Leader labels are written in the margin on the side their offset points
    // to, so that margin has to be wide enough to hold the widest of them —
    // a fixed 46 px turns "C1 Inv 93.00" into "C1…". Measure per side and grow
    // the pad, capped so the drawing never gives up more than a third of the
    // width to text.
    double padLeft = pad, padRight = pad;
    if (showLeaders) {
        const double cap = area.width() * 0.40;
        double wantL = 0.0, wantR = 0.0;
        for (const DiagramLeader &l : model.leaders) {
            if (l.text.isEmpty()) continue;
            // The elbow itself (pixelOffset + the 8 px landing) sits inside
            // this margin, so the text needs room BEYOND it — reserving only
            // the text width still elides by exactly the elbow's length.
            const double w = fm.horizontalAdvance(l.text)
                             + std::abs(l.pixelOffset.x()) + 16.0;
            (l.pixelOffset.x() >= 0.0 ? wantR : wantL)
                = std::max(l.pixelOffset.x() >= 0.0 ? wantR : wantL, w);
        }
        padLeft  = std::clamp(wantL, pad, std::max(pad, cap));
        padRight = std::clamp(wantR, pad, std::max(pad, cap));
    }

    // Dimension labels sit `pixelOffset` off their own line, so the margin on
    // that side must clear the offset AND the text — otherwise the width
    // dimension of a section drawn in a short panel is written over the
    // subtitle. Half the annotation pad only covers an offset of 23 px.
    double padTop = pad * 0.5, padBottom = pad * 0.5;
    if (showDims) {
        const double capV = area.height() * 0.30;
        const double capH = area.width()  * 0.40;
        for (const DiagramDim &d : model.dims) {
            const double need = std::abs(d.pixelOffset) + fm.height() + 4.0;
            const bool horizontal =
                std::abs(d.to.y() - d.from.y()) <= std::abs(d.to.x() - d.from.x());
            if (horizontal) {
                double &side = (d.pixelOffset < 0.0) ? padTop : padBottom;
                side = std::clamp(std::max(side, need), pad * 0.5,
                                  std::max(pad * 0.5, capV));
            } else {
                double &side = (d.pixelOffset < 0.0) ? padLeft : padRight;
                side = std::clamp(std::max(side, need), pad,
                                  std::max(pad, capH));
            }
        }
    }
    const QRectF fitRect = area.adjusted(padLeft, padTop, -padRight, -padBottom);
    if (fitRectOut) *fitRectOut = fitRect;
    if (fitRect.width() <= 2.0 || fitRect.height() <= 2.0) {
        p.restore();
        return;
    }

    double sx = fitRect.width()  / b.width();
    double sy = fitRect.height() / b.height();
    if (model.uniformScale) sx = sy = std::min(sx, sy);

    // User zoom/pan layered over the fit. Scaling about the fit rect's centre
    // keeps "zoom out then in" returning to the same place.
    const double zoom = std::clamp(viewport.zoom, 0.05, 200.0);
    sx *= zoom;
    sy *= zoom;

    // Model y grows upward; screen y grows downward — hence the -sy.
    const double cx = fitRect.center().x() - sx * (b.left() + b.width()  * 0.5)
                      + viewport.panPx.x();
    const double cy = fitRect.center().y() + sy * (b.top()  + b.height() * 0.5)
                      + viewport.panPx.y();
    auto toPx = [sx, sy, cx, cy](const QPointF &m) {
        return QPointF(cx + sx * m.x(), cy - sy * m.y());
    };

    // Zoomed content must not spill over the header / footer / plan inset, all
    // of which are drawn in unscaled screen space.
    p.save();
    p.setClipRect(area.adjusted(-kMarginX * 0.5, 0.0, kMarginX * 0.5, 0.0));

    // ── Ground lines + hatch ───────────────────────────────────────────────
    for (const DiagramGround &g : model.grounds) {
        const QPointF a = toPx({g.x0, g.y});
        const QPointF c = toPx({g.x1, g.y});
        QPen groundPen(mix(palette.color(QPalette::Base),
                           QColor(140, 110, 70), 0.75));
        groundPen.setWidthF(1.4);
        p.setPen(groundPen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(a, c);

        const double step = 9.0;
        const double len  = 8.0;
        groundPen.setWidthF(0.8);
        p.setPen(groundPen);
        for (double x = std::min(a.x(), c.x()) + 3.0;
             x < std::max(a.x(), c.x()); x += step)
            p.drawLine(QPointF(x, a.y()),
                       QPointF(x - len * 0.6, a.y() + len));
    }

    // ── Filled shapes ──────────────────────────────────────────────────────
    for (const DiagramPoly &poly : model.polys) {
        if (poly.pts.size() < 2) continue;

        QPolygonF px;
        px.reserve(poly.pts.size());
        for (const QPointF &m : poly.pts) px << toPx(m);

        QColor fill   = diagramFillColor(poly.role, palette);
        QColor stroke = diagramStrokeColor(poly.role, palette);

        p.setBrush(fill);
        QPen pen(stroke);
        pen.setWidthF(poly.role == DiagramRole::Conduit ? 1.6 : 1.2);
        if (poly.unknown) {
            pen.setStyle(Qt::DashLine);
            p.setBrush(QBrush(fill, Qt::BDiagPattern));
        }

        if (poly.openTop) {
            // An open channel: fill the polygon, then stroke every edge except
            // the synthetic one across the top, so it doesn't read as a closed
            // conduit. outline() emits the right side bottom→top followed by
            // the left side top→bottom, so the top seam sits at the midpoint
            // and the invert seam sits between the last and first points.
            // Rotating the ring to start at the midpoint puts BOTH halves of
            // the top seam at the polyline's ends and leaves the invert — real
            // geometry on RECT_OPEN / TRAPEZOIDAL — properly stroked.
            p.setPen(Qt::NoPen);
            p.drawPolygon(px);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            const int n   = static_cast<int>(px.size());
            const int mid = n / 2;
            QPolygonF openRing;
            openRing.reserve(n + 1);
            for (int i = mid; i < n; ++i) openRing << px.at(i);   // left, top→bottom
            for (int i = 0; i < mid; ++i)  openRing << px.at(i);   // right, bottom→top
            p.drawPolyline(openRing);
        } else {
            p.setPen(pen);
            p.drawPolygon(px);
        }

        // Material pattern over the flat fill. Unknown layers keep their dashed
        // outline + diagonal brush instead — the point there is "no data", not
        // "this material".
        if (!poly.unknown)
            paintTexture(p, px, poly.texture, stroke);

        if (!poly.insetLabel.isEmpty()) {
            // Label on a plate so it stays readable over a texture.
            const QRectF bb = px.boundingRect();
            const QString txt = fm.elidedText(poly.insetLabel, Qt::ElideRight,
                                              bb.width() - 8.0);
            const QRectF tr(bb.center().x() - fm.horizontalAdvance(txt) * 0.5 - 4.0,
                            bb.center().y() - fm.height() * 0.5 - 1.0,
                            fm.horizontalAdvance(txt) + 8.0, fm.height() + 2.0);
            if (bb.height() > fm.height() + 4.0) {
                QColor plate = palette.color(QPalette::Base);
                plate.setAlphaF(0.78);
                p.setPen(Qt::NoPen);
                p.setBrush(plate);
                p.drawRoundedRect(tr, 3.0, 3.0);
                p.setPen(inkColor);
                p.drawText(tr, Qt::AlignCenter, txt);
            }
        }
    }

    // ── Vegetation ─────────────────────────────────────────────────────────
    {
        const QColor green = diagramStrokeColor(DiagramRole::Vegetation, palette)
                                 .darker(105);
        for (const DiagramVegetation &v : model.vegetation) {
            if (v.count < 1 || !(v.height > 0.0)) continue;
            const QPointF a = toPx({ v.x0, v.y });
            const QPointF b2 = toPx({ v.x1, v.y });
            // Height is in model units, so plants scale with the drawing —
            // shrubs that stayed a fixed pixel size would look like weeds on a
            // zoomed-in cell and like trees on a zoomed-out one.
            const double hPx = std::abs(toPx({ v.x0, v.y + v.height }).y() - a.y());
            const int n = std::clamp(v.count, 1, 64);
            for (int i = 0; i < n; ++i) {
                const double t = (n == 1) ? 0.5 : (i + 0.5) / static_cast<double>(n);
                QPointF at = a + (b2 - a) * t;
                at.rx() += jitter(i * 31 + n) * 2.0;
                paintPlant(p, at, hPx * (0.82 + 0.18 * std::abs(jitter(i * 17))),
                           v.grass, green, i * 101 + n);
            }
        }
    }

    // ── Circles (underdrain pipes, fittings) ───────────────────────────────
    for (const DiagramCircle &c : model.circles) {
        if (!(c.radius > 0.0)) continue;
        const QPointF ctr = toPx(c.centre);
        // Radius is a model length, but a hairline circle is useless — keep a
        // floor so an underdrain stays visible when the stack is zoomed out.
        const double rx = std::max(std::abs(toPx({ c.centre.x() + c.radius,
                                                   c.centre.y() }).x() - ctr.x()), 3.0);

        p.setBrush(diagramFillColor(c.role, palette));
        QPen pen(diagramStrokeColor(c.role, palette));
        pen.setWidthF(1.3);
        p.setPen(pen);
        p.drawEllipse(ctr, rx, rx);

        if (c.perforated) {
            // Conventional perforation ticks around the crown.
            pen.setWidthF(1.0);
            p.setPen(pen);
            for (int i = 0; i < 8; ++i) {
                const double a = M_PI * (0.15 + i * 0.10);
                const QPointF d(std::cos(a), -std::sin(a));
                p.drawLine(ctr + d * rx, ctr + d * (rx + 3.0));
            }
        }
    }

    // ── Annotated arrows (inflow, infiltration, overflow) ──────────────────
    for (const DiagramArrow &a : model.arrows) {
        const QPointF from = toPx(a.from);
        const QPointF to   = toPx(a.to);
        const double dx = to.x() - from.x(), dy = to.y() - from.y();
        const double len = std::hypot(dx, dy);
        if (len < 2.0) continue;

        const QColor col = diagramStrokeColor(a.role, palette);
        QPen pen(col);
        pen.setWidthF(1.6);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(from, to);
        drawArrowHead(p, to, std::atan2(dy, dx), col);

        if (!a.label.isEmpty() && showLeaders) {
            p.setPen(col);
            const bool rightward = dx >= 0.0;
            const QRectF tr(rightward ? from.x() - 120.0 : from.x() + 4.0,
                            from.y() - fm.height() - 2.0, 116.0, fm.height());
            p.drawText(tr, (rightward ? Qt::AlignRight : Qt::AlignLeft)
                               | Qt::AlignVCenter,
                       fm.elidedText(a.label, Qt::ElideRight, tr.width()));
        }
    }

    // ── Polylines ──────────────────────────────────────────────────────────
    for (const DiagramPolyline &pl : model.polylines) {
        if (pl.pts.size() < 2) continue;
        QPolygonF px;
        px.reserve(pl.pts.size());
        for (const QPointF &m : pl.pts) px << toPx(m);

        QPen pen(diagramStrokeColor(pl.role, palette));
        pen.setWidthF(1.0);
        if (pl.dashed) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(px);

        if (!pl.label.isEmpty()) {
            p.setPen(mutedColor);
            p.drawText(px.last() + QPointF(4.0, -3.0), pl.label);
        }
    }

    // ── Dimension lines ────────────────────────────────────────────────────
    if (showDims) {
        for (const DiagramDim &d : model.dims) {
            const QPointF a = toPx(d.from);
            const QPointF c = toPx(d.to);
            const double  dx = c.x() - a.x(), dy = c.y() - a.y();
            const double  len = std::hypot(dx, dy);
            if (len < 1.0) continue;

            // Unit perpendicular, offset the dimension line off the feature.
            const QPointF n(-dy / len, dx / len);
            const QPointF off = n * d.pixelOffset;
            const QPointF a2 = a + off, c2 = c + off;

            const QColor col = d.accent ? accentColor : dimColor;

            QPen ext(col.lighter(135));
            ext.setWidthF(0.7);
            p.setPen(ext);
            p.setBrush(Qt::NoBrush);
            p.drawLine(a, a2);
            p.drawLine(c, c2);

            QPen line(col);
            line.setWidthF(d.accent ? 1.4 : 1.0);
            p.setPen(line);
            p.drawLine(a2, c2);

            const double ang = std::atan2(c2.y() - a2.y(), c2.x() - a2.x());
            drawArrowHead(p, a2, ang + M_PI, col);
            drawArrowHead(p, c2, ang, col);

            if (d.text.isEmpty()) continue;

            // Text runs along the dimension line, flipped when it would read
            // upside down.
            p.save();
            const QPointF midPt = (a2 + c2) * 0.5;
            double deg = ang * 180.0 / M_PI;
            if (deg > 90.0 || deg < -90.0) deg += 180.0;
            p.translate(midPt);
            p.rotate(deg);
            p.setPen(col);
            const QString txt = fm.elidedText(d.text, Qt::ElideRight, len + 60.0);
            const QRectF tr(-0.5 * (len + 60.0), -fm.height() - 2.0,
                            len + 60.0, fm.height());
            p.drawText(tr, Qt::AlignCenter, txt);
            p.restore();
        }
    }

    // ── Leader lines ───────────────────────────────────────────────────────
    if (showLeaders) {
        QPen lead(dimColor);
        lead.setWidthF(0.8);
        // Labels live in `area` — the band left after the title, subtitle,
        // footer and plan inset were carved out — NOT in `target`. An anchor
        // at the crown or invert sits within a few pixels of that band's edge,
        // so an unclamped offset drops the text straight onto the subtitle or
        // the footer. Clamp the elbow into the band and let the leader line
        // stretch to reach it.
        const double halfLine = fm.height() * 0.5;
        const double bandTop    = area.top()    + halfLine;
        const double bandBottom = area.bottom() - halfLine;
        // One label per line: a node with several links at similar inverts
        // lands their leaders on the same y and writes them over each other.
        // Remember what each side has used and push a colliding label clear.
        QVector<double> usedL, usedR;
        auto deconflict = [&](double y, bool right) {
            QVector<double> &used = right ? usedR : usedL;
            const double step = fm.height() + 2.0;
            bool moved = true;
            for (int guard = 0; moved && guard < 24; ++guard) {
                moved = false;
                for (const double u : used) {
                    if (std::abs(y - u) < step) { y += step; moved = true; break; }
                }
            }
            if (bandBottom > bandTop && y > bandBottom) {
                // Ran out of room below — walk back up instead.
                y = bandBottom;
                for (int guard = 0; guard < 24; ++guard) {
                    bool hit = false;
                    for (const double u : used)
                        if (std::abs(y - u) < step) { y -= step; hit = true; break; }
                    if (!hit) break;
                }
            }
            used.push_back(y);
            return y;
        };
        for (const DiagramLeader &l : model.leaders) {
            if (l.text.isEmpty()) continue;
            const QPointF a   = toPx(l.anchor);
            QPointF       end = a + l.pixelOffset;
            if (bandBottom > bandTop)
                end.setY(std::clamp(end.y(), bandTop, bandBottom));
            end.setY(deconflict(end.y(), l.pixelOffset.x() >= 0.0));
            if (bandBottom > bandTop)
                end.setY(std::clamp(end.y(), bandTop, bandBottom));
            const bool    right = l.pixelOffset.x() >= 0.0;
            const double  landing = 8.0;
            const QPointF land(end.x() + (right ? landing : -landing), end.y());

            p.setPen(lead);
            p.setBrush(Qt::NoBrush);
            p.drawLine(a, end);
            p.drawLine(end, land);
            p.setBrush(dimColor);
            p.setPen(Qt::NoPen);
            p.drawEllipse(a, 1.8, 1.8);

            p.setPen(dimColor);
            const QRectF tr(right ? land.x() + 3.0 : area.left(),
                            land.y() - halfLine,
                            right ? area.right() - land.x() - 3.0
                                  : land.x() - 3.0 - area.left(),
                            fm.height());
            if (tr.width() < 4.0) continue;   // no room on this side; skip
            p.drawText(tr,
                       (right ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                       fm.elidedText(l.text, Qt::ElideRight, tr.width()));
        }
    }

    p.restore();   // end of the zoom/pan clip — chrome below is screen-space

    // ── Plan-view inset ────────────────────────────────────────────────────
    if (!planRect.isNull()) {
        const QPointF ctr = planRect.center();
        const double  r   = planRect.width() * 0.36;

        p.setBrush(mix(palette.color(QPalette::Base),
                       palette.color(QPalette::WindowText), 0.05));
        QPen ring(mutedColor);
        ring.setWidthF(0.8);
        p.setPen(ring);
        p.drawEllipse(ctr, r + 8.0, r + 8.0);

        p.setBrush(diagramFillColor(DiagramRole::Structure, palette));
        p.setPen(QPen(inkColor, 1.0));
        p.drawEllipse(ctr, 6.0, 6.0);

        QPen spoke(inkColor);
        spoke.setWidthF(1.6);
        QVector<QRectF> planLabels;
        for (const PlanSpoke &s : model.plan) {
            const double a = s.angleDeg * M_PI / 180.0;
            const QPointF dir(std::cos(a), -std::sin(a));
            const QPointF from = ctr + dir * 6.0;
            const QPointF to   = ctr + dir * r;
            p.setPen(spoke);
            p.setBrush(Qt::NoBrush);
            p.drawLine(from, to);
            // Inbound flows point at the node, outbound point away from it.
            if (s.inbound)
                drawArrowHead(p, ctr + dir * 12.0,
                              std::atan2(-dir.y(), -dir.x()), inkColor);
            else
                drawArrowHead(p, to, std::atan2(dir.y(), dir.x()), inkColor);

            if (!s.label.isEmpty()) {
                p.setPen(mutedColor);
                const QPointF lp = ctr + dir * (r + 10.0);
                QRectF lr(lp.x() - 30.0, lp.y() - 7.0, 60.0, 14.0);
                // Two links on the same bearing — a straight-through manhole,
                // which is the common case — put their labels at the same
                // point and write one over the other. Step the later one clear.
                for (int guard = 0; guard < 6; ++guard) {
                    bool hit = false;
                    for (const QRectF &u : planLabels)
                        if (u.intersects(lr)) { hit = true; break; }
                    if (!hit) break;
                    lr.moveTop(lr.top() + (dir.y() >= 0.0 ? 15.0 : -15.0));
                }
                planLabels.push_back(lr);
                p.drawText(lr, Qt::AlignCenter,
                           fm.elidedText(s.label, Qt::ElideRight, 58.0));
            }
        }

        p.setPen(mutedColor);
        p.drawText(QRectF(planRect.left(), planRect.bottom() - 2.0,
                          planRect.width(), 14.0),
                   Qt::AlignCenter, QStringLiteral("plan"));
    }

    // ── Footer ─────────────────────────────────────────────────────────────
    if (showFooter) {
        const QRectF fr(target.left() + kMarginX,
                        target.bottom() - kMarginBottom - kFooterH,
                        target.width() - 2 * kMarginX, kFooterH);
        QPen sep(mutedColor);
        sep.setWidthF(0.7);
        p.setPen(sep);
        p.drawLine(QPointF(fr.left(), fr.top()), QPointF(fr.right(), fr.top()));
        p.setPen(mutedColor);
        p.drawText(fr, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(model.footer, Qt::ElideRight, fr.width()));
    }

    p.restore();
}

} // namespace openswmmvis::sectionview
