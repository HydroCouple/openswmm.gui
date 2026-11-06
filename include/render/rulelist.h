/**
 * @file      rulelist.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Ordered, stackable list of Rules owned by a layer (Slice Z.1).
 *
 *         A RuleList is the per-layer container introduced by
 *         RENDERING_RULE_MODEL_PLAN.md §3.2. It owns Rules in a fixed
 *         order; paint order = list order (top of the list paints last).
 *         Per-Rule visibility lets the user toggle individual paint
 *         passes without removing them.
 *
 *         RuleList tracks an "active" index — the Rule currently focused
 *         in the Symbology tab's editor. This is purely an editor /
 *         selection concept; paint walks the whole list regardless.
 *
 *         Signals fire at three granularities:
 *           - ruleListChanged()      — any structural change (add / remove / move / clear).
 *           - activeIndexChanged(i)  — selection moved (no structural change).
 *           - ruleChanged(i)         — Rule at index i emitted ruleChanged().
 *
 *         JSON round-trip: toJson() yields a QJsonArray, fromJson() takes
 *         one and re-populates. loadLegacySublayersAsRules() migrates the
 *         older "sublayers" JSON array from shipped .oswp files
 *         (RENDERING_RULE_MODEL_PLAN.md §15).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_RULELIST_H
#define OPENSWMM_RENDER_RULELIST_H

#include "render/rule.h"

#include <QJsonArray>
#include <QObject>

#include <memory>
#include <vector>

namespace OpenSWMM::Render
{

/*!
 * \class RuleList
 * \brief Ordered owning container for Rules on one layer.
 */
class RuleList : public QObject
{
    Q_OBJECT

public:
    /** @brief Construct an empty owning list with no active rule.
     * @param parent Optional QObject owner. List mutations and connected rule
     * edits must occur on the owning Qt thread. */
    explicit RuleList(QObject *parent = nullptr);
    /** @brief Destroy every owned Rule and its renderer, invalidating borrowed pointers. */
    ~RuleList() override;

    /** @brief Prohibit copying QObject/list identity and unique rule ownership. */
    RuleList(const RuleList &) = delete;
    /** @brief Prohibit copy assignment of QObject/list identity. */
    RuleList &operator=(const RuleList &) = delete;
    /** @brief Prohibit moving a connected QObject/list identity. */
    RuleList(RuleList &&) = delete;
    /** @brief Prohibit move assignment of a connected QObject/list identity. */
    RuleList &operator=(RuleList &&) = delete;

    // ── Read access ───────────────────────────────────────────────────

    /** @brief Count the owned rules in paint/list order.
     * @return Current length, assuming the vector size fits int. */
    [[nodiscard]] int count() const;
    /** @brief Test whether the list contains no rules.
     * @return True exactly when count() is zero, independent of selection state. */
    [[nodiscard]] bool isEmpty() const { return count() == 0; }
    /** @brief Inspect a rule at a guarded list index.
     * @param index Zero-based list index.
     * @return Borrowed pointer, or nullptr when out of range. Removal, replacement
     * loading, clearing or list destruction can invalidate it; moving rules does not. */
    [[nodiscard]] Rule *at(int index) const;
    /** @brief Find a rule by pointer identity without inspecting its contents.
     * @param rule Optional borrowed pointer to search for; null returns -1.
     * @return First zero-based matching index, or -1 when absent; performs a linear scan. */
    [[nodiscard]] int indexOf(const Rule *rule) const;

    /** @brief Read the editor's active list index.
     * @return Selected index, or -1 for no selection; painting is independent of this choice. */
    [[nodiscard]] int activeIndex() const { return m_activeIndex; }
    /** @brief Inspect the active rule without transferring ownership.
     * @return Borrowed selected pointer, or nullptr for no valid active index. */
    [[nodiscard]] Rule *activeRule() const;

    // ── Mutators (each emits ruleListChanged after the change lands) ──

    /*! \brief Append a Rule. The list takes ownership and reparents the
     *         Rule. Returns the raw pointer for caller convenience.
     *         Becomes active whenever the list currently has no active selection.
     * @param rule Ownership transferred; null returns nullptr without mutation/signals.
     * @return Borrowed appended pointer, owned/reparented by this list. A new
     * selection emits activeIndexChanged() before ruleListChanged(); an existing
     * active selection is retained. Connections forward future rule edits by current index. */
    Rule *append(std::unique_ptr<Rule> rule);

    /** @brief Insert an owned rule at a clamped list position.
     * @param index Requested position clamped to [0,count()].
     * @param rule Ownership transferred; null returns nullptr without changes/signals.
     * @return Borrowed inserted pointer, reparented to this list. Existing active
     * rule identity is retained by shifting its index when needed; no selection
     * selects the inserted rule. Selection notification precedes ruleListChanged(). */
    Rule *insert(int index, std::unique_ptr<Rule> rule);

    /*! \brief Remove the Rule at \p index. Returns true on success.
     *         Active index is adjusted so the same Rule (or its neighbour)
     *         stays selected when possible.
     * @param index Zero-based rule index; invalid input is a no-op.
     * @return True after destruction/removal and ruleListChanged(), otherwise false.
     * Removing the active row selects the next row at its index, or the new last
     * row. Selection notification can fire even when its numeric index is unchanged. */
    bool remove(int index);

    /*! \brief Move the Rule at \p from to position \p to. Returns false
     *         if either index is out of range or from == to. Active index
     *         tracks the moved Rule.
     * @param from Existing zero-based source position.
     * @param to Existing zero-based destination position after relocation.
     * @return True after relocation and ruleListChanged(), false for invalid/equal
     * positions. The selected Rule identity is retained, with selection notification
     * only when its index moves; borrowed pointers to all rules remain valid. */
    bool move(int from, int to);

    /** @brief Destroy every Rule and reset selection to -1.
     * Empty lists are a no-op. Otherwise activeIndexChanged(-1) is emitted when
     * needed, followed by ruleListChanged(); every borrowed rule pointer is invalidated. */
    void clear();

    /*! \brief Update the active index. Clamped to [-1, count()-1]; -1
     *         means no active selection.
     * @param index Any negative value clears selection; nonnegative indices clamp
     * to the final row. Empty lists always select -1. Emits only activeIndexChanged()
     * when the resulting numeric index differs; structure is unchanged. */
    void setActiveIndex(int index);

    // ── JSON ──────────────────────────────────────────────────────────

    /** @brief Serialize every owned rule in current list order.
     * @return Owned JSON array; active selection is omitted. Each rule supplies
     * its own configuration through Rule::toJson(). */
    [[nodiscard]] QJsonArray toJson() const;

    /*! \brief Re-populate from a previously produced QJsonArray. Entries
     *         that fail Rule::fromJson() are skipped (forward compat —
     *         older saves with renderer ids we don't know about don't
     *         poison the rest of the list).
     * @param arr Ordered rule objects. Existing rules are destroyed before parsing;
     * nonobjects and unsupported rule configurations are skipped. No rollback
     * preserves old rules when all entries fail. On normal completion selects
     * index zero or -1 and unconditionally emits activeIndexChanged(), then
     * ruleListChanged(). Allocation/loader exceptions propagate. */
    void fromJson(const QJsonArray &arr);

    /*! \brief Migrate a legacy "sublayers" array (shipped .oswp format —
     *         see RENDERING_OUTPUT_SUBLAYERS_PLAN.md §S6) into Rules.
     *
     *         For each sublayer entry we create one Rule whose
     *         - name      = sublayer's "id" (a human-readable token like
     *                       "results.junctions"); callers can re-name.
     *         - isVisible = sublayer's "isVisible" (default true).
     *         - renderer  = SingleSymbolRenderer with opacity carried
     *                       from the sublayer's "opacity" field. Full
     *                       per-archetype style-payload migration (marker
     *                       shape, line width, etc.) lands in Slice Z.6
     *                       when the new SymbolLayer model arrives. For
     *                       Z.1 the bare-bones mapping preserves identity
     *                       + visibility so projects open without losing
     *                       sublayer order.
     *
     *         Append-only — existing Rules are left in place. Callers
     *         that want full replacement should clear() first.
     * @param sublayers Ordered legacy rows; nonobjects are skipped. Missing/empty
     * id becomes Sublayer, isVisible defaults true, and finite opacity clamps to
     * [0,1]. The visible key used by newer snapshots is not read here. Style
     * payloads are not migrated; created single-symbol stacks are empty. Appends
     * all accepted rows, selects index zero only if no selection exists, and emits
     * one structural notification for the batch. No valid rows means no signals. */
    void loadLegacySublayersAsRules(const QJsonArray &sublayers);

signals:
    /** @brief Notify completed structural mutation or wholesale JSON reload. */
    void ruleListChanged();
    /** @brief Notify active-selection changes, including some unchanged numeric indices.
     * @param i Current zero-based selected index, or -1 when cleared. */
    void activeIndexChanged(int i);
    /** @brief Forward a contained Rule's metadata or renderer-state notification.
     * @param i Rule's current zero-based index, resolved at signal time so moves
     * and insertions do not leave a stale captured index. */
    void ruleChanged(int i);

private:
    /** @brief Parent a rule to this list and forward future change notifications.
     * @param r Optional borrowed rule; null is a no-op. Ownership must separately
     * enter m_rules. Repeated connections are not deduplicated, and each callback
     * scans for the pointer's current index before emitting ruleChanged(). */
    void connectRule(Rule *r);

    std::vector<std::unique_ptr<Rule>> m_rules;
    int m_activeIndex = -1;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULELIST_H
