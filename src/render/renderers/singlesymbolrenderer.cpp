/*!
 * \file   singlesymbolrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/singlesymbolrenderer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

SingleSymbolRenderer::SingleSymbolRenderer(SymbolStyle symbol, QString label)
    : m_symbol(std::move(symbol)),
      m_legendLabel(std::move(label))
{}

void SingleSymbolRenderer::setSymbol(SymbolStyle s)
{
    m_symbol = std::move(s);
}

SymbolStyle SingleSymbolRenderer::symbolFor(const FeatureRef &, const QVariantMap &) const
{
    return m_symbol;
}

QList<LegendSymbolItem> SingleSymbolRenderer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label  = m_legendLabel;
    item.symbol = m_symbol;
    // range stays as the (NaN, NaN) sentinel — single-symbol has no numeric range.
    return { item };
}

QJsonObject SingleSymbolRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());
    obj.insert(QStringLiteral("symbol"), m_symbol.toJson());
    if (!m_legendLabel.isEmpty())
        obj.insert(QStringLiteral("legendLabel"), m_legendLabel);
    return obj;
}

void SingleSymbolRenderer::fromJson(const QJsonObject &j)
{
    m_symbol = SymbolStyle{};
    m_symbol.fromJson(j.value(QStringLiteral("symbol")).toObject());
    m_legendLabel = j.value(QStringLiteral("legendLabel")).toString();
}

std::unique_ptr<IFeatureRenderer> SingleSymbolRenderer::clone() const
{
    return std::make_unique<SingleSymbolRenderer>(*this);
}

} // namespace OpenSWMM::Render
