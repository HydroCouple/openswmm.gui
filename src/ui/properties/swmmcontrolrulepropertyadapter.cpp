/*!
 * \file   swmmcontrolrulepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmcontrolrulepropertyadapter.h"

#include "layers/swmmmodellayer.h"

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

    // Slice BR Phase 6.8.1 — route inline edits through the layer's
    // apply helper so controlRulesChanged(name) fires and any other UI
    // bound to the same rule (RulesEditorDialog, Object Browser, future
    // scenario-comparison views) refreshes through the registry/MVC seam.
    // When no layer is bound (test code constructing the adapter without
    // a host SWMMModelLayer) we fall back to the legacy direct engine
    // round-trip so existing unit tests keep passing.
    if (m_layer) {
        QString err;
        if (!m_layer->applyControlRuleReplace(m_name, text, &err)) {
            // Apply refused — likely the rule's name disappeared (manual
            // engine reload between fetch + write). Fall through to the
            // engine-direct path below as a recovery so the user's edit
            // is not silently dropped.
        } else {
            // The stored name may have changed if the user retyped the
            // `RULE <new_name>` header — surface the rename so callers
            // can cascade references.
            const int target = idx();
            if (target >= 0) {
                char nameBuf[256] = {};
                if (swmm_control_get_id(m_engine, target, nameBuf, sizeof(nameBuf)) == SWMM_OK) {
                    const QString newName = QString::fromUtf8(nameBuf);
                    if (newName != m_name) {
                        const QString oldName = m_name;
                        m_name = newName;
                        emit renameRequested(oldName, newName);
                    }
                }
            }
            emit changed();
            return;
        }
    }

    // Legacy direct-engine path (no layer bound, or apply helper rejected).
    // Engine has no per-rule mutator (DA-ENG-11): snapshot every rule,
    // clear, and re-add with the target slot's text replaced. This is
    // O(N) text but N is small (typically < 100 rules).
    const int target = idx();
    if (target < 0) return;
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
