/*!
 * \file   couplednodesublayer.cpp
 * \brief  SWMM-coupled vertex marker sublayer — style bag + plumbing.
 */
#include "render/sublayers/couplednodesublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

void CoupledNodeStyle::setMarkerSizePx(double v)
{
    v = qBound(0.5, v, 40.0);
    if (qFuzzyCompare(m_markerSizePx + 1.0, v + 1.0)) return;
    m_markerSizePx = v;
    setDirty();
}

QJsonObject CoupledNodeStyle::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("color")]        = m_color.name(QColor::HexArgb);
    j[QStringLiteral("markerSizePx")] = m_markerSizePx;
    return j;
}

void CoupledNodeStyle::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("color"))) {
        const QColor c(j.value(QStringLiteral("color")).toString());
        if (c.isValid()) m_color = c;
    }
    if (j.contains(QStringLiteral("markerSizePx")))
        m_markerSizePx = qBound(
            0.5, j.value(QStringLiteral("markerSizePx")).toDouble(m_markerSizePx), 40.0);
    setDirty();
}

CoupledNodeSublayer::CoupledNodeSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new CoupledNodeStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void CoupledNodeSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void CoupledNodeSublayer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> CoupledNodeSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label      = tr("SWMM-coupled vertices");
    item.sublayerId = m_id;

    SymbolLayer marker;
    marker.kind = SymbolLayerKind::SimpleMarker;
    SymbolProps::writeColor(marker.props, QStringLiteral("color"), m_style->color());
    marker.props.insert(QStringLiteral("sizePx"), m_style->markerSizePx());
    item.symbol.layers.append(marker);
    item.symbol.opacity = m_opacity;
    return { item };
}

QSGNode *CoupledNodeSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing; // QSG geometry is built in SWMM2DMeshQSGRenderer (style is consumed there).
}

} // namespace OpenSWMM::Render
