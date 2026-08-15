/*!
 * \file   rulelist.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Ordered, stackable list of Rules owned by a layer (Slice Z.1).
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
    explicit RuleList(QObject *parent = nullptr);
    ~RuleList() override;

    RuleList(const RuleList &) = delete;
    RuleList &operator=(const RuleList &) = delete;
    RuleList(RuleList &&) = delete;
    RuleList &operator=(RuleList &&) = delete;

    // ── Read access ───────────────────────────────────────────────────

    [[nodiscard]] int      count() const;
    [[nodiscard]] bool     isEmpty() const { return count() == 0; }
    [[nodiscard]] Rule    *at(int index) const;
    [[nodiscard]] int      indexOf(const Rule *rule) const;

    [[nodiscard]] int      activeIndex() const { return m_activeIndex; }
    [[nodiscard]] Rule    *activeRule() const;

    // ── Mutators (each emits ruleListChanged after the change lands) ──

    /*! \brief Append a Rule. The list takes ownership and reparents the
     *         Rule. Returns the raw pointer for caller convenience.
     *         Becomes the active Rule when the list was previously empty. */
    Rule *append(std::unique_ptr<Rule> rule);

    /*! \brief Insert at \p index. \p index is clamped to [0, count()]. */
    Rule *insert(int index, std::unique_ptr<Rule> rule);

    /*! \brief Remove the Rule at \p index. Returns true on success.
     *         Active index is adjusted so the same Rule (or its neighbour)
     *         stays selected when possible. */
    bool  remove(int index);

    /*! \brief Move the Rule at \p from to position \p to. Returns false
     *         if either index is out of range or from == to. Active index
     *         tracks the moved Rule. */
    bool  move(int from, int to);

    /*! \brief Remove every Rule. */
    void  clear();

    /*! \brief Update the active index. Clamped to [-1, count()-1]; -1
     *         means no active selection. */
    void  setActiveIndex(int index);

    // ── JSON ──────────────────────────────────────────────────────────

    [[nodiscard]] QJsonArray toJson() const;

    /*! \brief Re-populate from a previously produced QJsonArray. Entries
     *         that fail Rule::fromJson() are skipped (forward compat —
     *         older saves with renderer ids we don't know about don't
     *         poison the rest of the list). */
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
     *         that want full replacement should clear() first. */
    void loadLegacySublayersAsRules(const QJsonArray &sublayers);

signals:
    void ruleListChanged();          /*!< Structural change. */
    void activeIndexChanged(int i);  /*!< Selection-only change. */
    void ruleChanged(int i);         /*!< Rule[i]'s ruleChanged() fired. */

private:
    void connectRule(Rule *r);       /*!< Wires the Rule's ruleChanged → our ruleChanged(i). */

    std::vector<std::unique_ptr<Rule>> m_rules;
    int                                m_activeIndex = -1;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULELIST_H
