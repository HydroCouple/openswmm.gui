/*!
 * \file   meshnodesublayer.cpp
 * \brief  Mesh-vertex marker sublayer — style bag + sublayer plumbing.
 */
#include "render/sublayers/meshnodesublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

void MeshNodeStyle::setMarkerSizePx(double v)
{
    v = qBound(0.5, v, 50.0);
    if (qFuzzyCompare(m_markerSizePx + 1.0, v + 1.0)) return;
    m_markerSizePx = v;
    setDirty();
}

void MeshNodeStyle::setOutlineWidthPx(double v)
{
    v = qBound(0.0, v, 10.0);
    if (qFuzzyCompare(m_outlineWidthPx + 1.0, v + 1.0)) return;
    m_outlineWidthPx = v;
    setDirty();
}

void MeshNodeStyle::setTaggedSizePx(double v)
{
    v = qBound(0.5, v, 50.0);
    if (qFuzzyCompare(m_taggedSizePx + 1.0, v + 1.0)) return;
    m_taggedSizePx = v;
    setDirty();
}

QJsonObject MeshNodeStyle::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("color")]           = m_color.name(QColor::HexArgb);
    j[QStringLiteral("markerSizePx")]    = m_markerSizePx;
    j[QStringLiteral("shape")]           = int(m_shape);
    j[QStringLiteral("outlineColor")]    = m_outlineColor.name(QColor::HexArgb);
    j[QStringLiteral("outlineWidthPx")]  = m_outlineWidthPx;
    j[QStringLiteral("highlightTagged")] = m_highlightTagged;
    j[QStringLiteral("taggedColor")]     = m_taggedColor.name(QColor::HexArgb);
    j[QStringLiteral("taggedSizePx")]    = m_taggedSizePx;
    return j;
}

void MeshNodeStyle::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("color"))) {
        const QColor c(j.value(QStringLiteral("color")).toString());
        if (c.isValid()) m_color = c;
    }
    if (j.contains(QStringLiteral("markerSizePx")))
        m_markerSizePx = qBound(0.5, j.value(QStringLiteral("markerSizePx")).toDouble(m_markerSizePx), 50.0);
    if (j.contains(QStringLiteral("shape")))
        m_shape = static_cast<MarkerShape>(j.value(QStringLiteral("shape")).toInt(int(Circle)));
    if (j.contains(QStringLiteral("outlineColor"))) {
        const QColor c(j.value(QStringLiteral("outlineColor")).toString());
        if (c.isValid()) m_outlineColor = c;
    }
    if (j.contains(QStringLiteral("outlineWidthPx")))
        m_outlineWidthPx = qBound(0.0, j.value(QStringLiteral("outlineWidthPx")).toDouble(m_outlineWidthPx), 10.0);
    if (j.contains(QStringLiteral("highlightTagged")))
        m_highlightTagged = j.value(QStringLiteral("highlightTagged")).toBool(m_highlightTagged);
    if (j.contains(QStringLiteral("taggedColor"))) {
        const QColor c(j.value(QStringLiteral("taggedColor")).toString());
        if (c.isValid()) m_taggedColor = c;
    }
    if (j.contains(QStringLiteral("taggedSizePx")))
        m_taggedSizePx = qBound(0.5, j.value(QStringLiteral("taggedSizePx")).toDouble(m_taggedSizePx), 50.0);
    setDirty();
}

MeshNodeSublayer::MeshNodeSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new MeshNodeStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void MeshNodeSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void MeshNodeSublayer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> MeshNodeSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label      = tr("Mesh vertices");
    item.sublayerId = m_id;

    SymbolLayer marker;
    marker.kind = SymbolLayerKind::SimpleMarker;
    SymbolProps::writeColor(marker.props, QStringLiteral("color"), m_style->color());
    marker.props.insert(QStringLiteral("sizePx"),  m_style->markerSizePx());
    marker.props.insert(QStringLiteral("shape"),   int(m_style->shape()));
    item.symbol.layers.append(marker);
    item.symbol.opacity = m_opacity;
    return { item };
}

QSGNode *MeshNodeSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing; // QSG geometry is built in SWMM2DMeshQSGRenderer (style is consumed there).
}

} // namespace OpenSWMM::Render
