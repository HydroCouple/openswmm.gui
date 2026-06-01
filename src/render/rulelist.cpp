/*!
 * \file   rulelist.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  RuleList implementation (Slice Z.1).
 */

#include "render/rulelist.h"

#include "render/renderers/singlesymbolrenderer.h"

#include <algorithm>
#include <utility>

namespace OpenSWMM::Render
{

RuleList::RuleList(QObject *parent) : QObject(parent) {}
RuleList::~RuleList() = default;

int RuleList::count() const
{
    return static_cast<int>(m_rules.size());
}

Rule *RuleList::at(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_rules[static_cast<std::size_t>(index)].get();
}

int RuleList::indexOf(const Rule *rule) const
{
    if (!rule)
        return -1;
    for (std::size_t i = 0; i < m_rules.size(); ++i)
        if (m_rules[i].get() == rule)
            return static_cast<int>(i);
    return -1;
}

Rule *RuleList::activeRule() const
{
    return at(m_activeIndex);
}

void RuleList::connectRule(Rule *r)
{
    if (!r)
        return;
    r->setParent(this);
    // Capture the Rule pointer (not the index — indices shift on move/insert).
    // On signal, re-resolve the current index by linear scan; lists are
    // small (rarely >20 Rules), the lookup is cheap.
    QObject::connect(r, &Rule::ruleChanged, this, [this, r]() {
        const int idx = indexOf(r);
        if (idx >= 0)
            emit ruleChanged(idx);
    });
}

Rule *RuleList::append(std::unique_ptr<Rule> rule)
{
    if (!rule)
        return nullptr;
    Rule *raw = rule.get();
    connectRule(raw);
    m_rules.push_back(std::move(rule));
    if (m_activeIndex < 0) {
        m_activeIndex = static_cast<int>(m_rules.size()) - 1;
        emit activeIndexChanged(m_activeIndex);
    }
    emit ruleListChanged();
    return raw;
}

Rule *RuleList::insert(int index, std::unique_ptr<Rule> rule)
{
    if (!rule)
        return nullptr;
    const int clamped = std::clamp(index, 0, count());
    Rule *raw = rule.get();
    connectRule(raw);
    m_rules.insert(m_rules.begin() + clamped, std::move(rule));
    // If we inserted at or before the active index, the active Rule's
    // index shifts by one.
    if (m_activeIndex < 0) {
        m_activeIndex = clamped;
        emit activeIndexChanged(m_activeIndex);
    } else if (clamped <= m_activeIndex) {
        ++m_activeIndex;
        emit activeIndexChanged(m_activeIndex);
    }
    emit ruleListChanged();
    return raw;
}

bool RuleList::remove(int index)
{
    if (index < 0 || index >= count())
        return false;
    m_rules.erase(m_rules.begin() + index);
    // Keep selection as stable as possible.
    if (m_rules.empty()) {
        if (m_activeIndex != -1) {
            m_activeIndex = -1;
            emit activeIndexChanged(m_activeIndex);
        }
    } else if (index < m_activeIndex) {
        --m_activeIndex;
        emit activeIndexChanged(m_activeIndex);
    } else if (index == m_activeIndex) {
        m_activeIndex = std::min(m_activeIndex, count() - 1);
        emit activeIndexChanged(m_activeIndex);
    }
    emit ruleListChanged();
    return true;
}

bool RuleList::move(int from, int to)
{
    if (from == to)
        return false;
    if (from < 0 || from >= count())
        return false;
    if (to < 0 || to >= count())
        return false;

    auto rule = std::move(m_rules[from]);
    m_rules.erase(m_rules.begin() + from);
    m_rules.insert(m_rules.begin() + to, std::move(rule));

    // Adjust active index so the same Rule stays selected.
    if (m_activeIndex == from) {
        m_activeIndex = to;
        emit activeIndexChanged(m_activeIndex);
    } else if (from < m_activeIndex && to >= m_activeIndex) {
        --m_activeIndex;
        emit activeIndexChanged(m_activeIndex);
    } else if (from > m_activeIndex && to <= m_activeIndex) {
        ++m_activeIndex;
        emit activeIndexChanged(m_activeIndex);
    }
    emit ruleListChanged();
    return true;
}

void RuleList::clear()
{
    if (m_rules.empty())
        return;
    m_rules.clear();
    if (m_activeIndex != -1) {
        m_activeIndex = -1;
        emit activeIndexChanged(m_activeIndex);
    }
    emit ruleListChanged();
}

void RuleList::setActiveIndex(int index)
{
    const int clamped = (index < 0 || count() == 0)
                            ? -1
                            : std::clamp(index, 0, count() - 1);
    if (clamped == m_activeIndex)
        return;
    m_activeIndex = clamped;
    emit activeIndexChanged(m_activeIndex);
}

QJsonArray RuleList::toJson() const
{
    QJsonArray arr;
    for (const auto &r : m_rules)
        arr.append(r->toJson());
    return arr;
}

void RuleList::fromJson(const QJsonArray &arr)
{
    // Replace strategy — caller can clear() first explicitly if they want
    // the append semantics. Here we re-populate from scratch so a project
    // load gives a deterministic result.
    m_rules.clear();
    for (const auto &v : arr) {
        if (!v.isObject())
            continue;
        auto rule = Rule::fromJson(v.toObject(), this);
        if (!rule)
            continue;
        connectRule(rule.get());
        m_rules.push_back(std::move(rule));
    }
    m_activeIndex = m_rules.empty() ? -1 : 0;
    emit activeIndexChanged(m_activeIndex);
    emit ruleListChanged();
}

void RuleList::loadLegacySublayersAsRules(const QJsonArray &sublayers)
{
    bool changed = false;
    for (const auto &v : sublayers) {
        if (!v.isObject())
            continue;
        const QJsonObject sl = v.toObject();
        const QString id  = sl.value(QStringLiteral("id")).toString();
        const bool    vis = sl.value(QStringLiteral("isVisible")).toBool(true);
        const double  op  = sl.value(QStringLiteral("opacity")).toDouble(1.0);

        // Z.1 migration: identity + visibility + opacity → SingleSymbol
        // placeholder. Full style-payload migration (per-archetype marker
        // shape, line width, etc.) lands in Slice Z.6.
        auto single = std::make_unique<SingleSymbolRenderer>();
        SymbolStyle s = single->symbol();
        s.opacity = std::clamp(op, 0.0, 1.0);
        single->setSymbol(s);

        auto rule = std::make_unique<Rule>(id.isEmpty()
                                               ? QStringLiteral("Sublayer")
                                               : id,
                                           std::move(single),
                                           this);
        rule->setVisible(vis);
        connectRule(rule.get());
        m_rules.push_back(std::move(rule));
        changed = true;
    }
    if (changed) {
        if (m_activeIndex < 0 && !m_rules.empty()) {
            m_activeIndex = 0;
            emit activeIndexChanged(m_activeIndex);
        }
        emit ruleListChanged();
    }
}

} // namespace OpenSWMM::Render
