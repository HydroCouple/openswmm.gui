/*!
 * \file   graduatedrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/graduatedrenderer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

namespace
{

// Convention for SymbolLayer::props: any colour value is stored as a hex
// string (e.g. "#FF1F77B4") rather than a QColor variant, so JSON
// round-trip is lossless. This helper does the swap.
void overrideColorInPlace(SymbolStyle &style, const QColor &c)
{
    const QString hex = c.name(QColor::HexArgb);
    for (SymbolLayer &sl : style.layers)
    {
        // Apply only when the symbol layer already advertises a colour slot.
        // Layers that don't paint with a colour (e.g. a PatternFill) are left
        // untouched. We do not invent the key.
        if (sl.props.contains(QStringLiteral("color")))
            sl.props.insert(QStringLiteral("color"), hex);
    }
}

QJsonArray colorListToJson(const QList<QColor> &colors)
{
    QJsonArray arr;
    for (const QColor &c : colors)
        arr.append(c.name(QColor::HexArgb));
    return arr;
}

QList<QColor> jsonToColorList(const QJsonArray &arr)
{
    QList<QColor> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
    {
        const QColor c(v.toString());
        if (c.isValid())
            out.append(c);
        else
            out.append(Qt::transparent);
    }
    return out;
}

} // namespace

void GraduatedRenderer::setRange(double minValue, double maxValue)
{
    m_minValue = minValue;
    m_maxValue = maxValue;
}

void GraduatedRenderer::setBinColors(QList<QColor> colors)
{
    m_binColors = std::move(colors);
}

void GraduatedRenderer::setBaseSymbol(SymbolStyle s)
{
    m_baseSymbol = std::move(s);
}

void GraduatedRenderer::autoClassify(const QVector<double> &samples)
{
    bool any = false;
    double mn = std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();
    for (double v : samples)
    {
        if (!std::isfinite(v))
            continue;
        any = true;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (!any)
        return;  // leave existing range unchanged
    // Degenerate (constant) sample → invent a tiny window so binning is defined.
    if (mn == mx)
    {
        const double eps = std::abs(mn) > 0.0 ? std::abs(mn) * 1e-9 : 1e-9;
        mn -= eps;
        mx += eps;
    }
    m_minValue = mn;
    m_maxValue = mx;
}

QColor GraduatedRenderer::colorForValue(double v) const
{
    const int n = m_binColors.size();
    if (n <= 0)
        return Qt::transparent;
    if (!std::isfinite(v))
        return m_binColors.first();

    if (m_maxValue <= m_minValue)
        return m_binColors.first();

    const double t = (v - m_minValue) / (m_maxValue - m_minValue);
    // Map t in [0, 1] (with clamping) to bin index in [0, n-1].
    int bin = static_cast<int>(std::floor(t * n));
    if (bin < 0)      bin = 0;
    if (bin >= n)     bin = n - 1;
    return m_binColors.at(bin);
}

SymbolStyle GraduatedRenderer::symbolFor(const FeatureRef &, const QVariantMap &attrs) const
{
    SymbolStyle styled = m_baseSymbol;  // value-copy is intentional
    const QVariant v = attrs.value(m_classifyAttribute);
    bool ok = false;
    const double dv = v.toDouble(&ok);
    if (!ok)
        return styled;  // attribute missing or non-numeric → return template untouched
    overrideColorInPlace(styled, colorForValue(dv));
    return styled;
}

QList<LegendSymbolItem> GraduatedRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    const int n = m_binColors.size();
    if (n <= 0)
        return items;

    const double span = m_maxValue - m_minValue;
    const double step = span / static_cast<double>(n);

    items.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        LegendSymbolItem item;
        const double low  = m_minValue + step * static_cast<double>(i);
        const double high = (i == n - 1) ? m_maxValue : (m_minValue + step * static_cast<double>(i + 1));
        item.range = { low, high };
        item.label = QStringLiteral("%1 – %2").arg(low).arg(high);

        // Build the legend swatch the same way symbolFor would have for a
        // representative value in this bin — keeps the legend-from-renderer
        // rule (§J.5) honest.
        item.symbol = m_baseSymbol;
        overrideColorInPlace(item.symbol, m_binColors.at(i));
        item.sortIndex = i;
        items.append(item);
    }
    return items;
}

QJsonObject GraduatedRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());
    obj.insert(QStringLiteral("classifyAttribute"), m_classifyAttribute);
    obj.insert(QStringLiteral("minValue"), m_minValue);
    obj.insert(QStringLiteral("maxValue"), m_maxValue);
    obj.insert(QStringLiteral("binColors"), colorListToJson(m_binColors));
    obj.insert(QStringLiteral("baseSymbol"), m_baseSymbol.toJson());
    return obj;
}

void GraduatedRenderer::fromJson(const QJsonObject &j)
{
    m_classifyAttribute = j.value(QStringLiteral("classifyAttribute")).toString();
    m_minValue = j.value(QStringLiteral("minValue")).toDouble(0.0);
    m_maxValue = j.value(QStringLiteral("maxValue")).toDouble(1.0);
    m_binColors = jsonToColorList(j.value(QStringLiteral("binColors")).toArray());
    m_baseSymbol = SymbolStyle{};
    m_baseSymbol.fromJson(j.value(QStringLiteral("baseSymbol")).toObject());
}

std::unique_ptr<IFeatureRenderer> GraduatedRenderer::clone() const
{
    return std::make_unique<GraduatedRenderer>(*this);
}

} // namespace OpenSWMM::Render
