/*!
 * \file   rulebasedrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer that evaluates a list of expression-gated rules.
 *
 *         Sub-phase 8.13.6.2 STUB: this v1 stores rules (expression /
 *         symbol / label / scale range) and JSON-round-trips them, but
 *         expression evaluation is deferred to Slice **BI.2** (the label
 *         engine introduces the LabelExpression DSL parser, which this
 *         renderer reuses per §J.7). Until BI.2 lands, symbolFor() returns
 *         the fallback symbol.
 *
 *         Storing the rules now keeps `.oswp` files written by a future
 *         build forward-loadable today (and vice versa). The full Map
 *         Symbology Dialog editor is deferred to Slice **BI.3**.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.2 — stub.
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
 *        v1 storage / JSON / fallback paint — expression eval lands with
 *        Slice BI.2.
 */
class RuleBasedRenderer final : public IFeatureRenderer
{
public:
    /*!
     * \struct Rule
     * \brief One rule in the list.
     */
    struct Rule
    {
        QString               expression;   /*!< Boolean DSL expression — empty matches always. */
        QString               label;        /*!< Legend label for this rule. */
        SymbolStyle           symbol;       /*!< Symbol painted when this rule matches. */
        QPair<double, double> scaleRange = { 0.0, 0.0 };  /*!< (minScale, maxScale); (0,0) = always. */
    };

    RuleBasedRenderer() = default;
    ~RuleBasedRenderer() override = default;

    [[nodiscard]] const QList<Rule> &rules() const { return m_rules; }
    void setRules(QList<Rule> rules);
    void addRule(Rule r);

    /*!
     * \brief Symbol returned when no rule matches.
     */
    [[nodiscard]] const SymbolStyle &fallbackSymbol() const { return m_fallback; }
    void setFallbackSymbol(SymbolStyle s) { m_fallback = std::move(s); }

    // IFeatureRenderer.
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("rule"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    QList<Rule>  m_rules;
    SymbolStyle  m_fallback;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULEBASEDRENDERER_H
