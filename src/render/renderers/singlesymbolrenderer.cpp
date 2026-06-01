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

SymbolStyle SingleSymbolRenderer::symbolFor(const FeatureRef &, const QVariantMap &attrs) const
{
    if (!m_sizeData.isValid())
        return m_symbol;

    // Slice BI Phase 8.13.43-α — evaluate the data-defined override and
    // write the resolved scalar into the symbol's size/width prop. We
    // don't know up-front whether the symbol layer is a marker (size) or
    // a line (width), so we set both keys whose presence the painter
    // already honours — the irrelevant one is benign.
    SymbolStyle out = m_symbol;
    const QVariant raw = attrs.value(m_sizeData.attribute);
    bool ok = false;
    const double v = raw.toDouble(&ok);
    const double resolved = ok ? m_sizeData.evaluate(v) : m_sizeData.outMin;
    for (SymbolLayer &sl : out.layers) {
        if (sl.props.contains(QStringLiteral("size")))
            sl.props.insert(QStringLiteral("size"), resolved);
        if (sl.props.contains(QStringLiteral("width")))
            sl.props.insert(QStringLiteral("width"), resolved);
    }
    return out;
}

QList<LegendSymbolItem> SingleSymbolRenderer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label    = m_legendLabel;
    item.symbol   = m_symbol;
    item.classKey = QStringLiteral("single");   // matches setColorForClass key
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
    if (m_sizeData.isValid())
        obj.insert(QStringLiteral("sizeData"), m_sizeData.toJson());
    return obj;
}

void SingleSymbolRenderer::fromJson(const QJsonObject &j)
{
    m_symbol = SymbolStyle{};
    m_symbol.fromJson(j.value(QStringLiteral("symbol")).toObject());
    m_legendLabel = j.value(QStringLiteral("legendLabel")).toString();
    m_sizeData = j.contains(QStringLiteral("sizeData"))
        ? DataDefinedScalar::fromJson(j.value(QStringLiteral("sizeData")).toObject())
        : DataDefinedScalar{};
}

std::unique_ptr<IFeatureRenderer> SingleSymbolRenderer::clone() const
{
    return std::make_unique<SingleSymbolRenderer>(*this);
}

// ── Per-class editing (Slice BB Phase 8.6.16) ─────────────────────────
// SingleSymbolRenderer accepts only the "single" classKey (the renderer
// has exactly one legend row). Mismatched keys silently no-op so callers
// can blindly hand through whatever LegendSymbolItem.sortIndex they have.

namespace {
constexpr auto kSingleClassKey = QLatin1String("single");

void writePropOnAllLayers(SymbolStyle &style, QLatin1String key, const QVariant &v)
{
    // Only touch layers that already advertise the slot; preserves
    // marker-vs-line per-layer semantics (size on markers, width on lines).
    for (SymbolLayer &sl : style.layers) {
        if (sl.props.contains(key))
            sl.props.insert(key, v);
    }
}
} // namespace

QColor SingleSymbolRenderer::colorForClass(const QString &classKey) const
{
    if (classKey != kSingleClassKey) return {};
    // Return the first matching layer's stored colour; matches what the
    // legend swatch displays (firstSymbolColor convention).
    for (const SymbolLayer &sl : m_symbol.layers) {
        const auto it = sl.props.constFind(QStringLiteral("color"));
        if (it != sl.props.constEnd()) {
            const QColor c(it.value().toString());
            if (c.isValid()) return c;
        }
    }
    return {};
}

qreal SingleSymbolRenderer::sizeForClass(const QString &classKey) const
{
    if (classKey != kSingleClassKey) return -1.0;
    // Prefer "size" (markers); fall back to "width" (lines).
    for (const SymbolLayer &sl : m_symbol.layers) {
        const auto it = sl.props.constFind(QStringLiteral("size"));
        if (it != sl.props.constEnd()) return it.value().toReal();
    }
    for (const SymbolLayer &sl : m_symbol.layers) {
        const auto it = sl.props.constFind(QStringLiteral("width"));
        if (it != sl.props.constEnd()) return it.value().toReal();
    }
    return -1.0;
}

void SingleSymbolRenderer::setColorForClass(const QString &classKey, const QColor &color)
{
    if (classKey != kSingleClassKey || !color.isValid()) return;
    writePropOnAllLayers(m_symbol, QLatin1String("color"), color.name(QColor::HexArgb));
}

void SingleSymbolRenderer::setSizeForClass(const QString &classKey, qreal size)
{
    if (classKey != kSingleClassKey) return;
    writePropOnAllLayers(m_symbol, QLatin1String("size"), size);
}

void SingleSymbolRenderer::setWidthForClass(const QString &classKey, qreal width)
{
    if (classKey != kSingleClassKey) return;
    writePropOnAllLayers(m_symbol, QLatin1String("width"), width);
}

void SingleSymbolRenderer::setSymbolForClass(const QString &classKey, const SymbolStyle &style)
{
    if (classKey != kSingleClassKey) return;
    m_symbol = style;
}

} // namespace OpenSWMM::Render
