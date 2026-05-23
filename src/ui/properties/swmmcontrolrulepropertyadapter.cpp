/*!
 * \file   swmmcontrolrulepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmcontrolrulepropertyadapter.h"

#include <openswmm/engine/openswmm_controls.h>

#include <vector>
#include <string>

QString SWMMControlRulePropertyAdapter::displayLabelFor(
        const QString &property) const
{
    if (property == QLatin1String("name"))     return tr("Name");
    if (property == QLatin1String("ruleText")) return tr("Rule Text");
    return {};
}

int SWMMControlRulePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    const int n = swmm_control_count(m_engine);
    const QByteArray want = m_name.toUtf8();
    for (int i = 0; i < n; ++i) {
        char buf[256] = {};
        if (swmm_control_get_id(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
            continue;
        if (want == buf) return i;
    }
    return -1;
}

QString SWMMControlRulePropertyAdapter::ruleText() const
{
    const int i = idx();
    if (i < 0) return {};
    char buf[8192] = {};
    if (swmm_control_get_rule(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

void SWMMControlRulePropertyAdapter::setRuleText(const QString &text)
{
    if (!m_engine) return;
    const int target = idx();
    if (target < 0) return;

    // Engine has no per-rule mutator (DA-ENG-11): snapshot every rule,
    // clear, and re-add with the target slot's text replaced. This is
    // O(N) text but N is small (typically < 100 rules).
    const int n = swmm_control_count(m_engine);
    std::vector<std::string> snapshot;
    snapshot.reserve(n);
    for (int i = 0; i < n; ++i) {
        char buf[8192] = {};
        if (swmm_control_get_rule(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
            continue;
        snapshot.emplace_back(buf);
    }
    if (target >= static_cast<int>(snapshot.size())) return;
    snapshot[static_cast<std::size_t>(target)] = text.toStdString();

    if (swmm_control_clear_rules(m_engine) != SWMM_OK) return;
    for (const auto &t : snapshot) swmm_control_add_rule(m_engine, t.c_str());

    // The stored name may have changed if the user retyped the
    // `RULE <new_name>` header — let the layer-side handler reconcile.
    char nameBuf[256] = {};
    if (swmm_control_get_id(m_engine, target, nameBuf, sizeof(nameBuf)) == SWMM_OK) {
        const QString newName = QString::fromUtf8(nameBuf);
        if (newName != m_name) {
            const QString oldName = m_name;
            m_name = newName;
            emit renameRequested(oldName, newName);
        }
    }

    emit changed();
}
