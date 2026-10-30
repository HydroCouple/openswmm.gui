/**
 * @file      controlruleregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.1 — project-scoped factory + lookup for
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CONTROLS_CONTROLRULEREGISTRY_H
#define OPENSWMMVIS_CONTROLS_CONTROLRULEREGISTRY_H

#include "controls/controlruleprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::controls
{

class ControlRuleRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an empty owner of rule providers on the QObject owning thread.
     * @param[in] parent Optional QObject owner.
     */
    explicit ControlRuleRegistry(QObject *parent = nullptr);
    /**
     * @brief Destroy child providers and signal connections without touching an engine.
     */
    ~ControlRuleRegistry() override;

    /**
     * @brief Copy the rule list in insertion/load order.
     * @return Borrowed registry-owned provider pointers, invalidated by remove/clear/load or destruction.
     */
    QVector<ControlRuleProvider *> providers() const { return m_providers; }
    /**
     * @brief Count currently registered rules.
     * @return Provider count narrowed to int.
     */
    int providerCount() const noexcept { return m_providers.size(); }

    /**
     * @brief Resolve an identifier through lowercase-name lookup.
     * @param[in] name Identifier without whitespace trimming.
     * @return Borrowed owned provider or null when absent.
     */
    ControlRuleProvider *findByName(const QString &name) const;
    /**
     * @brief Test whether a case-insensitive identifier is occupied.
     * @param[in] name Candidate identifier, not trimmed.
     * @return True when lookup resolves.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name + body.
     *  Returns the new provider, or `nullptr` if the name collides.
     *  Does NOT push to the engine — the layer's apply-helpers do that.
     * @param[in] name Nonempty case-insensitively unused identifier.
     * @param[in] body Full body copied without parsing or header/name consistency checks.
     * @return Borrowed child, or null for an empty/duplicate name; emits providerAdded() after insertion and wiring.
     */
    ControlRuleProvider *create(const QString &name, const QString &body);

    /*! \brief Remove a provider from the registry (no engine touch — the
     *  layer does that via `applyControlRuleRemove`).
     * @param[in] p Owned provider; null/nonmember is ignored.
     * @details Emits providerAboutToBeRemoved() before unregistering, then schedules deleteLater().
     */
    void remove(ControlRuleProvider *p);

    /*! \brief Rename a provider with uniqueness check. Returns false on
     *  collision (case-insensitive) or empty name.
     * @param[in] p Non-null provider expected to belong to this registry; membership is not tested.
     * @param[in] newName Nonempty identifier; case-only changes allowed.
     * @return True after local setName(), including an unchanged-name no-op; false for invalid input or another occupied name.
     * @details Updates name lookup through the signal connection; does not rewrite body or engine rules.
     */
    bool rename(ControlRuleProvider *p, const QString &newName);

    /*! \brief Reload every provider from a live SWMM engine. Walks
     *  `swmm_control_count` + `_get_rule` + `_get_id`. Rules whose text
     *  has no parseable `RULE <name>` header (engine returns
     *  `SWMM_ERR_BADPARAM`) get a sentinel name `"Rule N [unnamed]"`
     *  matching the DA.1 convention. Existing providers are cleared
     *  before the reload so the registry is a faithful mirror of the
     *  engine's current rule list. Returns the number of providers
     *  loaded (0 on empty engine).
     * @param[in] engineHandle Borrowed live SWMM_Engine; null still clears all local providers and returns zero.
     * @return Number loaded; rules with unreadable text are skipped.
     * @details Text/ID buffers are 8192/256 bytes. Duplicate IDs receive #2, #3, etc. local suffixes without rewriting body headers. Clearing emits removal signals and schedules provider deletion.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via
     *  `swmm_control_clear_rules` + a `swmm_control_add_rule` loop. Used
     *  by the layer's apply-helpers and by the project-save serialiser.
     * @param[in] engineHandle Borrowed editable engine used synchronously; null returns zero without touching local providers.
     * @return Number of bodies added after clearing, or zero on clear/add failure (also zero for successful empty output).
     * @details Snapshots readable original bodies first; a failed add attempts to clear and restore that snapshot. Restore return codes are ignored, so rollback is best-effort. Providers and cached validation remain unchanged.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Drop every provider without touching the engine. Used by
     *  `loadFromEngine` and by project teardown.
     * @details Emits providerAboutToBeRemoved() in reverse order before unregistering each provider and scheduling deleteLater().
     */
    void clear();

signals:
    /**
     * @brief Announce a provider after local insertion and signal wiring.
     * @param[in] provider Borrowed owned child, already available through lookup.
     */
    void providerAdded(openswmmvis::controls::ControlRuleProvider *provider);
    /**
     * @brief Announce a provider while it is still registered and alive.
     * @param[in] provider Borrowed child whose references should be released.
     */
    void providerAboutToBeRemoved(openswmmvis::controls::ControlRuleProvider *provider);
    /**
     * @brief Announce a changed name after rekeying the lookup.
     * @param[in] provider Borrowed rule provider.
     * @param[in] prevName Removed lookup name.
     * @param[in] newName Installed lookup name.
     */
    void providerRenamed(openswmmvis::controls::ControlRuleProvider *provider,
                         const QString &prevName, const QString &newName);

private:
    QVector<ControlRuleProvider *> m_providers;
    QHash<QString, ControlRuleProvider *> m_byLowerName;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_CONTROLRULEREGISTRY_H
