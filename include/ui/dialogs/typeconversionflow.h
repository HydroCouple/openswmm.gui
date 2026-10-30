/**
 * @file      typeconversionflow.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared confirm → convert → summary flow for changing a node or
 *         link to a different SWMM type. Reused by:
 *           - `OpenSWMMVisMapToolSelect::showContextMenu` ("Convert To ▸")
 *           - `AttributeTablePanel::onChangeTypeTriggered` ("Change Type…")
 *
 * The user is warned BEFORE the conversion that the old type's specific
 * attributes will be cleared (the conversion is not undoable), and shown
 * the engine-reported cleared fields + topology warnings AFTER it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H
#define OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

class QWidget;
class SWMMModelLayer;

namespace openswmmvis::ui
{

class TypeConversionFlow
{
    Q_DECLARE_TR_FUNCTIONS(TypeConversionFlow)
public:
    /**
     * @brief Static-only interaction utility; instances cannot be constructed and own no model
     *   state.
     */
    TypeConversionFlow() = delete;

    /*! \brief GUI-only pseudo node type for the Convert To flow: a virtual
     *  junction (engine-side it is a JUNCTION with the is_virtual flag, not
     *  a fifth SWMM_NodeType). Callers pass this as currentType when the
     *  node's flag is set, and as newType to convert into one. */
    static constexpr int kVirtualNodeType = 4;

    /*! \brief GUI-only pseudo node type for an inlet junction: engine-side a
     *  JUNCTION with BOTH is_virtual and is_inlet set, plus one
     *  [INLET_USAGE] row. Converting INTO one needs a design and a capture
     *  node, which the caller collects with InletJunctionSetupDialog and
     *  passes through \ref runToInletJunction. */
    static constexpr int kInletNodeType = 5;

    /*! \brief User-facing label for a SWMM_NodeType value
     *  (Junction / Outfall / Storage / Divider), kVirtualNodeType
     *  (Virtual Junction) or kInletNodeType (Inlet Junction). Empty if out
     *  of range.
     * @param[in] swmmNodeType Raw engine node enum 0..3 or GUI pseudo type 4 virtual/5 inlet.
     * @return Translated corresponding name, empty for unrecognized value; no engine query or
     *   validation.
 */
    static QString nodeTypeLabel(int swmmNodeType);

    /*! \brief User-facing label for a SWMM_LinkType value
     *  (Conduit / Pump / Orifice / Weir / Outlet). Empty if out of range.
     * @param[in] swmmLinkType Raw engine link enum 0..4 (Conduit, Pump, Orifice, Weir, Outlet).
     * @return Translated label, empty outside known range; no model query.
 */
    static QString linkTypeLabel(int swmmLinkType);

    /*! \brief Pre-conversion confirmation text warning that the old type's
     *  specific attributes will be lost. Pure text builder.
     * @param[in] isNode True uses node/pseudo-type labels, false link labels.
     * @param[in] name Raw element identity embedded into plain confirmation text.
     * @param[in] currentType Caller-reported source kind; no actual model lookup here.
     * @param[in] newType Requested destination kind, unknown produces empty label.
     * @return Owned confirmation text describing type-specific data loss and lack of undo. Pure
     *   formatting, no modal prompt or mutation.
 */
    static QString confirmText(bool isNode, const QString &name, int currentType, int newType);

    /*! \brief Post-conversion summary (cleared fields + topology warnings,
     *  or "(no side effects)" when both are empty). Pure text builder.
     * @param[in] cleared Engine-reported field names joined with commas, inserted as raw unescaped
     *   HTML text.
     * @param[in] warnings Engine-reported topology messages prefixed with bullets and HTML breaks,
     *   not escaped.
     * @return Rich-text cleared/warnings summary, translated no-side-effects label when both lists
     *   empty. Does not verify fields actually changed or query engine.
 */
    static QString summaryHtml(const QStringList &cleared, const QStringList &warnings);

    /*! \brief Confirm with the user, then convert via
     *  `SWMMModelLayer::applyNodeConvert/applyLinkConvert`, then show the
     *  engine-reported summary (or the failure reason). The layer method
     *  emits the repaint/geometry/attribute signals that refresh the map
     *  and panels. Returns true only if the conversion succeeded.
     * @param[in] parent Optional borrowed modal-message parent; host must preserve parent/layer
     *   lifetimes through nested event loops.
     * @param[in] layer Optional borrowed live model layer; null immediately fails, no ownership or
     *   deletion guard.
     * @param[in] isNode True node/pseudo-kind operation, false engine link conversion.
     * @param[in] name Nonempty exact object name resolved by layer helpers; empty fails before
     *   prompt.
     * @param[in] currentType Caller-reported current type; exact equality with newType returns
     *   false BEFORE checking engine inlet flag.
     * @param[in] newType Requested engine type or virtual-node pseudo type; inlet promotion needs
     *   runToInletJunction instead.
     * @return True after requested sequence succeeded and completion message closed; false for
     *   early no-op, declined confirmation or reported helper failure. False can still leave
     *   earlier mutations.
     * @note Confirm BEFORE writes. Read actual inlet flag to demote inlet role/usage when needed;
     *   virtual target may first convert to Junction, then set virtual flag. Plain junction target
     *   clears inlet then virtual; other targets clear inlet (return ignored in plain branch) then
     *   convert. Stale caller pseudo-kind can affect branch/default behavior.
     * @note Nonundoable direct layer helpers update caches/repaint/attributes, conversions also
     *   geometry; no transaction or restoration after a later failed step. Failure/completion
     *   messages are modal and interpolate raw identity/summary text. This function does not
     *   independently set project dirty flag beyond chosen layer helpers.
 */
    static bool run(QWidget *parent, SWMMModelLayer *layer, bool isNode, const QString &name,
                    int currentType, int newType);

    /*! \brief Promote \p name to an inlet junction: confirm, set the flag
     *  (`swmm_node_set_inlet`), then install the usage row naming
     *  \p inletDesign / \p captureNode. Split out of run() because the
     *  identity fields have to be collected BEFORE the mutation (D-G6) and
     *  run()'s signature carries no room for them. Not undoable, like every
     *  other type conversion — the confirm text says so.
     * @param[in] parent Optional borrowed message-box parent; preserve dialog/layer/engine lifetime
     *   through nested loops.
     * @param[in] layer Optional borrowed live model layer; null fails.
     * @param[in] name Required existing source node name; no trimming.
     * @param[in] currentType Caller-reported source kind; values other than Junction 0 or Virtual 4
     *   trigger preliminary node conversion to Junction.
     * @param[in] inletDesign Required nonempty design ID resolved AFTER promotion, not validated
     *   before first mutation.
     * @param[in] captureNode Required nonempty capture node ID resolved AFTER promotion.
     * @param[in] placement Raw SWMM inlet placement enum forwarded to usage validation, no local
     *   bounds check.
     * @return True after Junction/inlet promotion and usage installation plus success summary;
     *   false for missing input/decline/helper/identity/usage failure, possibly with partial
     *   changes.
     * @note After confirmation, optionally convert source to Junction, set inlet=true (also
     *   virtual), then build zero-initialized node-host usage with design/capture indexes,
     *   num_inlets=1 and given placement. On missing identity/usage failure attempt set inlet=false
     *   without checking rollback result; retains virtual role and any prior base-type
     *   conversion/attribute loss. Original complete model state is not restored despite generic
     *   failure wording. No undo transaction.
 */
    static bool runToInletJunction(QWidget *parent, SWMMModelLayer *layer, const QString &name,
                                   int currentType, const QString &inletDesign,
                                   const QString &captureNode, int placement);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H
