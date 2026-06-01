/*!
 * \file   controlruleregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "controls/controlruleregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_controls.h>

namespace openswmmvis::controls {

ControlRuleRegistry::ControlRuleRegistry(QObject *parent)
    : QObject(parent)
{
}

ControlRuleRegistry::~ControlRuleRegistry() = default;

ControlRuleProvider *ControlRuleRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

ControlRuleProvider *ControlRuleRegistry::create(const QString &name, const QString &body)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new ControlRuleProvider(name, body, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);

    connect(p, &ControlRuleProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });

    emit providerAdded(p);
    return p;
}

void ControlRuleRegistry::remove(ControlRuleProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool ControlRuleRegistry::rename(ControlRuleProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    // Case-only rename is permitted (re-use the same provider's identity).
    if (p->name().compare(newName, Qt::CaseInsensitive) == 0) {
        p->setName(newName);
        return true;
    }
    if (hasName(newName)) return false;
    p->setName(newName);
    return true;
}

void ControlRuleRegistry::clear()
{
    // Drop in reverse so providerAboutToBeRemoved fires for each slot
    // before the vector shrinks. deleteLater() is safe here because the
    // QObject parent (this) holds ownership only via raw pointers.
    while (!m_providers.isEmpty()) {
        ControlRuleProvider *p = m_providers.back();
        emit providerAboutToBeRemoved(p);
        m_byLowerName.remove(p->name().toLower());
        m_providers.pop_back();
        p->deleteLater();
    }
    m_byLowerName.clear();
}

int ControlRuleRegistry::loadFromEngine(void *engineHandle)
{
    clear();
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);

    const int n = swmm_control_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        // Pull the full rule body (RULE <name> + premises + actions + PRIORITY).
        char body[8192] = {};
        if (swmm_control_get_rule(eng, i, body, sizeof(body)) != SWMM_OK)
            continue;

        // Resolve the canonical rule name. The engine returns
        // SWMM_ERR_BADPARAM when the body has no parseable `RULE <name>`
        // prefix — we surface the DA.1 sentinel so the user can find
        // and fix the rule from the UI.
        char nameBuf[256] = {};
        QString name;
        if (swmm_control_get_id(eng, i, nameBuf, sizeof(nameBuf)) == SWMM_OK) {
            name = QString::fromUtf8(nameBuf);
        } else {
            name = QStringLiteral("Rule %1 [unnamed]").arg(i + 1);
        }

        // Name uniqueness: the engine permits two rules with the same
        // header (legacy parity); the registry's hash collapses them.
        // We dedupe by appending a numeric suffix so the registry stays
        // well-formed. The user can fix the underlying duplicate names
        // from the editor.
        QString unique = name;
        int suffix = 1;
        while (hasName(unique)) {
            unique = QStringLiteral("%1#%2").arg(name).arg(++suffix);
        }

        if (create(unique, QString::fromUtf8(body)))
            ++added;
    }
    return added;
}

int ControlRuleRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);

    if (swmm_control_clear_rules(eng) != SWMM_OK) return 0;

    int written = 0;
    for (ControlRuleProvider *p : m_providers) {
        if (!p) continue;
        const QByteArray utf8 = p->body().toUtf8();
        if (swmm_control_add_rule(eng, utf8.constData()) == SWMM_OK)
            ++written;
    }
    return written;
}

} // namespace openswmmvis::controls
