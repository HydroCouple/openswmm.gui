/*!
 * \file   featuresublayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/sublayers/feature/featuresublayer.h"
#include "render/symbollayer.h"

namespace OpenSWMM::Render
{

FeatureSublayer::Archetype FeatureSublayer::archetypeFor(OpenSWMMVis::SwmmCategory c)
{
    switch (c) {
        case OpenSWMMVis::CatJunctions:
        case OpenSWMMVis::CatOutfalls:
        case OpenSWMMVis::CatStorage:
        case OpenSWMMVis::CatDividers:
        case OpenSWMMVis::CatRainGages:
            return Archetype::Point;
        case OpenSWMMVis::CatConduits:
        case OpenSWMMVis::CatPumps:
        case OpenSWMMVis::CatOrifices:
        case OpenSWMMVis::CatWeirs:
        case OpenSWMMVis::CatOutlets:
            return Archetype::Line;
        case OpenSWMMVis::CatSubcatchments:
            return Archetype::Polygon;
        default:
            return Archetype::Point;
    }
}

FeatureSublayer::FeatureSublayer(OpenSWMMVis::SwmmCategory category,
                                  QString id_,
                                  QString displayName_,
                                  QObject *parent)
    : ISublayer(parent)
    , m_category(category)
    , m_archetype(archetypeFor(category))
    , m_id(std::move(id_))
    , m_displayName(std::move(displayName_))
{
    switch (m_archetype) {
        case Archetype::Point:
            m_style = new PointFeatureSublayerStyle(this);
            break;
        case Archetype::Line: {
            auto *ls = new LineFeatureSublayerStyle(this);
            // Conduits default to polyline rendering; point-like link kinds
            // (Pumps/Orifices/Weirs/Outlets) default to midpoint-glyph mode
            // so the dialog tab shows a sensible starting point.
            if (m_category != OpenSWMMVis::CatConduits)
                ls->setRenderAsLine(false);
            m_style = ls;
            break;
        }
        case Archetype::Polygon:
            m_style = new PolygonFeatureSublayerStyle(this);
            break;
    }

    // RainGages have no result attribute; force single-symbol mode and
    // clear the attribute name so the dialog shows the static-symbol path.
    if (m_category == OpenSWMMVis::CatRainGages && m_style) {
        m_style->setAttribute(QString());
        m_style->setUseColorRamp(false);
    }

    // Forward style edits to the sublayer's invalidated() signal so hosts
    // schedule a repaint.
    connect(m_style, &SublayerStyle::styleChanged,
            this,    &ISublayer::invalidated,
            Qt::UniqueConnection);
}

PointFeatureSublayerStyle *FeatureSublayer::pointStyle() const
{
    return qobject_cast<PointFeatureSublayerStyle *>(m_style);
}
LineFeatureSublayerStyle *FeatureSublayer::lineStyle() const
{
    return qobject_cast<LineFeatureSublayerStyle *>(m_style);
}
PolygonFeatureSublayerStyle *FeatureSublayer::polygonStyle() const
{
    return qobject_cast<PolygonFeatureSublayerStyle *>(m_style);
}

void FeatureSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void FeatureSublayer::setOpacity(qreal o)
{
    if (qFuzzyCompare(m_opacity, o)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> FeatureSublayer::legendSymbolItems() const
{
    // Single-row preview. SWMMResultsLayer::sublayerLegendItems() emits
    // graduated swatches for the visible, attribute-bound case; this
    // single-row fallback covers the static-symbol case (raingages or
    // useColorRamp=false sublayers).
    LegendSymbolItem item;
    item.label      = m_displayName;
    item.sublayerId = m_id;

    SymbolLayer sl;
    switch (m_archetype) {
        case Archetype::Point:   sl.kind = SymbolLayerKind::SimpleMarker; break;
        case Archetype::Line:    sl.kind = SymbolLayerKind::SimpleLine;   break;
        case Archetype::Polygon: sl.kind = SymbolLayerKind::SimpleFill;   break;
    }
    sl.props.insert(QStringLiteral("color"),
                    m_style ? m_style->color().name(QColor::HexArgb)
                            : QStringLiteral("#606060"));
    if (auto *ps = pointStyle())
        sl.props.insert(QStringLiteral("size"), ps->markerSizePx());
    else if (auto *ls = lineStyle())
        sl.props.insert(QStringLiteral("width"), ls->lineWidthPx());
    item.symbol.layers.append(sl);
    return { item };
}

QSGNode *FeatureSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &)
{
    // QGraphicsScene-driven paint path still owns rendering. The QSG
    // adoption slice will replace SWMMResultsLayer::populateScene with
    // a per-sublayer geometry builder routed through here.
    return existing;
}

} // namespace OpenSWMM::Render
