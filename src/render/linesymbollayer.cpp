/*!
 * \file   linesymbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  LineSymbolLayerSpec + arrow draw helper (Slice Z.5).
 */

#include "render/linesymbollayer.h"

#include "render/symbolstyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QVariantList>

#include <array>
#include <cmath>

namespace OpenSWMM::Render
{

namespace {

struct Mapping {
    ArrowPlacement p;
    const char *token;
};
constexpr std::array<Mapping, 5> kArrowMap = {{
    {ArrowPlacement::End,            "end"},
    {ArrowPlacement::Both,           "both"},
    {ArrowPlacement::Centered,       "centered"},
    {ArrowPlacement::RepeatEveryNPx, "repeatEveryNPx"},
    {ArrowPlacement::AtVertices,     "atVertices"},
}};

QVariantList customDashToVariantList(const QVector<qreal> &dashes)
{
    QVariantList out;
    out.reserve(dashes.size());
    for (qreal d : dashes)
        out.append(d);
    return out;
}

QVector<qreal> customDashFromVariantList(const QVariantList &list)
{
    QVector<qreal> out;
    out.reserve(list.size());
    for (const QVariant &v : list)
        out.append(v.toDouble());
    return out;
}

/*! Paint one filled-triangle arrow head at \p tip pointing along the
 *  unit tangent vector (tx, ty). lengthPx is along-tangent length;
 *  widthPx is perpendicular extent. */
void paintArrowAt(QPainter *p, const QPointF &tip,
                  qreal tx, qreal ty,
                  qreal lengthPx, qreal widthPx,
                  const QColor &color)
{
    // Perpendicular vector (rotate 90° left).
    const qreal nx = -ty;
    const qreal ny =  tx;

    const QPointF tail = tip - QPointF(tx, ty) * lengthPx;
    const QPointF a    = tail + QPointF(nx, ny) * (widthPx * 0.5);
    const QPointF b    = tail - QPointF(nx, ny) * (widthPx * 0.5);

    QPolygonF poly;
    poly << tip << a << b;

    QPen oldPen = p->pen();
    QBrush oldBrush = p->brush();
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawPolygon(poly);
    p->setPen(oldPen);
    p->setBrush(oldBrush);
}

/*! Tangent direction at fractional path position \p t in [0,1] on
 *  \p path. Returns (tx, ty) as a unit vector along the local segment
 *  in the polyline's forward direction. */
void tangentAt(const QPainterPath &path, qreal t, qreal &tx, qreal &ty)
{
    const qreal dt = 0.001;
    const qreal t0 = std::max(0.0, t - dt);
    const qreal t1 = std::min(1.0, t + dt);
    const QPointF p0 = path.pointAtPercent(t0);
    const QPointF p1 = path.pointAtPercent(t1);
    qreal dx = p1.x() - p0.x();
    qreal dy = p1.y() - p0.y();
    const qreal len = std::hypot(dx, dy);
    if (len > 1e-9) { dx /= len; dy /= len; }
    else            { dx = 1.0; dy = 0.0; }
    tx = dx; ty = dy;
}

} // namespace

QString arrowPlacementToString(ArrowPlacement p)
{
    for (const auto &m : kArrowMap)
        if (m.p == p) return QString::fromLatin1(m.token);
    return QStringLiteral("end");
}

ArrowPlacement arrowPlacementFromString(const QString &s)
{
    for (const auto &m : kArrowMap)
        if (s == QLatin1String(m.token)) return m.p;
    return ArrowPlacement::End;
}

QPen LineSymbolLayerSpec::toQPen() const
{
    QPen pen(color);
    pen.setWidthF(width);
    if (penStyle == Qt::CustomDashLine && !customDash.isEmpty()) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(customDash);
    } else {
        pen.setStyle(penStyle);
    }
    pen.setCapStyle(capStyle);
    pen.setJoinStyle(joinStyle);
    pen.setCosmetic(false);
    return pen;
}

SymbolLayer LineSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

LineSymbolLayerSpec LineSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    LineSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    // Gap A1.2 — tolerant read (QColor variant or legacy hex string).
    s.color = SymbolProps::readColor(p, QStringLiteral("color"), s.color);
    if (p.contains(QStringLiteral("width")))
        s.width = p.value(QStringLiteral("width")).toDouble();
    if (p.contains(QStringLiteral("penStyle")))
        s.penStyle = static_cast<Qt::PenStyle>(
            p.value(QStringLiteral("penStyle")).toInt());
    if (p.contains(QStringLiteral("capStyle")))
        s.capStyle = static_cast<Qt::PenCapStyle>(
            p.value(QStringLiteral("capStyle")).toInt());
    if (p.contains(QStringLiteral("joinStyle")))
        s.joinStyle = static_cast<Qt::PenJoinStyle>(
            p.value(QStringLiteral("joinStyle")).toInt());
    if (p.contains(QStringLiteral("customDash")))
        s.customDash = customDashFromVariantList(
            p.value(QStringLiteral("customDash")).toList());
    if (p.contains(QStringLiteral("offsetPx")))
        s.offsetPx = p.value(QStringLiteral("offsetPx")).toDouble();

    if (p.contains(QStringLiteral("drawArrows")))
        s.drawArrows = p.value(QStringLiteral("drawArrows")).toBool();
    s.arrows.color = SymbolProps::readColor(p, QStringLiteral("arrowColor"),
                                            s.arrows.color);
    if (p.contains(QStringLiteral("arrowLengthPx")))
        s.arrows.lengthPx = p.value(QStringLiteral("arrowLengthPx")).toDouble();
    if (p.contains(QStringLiteral("arrowWidthPx")))
        s.arrows.widthPx = p.value(QStringLiteral("arrowWidthPx")).toDouble();
    if (p.contains(QStringLiteral("arrowPlacement")))
        s.arrows.placement = static_cast<ArrowPlacement>(
            p.value(QStringLiteral("arrowPlacement")).toInt());
    if (p.contains(QStringLiteral("arrowSpacingPx")))
        s.arrows.spacingPx = p.value(QStringLiteral("arrowSpacingPx")).toDouble();
    if (p.contains(QStringLiteral("arrowReverse")))
        s.arrows.reverse = p.value(QStringLiteral("arrowReverse")).toBool();

    // Slice SS.3 — SWMM arrow gate + label fields.
    if (p.contains(QStringLiteral("arrowOnlyWhenFlowPos")))
        s.arrowOnlyWhenFlowPos =
            p.value(QStringLiteral("arrowOnlyWhenFlowPos")).toBool();
    if (p.contains(QStringLiteral("showLabel")))
        s.showLabel = p.value(QStringLiteral("showLabel")).toBool();
    if (p.contains(QStringLiteral("labelFont"))) {
        const QFont f = p.value(QStringLiteral("labelFont")).value<QFont>();
        s.labelFont = f;
    }
    s.labelColor = SymbolProps::readColor(p, QStringLiteral("labelColor"),
                                          s.labelColor);

    return s;
}

void LineSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = drawArrows ? SymbolLayerKind::MarkerLine
                            : SymbolLayerKind::SimpleLine;
    layer.props[QStringLiteral("color")]      = color;   // QColor variant (hex reverted — X1 regression)
    layer.props[QStringLiteral("width")]      = width;
    layer.props[QStringLiteral("penStyle")]   = static_cast<int>(penStyle);
    layer.props[QStringLiteral("capStyle")]   = static_cast<int>(capStyle);
    layer.props[QStringLiteral("joinStyle")]  = static_cast<int>(joinStyle);
    layer.props[QStringLiteral("customDash")] = customDashToVariantList(customDash);
    layer.props[QStringLiteral("offsetPx")]   = offsetPx;

    layer.props[QStringLiteral("drawArrows")]     = drawArrows;
    layer.props[QStringLiteral("arrowColor")]     = arrows.color;   // QColor variant (hex reverted)
    layer.props[QStringLiteral("arrowLengthPx")]  = arrows.lengthPx;
    layer.props[QStringLiteral("arrowWidthPx")]   = arrows.widthPx;
    layer.props[QStringLiteral("arrowPlacement")] = static_cast<int>(arrows.placement);
    layer.props[QStringLiteral("arrowSpacingPx")] = arrows.spacingPx;
    layer.props[QStringLiteral("arrowReverse")]   = arrows.reverse;

    // Slice SS.3 — SWMM arrow gate + label fields.
    layer.props[QStringLiteral("arrowOnlyWhenFlowPos")] = arrowOnlyWhenFlowPos;
    layer.props[QStringLiteral("showLabel")]            = showLabel;
    layer.props[QStringLiteral("labelFont")]            = labelFont;
    layer.props[QStringLiteral("labelColor")]           = labelColor;   // QColor variant (hex reverted)
}

// ── Arrow paint ──────────────────────────────────────────────────────

void drawArrowsAlongPolyline(QPainter *painter,
                              const QPolygonF &polyline,
                              const LineArrowSpec &spec)
{
    if (!painter || polyline.size() < 2 || spec.lengthPx <= 0.0)
        return;

    QPainterPath path;
    path.moveTo(polyline.first());
    for (int i = 1; i < polyline.size(); ++i)
        path.lineTo(polyline[i]);

    const qreal totalLen = path.length();
    if (totalLen <= 0.0)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    auto paintAtPercent = [&](qreal t) {
        qreal tx, ty;
        tangentAt(path, t, tx, ty);
        if (spec.reverse) { tx = -tx; ty = -ty; }
        const QPointF tip = path.pointAtPercent(t);
        paintArrowAt(painter, tip, tx, ty,
                     spec.lengthPx, spec.widthPx, spec.color);
    };

    switch (spec.placement)
    {
    case ArrowPlacement::End:
        paintAtPercent(1.0);
        break;

    case ArrowPlacement::Both:
    {
        // Forward arrow at end; reversed arrow at start (pointing outward).
        paintAtPercent(1.0);
        qreal tx, ty;
        tangentAt(path, 0.0, tx, ty);
        // Flip for the start arrow so it points outward (back along start).
        if (!spec.reverse) { tx = -tx; ty = -ty; }
        const QPointF tip = path.pointAtPercent(0.0);
        paintArrowAt(painter, tip, tx, ty,
                     spec.lengthPx, spec.widthPx, spec.color);
        break;
    }

    case ArrowPlacement::Centered:
        paintAtPercent(0.5);
        break;

    case ArrowPlacement::RepeatEveryNPx:
    {
        const qreal spacing = qMax(spec.spacingPx, 1.0);
        // Place arrows at arc-length offsets spacing, 2*spacing, … up to
        // total length. Starting offset = spacing so we don't double up
        // on the start vertex.
        for (qreal d = spacing; d < totalLen; d += spacing) {
            const qreal t = d / totalLen;
            paintAtPercent(t);
        }
        break;
    }

    case ArrowPlacement::AtVertices:
        // One arrow at each interior vertex. Tangent = direction of the
        // next segment (so the arrow points toward the next vertex).
        // Skip the very last vertex (no "next segment").
        for (int i = 1; i < polyline.size() - 1; ++i) {
            qreal dx = polyline[i + 1].x() - polyline[i].x();
            qreal dy = polyline[i + 1].y() - polyline[i].y();
            const qreal len = std::hypot(dx, dy);
            if (len <= 1e-9) continue;
            dx /= len; dy /= len;
            if (spec.reverse) { dx = -dx; dy = -dy; }
            paintArrowAt(painter, polyline[i], dx, dy,
                         spec.lengthPx, spec.widthPx, spec.color);
        }
        break;
    }

    painter->restore();
}

} // namespace OpenSWMM::Render
