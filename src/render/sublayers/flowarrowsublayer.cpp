/*!
 * \file   flowarrowsublayer.cpp
 * \brief  Direction-only flow-arrow sublayer — style bag + sublayer plumbing.
 *
 *         The QSG geometry for this sublayer is built inside
 *         SWMM2DResultsQSGRenderer (same pattern as ContourBand/Isoline),
 *         which has direct access to the cell-centered velocity field and
 *         the dry-depth mask. buildOrUpdateNode() therefore returns the
 *         existing node unchanged.
 */
#include "render/sublayers/flowarrowsublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

void FlowArrowStyle::setArrowLengthPx(double v)
{
    v = qBound(2.0, v, 200.0);
    if (qFuzzyCompare(m_arrowLengthPx + 1.0, v + 1.0)) return;
    m_arrowLengthPx = v;
    setDirty();
}

void FlowArrowStyle::setArrowSpacingPx(double v)
{
    v = qBound(4.0, v, 1000.0);
    if (qFuzzyCompare(m_arrowSpacingPx + 1.0, v + 1.0)) return;
    m_arrowSpacingPx = v;
    setDirty();
}

void FlowArrowStyle::setHeadSizePx(double v)
{
    v = qBound(1.0, v, 64.0);
    if (qFuzzyCompare(m_headSizePx + 1.0, v + 1.0)) return;
    m_headSizePx = v;
    setDirty();
}

void FlowArrowStyle::setShaftWidthPx(double v)
{
    v = qBound(0.5, v, 20.0);
    if (qFuzzyCompare(m_shaftWidthPx + 1.0, v + 1.0)) return;
    m_shaftWidthPx = v;
    setDirty();
}

void FlowArrowStyle::setDryDepthCutoff(double v)
{
    v = qBound(0.0, v, 100.0);
    if (qFuzzyCompare(m_dryDepthCutoff + 1.0, v + 1.0)) return;
    m_dryDepthCutoff = v;
    setDirty();
}

QColor FlowArrowStyle::colorForSpeed(double speedMps) const
{
    if (!m_colorByMagnitude)
        return m_color;
    RasterColorRamp ramp = RasterColorRamp::builtin(m_colorRampName);
    ramp.minValue = m_speedMinMps;
    ramp.maxValue = (m_speedMaxMps > m_speedMinMps) ? m_speedMaxMps
                                                    : m_speedMinMps + 1.0;
    return ramp.colorForValue(speedMps);
}

QJsonObject FlowArrowStyle::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("arrowLengthPx")]      = m_arrowLengthPx;
    j[QStringLiteral("arrowSpacingPx")]     = m_arrowSpacingPx;
    j[QStringLiteral("headSizePx")]         = m_headSizePx;
    j[QStringLiteral("shaftWidthPx")]       = m_shaftWidthPx;
    j[QStringLiteral("color")]              = m_color.name(QColor::HexArgb);
    j[QStringLiteral("outlineColor")]       = m_outlineColor.name(QColor::HexArgb);
    j[QStringLiteral("dryDepthCutoff")]     = m_dryDepthCutoff;
    j[QStringLiteral("placeAtCellCenters")] = m_placeAtCellCenters;
    j[QStringLiteral("colorByMagnitude")]   = m_colorByMagnitude;
    j[QStringLiteral("colorRampName")]      = m_colorRampName;
    j[QStringLiteral("speedMinMps")]        = m_speedMinMps;
    j[QStringLiteral("speedMaxMps")]        = m_speedMaxMps;
    return j;
}

void FlowArrowStyle::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("arrowLengthPx")))
        m_arrowLengthPx  = qBound(2.0, j.value(QStringLiteral("arrowLengthPx")).toDouble(m_arrowLengthPx), 200.0);
    if (j.contains(QStringLiteral("arrowSpacingPx")))
        m_arrowSpacingPx = qBound(4.0, j.value(QStringLiteral("arrowSpacingPx")).toDouble(m_arrowSpacingPx), 1000.0);
    if (j.contains(QStringLiteral("headSizePx")))
        m_headSizePx     = qBound(1.0, j.value(QStringLiteral("headSizePx")).toDouble(m_headSizePx), 64.0);
    if (j.contains(QStringLiteral("shaftWidthPx")))
        m_shaftWidthPx   = qBound(0.5, j.value(QStringLiteral("shaftWidthPx")).toDouble(m_shaftWidthPx), 20.0);
    if (j.contains(QStringLiteral("color"))) {
        const QColor c(j.value(QStringLiteral("color")).toString());
        if (c.isValid()) m_color = c;
    }
    if (j.contains(QStringLiteral("outlineColor"))) {
        const QColor c(j.value(QStringLiteral("outlineColor")).toString());
        if (c.isValid()) m_outlineColor = c;
    }
    if (j.contains(QStringLiteral("dryDepthCutoff")))
        m_dryDepthCutoff = qBound(0.0, j.value(QStringLiteral("dryDepthCutoff")).toDouble(m_dryDepthCutoff), 100.0);
    if (j.contains(QStringLiteral("placeAtCellCenters")))
        m_placeAtCellCenters = j.value(QStringLiteral("placeAtCellCenters")).toBool(m_placeAtCellCenters);
    m_colorByMagnitude = j.value(QStringLiteral("colorByMagnitude")).toBool(m_colorByMagnitude);
    m_colorRampName    = j.value(QStringLiteral("colorRampName")).toString(m_colorRampName);
    m_speedMinMps      = j.value(QStringLiteral("speedMinMps")).toDouble(m_speedMinMps);
    m_speedMaxMps      = j.value(QStringLiteral("speedMaxMps")).toDouble(m_speedMaxMps);
    setDirty();
}

FlowArrowSublayer::FlowArrowSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new FlowArrowStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void FlowArrowSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void FlowArrowSublayer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> FlowArrowSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label      = tr("Flow direction");
    item.sublayerId = m_id;

    SymbolLayer arrow;
    arrow.kind = SymbolLayerKind::MarkerLine;
    SymbolProps::writeColor(arrow.props, QStringLiteral("color"), m_style->color());
    arrow.props.insert(QStringLiteral("sizePx"), m_style->arrowLengthPx());
    item.symbol.layers.append(arrow);
    item.symbol.opacity = m_opacity;
    return { item };
}

QSGNode *FlowArrowSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing;
}

} // namespace OpenSWMM::Render
