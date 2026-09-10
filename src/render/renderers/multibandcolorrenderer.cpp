/*!
 * \file   multibandcolorrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/multibandcolorrenderer.h"

#include "render/symbollayer.h"

#include <QJsonObject>

#include <algorithm>

namespace OpenSWMM::Render
{

MultiBandColorRenderer::MultiBandColorRenderer(int red, int green, int blue, int alpha)
{
    setBands(red, green, blue, alpha);
}

void MultiBandColorRenderer::setBands(int red, int green, int blue, int alpha)
{
    m_red   = std::max(1, red);
    m_green = std::max(1, green);
    m_blue  = std::max(1, blue);
    m_alpha = std::max(0, alpha);
}

QColor MultiBandColorRenderer::colorForValue(double /*value*/, bool /*isNoData*/) const
{
    // The RGB composite is assembled from the warped byte bands directly;
    // there is no scalar → colour mapping to answer.
    return QColor(Qt::transparent);
}

QList<LegendSymbolItem> MultiBandColorRenderer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label = QStringLiteral("RGB (bands %1, %2, %3)")
                     .arg(m_red).arg(m_green).arg(m_blue);
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(sl.props, QStringLiteral("color"), QColor(128, 128, 128));
    item.symbol.layers.append(sl);
    return { item };
}

QJsonObject MultiBandColorRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"),        rendererId());
    obj.insert(QStringLiteral("redBand"),   m_red);
    obj.insert(QStringLiteral("greenBand"), m_green);
    obj.insert(QStringLiteral("blueBand"),  m_blue);
    obj.insert(QStringLiteral("alphaBand"), m_alpha);
    return obj;
}

void MultiBandColorRenderer::fromJson(const QJsonObject &j)
{
    setBands(j.value(QStringLiteral("redBand")).toInt(1),
             j.value(QStringLiteral("greenBand")).toInt(2),
             j.value(QStringLiteral("blueBand")).toInt(3),
             j.value(QStringLiteral("alphaBand")).toInt(0));
}

std::unique_ptr<IRasterRenderer> MultiBandColorRenderer::clone() const
{
    return std::make_unique<MultiBandColorRenderer>(*this);
}

} // namespace OpenSWMM::Render
