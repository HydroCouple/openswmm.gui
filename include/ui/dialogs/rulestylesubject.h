/**
 * @file      rulestylesubject.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Adapter — present a Rule as an ILayerStyleSubject (Slice Z.2).
 *
 *         Slice Z.2 (RENDERING_RULE_MODEL_PLAN.md §16) — backwards-compat
 *         seam. The shipped §W editors (FeatureStyleEditor,
 *         SwmmElementSymbolEditor, GisVectorSymbolEditor,
 *         RasterColorRampEditor) bind to ILayerStyleSubject's
 *         propertyObject() to read/write Q_PROPERTYs. Rule is a QObject
 *         with Q_PROPERTYs (name / isVisible / filterExpression /
 *         minScale / maxScale / blendMode); wrapping a Rule as a subject
 *         lets the dialog show it as a tab via the existing QPropertyModel
 *         path without touching any editor.
 *
 *         `subjectsFromRuleList` is the bulk helper Z.3's SymbologyTab
 *         calls to enumerate Rules as subjects. Z.2 only ships the
 *         adapter + helper; the SymbologyTab integration is Z.3.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_RULESTYLESUBJECT_H
#define OPENSWMMVIS_UI_DIALOGS_RULESTYLESUBJECT_H

#include "ui/dialogs/ilayerstylesubject.h"

#include <QString>

#include <memory>
#include <vector>

namespace OpenSWMM::Render
{
class Rule;
class RuleList;
}

namespace openswmmvis::ui
{

/*!
 * \class RuleStyleSubject
 * \brief Wraps a Rule as one ILayerStyleSubject for the legacy dialog.
 *
 *        The Rule must outlive this subject. The subject does not take
 *        ownership of the Rule; ownership remains with the layer's
 *        RuleList.
 */
class RuleStyleSubject : public ILayerStyleSubject
{
public:
    /**
     * @brief Wrap a rule as a nonowning legacy style subject with copied routing/group metadata.
     * @param rule Optional borrowed raw rule; nonnull rules must outlive this subject and
     * all editors using propertyObject(). No destruction tracking or rule ownership.
     * @param routingId Owned value moved into the subject; intended dialog-local routing
     * identity, not an automatically maintained list index.
     * @param section Owned group label moved into storage, default Rules.
     * Does not attach to signals, snapshot properties, validate identifiers or mutate rule.
     */
    RuleStyleSubject(OpenSWMM::Render::Rule *rule, QString routingId,
                     QString section = QStringLiteral("Rules"));

    /**
     * @brief Read the current rule's display title without retaining its name reference.
     * @return Owned current name, Rule for an unnamed live rule, empty for null target.
     * Requires nonnull raw rule lifetime; title follows renames but routing id does not.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Read the stored section-group label.
     * @return Owned construction-time section value, independent of live rule state.
     */
    [[nodiscard]] QString section() const override;
    /**
     * @brief Expose the wrapped rule for property editing.
     * @return Borrowed rule QObject or null; no ownership transfer or lifetime protection.
     * Caller must keep the rule alive through property models/editor callbacks.
     */
    [[nodiscard]] QObject *propertyObject() const override;
    /**
     * @brief Read the stored dialog routing identity.
     * @return Owned construction-time string; never recomputed after rule reorder/rename.
     */
    [[nodiscard]] QString routingId() const override;

private:
    OpenSWMM::Render::Rule *m_rule;
    QString m_routingId;
    QString m_section;
};

/**
 * @brief Build owning subject wrappers for the current nonnull entries of a borrowed rule list.
 * @param list Optional borrowed list; rules/list must remain valid during enumeration,
 * and wrapped rules must outlive the resulting subjects/editors. No concurrent reorder.
 * @return Owned vector of unique_ptr subjects, empty for null/empty list. Each wrapper
 * borrows its rule and stores rule.index using the current zero-based list position;
 * null entries are skipped without renumbering suffixes. IDs remain fixed in wrappers
 * and are not globally persistent or updated after later list edits. No rule mutation.
 */
[[nodiscard]] std::vector<std::unique_ptr<ILayerStyleSubject>>
subjectsFromRuleList(OpenSWMM::Render::RuleList *list);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RULESTYLESUBJECT_H
