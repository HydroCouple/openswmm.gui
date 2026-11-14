/**
 * @file      rulebasedrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer that evaluates a list of expression-gated rules.
 *
 *         Rules are evaluated in list order by ExpressionEvaluator. The first
 *         expression that succeeds and is true selects its symbol; invalid
 *         expressions are skipped and empty expressions match unconditionally.
 *         Scale ranges are persisted metadata and are not enforced by symbolFor().
 *
 *         Storing the rules now keeps `.oswp` files written by a future
 *         build forward-loadable today (and vice versa). The full Map
 *         Symbology Dialog editor is deferred to Slice **BI.3**.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.2 — stub.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_RULEBASEDRENDERER_H
#define OPENSWMM_RENDER_RULEBASEDRENDERER_H

#include "render/ifeaturerenderer.h"

#include <QPair>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class RuleBasedRenderer
 * \brief First-match expression-gated rule list.
 *
 *        Configuration is stored by value; callers coordinate mutation with
 *        painting and issue their own repaint notifications.
 */
class RuleBasedRenderer final : public IFeatureRenderer
{
public:
    /*!
     * \struct Rule
     * \brief One rule in the list.
     */
    struct Rule {
        QString expression; /*!< Boolean DSL expression — empty matches always. */
        QString label;      /*!< Legend label for this rule. */
        SymbolStyle symbol; /*!< Symbol painted when this rule matches. */
        QPair<double, double> scaleRange = {0.0, 0.0}; /*!< (minScale, maxScale); (0,0) = always. */
    };

    /** @brief Construct an empty rule list and empty fallback symbol. */
    RuleBasedRenderer() = default;
    /** @brief Destroy the stored rules and fallback values. */
    ~RuleBasedRenderer() override = default;

    /** @brief Inspect rules in first-match evaluation order.
     * @return Borrowed list valid for this renderer's lifetime; edits/loading can replace it. */
    [[nodiscard]] const QList<Rule> &rules() const { return m_rules; }
    /** @brief Replace the ordered rules without compiling or validating expressions.
     * @param rules List moved into storage; empty/default rules and duplicate expressions are retained. */
    void setRules(QList<Rule> rules);
    /** @brief Append a rule after all existing rules.
     * @param r Rule moved without validation; earlier unconditional matches can shadow it. */
    void addRule(Rule r);

    /*!
     * \brief Symbol returned when no rule matches.
     * @return Borrowed template valid until loading/replacement/destruction.
     */
    [[nodiscard]] const SymbolStyle &fallbackSymbol() const { return m_fallback; }
    /** @brief Replace the unmatched-feature template without notification.
     * @param s Style moved into storage without validation. */
    void setFallbackSymbol(SymbolStyle s) { m_fallback = std::move(s); }

    // IFeatureRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token rule. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("rule"); }
    /** @brief Evaluate rules in order, skipping failures and stopping at the first true result.
     * @param f Feature identity, unused by this renderer.
     * @param attrs Exact-key values supplied to ExpressionEvaluator for each expression.
     * @return Owned first matching symbol, otherwise the fallback. Empty expressions
     * match always. Rule scaleRange values and data-defined symbol expressions are
     * not applied here, and parser errors are suppressed as nonmatches. */
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    /** @brief Build one legend row for every rule without evaluating its condition.
     * @return Owned rows in list order, using nonempty labels or expression text.
     * No fallback row or editable class key is supplied, including for shadowed/invalid rules. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize ordered rules, symbols, optional labels/scale ranges and fallback.
     * @return Owned configuration object; expression and scale validity are not checked. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace rule and fallback configuration from JSON without validation.
     * @param j Object whose id is ignored. Missing/nonarray rules clear the list;
     * nonobject entries become default rules with empty expressions that match
     * unconditionally. Missing scale bounds become zero and missing fallback an empty symbol. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy the entire rule configuration into an independent renderer.
     * @return Sole ownership of the new snapshot. */
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    QList<Rule> m_rules;
    SymbolStyle m_fallback;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULEBASEDRENDERER_H
