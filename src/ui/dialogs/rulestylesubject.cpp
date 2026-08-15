/*!
 * \file   rulestylesubject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  RuleStyleSubject implementation (Slice Z.2).
 */

#include "ui/dialogs/rulestylesubject.h"

#include "render/rule.h"
#include "render/rulelist.h"

#include <utility>

namespace openswmmvis::ui {

RuleStyleSubject::RuleStyleSubject(OpenSWMM::Render::Rule *rule,
                                   QString routingId,
                                   QString section)
    : m_rule(rule)
    , m_routingId(std::move(routingId))
    , m_section(std::move(section))
{
}

QString RuleStyleSubject::title() const
{
    if (!m_rule)
        return {};
    const QString name = m_rule->name();
    return name.isEmpty() ? QStringLiteral("Rule") : name;
}

QString RuleStyleSubject::section() const
{
    return m_section;
}

QObject *RuleStyleSubject::propertyObject() const
{
    return m_rule;
}

QString RuleStyleSubject::routingId() const
{
    return m_routingId;
}

std::vector<std::unique_ptr<ILayerStyleSubject>>
subjectsFromRuleList(OpenSWMM::Render::RuleList *list)
{
    std::vector<std::unique_ptr<ILayerStyleSubject>> subjects;
    if (!list)
        return subjects;

    const int n = list->count();
    subjects.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        OpenSWMM::Render::Rule *r = list->at(i);
        if (!r)
            continue;
        subjects.push_back(std::make_unique<RuleStyleSubject>(
            r, QStringLiteral("rule.%1").arg(i)));
    }
    return subjects;
}

} // namespace openswmmvis::ui
