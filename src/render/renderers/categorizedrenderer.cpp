/*!
 * \file   categorizedrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/categorizedrenderer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace OpenSWMM::Render
{

void CategorizedRenderer::setCategories(QList<Category> cats)
{
    m_categories = std::move(cats);
}

void CategorizedRenderer::addCategory(Category c)
{
    m_categories.append(std::move(c));
}

SymbolStyle CategorizedRenderer::symbolFor(const FeatureRef &, const QVariantMap &attrs) const
{
    const QString value = attrs.value(m_classifyAttribute).toString();
    for (const Category &c : m_categories)
    {
        if (c.value == value)
            return c.symbol;
    }
    return m_fallback;
}

QList<LegendSymbolItem> CategorizedRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    items.reserve(m_categories.size());
    int idx = 0;
    for (const Category &c : m_categories)
    {
        LegendSymbolItem item;
        item.label     = c.label.isEmpty() ? c.value : c.label;
        item.symbol    = c.symbol;
        item.sortIndex = idx;
        item.classKey  = QString::number(idx);
        ++idx;
        items.append(item);
    }
    return items;
}

QJsonObject CategorizedRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());
    obj.insert(QStringLiteral("classifyAttribute"), m_classifyAttribute);

    QJsonArray cats;
    for (const Category &c : m_categories)
    {
        QJsonObject co;
        co.insert(QStringLiteral("value"),  c.value);
        if (!c.label.isEmpty())
            co.insert(QStringLiteral("label"), c.label);
        co.insert(QStringLiteral("symbol"), c.symbol.toJson());
        cats.append(co);
    }
    obj.insert(QStringLiteral("categories"), cats);
    obj.insert(QStringLiteral("fallback"), m_fallback.toJson());
    return obj;
}

void CategorizedRenderer::fromJson(const QJsonObject &j)
{
    m_classifyAttribute = j.value(QStringLiteral("classifyAttribute")).toString();
    m_categories.clear();
    const QJsonArray cats = j.value(QStringLiteral("categories")).toArray();
    for (const QJsonValue &v : cats)
    {
        const QJsonObject co = v.toObject();
        Category c;
        c.value = co.value(QStringLiteral("value")).toString();
        c.label = co.value(QStringLiteral("label")).toString();
        c.symbol.fromJson(co.value(QStringLiteral("symbol")).toObject());
        m_categories.append(c);
    }
    m_fallback = SymbolStyle{};
    m_fallback.fromJson(j.value(QStringLiteral("fallback")).toObject());
}

std::unique_ptr<IFeatureRenderer> CategorizedRenderer::clone() const
{
    return std::make_unique<CategorizedRenderer>(*this);
}

// ── Per-class editing (Slice BB Phase 8.6.16) ─────────────────────────

QColor CategorizedRenderer::colorForClass(const QString &classKey) const
{
    bool ok = false;
    const int idx = classKey.toInt(&ok);
    if (!ok || idx < 0 || idx >= m_categories.size()) return {};
    for (const SymbolLayer &sl : m_categories.at(idx).symbol.layers) {
        const auto it = sl.props.constFind(QStringLiteral("color"));
        if (it != sl.props.constEnd()) {
            const QColor c(it.value().toString());
            if (c.isValid()) return c;
        }
    }
    return {};
}

void CategorizedRenderer::setColorForClass(const QString &classKey, const QColor &color)
{
    bool ok = false;
    const int idx = classKey.toInt(&ok);
    if (!ok || idx < 0 || idx >= m_categories.size() || !color.isValid())
        return;
    const QString hex = color.name(QColor::HexArgb);
    for (SymbolLayer &sl : m_categories[idx].symbol.layers) {
        if (sl.props.contains(QStringLiteral("color")))
            sl.props.insert(QStringLiteral("color"), hex);
    }
}

void CategorizedRenderer::setSymbolForClass(const QString &classKey, const SymbolStyle &style)
{
    bool ok = false;
    const int idx = classKey.toInt(&ok);
    if (!ok || idx < 0 || idx >= m_categories.size())
        return;
    m_categories[idx].symbol = style;
}

} // namespace OpenSWMM::Render
