/*!
 * \file   rulebasedrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/rulebasedrenderer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace OpenSWMM::Render
{

void RuleBasedRenderer::setRules(QList<Rule> rules)
{
    m_rules = std::move(rules);
}

void RuleBasedRenderer::addRule(Rule r)
{
    m_rules.append(std::move(r));
}

SymbolStyle RuleBasedRenderer::symbolFor(const FeatureRef &, const QVariantMap &) const
{
    // STUB: expression evaluation lands with Slice BI.2 (LabelExpression DSL
    // parser). Until then, an empty-expression rule (which "matches always")
    // can still take effect — useful for static "default rule first" setups.
    for (const Rule &r : m_rules)
    {
        if (r.expression.isEmpty())
            return r.symbol;
    }
    return m_fallback;
}

QList<LegendSymbolItem> RuleBasedRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    items.reserve(m_rules.size());
    int idx = 0;
    for (const Rule &r : m_rules)
    {
        LegendSymbolItem item;
        item.label     = r.label.isEmpty() ? r.expression : r.label;
        item.symbol    = r.symbol;
        item.sortIndex = idx++;
        items.append(item);
    }
    return items;
}

QJsonObject RuleBasedRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());

    QJsonArray rules;
    for (const Rule &r : m_rules)
    {
        QJsonObject ro;
        ro.insert(QStringLiteral("expression"), r.expression);
        if (!r.label.isEmpty())
            ro.insert(QStringLiteral("label"), r.label);
        ro.insert(QStringLiteral("symbol"), r.symbol.toJson());
        // Only emit scaleRange when non-default to keep the JSON compact.
        if (r.scaleRange.first != 0.0 || r.scaleRange.second != 0.0)
        {
            QJsonObject sr;
            sr.insert(QStringLiteral("min"), r.scaleRange.first);
            sr.insert(QStringLiteral("max"), r.scaleRange.second);
            ro.insert(QStringLiteral("scaleRange"), sr);
        }
        rules.append(ro);
    }
    obj.insert(QStringLiteral("rules"), rules);
    obj.insert(QStringLiteral("fallback"), m_fallback.toJson());
    return obj;
}

void RuleBasedRenderer::fromJson(const QJsonObject &j)
{
    m_rules.clear();
    const QJsonArray rules = j.value(QStringLiteral("rules")).toArray();
    for (const QJsonValue &v : rules)
    {
        const QJsonObject ro = v.toObject();
        Rule r;
        r.expression = ro.value(QStringLiteral("expression")).toString();
        r.label      = ro.value(QStringLiteral("label")).toString();
        r.symbol.fromJson(ro.value(QStringLiteral("symbol")).toObject());
        if (ro.contains(QStringLiteral("scaleRange")))
        {
            const QJsonObject sr = ro.value(QStringLiteral("scaleRange")).toObject();
            r.scaleRange.first  = sr.value(QStringLiteral("min")).toDouble(0.0);
            r.scaleRange.second = sr.value(QStringLiteral("max")).toDouble(0.0);
        }
        m_rules.append(r);
    }
    m_fallback = SymbolStyle{};
    m_fallback.fromJson(j.value(QStringLiteral("fallback")).toObject());
}

std::unique_ptr<IFeatureRenderer> RuleBasedRenderer::clone() const
{
    return std::make_unique<RuleBasedRenderer>(*this);
}

} // namespace OpenSWMM::Render
