/*!
 * \file   swmmcontrolrulepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [CONTROLS] rule blocks. The
 * scalar surface today is: (1) the parsed rule name (from DA-ENG-02
 * `swmm_control_get_id`) and (2) the full rule text as a multi-line
 * string editable via QPropertyModel's text editor delegate. BR
 * replaces the freeform text editor with a structured RulesEditorDialog
 * + RuleSyntaxHighlighter later.
 *
 * Rule identity is by **stored zero-based index** rather than name —
 * the engine has no per-rule mutator (DA-ENG-11) so writes round-trip
 * via `swmm_control_clear_rules` + a re-add loop.
 */

#ifndef SWMMCONTROLRULEPROPERTYADAPTER_H
#define SWMMCONTROLRULEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMControlRulePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    /*! Multi-line rule body. The Property Browser shows this with a
     *  scrollable text editor delegate. */
    Q_PROPERTY(QString ruleText READ ruleText WRITE setRuleText NOTIFY changed)

public:
    /*! Construct with the engine handle + the parsed RULE name. The
     *  index is resolved at access time so the adapter remains valid
     *  across reorderings. */
    SWMMControlRulePropertyAdapter(SWMM_Engine engine, QString name,
                                     QObject *parent = nullptr)
        : SWMMDataObjectPropertyAdapter(engine, std::move(name), parent) {}

    [[nodiscard]] QString ruleText() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setRuleText(const QString &text);

private:
    /*! Resolve the rule's zero-based index by scanning every rule and
     *  matching the parsed name. Returns -1 if not found. */
    [[nodiscard]] int idx() const;
};

#endif // SWMMCONTROLRULEPROPERTYADAPTER_H
