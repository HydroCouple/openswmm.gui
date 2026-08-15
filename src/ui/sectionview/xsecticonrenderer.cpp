/*!
 * \file   xsecticonrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/xsecticonrenderer.h"

#include "ui/sectionview/sectiondiagram.h"
#include "ui/sectionview/xsectsampler.h"

#include <openswmm/engine/openswmm_links.h>

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QPolygonF>
#include <QVector>

#include <algorithm>

namespace openswmmvis::sectionview {

namespace {

//! Icons are drawn at unit depth; widths below are relative to that.
constexpr double kUnitDepth = 1.0;

QIcon placeholderIcon(const QSize &size, const QPalette &palette)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);

    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(4, 4, size.width() - 8.0, size.height() - 8.0),
                            5.0, 5.0);
        p.fillPath(path, diagramFillColor(DiagramRole::Muted, palette));
        QPen pen(palette.color(QPalette::Mid));
        pen.setWidthF(1.2);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawPath(path);
    }   // finish painting before the pixmap is copied into the icon
    return QIcon(pm);
}

/*!
 * Canned geometry for the tabulated shapes, so their palette tiles depict a
 * real channel / street / shape-curve instead of a generic placeholder.
 *
 * A tile answers "what kind of section is this?", not "what are this link's
 * dimensions?" — so a representative table is exactly as truthful here as the
 * nominal geoms used for the parametric shapes, and strictly better than the
 * dashed box these shapes fell back to.
 */
XsectSampler tabulatedIconSampler(int shape)
{
    switch (shape) {
    case SWMM_XSECT_IRREGULAR: {
        // A textbook compound channel: overbanks either side of a deeper
        // main channel, with the roughness triple that shape implies.
        const QVector<double> stations   { 0.0, 2.0, 4.0, 5.0,  7.0,  8.0, 10.0, 12.0 };
        const QVector<double> elevations { 1.0, 0.7, 0.55, 0.0, 0.0, 0.55, 0.7,  1.0 };
        return XsectSampler::fromTransect(stations, elevations,
                                          /*xLeftBank=*/4.0, /*xRightBank=*/8.0,
                                          /*nLeft=*/0.05, /*nChannel=*/0.03,
                                          /*nRight=*/0.05, /*lengthFactor=*/1.0,
                                          /*si=*/true);
    }
    case SWMM_XSECT_STREET:
        // A residential street, both sides: 6 m crown to curb, 150 mm curb,
        // 2 % cross slope, 0.5 m depressed gutter. Note the engine reports a
        // street as a CLOSED section (its isOpen() whitelist excludes
        // STREET_XSECT), so this tile is stroked all the way round.
        return XsectSampler::fromStreet(/*width=*/6.0, /*curbHeight=*/0.15,
                                        /*slope=*/2.0, /*roughness=*/0.016,
                                        /*gutterDepression=*/0.05,
                                        /*gutterWidth=*/0.5, /*sides=*/2,
                                        /*backWidth=*/0.0, /*backSlope=*/0.0,
                                        /*backRoughness=*/0.016, /*si=*/true);
    case SWMM_XSECT_CUSTOM: {
        // A normalized shape curve — widest at mid-depth, narrowing towards
        // both invert and crown, which is what a custom section usually
        // looks like.
        const QVector<double> depths { 0.0, 0.25, 0.50, 0.75, 1.0 };
        const QVector<double> widths { 0.35, 0.85, 1.0, 0.85, 0.30 };
        return XsectSampler::fromCurve(/*yFull=*/1.0, depths, widths, /*si=*/true);
    }
    default:
        // SWMM_XSECT_DUMMY genuinely has no geometry — placeholder is correct.
        return {};
    }
}

} // namespace

void nominalGeomsFor(int shape, double &g1, double &g2, double &g3, double &g4)
{
    // Depth-like geom1 for every shape; the rest are proportions chosen to
    // look like the textbook drawing of that section.
    g1 = kUnitDepth;
    g2 = g3 = g4 = 0.0;

    switch (shape) {
    case SWMM_XSECT_CIRCULAR:
    case SWMM_XSECT_FORCE_MAIN:
    case SWMM_XSECT_EGGSHAPED:
    case SWMM_XSECT_HORSESHOE:
    case SWMM_XSECT_GOTHIC:
    case SWMM_XSECT_CATENARY:
    case SWMM_XSECT_SEMIELLIPTICAL:
    case SWMM_XSECT_BASKETHANDLE:
    case SWMM_XSECT_SEMICIRCULAR:
        // geom1 alone defines these. FORCE_MAIN's geom2 is a roughness
        // coefficient, not a dimension — a Hazen-Williams C of 100 keeps the
        // engine happy without affecting the drawn geometry.
        if (shape == SWMM_XSECT_FORCE_MAIN) g2 = 100.0;
        break;

    case SWMM_XSECT_FILLED_CIRCULAR:
        g2 = 0.25 * kUnitDepth;                  // sediment depth
        break;

    case SWMM_XSECT_RECT_CLOSED:
    case SWMM_XSECT_RECT_OPEN:
        g2 = 0.75 * kUnitDepth;                  // width
        break;

    case SWMM_XSECT_TRAPEZOIDAL:
        g2 = 0.45 * kUnitDepth;                  // bottom width
        g3 = 1.0;                                // left slope (run:rise)
        g4 = 1.0;                                // right slope
        break;

    case SWMM_XSECT_TRIANGULAR:
    case SWMM_XSECT_PARABOLIC:
        g2 = 1.0 * kUnitDepth;                   // top width
        break;

    case SWMM_XSECT_POWER:
        g2 = 1.0 * kUnitDepth;                   // top width
        g3 = 2.0;                                // exponent
        break;

    case SWMM_XSECT_RECT_TRIANG:
        g2 = 0.8 * kUnitDepth;                   // top width
        g3 = 0.3 * kUnitDepth;                   // triangle height
        break;

    case SWMM_XSECT_RECT_ROUND:
        g2 = 0.8 * kUnitDepth;                   // top width
        g3 = 0.4 * kUnitDepth;                   // bottom radius
        break;

    case SWMM_XSECT_MOD_BASKET:
        g2 = 0.7 * kUnitDepth;                   // bottom width
        g3 = 0.35 * kUnitDepth;                  // top radius
        break;

    case SWMM_XSECT_HORIZ_ELLIPSE:
        g2 = 1.5 * kUnitDepth;                   // width > height
        break;

    case SWMM_XSECT_VERT_ELLIPSE:
        g2 = 0.65 * kUnitDepth;                  // width < height
        break;

    case SWMM_XSECT_ARCH:
        g2 = 1.35 * kUnitDepth;                  // span
        break;

    default:
        // IRREGULAR / CUSTOM / STREET are tabulated (no standalone geometry)
        // and DUMMY has none at all — all fall through to the placeholder.
        break;
    }
}

QIcon xsectShapeIcon(int shape, const QSize &size, const QPalette &palette)
{
    const QSize sz = size.isValid() && !size.isEmpty() ? size : QSize(112, 84);

    const QString key = QStringLiteral("swmmvis.xsecticon.%1.%2x%3.%4.%5.%6")
                            .arg(shape)
                            .arg(sz.width())
                            .arg(sz.height())
                            .arg(palette.color(QPalette::WindowText).rgba())
                            .arg(palette.color(QPalette::Base).rgba())
                            .arg(palette.color(QPalette::Highlight).rgba());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) return QIcon(cached);

    // Tabulated shapes have no standalone geometry, so they get a canned but
    // representative table; everything else is built from its nominal geoms.
    // Icons are unit-scale drawings, so the unit system is immaterial; SI
    // keeps the numbers reading as metres.
    XsectSampler sampler = tabulatedIconSampler(shape);
    if (!sampler.isValid()) {
        double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0;
        nominalGeomsFor(shape, g1, g2, g3, g4);
        sampler = XsectSampler::fromShape(shape, g1, g2, g3, g4, /*si=*/true);
    }
    const QPolygonF outline = sampler.isValid() ? sampler.outline(128)
                                                : QPolygonF();
    if (outline.size() < 3)
        return placeholderIcon(sz, palette);

    constexpr qreal kDpr = 2.0;   // crisp on HiDPI, downscaled cleanly on 1x
    QPixmap pm(sz * kDpr);
    pm.setDevicePixelRatio(kDpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Fit the outline (model y up) into the pixmap with a small margin.
        // Work in logical pixels; the painter honours the pixmap's DPR.
        const QRectF b = outline.boundingRect();
        const QRectF fit = QRectF(QPointF(0, 0), QSizeF(sz)).adjusted(5, 5, -5, -5);
        if (b.width() <= 0.0 || b.height() <= 0.0)
            return placeholderIcon(sz, palette);

        const double s = std::min(fit.width() / b.width(),
                                  fit.height() / b.height());
        const double cx = fit.center().x() - s * b.center().x();
        const double cy = fit.center().y() + s * b.center().y();

        QPolygonF px;
        px.reserve(outline.size());
        for (const QPointF &m : outline)
            px << QPointF(cx + s * m.x(), cy - s * m.y());

        const bool open = sampler.fullProps().open;
        p.setBrush(diagramFillColor(DiagramRole::Conduit, palette));
        QPen pen(diagramStrokeColor(DiagramRole::Conduit, palette));
        pen.setWidthF(1.4);

        if (open) {
            p.setPen(Qt::NoPen);
            p.drawPolygon(px);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            const int n   = static_cast<int>(px.size());
            const int mid = n / 2;
            p.drawPolyline(px.constData(), mid);
            p.drawPolyline(px.constData() + mid, n - mid);
        } else {
            p.setPen(pen);
            p.drawPolygon(px);
        }
    }

    QPixmapCache::insert(key, pm);
    return QIcon(pm);
}

} // namespace openswmmvis::sectionview
