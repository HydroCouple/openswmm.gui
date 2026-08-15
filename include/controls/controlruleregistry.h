/*!
 * \file   controlruleregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.1 — project-scoped factory + lookup for
 *         `ControlRuleProvider` instances.
 *
 * Mirrors `CurveRegistry` / `PatternRegistry` / `TimeseriesRegistry` in
 * structure. Engine I/O uses the existing `openswmm_controls.h` C ABI
 * (`swmm_control_count`, `_get_rule`, `_get_id`, `_add_rule`,
 * `_clear_rules`) — no new mutation surface is required for the CRUD
 * editor because all four operations reduce to snapshot+clear+re-add,
 * matching today's `SWMMControlRulePropertyAdapter::setRuleText`.
 *
 * The registry is owned by `SWMMModelLayer` and accessed via
 * `layer->ensureControlRuleRegistry()` (lazy construction, same as
 * `ensureCurveRegistry` / `ensurePatternRegistry`). All UI mutations
 * route through `SWMMModelLayer::applyControlRule*` helpers which call
 * the registry's add/remove/rename + the engine API and then emit
 * `SWMMModelLayer::controlRulesChanged(QString)` to fan out to every
 * subscribed surface (Object Browser, property panel, future
 * `RulesEditorDialog`, future scenario-comparison views).
 */
#ifndef OPENSWMMVIS_CONTROLS_CONTROLRULEREGISTRY_H
#define OPENSWMMVIS_CONTROLS_CONTROLRULEREGISTRY_H

#include "controls/controlruleprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::controls {

class ControlRuleRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ControlRuleRegistry(QObject *parent = nullptr);
    ~ControlRuleRegistry() override;

    QVector<ControlRuleProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    ControlRuleProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name + body.
     *  Returns the new provider, or `nullptr` if the name collides.
     *  Does NOT push to the engine — the layer's apply-helpers do that. */
    ControlRuleProvider *create(const QString &name, const QString &body);

    /*! \brief Remove a provider from the registry (no engine touch — the
     *  layer does that via `applyControlRuleRemove`). */
    void remove(ControlRuleProvider *p);

    /*! \brief Rename a provider with uniqueness check. Returns false on
     *  collision (case-insensitive) or empty name. */
    bool rename(ControlRuleProvider *p, const QString &newName);

    /*! \brief Reload every provider from a live SWMM engine. Walks
     *  `swmm_control_count` + `_get_rule` + `_get_id`. Rules whose text
     *  has no parseable `RULE <name>` header (engine returns
     *  `SWMM_ERR_BADPARAM`) get a sentinel name `"Rule N [unnamed]"`
     *  matching the DA.1 convention. Existing providers are cleared
     *  before the reload so the registry is a faithful mirror of the
     *  engine's current rule list. Returns the number of providers
     *  loaded (0 on empty engine). */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via
     *  `swmm_control_clear_rules` + a `swmm_control_add_rule` loop. Used
     *  by the layer's apply-helpers and by the project-save serialiser. */
    int saveToEngine(void *engineHandle);

    /*! \brief Drop every provider without touching the engine. Used by
     *  `loadFromEngine` and by project teardown. */
    void clear();

signals:
    void providerAdded(openswmmvis::controls::ControlRuleProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::controls::ControlRuleProvider *provider);
    void providerRenamed(openswmmvis::controls::ControlRuleProvider *provider,
                         const QString &prevName, const QString &newName);

private:
    QVector<ControlRuleProvider *>          m_providers;
    QHash<QString, ControlRuleProvider *>   m_byLowerName;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_CONTROLRULEREGISTRY_H
