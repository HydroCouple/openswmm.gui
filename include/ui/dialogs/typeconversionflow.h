/**
 * @file      typeconversionflow.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared confirm → convert → summary flow for changing a node or
 *         link to a different SWMM type. Reused by:
 *           - `OpenSWMMVisMapToolSelect::showContextMenu` ("Convert To ▸")
 *           - `AttributeTablePanel::onContextMenuRequested` ("Change Type…")
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
#include "layers/typeconversion.h"

class QWidget;
class QMenu;
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
     *  node, which the menu collects with InletJunctionSetupDialog and
     *  passes to the batch conversion. */
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

    // Populate lazily from live engine state. Both entry points snapshot typed IDs before
    // opening the menu. The request survives sorting, refiltering and modeless capture picking.
    static void addActions(QMenu *menu, SWMMModelLayer *layer, TypeConversionRequest request,
                           int hiddenCount = 0);

};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H
