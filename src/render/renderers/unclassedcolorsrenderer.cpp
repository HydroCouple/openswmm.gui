/*!
 * \file   unclassedcolorsrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  UnclassedColorsRenderer implementation (Slice Z.9).
 */

#include "render/renderers/unclassedcolorsrenderer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

namespace {

// Gap A1.2 — colour convention now lives in one place: SymbolProps
// (render/symbolstyle.h). Local alias keeps the call sites unchanged.
using SymbolProps::overrideColorInPlace;

QString jsonObjectToString(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject jsonObjectFromString(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

} // namespace

// ── Setters ──────────────────────────────────────────────────────────

void UnclassedColorsRenderer::setRamp(RasterColorRamp ramp)
{
    m_ramp = std::move(ramp);
}

void UnclassedColorsRenderer::setRange(double minValue, double maxValue)
{
    m_ramp.minValue = minValue;
    m_ramp.maxValue = maxValue;
}

// ── colorForValue ────────────────────────────────────────────────────

QColor UnclassedColorsRenderer::colorForValue(double v) const
{
    if (!std::isfinite(v))
        return m_noData;

    const double mn = m_ramp.minValue;
    const double mx = m_ramp.maxValue;

    if (v < mn) {
        return m_belowRange.isValid() ? m_belowRange : m_ramp.colorAt(0.0);
    }
    if (v > mx) {
        return m_aboveRange.isValid() ? m_aboveRange : m_ramp.colorAt(1.0);
    }
    // Degenerate range: emit the midpoint colour so we never divide by zero.
    if (qFuzzyCompare(mn + 1.0, mx + 1.0))
        return m_ramp.colorAt(0.5);

    const double t = (v - mn) / (mx - mn);
    return m_ramp.colorAt(std::clamp(t, 0.0, 1.0));
}

// ── IFeatureRenderer ────────────────────────────────────────────────

SymbolStyle UnclassedColorsRenderer::symbolFor(const FeatureRef & /*f*/,
                                                const QVariantMap &attrs) const
{
    SymbolStyle out = m_baseSymbol;
    if (m_classifyAttribute.isEmpty())
        return out;

    const QVariant val = attrs.value(m_classifyAttribute);
    if (!val.isValid()) {
        overrideColorInPlace(out, m_noData);
        return out;
    }
    bool ok = false;
    const double v = val.toDouble(&ok);
    if (!ok) {
        overrideColorInPlace(out, m_noData);
        return out;
    }
    overrideColorInPlace(out, colorForValue(v));
    return out;
}

QList<LegendSymbolItem> UnclassedColorsRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    const double mn = m_ramp.minValue;
    const double mx = m_ramp.maxValue;
    const int    n  = m_legendLabelCount;

    items.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.0
                                  : static_cast<double>(i) / (n - 1);
        const double v = mn + t * (mx - mn);

        LegendSymbolItem item;
        item.label = QString::number(v, 'g', 4);
        item.symbol = m_baseSymbol;
        overrideColorInPlace(item.symbol, m_ramp.colorAt(t));
        item.range = { v, v };
        item.sortIndex = i;
        item.classKey  = QString::number(i);
        items.append(item);
    }
    return items;
}

QJsonObject UnclassedColorsRenderer::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("id")]                = rendererId();
    j[QStringLiteral("classifyAttribute")] = m_classifyAttribute;
    j[QStringLiteral("ramp")]              = m_ramp.toJson();
    j[QStringLiteral("baseSymbol")]        = m_baseSymbol.toJson();
    if (m_belowRange.isValid())
        j[QStringLiteral("belowRange")] = m_belowRange.name(QColor::HexArgb);
    if (m_aboveRange.isValid())
        j[QStringLiteral("aboveRange")] = m_aboveRange.name(QColor::HexArgb);
    if (m_noData.alpha() != 0 || m_noData.red() != 0
        || m_noData.green() != 0 || m_noData.blue() != 0)
        j[QStringLiteral("noDataColor")] = m_noData.name(QColor::HexArgb);
    if (m_legendLabelCount != 5)
        j[QStringLiteral("legendLabelCount")] = m_legendLabelCount;
    return j;
}

void UnclassedColorsRenderer::fromJson(const QJsonObject &j)
{
    m_classifyAttribute = j.value(QStringLiteral("classifyAttribute")).toString();
    if (j.contains(QStringLiteral("ramp")))
        m_ramp = RasterColorRamp::fromJson(j.value(QStringLiteral("ramp")).toObject());
    if (j.contains(QStringLiteral("baseSymbol")))
        m_baseSymbol.fromJson(j.value(QStringLiteral("baseSymbol")).toObject());

    const QString below = j.value(QStringLiteral("belowRange")).toString();
    m_belowRange = below.isEmpty() ? QColor() : QColor(below);
    const QString above = j.value(QStringLiteral("aboveRange")).toString();
    m_aboveRange = above.isEmpty() ? QColor() : QColor(above);

    const QString nd = j.value(QStringLiteral("noDataColor")).toString();
    m_noData = nd.isEmpty() ? QColor(0, 0, 0, 0) : QColor(nd);

    m_legendLabelCount = j.value(QStringLiteral("legendLabelCount")).toInt(5);
    if (m_legendLabelCount < 2) m_legendLabelCount = 2;
}

std::unique_ptr<IFeatureRenderer> UnclassedColorsRenderer::clone() const
{
    auto c = std::make_unique<UnclassedColorsRenderer>();
    c->m_classifyAttribute = m_classifyAttribute;
    c->m_ramp              = m_ramp;
    c->m_baseSymbol        = m_baseSymbol;
    c->m_belowRange        = m_belowRange;
    c->m_aboveRange        = m_aboveRange;
    c->m_noData            = m_noData;
    c->m_legendLabelCount  = m_legendLabelCount;
    return c;
}

} // namespace OpenSWMM::Render
