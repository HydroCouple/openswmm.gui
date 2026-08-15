/*!
 * \file   rulestylesubject.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Adapter — present a Rule as an ILayerStyleSubject (Slice Z.2).
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
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_RULESTYLESUBJECT_H
#define OPENSWMMVIS_UI_DIALOGS_RULESTYLESUBJECT_H

#include "ui/dialogs/ilayerstylesubject.h"

#include <QString>

#include <memory>
#include <vector>

namespace OpenSWMM::Render {
class Rule;
class RuleList;
}

namespace openswmmvis::ui {

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
    /*! \param rule       Non-null. Outlives this subject.
     *  \param routingId  Stable id for right-click → "focus this tab"
     *                    routing. Typically `"rule.0"`, `"rule.1"`, etc.
     *  \param section    Optional section grouping; defaults to "Rules". */
    RuleStyleSubject(OpenSWMM::Render::Rule *rule,
                     QString routingId,
                     QString section = QStringLiteral("Rules"));

    [[nodiscard]] QString  title()          const override;
    [[nodiscard]] QString  section()        const override;
    [[nodiscard]] QObject *propertyObject() const override;
    [[nodiscard]] QString  routingId()      const override;

private:
    OpenSWMM::Render::Rule *m_rule;
    QString                 m_routingId;
    QString                 m_section;
};

/*! \brief Build one RuleStyleSubject per Rule in \p list. Empty / null
 *         list yields an empty vector. RoutingIds are auto-assigned as
 *         `"rule.0"`, `"rule.1"`, ... — stable for the duration of the
 *         dialog session. */
[[nodiscard]] std::vector<std::unique_ptr<ILayerStyleSubject>>
subjectsFromRuleList(OpenSWMM::Render::RuleList *list);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RULESTYLESUBJECT_H
