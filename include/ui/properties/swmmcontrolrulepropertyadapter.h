/**
 * @file      swmmcontrolrulepropertyadapter.h
 * @brief     Property-tree adapter for [CONTROLS] rule blocks.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [CONTROLS] rule blocks. The
 * scalar surface today is: (1) the parsed rule name (from DA-ENG-02
 * `swmm_control_get_id`) and (2) the full rule text as a multi-line
 * string editable via QPropertyModel's text editor delegate. BR
 * replaces the freeform text editor with a structured RulesEditorDialog
 * + RuleSyntaxHighlighter later.
 *
 * Rule identity is resolved by stored name at each access —
 * the engine has no per-rule mutator (DA-ENG-11) so writes round-trip
 * via `swmm_control_clear_rules` + a re-add loop.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
     *  across reorderings.
     * @param[in] engine Borrowed live engine, with lifetime requirements matching the shared data-object adapter.
     * @param[in] name Parsed RULE ID copied into stored identity.
     * @param[in] parent Optional Qt owner; use on the GUI thread.
     * @details Construction does not cache an index or mutate the control-rule list.
 */
    SWMMControlRulePropertyAdapter(SWMM_Engine engine, QString name, QObject *parent = nullptr)
        : SWMMDataObjectPropertyAdapter(engine, std::move(name), parent)
    {
    }

    /**
     * @return UTF-8 rule block read into an 8192-byte buffer, or empty when unresolved/unreadable.
     */
    [[nodiscard]] QString ruleText() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Replace one control-rule block and notify successful text/name changes.
     * @param[in] text Complete engine rule text, including its RULE name header.
     * @details First tries the model-layer apply helper. If unavailable/refused, snapshots readable rules, clears the engine list and re-adds with this slot replaced; a rejected add attempts restoration without a returned status. Success emits changed and may update stored name/emit renameRequested. Failed writes return silently.
     */
    void setRuleText(const QString &text);

private:
    /*! Resolve the rule's zero-based index by scanning every rule and
     *  matching the parsed name. Returns -1 if not found.
     * @brief Resolve the stored object name against the current engine collection.
     * @return Zero-based live index, or -1 for a missing engine, empty name or unresolved ID; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
};

#endif // SWMMCONTROLRULEPROPERTYADAPTER_H
